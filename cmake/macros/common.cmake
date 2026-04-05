# common macros
# this file will also load platform specific macros

# platform specific macros
if(WIN32)
    include(${CMAKE_MODULE_PATH}/macros/windows.cmake)
endif()
