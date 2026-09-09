cmake_minimum_required(VERSION 3.20)
project(ctest-demo LANGUAGES NONE)

enable_testing()

add_test(NAME always_passes COMMAND ${CMAKE_COMMAND} -E true)
add_test(NAME also_passes COMMAND ${CMAKE_COMMAND} -E echo ok)
