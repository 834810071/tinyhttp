cmake_minimum_required(VERSION 3.13)
project(tinyhttp)

set(CMAKE_CXX_STANDARD 14)

add_executable(tinyhttp httpd.c)
add_executable(main main.cpp)

target_link_libraries(tinyhttp pthread)