cmake_minimum_required(VERSION 3.0)

project(SunshineSDK VERSION 0.1.0 DESCRIPTION "Sunshine is a Gamestream host for Moonlight.")

set(CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/cmake)
set(SUNSHINE_SOURCE_ASSETS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/config")


# Ugly hack to compile with #include <qos2.h>
add_compile_definitions(
	QOS_FLOWID=UINT32
	PQOS_FLOWID=UINT32*
	QOS_NON_ADAPTIVE_FLOW=2)

add_subdirectory(third-party/moonlight-common-c/enet)
add_subdirectory(third-party/Simple-Web-Server)

set(UPNPC_BUILD_SHARED OFF CACHE BOOL "no shared libraries")
set(UPNPC_BUILD_TESTS OFF CACHE BOOL "Don't build tests for miniupnpc")
set(UPNPC_BUILD_SAMPLE OFF CACHE BOOL "Don't build samples for miniupnpc")
set(UPNPC_NO_INSTALL ON CACHE BOOL "Don't install any libraries build for miniupnpc")
add_subdirectory(third-party/miniupnp/miniupnpc)
include_directories(third-party/miniupnp)


find_package(Threads REQUIRED)
find_package(OpenSSL REQUIRED)
set(Boost_USE_STATIC_LIBS ON)
find_package(Boost COMPONENTS log filesystem REQUIRED)

list(APPEND SUNSHINE_COMPILE_OPTIONS -Wall -Wno-missing-braces -Wno-maybe-uninitialized -Wno-sign-compare)




enable_language(RC)
set(CMAKE_RC_COMPILER windres)
file(
	DOWNLOAD "https://github.com/TheElixZammuto/sunshine-prebuilt/releases/download/1.0.0/pre-compiled.zip" "${CMAKE_CURRENT_BINARY_DIR}/pre-compiled.zip"
	TIMEOUT 60
	EXPECTED_HASH SHA256=5d59986bd7f619eaaf82b2dd56b5127b747c9cbe8db61e3b898ff6b485298ed6)
file(ARCHIVE_EXTRACT
	INPUT "${CMAKE_CURRENT_BINARY_DIR}/pre-compiled.zip"
	DESTINATION ${CMAKE_CURRENT_BINARY_DIR}/pre-compiled)
set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -static")

if(NOT DEFINED SUNSHINE_PREPARED_BINARIES)
	set(SUNSHINE_PREPARED_BINARIES "${CMAKE_CURRENT_BINARY_DIR}/pre-compiled/windows")
endif()

add_compile_definitions(SUNSHINE_PLATFORM="windows")

# TODO
# add_subdirectory(tools) #This is temporary, only tools for Windows are needed, for now

include_directories(third-party/ViGEmClient/include)

if(NOT DEFINED SUNSHINE_ICON_PATH)
	set(SUNSHINE_ICON_PATH "${CMAKE_CURRENT_SOURCE_DIR}/sunshine.ico")
endif()
configure_file(sunshine/platform/windows/windows.rs.in windows.rc @ONLY)
set(PLATFORM_TARGET_FILES
	"${CMAKE_CURRENT_BINARY_DIR}/windows.rc"
	sunshine/platform/windows/publish.cpp
	sunshine/platform/windows/misc.cpp
	sunshine/platform/windows/input.cpp
	sunshine/platform/windows/display_base.cpp
	sunshine/platform/windows/display_vram.cpp
	sunshine/platform/windows/display_ram.cpp
	sunshine/platform/windows/audio.cpp

	third-party/ViGEmClient/src/ViGEmClient.cpp
	)

set(PLATFORM_INCLUDE_DIRS
	sunshine/platform/windows
	third-party/ViGEmClient/include/ViGEm)

set(OPENSSL_LIBRARIES
	libssl.a
	libcrypto.a)

set(FFMPEG_INCLUDE_DIRS
	${SUNSHINE_PREPARED_BINARIES}/include)
set(FFMPEG_LIBRARIES
	${SUNSHINE_PREPARED_BINARIES}/lib/libavcodec.a
	${SUNSHINE_PREPARED_BINARIES}/lib/libavdevice.a
	${SUNSHINE_PREPARED_BINARIES}/lib/libavfilter.a
	${SUNSHINE_PREPARED_BINARIES}/lib/libavformat.a
	${SUNSHINE_PREPARED_BINARIES}/lib/libavutil.a
	${SUNSHINE_PREPARED_BINARIES}/lib/libpostproc.a
	${SUNSHINE_PREPARED_BINARIES}/lib/libswresample.a
	${SUNSHINE_PREPARED_BINARIES}/lib/libswscale.a
	${SUNSHINE_PREPARED_BINARIES}/lib/libx264.a
	${SUNSHINE_PREPARED_BINARIES}/lib/libx265.a
	${SUNSHINE_PREPARED_BINARIES}/lib/libhdr10plus.a
	z lzma bcrypt libiconv.a)

list(PREPEND PLATFORM_LIBRARIES
	libstdc++.a
	libwinpthread.a
	libssp.a
	ksuser
	wsock32
	ws2_32
	d3d11 dxgi D3DCompiler
	setupapi
	dwmapi
	)

set_source_files_properties(third-party/ViGEmClient/src/ViGEmClient.cpp PROPERTIES COMPILE_DEFINITIONS "UNICODE=1;ERROR_INVALID_DEVICE_OBJECT_PARAMETER=650")
set_source_files_properties(third-party/ViGEmClient/src/ViGEmClient.cpp PROPERTIES COMPILE_FLAGS "-Wno-unknown-pragmas -Wno-misleading-indentation -Wno-class-memaccess")






configure_file(version.h.in version.h @ONLY)
include_directories(${CMAKE_CURRENT_BINARY_DIR})

set(SUNSHINE_TARGET_FILES
	third-party/moonlight-common-c/reedsolomon/rs.c
	third-party/moonlight-common-c/src/RtspParser.c

	examples/sunshine/main.cpp
	sunshine/src/util/upnp.cpp
	sunshine/src/util/process.cpp
	sunshine/src/http/crypto.cpp
	sunshine/src/http/httpcommon.cpp
	sunshine/src/http/confighttp.cpp
	sunshine/src/http/nvhttp.cpp
	sunshine/src/http/network.cpp
	sunshine/src/video/video.cpp
	sunshine/src/video/cbs.cpp
	sunshine/src/input/input.cpp
	sunshine/src/audio/audio.cpp
	sunshine/src/config/config.cpp
	sunshine/src/rstp/rtsp.cpp
	sunshine/src/rstp/stream.cpp
	${PLATFORM_TARGET_FILES})

set_source_files_properties(sunshine/src/util/upnp.cpp 
	PROPERTIES COMPILE_FLAGS -Wno-pedantic)

include_directories(
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${CMAKE_CURRENT_SOURCE_DIR}/sunshine/header/config
  ${CMAKE_CURRENT_SOURCE_DIR}/sunshine/header/audio
  ${CMAKE_CURRENT_SOURCE_DIR}/sunshine/header/video
  ${CMAKE_CURRENT_SOURCE_DIR}/sunshine/header/input
  ${CMAKE_CURRENT_SOURCE_DIR}/sunshine/header/http
  ${CMAKE_CURRENT_SOURCE_DIR}/sunshine/header/rstp
  ${CMAKE_CURRENT_SOURCE_DIR}/sunshine/header/thread
  ${CMAKE_CURRENT_SOURCE_DIR}/sunshine/header/util
  ${CMAKE_CURRENT_SOURCE_DIR}/sunshine/platform

  ${CMAKE_CURRENT_SOURCE_DIR}/examples/sunshine

  ${CMAKE_CURRENT_SOURCE_DIR}/third-party
  ${CMAKE_CURRENT_SOURCE_DIR}/third-party/cbs/include
  ${CMAKE_CURRENT_SOURCE_DIR}/third-party/moonlight-common-c/enet/include
  ${CMAKE_CURRENT_SOURCE_DIR}/third-party/moonlight-common-c/reedsolomon
  ${CMAKE_CURRENT_SOURCE_DIR}/third-party/moonlight-common-c/src
  ${FFMPEG_INCLUDE_DIRS}
  ${PLATFORM_INCLUDE_DIRS}
)

add_subdirectory(third-party/cbs)

string(TOUPPER "x${CMAKE_BUILD_TYPE}" BUILD_TYPE)
if("${BUILD_TYPE}" STREQUAL "XDEBUG")
	list(APPEND SUNSHINE_COMPILE_OPTIONS -O0 -ggdb3)
	if(WIN32)
		set_source_files_properties(sunshine/src/http/nvhttp.cpp 
			PROPERTIES COMPILE_FLAGS -O2)
	endif()
else()
	add_definitions(-DNDEBUG)
	list(APPEND SUNSHINE_COMPILE_OPTIONS -O3)
endif()

if(NOT SUNSHINE_ASSETS_DIR)
	set(SUNSHINE_ASSETS_DIR "${CMAKE_CURRENT_BINARY_DIR}/assets")
endif()

if(NOT SUNSHINE_CONFIG_DIR)
	set(SUNSHINE_CONFIG_DIR "${CMAKE_CURRENT_BINARY_DIR}/config")
endif()

list(APPEND CBS_EXTERNAL_LIBRARIES
	cbs)

list(APPEND SUNSHINE_EXTERNAL_LIBRARIES
		libminiupnpc-static
		${CBS_EXTERNAL_LIBRARIES}
		${CMAKE_THREAD_LIBS_INIT}
		enet
		opus
		${FFMPEG_LIBRARIES}
		${Boost_LIBRARIES}
		${OPENSSL_LIBRARIES}
		${PLATFORM_LIBRARIES}
		)

list(APPEND SUNSHINE_DEFINITIONS SUNSHINE_ASSETS_DIR="${SUNSHINE_ASSETS_DIR}")
list(APPEND SUNSHINE_DEFINITIONS SUNSHINE_CONFIG_DIR="${SUNSHINE_CONFIG_DIR}")

add_executable(sunshine ${SUNSHINE_TARGET_FILES})
add_library(sunshinesdk STATIC ${SUNSHINE_TARGET_FILES})

target_link_libraries(sunshine ${SUNSHINE_EXTERNAL_LIBRARIES} ${EXTRA_LIBS})
target_compile_definitions(sunshine PUBLIC ${SUNSHINE_DEFINITIONS})
set_target_properties(sunshine PROPERTIES CXX_STANDARD 17
                            VERSION ${PROJECT_VERSION}
                            SOVERSION ${PROJECT_VERSION_MAJOR}
                            )

target_link_libraries(sunshinesdk ${SUNSHINE_EXTERNAL_LIBRARIES} ${EXTRA_LIBS})
target_compile_definitions(sunshinesdk PUBLIC ${SUNSHINE_DEFINITIONS})
set_target_properties(sunshinesdk PROPERTIES CXX_STANDARD 17
                            VERSION ${PROJECT_VERSION}
                            SOVERSION ${PROJECT_VERSION_MAJOR}
                            )

if(NOT DEFINED CMAKE_CUDA_STANDARD)
    set(CMAKE_CUDA_STANDARD 17)
    set(CMAKE_CUDA_STANDARD_REQUIRED ON)
endif()

if(APPLE)
	target_link_options(sunshine PRIVATE LINKER:-sectcreate,__TEXT,__info_plist,${APPLE_PLIST_FILE})
	target_link_options(sunshinesdk PRIVATE LINKER:-sectcreate,__TEXT,__info_plist,${APPLE_PLIST_FILE})
endif()

foreach(flag IN LISTS SUNSHINE_COMPILE_OPTIONS)
	list(APPEND SUNSHINE_COMPILE_OPTIONS_CUDA "$<$<COMPILE_LANGUAGE:CUDA>:--compiler-options=${flag}>")
endforeach()

target_compile_options(sunshine PRIVATE $<$<COMPILE_LANGUAGE:CXX>:${SUNSHINE_COMPILE_OPTIONS}>;$<$<COMPILE_LANGUAGE:CUDA>:${SUNSHINE_COMPILE_OPTIONS_CUDA};-std=c++17>)
target_compile_options(sunshinesdk PRIVATE $<$<COMPILE_LANGUAGE:CXX>:${SUNSHINE_COMPILE_OPTIONS}>;$<$<COMPILE_LANGUAGE:CUDA>:${SUNSHINE_COMPILE_OPTIONS_CUDA};-std=c++17>)

