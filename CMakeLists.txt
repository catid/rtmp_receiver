cmake_minimum_required(VERSION 3.10)
project(rtmp_receiver)

# Set the C++ standard
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
    bytestream_writer.cpp
    bytestream_writer.h
)

add_executable(rtmp_receiver_test
    main.cpp
)
target_link_libraries(rtmp_receiver_test rtmp_tools)
