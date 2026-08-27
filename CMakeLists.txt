cmake_minimum_required(VERSION 3.10)

project(gc)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_executable(gc src/main.c src/gc/tracing.c)
