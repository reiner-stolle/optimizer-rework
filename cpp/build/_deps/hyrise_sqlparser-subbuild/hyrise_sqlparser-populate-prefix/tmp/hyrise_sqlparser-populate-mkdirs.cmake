# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file Copyright.txt or https://cmake.org/licensing for details.

cmake_minimum_required(VERSION 3.5)

file(MAKE_DIRECTORY
  "/home/stolle/ws25-optimizer-cpp/cpp/build/_deps/hyrise_sqlparser-src"
  "/home/stolle/ws25-optimizer-cpp/cpp/build/_deps/hyrise_sqlparser-build"
  "/home/stolle/ws25-optimizer-cpp/cpp/build/_deps/hyrise_sqlparser-subbuild/hyrise_sqlparser-populate-prefix"
  "/home/stolle/ws25-optimizer-cpp/cpp/build/_deps/hyrise_sqlparser-subbuild/hyrise_sqlparser-populate-prefix/tmp"
  "/home/stolle/ws25-optimizer-cpp/cpp/build/_deps/hyrise_sqlparser-subbuild/hyrise_sqlparser-populate-prefix/src/hyrise_sqlparser-populate-stamp"
  "/home/stolle/ws25-optimizer-cpp/cpp/build/_deps/hyrise_sqlparser-subbuild/hyrise_sqlparser-populate-prefix/src"
  "/home/stolle/ws25-optimizer-cpp/cpp/build/_deps/hyrise_sqlparser-subbuild/hyrise_sqlparser-populate-prefix/src/hyrise_sqlparser-populate-stamp"
)

set(configSubDirs )
foreach(subDir IN LISTS configSubDirs)
    file(MAKE_DIRECTORY "/home/stolle/ws25-optimizer-cpp/cpp/build/_deps/hyrise_sqlparser-subbuild/hyrise_sqlparser-populate-prefix/src/hyrise_sqlparser-populate-stamp/${subDir}")
endforeach()
