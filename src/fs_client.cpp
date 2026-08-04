#include "fs_client.hpp"
#include "sockets.hpp"
#include <socket/socket.h>
#include <utils/socket_handlers.hpp>

namespace waos {
  namespace fs_client {

    std::shared_ptr<rpc_client_type> rpc_client;
    std::shared_ptr<waos::fs_rpc<rpc_client_type*>> fs_rpc;

    namespace {
      std::shared_ptr<serialization::promise<void>> gate = serialization::promise<void>::resolved();
    }

    template <typename T>
    std::shared_ptr<serialization::promise<T>> run_serialized(
        std::function<std::shared_ptr<serialization::promise<T>>()> op
    ) {
      auto result = std::make_shared<serialization::promise<T>>();
      auto prev = gate;
      auto next = std::make_shared<serialization::promise<void>>();
      gate = next;

      auto run_op = [op, result, next]() {
        op()->handle([result, next](T& value) {
          result->resolve(value);
          next->resolve();
        }, [result, next](std::exception_ptr e) {
          result->reject(e);
          next->resolve();
        });
      };

      prev->handle(run_op, [run_op](std::exception_ptr) { run_op(); });
      return result;
    }

    template <>
    std::shared_ptr<serialization::promise<void>> run_serialized<void>(
        std::function<std::shared_ptr<serialization::promise<void>>()> op
    ) {
      auto result = std::make_shared<serialization::promise<void>>();
      auto prev = gate;
      auto next = std::make_shared<serialization::promise<void>>();
      gate = next;

      auto run_op = [op, result, next]() {
        op()->handle([result, next]() {
          result->resolve();
          next->resolve();
        }, [result, next](std::exception_ptr e) {
          result->reject(e);
          next->resolve();
        });
      };

      prev->handle(run_op, [run_op](std::exception_ptr) { run_op(); });
      return result;
    }

    template std::shared_ptr<serialization::promise<std::uint32_t>> run_serialized<std::uint32_t>(
        std::function<std::shared_ptr<serialization::promise<std::uint32_t>>()>
    );

    void start() {
      gate = serialization::promise<void>::resolved();

      struct socket_options socket_options = {
        .input_buffer_size = 65536,
        .output_buffer_size = 65536,
        .flush_threshold = 4096,
        .data_event_threshold = 0,
        .drain_event_threshold = 4,
        .flags = 0,
      };
      std::uint32_t socket_id = static_cast<std::uint32_t>(base_socket_id::FS);
      socket_open(socket_id, &socket_options);

      rpc_client = std::make_shared<rpc_client_type>(socket_id);
      rpc_client->register_basic_types();
      fs_rpc = std::make_shared<waos::fs_rpc<rpc_client_type*>>(rpc_client.get());

      auto client_ptr = rpc_client.get();
      socket_handlers::register_socket_handler(socket_id, [client_ptr]() {
        client_ptr->handle_data();
      });
    }

  }
}
