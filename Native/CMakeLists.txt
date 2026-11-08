cmake_minimum_required(VERSION 3.22)
project(proxymap_boringssl LANGUAGES C CXX)

set(CMAKE_POSITION_INDEPENDENT_CODE ON)

if (NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../boringssl/CMakeLists.txt")
    message(FATAL_ERROR "BoringSSL was not found. Clone it into ../boringssl")
endif()

set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)

set(BROTLI_BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(BROTLI_BUILD_TOOLS OFF CACHE BOOL "" FORCE)
set(BROTLI_BUILD_TESTING OFF CACHE BOOL "" FORCE)

add_subdirectory(
    "${CMAKE_CURRENT_SOURCE_DIR}/../brotli"
    "${CMAKE_BINARY_DIR}/brotli"
    EXCLUDE_FROM_ALL
)

add_subdirectory(
    "${CMAKE_CURRENT_SOURCE_DIR}/../boringssl"
    "${CMAKE_BINARY_DIR}/boringssl"
    EXCLUDE_FROM_ALL)

add_library(proxymap_boringssl SHARED ssl_wrapper.cc)

# MSVC produces proxymap_boringssl.dll, but MinGW-based toolchains prefix
# shared libraries with "lib" (libproxymap_boringssl.dll). Strip the prefix
# so the DLL name always matches the P/Invoke name on Windows.
if(WIN32)
    set_target_properties(proxymap_boringssl PROPERTIES PREFIX "")
endif()

target_include_directories(proxymap_boringssl PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../boringssl/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/../brotli/c/include"
)

target_link_libraries(proxymap_boringssl PRIVATE ssl crypto brotlidec brotlienc)

target_compile_features(proxymap_boringssl PRIVATE cxx_std_17)

if (MSVC)
    target_compile_options(proxymap_boringssl PRIVATE /W4)
else()
    target_compile_options(proxymap_boringssl PRIVATE -Wall -Wextra)
endif()
