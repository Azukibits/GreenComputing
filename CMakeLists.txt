cmake_minimum_required(VERSION 3.20)
project(GreenComputing LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include_directories(${CMAKE_SOURCE_DIR})

if(APPLE)
    enable_language(OBJCXX)
    add_executable(GreenComputing MACOSX_BUNDLE gui_main.mm)
    set_target_properties(GreenComputing PROPERTIES
        MACOSX_BUNDLE_BUNDLE_NAME "GreenComputing"
        MACOSX_BUNDLE_GUI_IDENTIFIER "com.azukibits.greencomputing"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "0.1.0"
        MACOSX_BUNDLE_BUNDLE_VERSION "0.1.0"
    )
    target_link_libraries(GreenComputing PRIVATE "-framework Cocoa")
    target_compile_options(GreenComputing PRIVATE
        $<$<COMPILE_LANGUAGE:CXX,OBJCXX>:-Wall -Wextra -Wpedantic>
        $<$<COMPILE_LANGUAGE:OBJCXX>:-fobjc-arc>
    )
endif()

add_executable(GreenComputingCLI main.cpp)
target_compile_options(GreenComputingCLI PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-Wall -Wextra -Wpedantic>
    $<$<CXX_COMPILER_ID:MSVC>:/W4>
)
