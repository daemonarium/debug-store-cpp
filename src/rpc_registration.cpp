#include "rpc_registration.hpp"

namespace waos {
  namespace rpc_registration {

    serialization::type_registry registry(true);
    serialization::type_registry* active_registry = &registry;

    static waos::debug_store debug_store_api;
    static waos::debug_session debug_session_api;

    void register_messages(serialization::flat_socket_rpc_server* server, bool register_type) {
      waos::debug_snapshot_event snapEv;
      register_rpc(server, snapEv, register_type);
      waos::debug_io_event ioEv;
      register_rpc(server, ioEv, register_type);
      waos::debug_logs_event logsEv;
      register_rpc(server, logsEv, register_type);
      waos::debug_spans_event spansEv;
      register_rpc(server, spansEv, register_type);
      waos::debug_metrics_event metricsEv;
      register_rpc(server, metricsEv, register_type);
      waos::debug_bindings_event bindingsEv;
      register_rpc(server, bindingsEv, register_type);
      waos::debug_registry_event registryEv;
      register_rpc(server, registryEv, register_type);
      waos::debug_code_event codeEv;
      register_rpc(server, codeEv, register_type);
      waos::session_meta sessionMeta;
      register_rpc(server, sessionMeta, register_type);
      waos::snapshot_meta snapshotMeta;
      register_rpc(server, snapshotMeta, register_type);
      waos::bindings_snapshot bindingsSnap;
      register_rpc(server, bindingsSnap, register_type);

      #ifdef RPC_SERVER
      if(server) {
        // Wire indices after register_basic_types: 0..2 = rpc_*, then these messages.
        server->register_message_type<waos::debug_snapshot_event>();
        server->register_message_type<waos::debug_io_event>();
        server->register_message_type<waos::debug_logs_event>();
        server->register_message_type<waos::debug_spans_event>();
        server->register_message_type<waos::debug_metrics_event>();
        server->register_message_type<waos::debug_bindings_event>();
        server->register_message_type<waos::debug_registry_event>();
        server->register_message_type<waos::debug_code_event>();
      }
      #endif
    }

    void register_exceptions(serialization::flat_socket_rpc_server* server, bool register_type) {
      waos::debug_error e1;
      register_exception(server, e1, register_type);
      waos::debug_invalid_argument e2;
      register_exception(server, e2, register_type);
      waos::debug_conflict e3;
      register_exception(server, e3, register_type);
      waos::debug_closed e4;
      register_exception(server, e4, register_type);
      waos::debug_io_error e5;
      register_exception(server, e5, register_type);
      std::runtime_error runtimeEx("Runtime error");
      register_exception(server, runtimeEx, register_type);
    }

    void register_user_api(serialization::flat_socket_rpc_server* server, bool register_type) {
      if(server != nullptr) server->register_basic_types();
      waos::begin_session_request beginReq;
      register_rpc(server, beginReq, register_type);
      waos::begin_session_result beginRes;
      register_rpc(server, beginRes, register_type);
      register_rpc(server, debug_store_api, register_type);
      register_exceptions(server, register_type);
    }

    void register_session_api(serialization::flat_socket_rpc_server* server, bool register_type) {
      if(server != nullptr) server->register_basic_types();
      register_rpc(server, debug_session_api, register_type);
      register_messages(server, register_type);
      register_exceptions(server, register_type);
    }

    void register_all_types() {
      register_user_api(nullptr, true);
      register_rpc(nullptr, debug_session_api, true);
    }

  }
}
