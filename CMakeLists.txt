cmake_minimum_required(VERSION 3.15)

project(m0ss)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

add_library(m0ss STATIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src/m0ss.c
)

target_include_directories(m0ss PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
