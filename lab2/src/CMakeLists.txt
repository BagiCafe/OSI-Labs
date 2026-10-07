cmake_minimum_required(VERSION 3.10)
project(labaOC2 C)

set(CMAKE_C_STANDARD 11)

add_executable(child child.c)
add_executable(parent parent.c)

# чтобы при запуске parent всегда собирался и child
add_dependencies(parent child)