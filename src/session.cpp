#include "session.hpp"
#include "fs_client.hpp"
#include "rpc_registration.hpp"
#include "sockets.hpp"

#include <socket/socket.h>
#include <utils/socket_handlers.hpp>
#include <serialization/serialize.hpp>
#include <serialization/stream_writer.hpp>
#include <sstream>
#include <unordered_map>
#include <algorithm>

namespace waos {

  session_request_context_type session_request_context;
  std::uint64_t g_own_process_id = 1;

  namespace {

    constexpr std::uint32_t FS_CREATE = 4;
    constexpr std::uint32_t FS_WRITE = 2;
    constexpr std::uint32_t FS_TRUNCATE = 8;
    // Keep the file handle open and write sequentially; no APPEND flag needed.

    std::unordered_map<std::uint32_t, std::shared_ptr<session>> sessions_by_socket;
    std::uint64_t next_session_id = 1;
    std::uint32_t next_session_socket = static_cast<std::uint32_t>(base_socket_id::FIRST_SESSION);

    std::string status_string(session_status status) {
      switch(status) {
        case session_status::OPEN: return "open";
        case session_status::SEALED: return "sealed";
        case session_status::FAILED: return "failed";
      }
      return "open";
    }

    template <typename T>
    std::vector<std::uint8_t> serialize_to_bytes(const T& value) {
      std::ostringstream oss;
      serialization::stream_writer writer(oss);
      serialize(writer, value);
      writer.write_rest();
      std::string s = oss.str();
      return std::vector<std::uint8_t>(s.begin(), s.end());
    }

  }

  std::shared_ptr<session> get_session_by_socket(std::uint32_t socket_id) {
    auto it = sessions_by_socket.find(socket_id);
    if(it == sessions_by_socket.end()) {
      return nullptr;
    }
    return it->second;
  }

  std::shared_ptr<session> create_session(std::string name, std::uint64_t process_id, labels labels) {
    if(name.empty()) {
      throw debug_invalid_argument("session name must be non-empty");
    }

    std::uint64_t session_id = next_session_id++;
    std::uint32_t socket_id = next_session_socket++;

    struct socket_options socket_options = {
      .input_buffer_size = 8 * 1024 * 1024,
      .output_buffer_size = 8 * 1024 * 1024,
      .flush_threshold = 4096,
      .data_event_threshold = 0,
      .drain_event_threshold = 4,
      .flags = 0,
    };
    socket_open(socket_id, &socket_options);

    auto sess = std::make_shared<session>(session_id, socket_id, std::move(name), process_id, std::move(labels));
    sessions_by_socket[socket_id] = sess;

    auto server_ptr = sess->rpc_server();
    socket_handlers::register_socket_handler(socket_id, [server_ptr, socket_id]() {
      session_request_context.socket_id = socket_id;
      if(server_ptr) {
        server_ptr->handle_data();
      }
    });

    return sess;
  }

  session::session(std::uint64_t session_id, std::uint32_t socket_id, std::string name, std::uint64_t process_id, labels labels)
    : session_id_(session_id)
    , socket_id_(socket_id)
    , name_(std::move(name))
    , process_id_(process_id)
    , labels_(std::move(labels))
    , dir_path_("sessions/" + std::to_string(session_id))
  {
    rpc_server_ = std::make_unique<serialization::flat_socket_rpc_server>(socket_id);
    rpc_registration::register_session_api(rpc_server_.get(), false);
    register_handlers();
  }

  session::~session() = default;

  void session::track(std::shared_ptr<serialization::promise<void>> p) {
    inflight_.push_back(p);
    p->handle([this, p]() {
      inflight_.erase(std::remove(inflight_.begin(), inflight_.end(), p), inflight_.end());
    }, [this, p](std::exception_ptr) {
      inflight_.erase(std::remove(inflight_.begin(), inflight_.end(), p), inflight_.end());
    });
  }

  void session::register_handlers() {
    auto self = this;
    rpc_server_->on_message<debug_snapshot_event>([self](debug_snapshot_event& ev, size_t payload_size) {
      auto payload = self->read_payload(payload_size);
      if(self->status_ != session_status::OPEN) {
        return;
      }
      self->track(self->handle_snapshot(ev, std::move(payload)));
    });
    rpc_server_->on_message<debug_io_event>([self](debug_io_event&, size_t payload_size) {
      auto payload = self->read_payload(payload_size);
      if(self->status_ != session_status::OPEN) {
        return;
      }
      self->track(self->append_payload(self->io_file_, "io.bin", std::move(payload)));
    });
    rpc_server_->on_message<debug_logs_event>([self](debug_logs_event&, size_t payload_size) {
      auto payload = self->read_payload(payload_size);
      if(self->status_ != session_status::OPEN) {
        return;
      }
      self->track(self->append_payload(self->logs_file_, "logs.bin", std::move(payload)));
    });
    rpc_server_->on_message<debug_spans_event>([self](debug_spans_event&, size_t payload_size) {
      auto payload = self->read_payload(payload_size);
      if(self->status_ != session_status::OPEN) {
        return;
      }
      self->track(self->append_payload(self->spans_file_, "spans.bin", std::move(payload)));
    });
    rpc_server_->on_message<debug_metrics_event>([self](debug_metrics_event&, size_t payload_size) {
      auto payload = self->read_payload(payload_size);
      if(self->status_ != session_status::OPEN) {
        return;
      }
      self->track(self->append_payload(self->metrics_file_, "metrics.bin", std::move(payload)));
    });
    rpc_server_->on_message<debug_bindings_event>([self](debug_bindings_event&, size_t payload_size) {
      auto payload = self->read_payload(payload_size);
      if(self->status_ != session_status::OPEN) {
        return;
      }
      self->track(self->write_file_truncate("bindings.bin", std::move(payload)));
    });
    rpc_server_->on_message<debug_registry_event>([self](debug_registry_event& ev, size_t payload_size) {
      auto payload = self->read_payload(payload_size);
      if(self->status_ != session_status::OPEN) {
        return;
      }
      std::string filename = "registry." + std::to_string(ev.process_id) + "." + std::to_string(ev.socket_id) + ".bin";
      self->track(self->write_file_truncate(std::move(filename), std::move(payload)));
    });
    rpc_server_->on_message<debug_code_event>([self](debug_code_event&, size_t payload_size) {
      auto payload = self->read_payload(payload_size);
      if(self->status_ != session_status::OPEN) {
        return;
      }
      self->track(self->append_code(std::move(payload)));
    });
  }

  std::vector<std::uint8_t> session::read_payload(size_t payload_size) {
    std::vector<std::uint8_t> payload(payload_size);
    auto& reader = rpc_server_->channel().reader();
    for(size_t i = 0; i < payload_size; ++i) {
      std::uint8_t b = 0;
      reader.read_u8(b);
      payload[i] = b;
    }
    return payload;
  }

  void session::ensure_open() {
    if(status_ != session_status::OPEN) {
      throw debug_closed("session is closed");
    }
  }

  std::shared_ptr<serialization::promise<void>> session::initialize_storage() {
    co_await fs_client::fs_rpc->mkdir(dir_path_);
    co_await write_meta();
    co_return;
  }

  std::shared_ptr<serialization::promise<void>> session::write_meta() {
    session_meta meta{
      .name = name_,
      .process_id = process_id_,
      .status = status_string(status_),
      .error = error_,
      .labels = labels_,
    };
    auto bytes = serialize_to_bytes(meta);

    auto file_id = co_await fs_client::fs_rpc->open(
      dir_path_ + "/meta.bin",
      FS_CREATE | FS_WRITE | FS_TRUNCATE
    );
    co_await fs_client::fs_rpc->write(file_id, bytes);
    co_await fs_client::fs_rpc->sync(file_id);
    co_await fs_client::fs_rpc->close(file_id);
    co_return;
  }

  std::shared_ptr<serialization::promise<void>> session::wait_inflight() {
    auto pending = inflight_;
    for(auto& p : pending) {
      co_await p;
    }
    co_return;
  }

  std::shared_ptr<serialization::promise<void>> session::sync_open_files() {
    auto sync_one = [](std::int64_t id) -> std::shared_ptr<serialization::promise<void>> {
      if(id < 0) {
        return serialization::promise<void>::resolved();
      }
      return fs_client::fs_rpc->sync(static_cast<std::uint32_t>(id));
    };
    co_await sync_one(io_file_);
    co_await sync_one(logs_file_);
    co_await sync_one(spans_file_);
    co_await sync_one(metrics_file_);
    co_await sync_one(code_file_);
    co_return;
  }

  std::shared_ptr<serialization::promise<void>> session::append_payload(
      std::int64_t& file_id,
      std::string filename,
      std::vector<std::uint8_t> data
  ) {
    if(data.empty()) {
      co_return;
    }
    if(file_id < 0) {
      // CREATE|WRITE without TRUNCATE: create if missing, keep handle for sequential writes.
      file_id = static_cast<std::int64_t>(co_await fs_client::fs_rpc->open(
        dir_path_ + "/" + filename,
        FS_CREATE | FS_WRITE
      ));
    }
    co_await fs_client::fs_rpc->write(static_cast<std::uint32_t>(file_id), data);
    co_return;
  }

  std::shared_ptr<serialization::promise<void>> session::append_code(std::vector<std::uint8_t> data) {
    auto prev = code_chain_;
    auto op = append_code_after(prev, std::move(data));
    code_chain_ = op;
    return op;
  }

  std::shared_ptr<serialization::promise<void>> session::append_code_after(
      std::shared_ptr<serialization::promise<void>> prev,
      std::vector<std::uint8_t> data
  ) {
    if(prev) {
      co_await prev;
    }
    // First chunk opens with TRUNCATE; later chunks append on the kept handle.
    if(code_file_ < 0) {
      code_file_ = static_cast<std::int64_t>(co_await fs_client::fs_rpc->open(
        dir_path_ + "/code.wasm",
        FS_CREATE | FS_WRITE | FS_TRUNCATE
      ));
    }
    if(!data.empty()) {
      co_await fs_client::fs_rpc->write(static_cast<std::uint32_t>(code_file_), data);
    }
    co_return;
  }

  std::shared_ptr<serialization::promise<void>> session::write_file_truncate(
      std::string filename,
      std::vector<std::uint8_t> data
  ) {
    // Serialize truncating writes so back-to-back registry overwrites cannot interleave open(TRUNCATE).
    auto prev = truncate_chain_;
    auto op = write_file_truncate_after(prev, std::move(filename), std::move(data));
    truncate_chain_ = op;
    return op;
  }

  std::shared_ptr<serialization::promise<void>> session::write_file_truncate_after(
      std::shared_ptr<serialization::promise<void>> prev,
      std::string filename,
      std::vector<std::uint8_t> data
  ) {
    if(prev) {
      co_await prev;
    }
    auto file_id = co_await fs_client::fs_rpc->open(
      dir_path_ + "/" + filename,
      FS_CREATE | FS_WRITE | FS_TRUNCATE
    );
    if(!data.empty()) {
      co_await fs_client::fs_rpc->write(file_id, data);
    }
    co_await fs_client::fs_rpc->sync(file_id);
    co_await fs_client::fs_rpc->close(file_id);
    co_return;
  }

  std::shared_ptr<serialization::promise<void>> session::handle_snapshot(
      debug_snapshot_event ev,
      std::vector<std::uint8_t> payload
  ) {
    std::string snap_path = dir_path_ + "/snap." + std::to_string(ev.snapshot_uid) + ".bin";
    std::string meta_path = dir_path_ + "/snap." + std::to_string(ev.snapshot_uid) + ".meta.bin";

    auto file_id = co_await fs_client::fs_rpc->open(snap_path, FS_CREATE | FS_WRITE | FS_TRUNCATE);
    if(!payload.empty()) {
      co_await fs_client::fs_rpc->write(file_id, payload);
    }
    co_await fs_client::fs_rpc->sync(file_id);
    co_await fs_client::fs_rpc->close(file_id);

    snapshot_meta meta{
      .snapshot_uid = ev.snapshot_uid,
      .mana_used = ev.mana_used,
      .mana_limit = ev.mana_limit,
      .memory_size = payload.size(),
    };
    auto bytes = serialize_to_bytes(meta);
    auto meta_id = co_await fs_client::fs_rpc->open(meta_path, FS_CREATE | FS_WRITE | FS_TRUNCATE);
    co_await fs_client::fs_rpc->write(meta_id, bytes);
    co_await fs_client::fs_rpc->sync(meta_id);
    co_await fs_client::fs_rpc->close(meta_id);
    co_return;
  }

  std::shared_ptr<serialization::promise<void>> session::set_labels(labels new_labels) {
    ensure_open();
    for(const auto& [k, v] : new_labels) {
      labels_[k] = v;
    }
    co_await write_meta();
    co_return;
  }

  std::shared_ptr<serialization::promise<void>> session::sync() {
    ensure_open();
    co_await wait_inflight();
    co_await sync_open_files();
    co_await write_meta();
    co_return;
  }

  std::shared_ptr<serialization::promise<void>> session::seal() {
    if(status_ != session_status::OPEN) {
      throw debug_conflict("session already closed");
    }
    co_await wait_inflight();
    co_await sync_open_files();
    status_ = session_status::SEALED;
    co_await write_meta();
    co_await close_open_files();
    co_return;
  }

  std::shared_ptr<serialization::promise<void>> session::fail(std::string error) {
    if(status_ != session_status::OPEN) {
      throw debug_conflict("session already closed");
    }
    co_await wait_inflight();
    co_await sync_open_files();
    status_ = session_status::FAILED;
    error_ = std::move(error);
    co_await write_meta();
    co_await close_open_files();
    co_return;
  }

  std::shared_ptr<serialization::promise<void>> session::close_open_files() {
    auto close_one = [](std::int64_t& id) -> std::shared_ptr<serialization::promise<void>> {
      if(id < 0) {
        return serialization::promise<void>::resolved();
      }
      auto fid = static_cast<std::uint32_t>(id);
      id = -1;
      return fs_client::fs_rpc->close(fid);
    };
    co_await close_one(io_file_);
    co_await close_one(logs_file_);
    co_await close_one(spans_file_);
    co_await close_one(metrics_file_);
    co_await close_one(code_file_);
    co_return;
  }

}
