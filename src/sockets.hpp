#pragma once

#include <cstdint>

enum class base_socket_id : std::uint32_t {
  USER = 0,
  FS = 1,
  FIRST_SESSION = 2,
};
