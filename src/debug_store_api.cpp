#include <examples/structures/debug_store.hpp>
#include "session.hpp"

namespace waos {

  std::shared_ptr<serialization::promise<begin_session_result>> debug_store::begin(begin_session_request request) {
    auto sess = create_session(std::move(request.name), request.process_id, std::move(request.labels));
    co_await sess->initialize_storage();

    begin_session_result result;
    result.session_id = sess->session_id();
    result.store_process = g_own_process_id;
    result.store_socket = sess->socket_id();
    co_return result;
  }

  std::shared_ptr<serialization::promise<void>> debug_session::set_labels(labels labels) {
    auto sess = get_session_by_socket(session_request_context.socket_id);
    if(!sess) {
      throw debug_invalid_argument("no session for socket");
    }
    co_await sess->set_labels(std::move(labels));
    co_return;
  }

  std::shared_ptr<serialization::promise<void>> debug_session::sync() {
    auto sess = get_session_by_socket(session_request_context.socket_id);
    if(!sess) {
      throw debug_invalid_argument("no session for socket");
    }
    co_await sess->sync();
    co_return;
  }

  std::shared_ptr<serialization::promise<void>> debug_session::seal() {
    auto sess = get_session_by_socket(session_request_context.socket_id);
    if(!sess) {
      throw debug_invalid_argument("no session for socket");
    }
    co_await sess->seal();
    co_return;
  }

  std::shared_ptr<serialization::promise<void>> debug_session::fail(std::string error) {
    auto sess = get_session_by_socket(session_request_context.socket_id);
    if(!sess) {
      throw debug_invalid_argument("no session for socket");
    }
    co_await sess->fail(std::move(error));
    co_return;
  }

}
