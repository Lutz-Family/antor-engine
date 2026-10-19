add_library(antor_core
  src/config.cpp
  src/server.cpp
  src/protocol/codec.cpp
  src/net/socket.cpp
  src/net/net_thread.cpp
  src/sim/sim_thread.cpp
  src/zone/zone.cpp
  src/zone/interest.cpp
  "${ANTOR_GENERATED_HEADER}"
)
add_dependencies(antor_core antor_protocol_gen)
target_include_directories(antor_core
  PUBLIC "${CMAKE_SOURCE_DIR}/server/include"
)
target_include_directories(antor_core SYSTEM
  PUBLIC
    "${ANTOR_FLATBUFFERS_INCLUDE}"
    "${ANTOR_GENERATED_DIR}"
)
target_link_libraries(antor_core PUBLIC antor_warnings Threads::Threads)
set_target_properties(antor_core PROPERTIES OUTPUT_NAME antor_core)

add_executable(antor-server src/main.cpp)
target_link_libraries(antor-server PRIVATE antor_core)
set_target_properties(antor-server PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
)

add_executable(antor_tests
  tests/test_main.cpp
  tests/config_test.cpp
  tests/spsc_queue_test.cpp
  tests/tick_clock_test.cpp
  tests/frame_test.cpp
  tests/codec_test.cpp
  tests/interest_test.cpp
  tests/session_test.cpp
)
target_link_libraries(antor_tests PRIVATE antor_core)
target_include_directories(antor_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/tests")
set_target_properties(antor_tests PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
)

add_test(NAME antor_tests COMMAND antor_tests)
set_tests_properties(antor_tests PROPERTIES
  TIMEOUT 60
  ENVIRONMENT "ASAN_OPTIONS=halt_on_error=1:detect_leaks=1;UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1"
)
