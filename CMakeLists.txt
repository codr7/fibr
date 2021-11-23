cmake_minimum_required(VERSION 3.5)

project(fibr)
add_compile_options(-std=c11 -Wall -Werror -g -O2)
add_executable(fibr src/fibr.c)
