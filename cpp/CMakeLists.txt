cmake_minimum_required(VERSION 3.20)
project(Optimizer LANGUAGES CXX)

include(FetchContent)
include(CTest)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
option(BUILD_SHARED_LIBS "Build shared libraries" ON)

FetchContent_Declare(hyrise_sqlparser GIT_REPOSITORY https://github.com/hyrise/sql-parser.git GIT_TAG main)
FetchContent_Populate(hyrise_sqlparser)
set(SQLPARSER_INCLUDE_DIR "${hyrise_sqlparser_SOURCE_DIR}/src")
file(GLOB_RECURSE SQLPARSER_SRC "${SQLPARSER_INCLUDE_DIR}/*.cpp")
add_library(sql-parser STATIC ${SQLPARSER_SRC} code/include/optimizer/Logger.hpp)

FetchContent_Declare(nlohmann_json GIT_REPOSITORY https://github.com/nlohmann/json.git GIT_TAG v3.11.3)
FetchContent_MakeAvailable(nlohmann_json)

find_program(GRAPHVIZ_DOT NAMES dot)
if(NOT GRAPHVIZ_DOT)
  message(WARNING "Graphviz (dot) not found. Please install it.")
endif()

find_package(Protobuf 3.0 QUIET CONFIG)
if(NOT Protobuf_FOUND)
  find_package(Protobuf 3.0 REQUIRED)
endif()

FetchContent_Declare(abseil GIT_REPOSITORY https://github.com/abseil/abseil-cpp.git GIT_TAG 20240722.0)
set(ABSL_PROPAGATE_CXX_STD ON)
FetchContent_MakeAvailable(abseil)

file(GLOB_RECURSE PROJECT_SRC
        CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/code/src/*.[cC][cC]*"
        "${CMAKE_CURRENT_SOURCE_DIR}/code/src/*.[cC][pP][pP]"
        "${CMAKE_CURRENT_SOURCE_DIR}/generated/src/*.[cC][cC]*"
        "${CMAKE_CURRENT_SOURCE_DIR}/generated/src/*.[cC][pP][pP]"
)

file(GLOB_RECURSE PROJECT_HEADERS
        CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/code/include/*.[hH]*"
        "${CMAKE_CURRENT_SOURCE_DIR}/generated/include/*.[hH]*"
)

add_executable(optimizer-server "${CMAKE_CURRENT_SOURCE_DIR}/code/examples/tcp_server_example.cpp" ${PROJECT_SRC} ${PROJECT_HEADERS} code/include/optimizer/Logger.hpp)
add_executable(optimizer-unit "${CMAKE_CURRENT_SOURCE_DIR}/code/examples/OptimizerUnit.cpp" ${PROJECT_SRC} ${PROJECT_HEADERS} code/include/optimizer/QueryManager.hpp code/src/optimizer/QueryManager.cpp code/include/optimizer/Logger.hpp)
add_executable(optimizer-compute-unit "${CMAKE_CURRENT_SOURCE_DIR}/code/examples/ComputeUnit.cpp" ${PROJECT_SRC} ${PROJECT_HEADERS} code/include/optimizer/Logger.hpp)

if(TARGET protobuf::libprotobuf)
  set(PROTOBUF_LIB protobuf::libprotobuf)
elseif(TARGET protobuf::libprotobuf-lite)
  set(PROTOBUF_LIB protobuf::libprotobuf-lite)
else()
  message(FATAL_ERROR "Protobuf library target not found.")
endif()

set(ALL_TARGETS optimizer-server optimizer-unit optimizer-compute-unit)

foreach(t ${ALL_TARGETS})
  target_include_directories(${t} PUBLIC
          ${CMAKE_CURRENT_SOURCE_DIR}/code/include
          ${CMAKE_CURRENT_SOURCE_DIR}/generated/include
  )
  target_link_libraries(${t} PRIVATE
          ${PROTOBUF_LIB}
          absl::log
          absl::log_internal_check_op
          sql-parser
          nlohmann_json::nlohmann_json
  )
  if(MSVC)
    target_compile_options(${t} PRIVATE /W4 /permissive-)
  else()
    target_compile_options(${t} PRIVATE -Wall -Wextra -Wpedantic)
  endif()
endforeach()

target_include_directories(sql-parser PUBLIC ${SQLPARSER_INCLUDE_DIR})

if (BUILD_TESTING)
  FetchContent_Declare(googletest GIT_REPOSITORY https://github.com/google/googletest.git GIT_TAG v1.17.0)
  FetchContent_MakeAvailable(googletest)
  enable_testing()

  add_executable(optimizer-tests
          ${CMAKE_CURRENT_SOURCE_DIR}/code/tests/test_sql_parser.cpp
          ${CMAKE_CURRENT_SOURCE_DIR}/code/tests/test_sql_to_logical_plan_translator.cpp
          ${CMAKE_CURRENT_SOURCE_DIR}/code/tests/test_json_to_logical_plan_translator.cpp
          ${CMAKE_CURRENT_SOURCE_DIR}/code/tests/test_physical_optimizer.cpp
          ${PROJECT_SRC}
          ${PROJECT_HEADERS}
          code/include/optimizer/Logger.hpp
  )

  target_include_directories(optimizer-tests PUBLIC
          ${CMAKE_CURRENT_SOURCE_DIR}/code/include
          ${CMAKE_CURRENT_SOURCE_DIR}/generated/include
  )

  target_compile_definitions(optimizer-tests PRIVATE
          OPTIMIZER_REPO_ROOT="${CMAKE_CURRENT_SOURCE_DIR}/.."
  )

  target_link_libraries(optimizer-tests PRIVATE
          ${PROTOBUF_LIB}
          absl::log
          absl::log_internal_check_op
          sql-parser
          GTest::gtest_main
          nlohmann_json::nlohmann_json
  )

  include(GoogleTest)
  gtest_discover_tests(optimizer-tests)
endif()
