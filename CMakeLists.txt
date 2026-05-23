cmake_minimum_required(VERSION 3.20)
project(GreenComputing VERSION 0.3.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include_directories(${CMAKE_SOURCE_DIR})

if(APPLE)
    enable_language(OBJCXX)
    add_executable(GreenComputing MACOSX_BUNDLE gui_main.mm)
    set_target_properties(GreenComputing PROPERTIES
        MACOSX_BUNDLE_BUNDLE_NAME "GreenComputing"
        MACOSX_BUNDLE_GUI_IDENTIFIER "com.azukibits.greencomputing"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
        MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
    )
    target_link_libraries(GreenComputing PRIVATE "-framework Cocoa")
    target_compile_options(GreenComputing PRIVATE
        $<$<COMPILE_LANGUAGE:CXX,OBJCXX>:-Wall -Wextra -Wpedantic>
        $<$<COMPILE_LANGUAGE:OBJCXX>:-fobjc-arc>
    )
else()
    find_package(Qt6 REQUIRED COMPONENTS Widgets)
    qt_standard_project_setup()
    add_executable(GreenComputing qt_main.cpp)
    target_link_libraries(GreenComputing PRIVATE Qt6::Widgets)
    target_compile_options(GreenComputing PRIVATE
        $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-Wall -Wextra -Wpedantic>
        $<$<CXX_COMPILER_ID:MSVC>:/W4>
    )
endif()

add_executable(GreenComputingCLI main.cpp)
target_compile_options(GreenComputingCLI PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-Wall -Wextra -Wpedantic>
    $<$<CXX_COMPILER_ID:MSVC>:/W4>
)
