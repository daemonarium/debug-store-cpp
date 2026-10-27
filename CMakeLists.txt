cmake_minimum_required(VERSION 3.17)
project(debugStore)

set(CMAKE_CXX_STANDARD 26)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(TARGET cpp_rpc)
  set(RPC_CPP_DIR ${CMAKE_SOURCE_DIR}/daemonarium/rpc/rpc-cpp)
else()
  set(RPC_CPP_DIR ${CMAKE_CURRENT_SOURCE_DIR}/extern/rpc-cpp)
  if(NOT EXISTS ${RPC_CPP_DIR}/CMakeLists.txt)
    message(FATAL_ERROR
      "rpc-cpp not found. git submodule add https://github.com/daemonarium/rpc-cpp.git extern/rpc-cpp")
  endif()
endif()

include_directories(src)
aux_source_directory(src SRC_FILES)

set(WASM_FILES ${RPC_CPP_DIR}/src/utils/socket_handlers.cpp)
aux_source_directory(${RPC_CPP_DIR}/src/io IO_FILES)
aux_source_directory(${RPC_CPP_DIR}/src/socket SOCKET_FILES)

set(EXECUTABLE_NAME debug-store)

if(WASM)
  add_executable(${EXECUTABLE_NAME} ${SRC_FILES} ${WASM_FILES} ${IO_FILES} ${SOCKET_FILES})
  target_compile_options(${EXECUTABLE_NAME} PRIVATE
    -fwasm-exceptions
  )
  target_link_options(${EXECUTABLE_NAME} PRIVATE
    -s STANDALONE_WASM=1
    -fwasm-exceptions
    -mbulk-memory
    -Wl,--import-memory
    --no-entry
    -sALLOW_MEMORY_GROWTH=1
    -sINITIAL_MEMORY=2MB
    -sTOTAL_STACK=64000
    -sEXPORTED_FUNCTIONS=_rpc_description,_start,_malloc,_waos_socket_data,_waos_socket_output_drain
    -sERROR_ON_UNDEFINED_SYMBOLS=0
    -Wl,--allow-undefined-file=${RPC_CPP_DIR}/src/examples/modules/allowed-undefined-symbols.syms
  )

  target_include_directories(${EXECUTABLE_NAME} PRIVATE
    src
    ${RPC_CPP_DIR}/src
  )
  target_compile_definitions(${EXECUTABLE_NAME}
    PRIVATE
      RPC_SERVER
      RPC_TYPE_DESCRIPTION
  )
  set_target_properties(${EXECUTABLE_NAME} PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}
    SUFFIX ".wasm"
  )
endif()
