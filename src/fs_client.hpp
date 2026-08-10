#pragma once

#include <serialization/socket_rpc_client.hpp>
#include <serialization/promise.hpp>
#include <memory>
#include <functional>
#include "fs_rpc.hpp"

namespace waos {
  namespace fs_client {

    using rpc_client_type = serialization::flat_socket_rpc_client;

    extern std::shared_ptr<rpc_client_type> rpc_client;
    extern std::shared_ptr<waos::fs_rpc<rpc_client_type*>> fs_rpc;

    // Serialize FS RPCs — concurrent calls on one socket_rpc_client corrupt response handling.
    template <typename T>
    std::shared_ptr<serialization::promise<T>> run_serialized(
        std::function<std::shared_ptr<serialization::promise<T>>()> op
    );

    template <>
    std::shared_ptr<serialization::promise<void>> run_serialized<void>(
        std::function<std::shared_ptr<serialization::promise<void>>()> op
    );

    void start();

  }
}
