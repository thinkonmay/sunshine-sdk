set(SUNSHINE_SRC_ROOT "${CMAKE_CURRENT_LIST_DIR}/..")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)
set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
set(INSTALL_GMOCK OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
  googletest
  URL https://github.com/google/googletest/archive/refs/tags/v1.15.2.zip
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_MakeAvailable(googletest)

add_executable(test_ivshmem_protocol
  unit/test_ivshmem_protocol.cpp
)

target_include_directories(test_ivshmem_protocol PRIVATE "${SUNSHINE_SRC_ROOT}/src")
target_link_libraries(test_ivshmem_protocol PRIVATE GTest::gtest_main)

add_test(NAME ivshmem_protocol COMMAND test_ivshmem_protocol)
set_tests_properties(ivshmem_protocol PROPERTIES
  LABELS "unit;ivshmem"
  TIMEOUT 120
)
