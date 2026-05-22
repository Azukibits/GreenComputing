cmake_minimum_required(VERSION 3.20)
project(GreenComputing LANGUAGES CXX OBJCXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include_directories(${CMAKE_SOURCE_DIR})

add_executable(GreenComputing gui_main.mm)
target_link_libraries(GreenComputing PRIVATE "-framework Cocoa")
target_compile_options(GreenComputing PRIVATE
    $<$<COMPILE_LANGUAGE:CXX,OBJCXX>:-Wall -Wextra -Wpedantic>
    $<$<COMPILE_LANGUAGE:OBJCXX>:-fobjc-arc>
)

add_executable(GreenComputingCLI main.cpp)
target_compile_options(GreenComputingCLI PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-Wall -Wextra -Wpedantic>
)
