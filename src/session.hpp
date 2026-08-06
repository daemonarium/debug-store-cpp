#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>
#include <map>
#include <examples/structures/debug_store.hpp>
#include <serialization/socket_rpc_server.hpp>
#include <serialization/socket_message_channel.hpp>

namespace waos {

  enum class session_status : std::uint8_t {
    OPEN = 1,
    SEALED = 2,
    FAILED = 3,
  };

  struct session_request_context_type {
    std::uint32_t socket_id = 0;
  };

  extern session_request_context_type session_request_context;
  extern std::uint64_t g_own_process_id;

  class session : public std::enable_shared_from_this<session> {
    public:
      session(std::uint64_t session_id, std::uint32_t socket_id, std::string name, std::uint64_t process_id, labels labels);
      ~session();

      std::uint64_t session_id() const { return session_id_; }
      std::uint32_t socket_id() const { return socket_id_; }
      session_status status() const { return status_; }

      std::shared_ptr<serialization::promise<void>> set_labels(labels labels);
      std::shared_ptr<serialization::promise<void>> sync();
      std::shared_ptr<serialization::promise<void>> seal();
      std::shared_ptr<serialization::promise<void>> fail(std::string error);

      std::shared_ptr<serialization::promise<void>> initialize_storage();
      serialization::socket_rpc_server* rpc_server() { return rpc_server_.get(); }

    private:
      std::uint64_t session_id_;
      std::uint32_t socket_id_;
      std::string name_;
      std::uint64_t process_id_;
      labels labels_;
      session_status status_ = session_status::OPEN;
      std::string error_;

      std::unique_ptr<serialization::socket_rpc_server> rpc_server_;
      std::string dir_path_;
      std::vector<std::shared_ptr<serialization::promise<void>>> inflight_;

      std::int64_t io_file_ = -1;
      std::int64_t logs_file_ = -1;
      std::int64_t spans_file_ = -1;
      std::int64_t metrics_file_ = -1;
      std::shared_ptr<serialization::promise<void>> truncate_chain_;

      void ensure_open();
      void track(std::shared_ptr<serialization::promise<void>> p);
      std::shared_ptr<serialization::promise<void>> wait_inflight();
      std::shared_ptr<serialization::promise<void>> sync_open_files();
      std::shared_ptr<serialization::promise<void>> close_open_files();
      std::shared_ptr<serialization::promise<void>> write_meta();
      std::shared_ptr<serialization::promise<void>> append_payload(std::int64_t& file_id, std::string filename, std::vector<std::uint8_t> data);
      std::shared_ptr<serialization::promise<void>> write_file_truncate(std::string filename, std::vector<std::uint8_t> data);
      std::shared_ptr<serialization::promise<void>> write_file_truncate_after(
        std::shared_ptr<serialization::promise<void>> prev,
        std::string filename,
        std::vector<std::uint8_t> data
      );
      std::shared_ptr<serialization::promise<void>> handle_snapshot(debug_snapshot_event ev, std::vector<std::uint8_t> payload);
      std::vector<std::uint8_t> read_payload(size_t payload_size);
      void register_handlers();
  };

  std::shared_ptr<session> get_session_by_socket(std::uint32_t socket_id);
  std::shared_ptr<session> create_session(std::string name, std::uint64_t process_id, labels labels);

}
