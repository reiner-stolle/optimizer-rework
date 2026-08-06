# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file Copyright.txt or https://cmake.org/licensing for details.

cmake_minimum_required(VERSION 3.5)

file(MAKE_DIRECTORY
  "/home/stolle/ws25-optimizer-cpp/cpp/build/_deps/nlohmann_json-src"
  "/home/stolle/ws25-optimizer-cpp/cpp/build/_deps/nlohmann_json-build"
  "/home/stolle/ws25-optimizer-cpp/cpp/build/_deps/nlohmann_json-subbuild/nlohmann_json-populate-prefix"
  "/home/stolle/ws25-optimizer-cpp/cpp/build/_deps/nlohmann_json-subbuild/nlohmann_json-populate-prefix/tmp"
  "/home/stolle/ws25-optimizer-cpp/cpp/build/_deps/nlohmann_json-subbuild/nlohmann_json-populate-prefix/src/nlohmann_json-populate-stamp"
  "/home/stolle/ws25-optimizer-cpp/cpp/build/_deps/nlohmann_json-subbuild/nlohmann_json-populate-prefix/src"
  "/home/stolle/ws25-optimizer-cpp/cpp/build/_deps/nlohmann_json-subbuild/nlohmann_json-populate-prefix/src/nlohmann_json-populate-stamp"
)

set(configSubDirs )
foreach(subDir IN LISTS configSubDirs)
    file(MAKE_DIRECTORY "/home/stolle/ws25-optimizer-cpp/cpp/build/_deps/nlohmann_json-subbuild/nlohmann_json-populate-prefix/src/nlohmann_json-populate-stamp/${subDir}")
endforeach()
