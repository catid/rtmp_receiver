cmake_minimum_required(VERSION 3.10)
project(rtmp_parser)

# Set the C++ standard
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED True)

add_executable(rtmp_receiver
    rtmp_receiver.cpp
    rtmp_parser.cpp
    bytestream.cpp
)
