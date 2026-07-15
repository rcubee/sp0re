cmake_minimum_required(VERSION 3.15)

project(sp0re)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

add_library(sp0re STATIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src/sp0re.c
)

target_include_directories(sp0re PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
