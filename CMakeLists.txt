# Copyright (c) 2022 Light Labs Inc.
# All Rights Reserved
# Released under the MIT license.

cmake_minimum_required(VERSION 3.0)
project(fsview)
set(CMAKE_CXX_STANDARD 11)

set(CMAKE_CXX_FLAGS "-I${PROJECT_SOURCE_DIR} -Wno-deprecated")

set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -O0 -g")
set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -O3")

if(${CMAKE_SYSTEM_NAME} EQUAL "Android")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fPIE -fPIC")
    set(LINK_FLAGS "${LINK_FLAGS} -Wl,-pie -fuse-ld=gold")
endif()

if(${CMAKE_SYSTEM_PROCESSOR} EQUAL "aarch64")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -march=armv8-a+crc")
endif()

set(LIBRARY_HEADER_FILES
    allsys.h
    wrapper.h
    conf/cmdarg.h
    conf/config.h
    impl/attrib.h
    impl/burner.h
    impl/cd9660.h
    impl/datetm.h
    impl/device.h
    impl/endian.h
    impl/extent.h
    impl/hfplus.h
    impl/mapper.h
    impl/master.h
    impl/rlimit.h
    impl/source.h
    impl/strdec.h
    impl/strenc.h
    impl/strenc.inc
    impl/unique.h
    impl/vfat32.h
    impl/volume.h
)

set(LIBRARY_SOURCE_FILES
    conf/cmdarg.cpp
    conf/config.cpp
    impl/attrib.cpp
    impl/burner.cpp
    impl/cd9660.cpp
    impl/datetm.cpp
    impl/device.cpp
    impl/extent.cpp
    impl/hfplus.cpp
    impl/mapper.cpp
    impl/master.cpp
    impl/rlimit.cpp
    impl/source.cpp
    impl/strdec.cpp
    impl/strenc.cpp
    impl/unique.cpp
    impl/vfat32.cpp
    impl/volume.cpp
)

# Add executable target with source files listed in SOURCE_FILES variable
add_library(fsviewlib STATIC ${LIBRARY_HEADER_FILES} ${LIBRARY_SOURCE_FILES})

foreach(EXEC down fork hash mkfs name temp)
    set(FSVIEW_BINARY "fsview_${EXEC}")
    set(FSVIEW_SOURCE "${FSVIEW_BINARY}.cpp")
    add_executable(${FSVIEW_BINARY} ${FSVIEW_SOURCE})
    target_link_libraries(${FSVIEW_BINARY} fsviewlib)
endforeach()

