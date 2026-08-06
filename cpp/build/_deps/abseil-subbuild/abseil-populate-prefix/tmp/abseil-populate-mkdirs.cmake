# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file Copyright.txt or https://cmake.org/licensing for details.

cmake_minimum_required(VERSION 3.5)

file(MAKE_DIRECTORY
  "/home/stolle/ws25-optimizer-cpp/cpp/build/_deps/abseil-src"
  "/home/stolle/ws25-optimizer-cpp/cpp/build/_deps/abseil-build"
  "/home/stolle/ws25-optimizer-cpp/cpp/build/_deps/abseil-subbuild/abseil-populate-prefix"
  "/home/stolle/ws25-optimizer-cpp/cpp/build/_deps/abseil-subbuild/abseil-populate-prefix/tmp"
  "/home/stolle/ws25-optimizer-cpp/cpp/build/_deps/abseil-subbuild/abseil-populate-prefix/src/abseil-populate-stamp"
  "/home/stolle/ws25-optimizer-cpp/cpp/build/_deps/abseil-subbuild/abseil-populate-prefix/src"
  "/home/stolle/ws25-optimizer-cpp/cpp/build/_deps/abseil-subbuild/abseil-populate-prefix/src/abseil-populate-stamp"
)

set(configSubDirs )
foreach(subDir IN LISTS configSubDirs)
    file(MAKE_DIRECTORY "/home/stolle/ws25-optimizer-cpp/cpp/build/_deps/abseil-subbuild/abseil-populate-prefix/src/abseil-populate-stamp/${subDir}")
endforeach()
