# common compile definitions
# this file will also load platform specific definitions

list(APPEND SUNSHINE_COMPILE_OPTIONS -Wall -Wno-sign-compare)
# Wall - enable all warnings
# Wno-sign-compare - disable warnings for signed/unsigned comparisons

# setup assets directory
if(NOT SUNSHINE_ASSETS_DIR)
    set(SUNSHINE_ASSETS_DIR "assets")
endif()

# platform specific compile definitions
if(WIN32)
    include(${CMAKE_MODULE_PATH}/compile_definitions/windows.cmake)
elseif(UNIX)
    include(${CMAKE_MODULE_PATH}/compile_definitions/unix.cmake)

    if(APPLE)
        include(${CMAKE_MODULE_PATH}/compile_definitions/macos.cmake)
    else()
        include(${CMAKE_MODULE_PATH}/compile_definitions/linux.cmake)
    endif()
endif()

include_directories(SYSTEM third-party/nv-codec-headers/include)
file(GLOB NVENC_SOURCES CONFIGURE_DEPENDS "src/nvenc/*.cpp" "src/nvenc/*.h")
list(APPEND PLATFORM_TARGET_FILES ${NVENC_SOURCES})

include_directories(${CMAKE_CURRENT_BINARY_DIR})

set(TEST_TARGET_FILES 
        src/dll.h
        src/main.cpp)
set(SUNSHINE_TARGET_FILES
        src/cbs.cpp
        src/utility.h
        src/uuid.h
        src/config.h
        src/config.cpp
        src/main.h
        src/dll.h
        src/dll.cpp
        src/video.cpp
        src/video.h
        src/video_colorspace.cpp
        src/video_colorspace.h
        src/platform/common.h
        src/move_by_copy.h
        src/thread_safe.h
        src/sync.h
        ${PLATFORM_TARGET_FILES})


if(NOT SUNSHINE_ASSETS_DIR_DEF)
    set(SUNSHINE_ASSETS_DIR_DEF "${SUNSHINE_ASSETS_DIR}")
endif()
list(APPEND SUNSHINE_DEFINITIONS SUNSHINE_ASSETS_DIR="${SUNSHINE_ASSETS_DIR_DEF}")

list(APPEND SUNSHINE_DEFINITIONS SUNSHINE_TRAY=${SUNSHINE_TRAY})

include_directories(${CMAKE_CURRENT_SOURCE_DIR})

include_directories(
        SYSTEM
        ${CMAKE_CURRENT_SOURCE_DIR}/third-party
        ${FFMPEG_INCLUDE_DIRS}
        ${PLATFORM_INCLUDE_DIRS}
)

string(TOUPPER "x${CMAKE_BUILD_TYPE}" BUILD_TYPE)
if("${BUILD_TYPE}" STREQUAL "XDEBUG")
    if(WIN32)
        set_source_files_properties(src/nvhttp.cpp PROPERTIES COMPILE_FLAGS -O2)
    endif()
else()
    add_definitions(-DNDEBUG)
endif()

list(APPEND SUNSHINE_EXTERNAL_LIBRARIES
        ${CMAKE_THREAD_LIBS_INIT}
        opus
        ${FFMPEG_LIBRARIES}
        ${Boost_LIBRARIES}
        ${OPENSSL_LIBRARIES}
        ${CURL_LIBRARIES}
        ${PLATFORM_LIBRARIES})
