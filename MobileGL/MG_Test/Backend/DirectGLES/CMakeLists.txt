cmake_minimum_required(VERSION 3.14)

message(STATUS "Generating build files for MobileGL DirectGLES GPU Test...")

if (WIN32 OR APPLE)
    message(STATUS "Skipping DirectGLES GPU tests on this platform")
    return()
endif()

find_library(EGL_LIBRARY EGL)
find_library(GLESV2_LIBRARY GLESv2)

if (NOT EGL_LIBRARY OR NOT GLESV2_LIBRARY)
    message(STATUS "Skipping DirectGLES GPU tests because EGL or GLESv2 was not found")
    return()
endif()

add_executable(
    DirectGLESGPUTest
    AlphaCompatGpuTest.cpp
    GpuHarness.cpp
)

target_include_directories(DirectGLESGPUTest PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}
)

target_link_libraries(
    DirectGLESGPUTest PRIVATE
    GTest::gtest_main
    ${EGL_LIBRARY}
    ${GLESV2_LIBRARY}
)

include(GoogleTest)
gtest_discover_tests(DirectGLESGPUTest DISCOVERY_TIMEOUT 30)
