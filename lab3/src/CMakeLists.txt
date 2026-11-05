cmake_minimum_required(VERSION 3.10)
project(lab3 C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_FLAGS_RELEASE "-O2")
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

find_package(Threads REQUIRED)

add_executable(kmeans kmeans.c)
target_link_libraries(kmeans Threads::Threads)

add_executable(gen gen.c)
target_link_libraries(gen m)
