cmake_minimum_required(VERSION 3.0)

project(sunshine_tools)

include_directories("${CMAKE_SOURCE_DIR}")

add_executable(ddprobe ddprobe.cpp)
set_target_properties(ddprobe PROPERTIES CXX_STANDARD 20)
target_link_libraries(ddprobe
        ${CMAKE_THREAD_LIBS_INIT}
        dxgi
        d3d11
        ${PLATFORM_LIBRARIES})
target_compile_options(ddprobe PRIVATE ${SUNSHINE_COMPILE_OPTIONS})
