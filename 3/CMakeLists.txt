cmake_minimum_required(VERSION 3.19)
project(progtest_3)

set(CMAKE_CXX_STANDARD 17)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -pedantic -g" )
add_executable(progtest_3 solution.cpp another_test.inc cool_test.inc)