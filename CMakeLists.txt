cmake_minimum_required(VERSION 3.10)
project(rtmp_receiver)

# Set the default build type if not specified
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Choose the type of build." FORCE)
endif()

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED True)

# Find FFmpeg libraries
find_package(PkgConfig REQUIRED)
pkg_check_modules(AVCODEC REQUIRED libavcodec)
pkg_check_modules(AVFORMAT REQUIRED libavformat)
pkg_check_modules(AVUTIL REQUIRED libavutil)
pkg_check_modules(SWSCALE REQUIRED libswscale)

# Include directories
include_directories(${AVCODEC_INCLUDE_DIRS})
include_directories(${AVFORMAT_INCLUDE_DIRS})
include_directories(${AVUTIL_INCLUDE_DIRS})
include_directories(${SWSCALE_INCLUDE_DIRS})

add_library(rtmp_tools
    rtmp_tools.cpp
    rtmp_tools.h
    rtmp_receiver.cpp
    rtmp_receiver.h
    rtmp_parser.cpp
    rtmp_parser.h
    avcc_parser.cpp
    avcc_parser.h
    bytestream.cpp
    bytestream.h
)

add_executable(rtmp_receiver_test
    main.cpp
)
target_link_libraries(rtmp_receiver_test
    rtmp_tools
    ${AVCODEC_LIBRARIES}
    ${AVFORMAT_LIBRARIES}
    ${AVUTIL_LIBRARIES}
    ${SWSCALE_LIBRARIES}
)
