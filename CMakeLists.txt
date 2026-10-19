cmake_minimum_required(VERSION 3.28)

project(antor-engine LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif()

option(ANTOR_ENABLE_SANITIZERS "Build server targets with AddressSanitizer and UBSan" OFF)

find_package(Python3 COMPONENTS Interpreter REQUIRED)
find_package(Threads REQUIRED)

execute_process(
  COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tools/fetch_flatbuffers.py"
  WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
  COMMAND_ERROR_IS_FATAL ANY
)

set(ANTOR_FLATBUFFERS_ROOT "${CMAKE_SOURCE_DIR}/.deps/flatbuffers" CACHE INTERNAL "")
set(ANTOR_FLATC "${ANTOR_FLATBUFFERS_ROOT}/flatc" CACHE INTERNAL "")
set(ANTOR_FLATBUFFERS_INCLUDE "${ANTOR_FLATBUFFERS_ROOT}/src/include" CACHE INTERNAL "")

if(NOT EXISTS "${ANTOR_FLATC}")
  message(FATAL_ERROR "flatc was not fetched: ${ANTOR_FLATC}")
endif()

set(ANTOR_FBS "${CMAKE_SOURCE_DIR}/protocol/antor.fbs")
set(ANTOR_GENERATED_DIR "${CMAKE_BINARY_DIR}/generated")
set(ANTOR_GENERATED_HEADER "${ANTOR_GENERATED_DIR}/antor_generated.h")

file(MAKE_DIRECTORY "${ANTOR_GENERATED_DIR}")
add_custom_command(
  OUTPUT "${ANTOR_GENERATED_HEADER}"
  COMMAND "${ANTOR_FLATC}" --cpp --scoped-enums -o "${ANTOR_GENERATED_DIR}" "${ANTOR_FBS}"
  DEPENDS "${ANTOR_FBS}" "${ANTOR_FLATC}"
  COMMENT "Generating C++ protocol code"
  VERBATIM
)
add_custom_target(antor_protocol_gen DEPENDS "${ANTOR_GENERATED_HEADER}")

add_library(antor_warnings INTERFACE)
target_compile_options(antor_warnings INTERFACE
  -Wall
  -Wextra
  -Wpedantic
  -Werror
  -Wshadow
  -Wnon-virtual-dtor
  -Wold-style-cast
  -Wcast-align
  -Wunused
  -Woverloaded-virtual
  -Wnull-dereference
  -Wdouble-promotion
  -Wformat=2
  -Wimplicit-fallthrough
)
target_compile_definitions(antor_warnings INTERFACE _GNU_SOURCE)

if(ANTOR_ENABLE_SANITIZERS)
  target_compile_options(antor_warnings INTERFACE
    -fsanitize=address,undefined
    -fno-omit-frame-pointer
  )
  target_link_options(antor_warnings INTERFACE -fsanitize=address,undefined)
endif()

enable_testing()
add_subdirectory(server)
