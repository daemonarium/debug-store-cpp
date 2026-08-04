#pragma once

#include <serialization/socket_rpc_client.hpp>
#include <serialization/promise.hpp>
#include <memory>
#include <vector>
#include <cstdint>
#include <string>
#include <functional>

namespace waos {
  namespace fs_client {
    template <typename T>
    std::shared_ptr<serialization::promise<T>> run_serialized(
        std::function<std::shared_ptr<serialization::promise<T>>()> op
    );
  }

  // Method IDs must match ts-fs createGeneratedTypeRegistry + assignMethodIds order.
  // Return types follow generated fsApi (open/write use uint32 on the wire).
  template <typename rpc_client_ptr_type>
  class fs_rpc {
    public:
      using file_id = std::uint32_t;

      explicit fs_rpc(rpc_client_ptr_type rpc_client) : rpc_client_(rpc_client) {}

      std::shared_ptr<serialization::promise<void>> mkdir(std::string path) {
        return fs_client::run_serialized<void>([this, path = std::move(path)]() {
          return rpc_client_->template call<void>(0, path);
        });
      }

      std::shared_ptr<serialization::promise<file_id>> open(std::string path, std::uint32_t flags) {
        return fs_client::run_serialized<file_id>([this, path = std::move(path), flags]() {
          return rpc_client_->template call<file_id>(7, path, flags);
        });
      }

      std::shared_ptr<serialization::promise<void>> close(file_id id) {
        return fs_client::run_serialized<void>([this, id]() {
          return rpc_client_->template call<void>(8, id);
        });
      }

      std::shared_ptr<serialization::promise<std::uint32_t>> write(file_id id, std::vector<std::uint8_t> data) {
        return fs_client::run_serialized<std::uint32_t>([this, id, data = std::move(data)]() {
          return rpc_client_->template call<std::uint32_t>(9, id, data);
        });
      }

      std::shared_ptr<serialization::promise<void>> sync(file_id id) {
        return fs_client::run_serialized<void>([this, id]() {
          return rpc_client_->template call<void>(12, id);
        });
      }

    private:
      rpc_client_ptr_type rpc_client_;
  };

}
