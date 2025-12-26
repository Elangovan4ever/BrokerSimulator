# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file Copyright.txt or https://cmake.org/licensing for details.

cmake_minimum_required(VERSION 3.5)

file(MAKE_DIRECTORY
  "/home/elan/projects/BrokerSimulator/build_asan/_deps/httplib-src"
  "/home/elan/projects/BrokerSimulator/build_asan/_deps/httplib-build"
  "/home/elan/projects/BrokerSimulator/build_asan/_deps/httplib-subbuild/httplib-populate-prefix"
  "/home/elan/projects/BrokerSimulator/build_asan/_deps/httplib-subbuild/httplib-populate-prefix/tmp"
  "/home/elan/projects/BrokerSimulator/build_asan/_deps/httplib-subbuild/httplib-populate-prefix/src/httplib-populate-stamp"
  "/home/elan/projects/BrokerSimulator/build_asan/_deps/httplib-subbuild/httplib-populate-prefix/src"
  "/home/elan/projects/BrokerSimulator/build_asan/_deps/httplib-subbuild/httplib-populate-prefix/src/httplib-populate-stamp"
)

set(configSubDirs )
foreach(subDir IN LISTS configSubDirs)
    file(MAKE_DIRECTORY "/home/elan/projects/BrokerSimulator/build_asan/_deps/httplib-subbuild/httplib-populate-prefix/src/httplib-populate-stamp/${subDir}")
endforeach()
if(cfgdir)
  file(MAKE_DIRECTORY "/home/elan/projects/BrokerSimulator/build_asan/_deps/httplib-subbuild/httplib-populate-prefix/src/httplib-populate-stamp${cfgdir}") # cfgdir has leading slash
endif()
