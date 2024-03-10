cmake_minimum_required(VERSION 3.10)
project(rtmp_receiver)

# Set the default build type if not specified
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Choose the type of build." FORCE)
endif()

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED True)

add_library(rtmp_tools
    rtmp_tools.cpp
    rtmp_tools.h
    rtmp_receiver.cpp
    rtmp_receiver.h
    rtmp_parser.cpp
    rtmp_parser.h
    bytestream.cpp
    bytestream.h
)

add_executable(rtmp_receiver_test
    main.cpp
)
target_link_libraries(rtmp_receiver_test rtmp_tools)
