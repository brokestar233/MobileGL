cmake_minimum_required(VERSION 3.14)

message(STATUS "Generating build files for MobileGL render-path GPU Perf Test...")

if (WIN32 OR APPLE)
    message(STATUS "Skipping MobileGL render-path GPU perf tests on this platform")
    return()
endif()

if (NOT TARGET MobileGL)
    message(STATUS "Skipping MobileGL render-path GPU perf tests because MobileGL target is missing")
    return()
endif()

add_executable(
    MobileGLPathPerfTest
    MobileGLPathPerfTest.cpp
    ${CMAKE_SOURCE_DIR}/MobileGL/MG_Test/Compat/SFPEW/SfpewHarness.cpp
)

target_include_directories(MobileGLPathPerfTest PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}
    ${CMAKE_SOURCE_DIR}/MobileGL/MG_Test/Compat/SFPEW
)

target_link_libraries(
    MobileGLPathPerfTest PRIVATE
    GTest::gtest_main
    MobileGL
)

set_target_properties(MobileGLPathPerfTest PROPERTIES
    BUILD_RPATH "$<TARGET_FILE_DIR:MobileGL>"
)

add_test(NAME MobileGLPath.AngelicaWorldOneshotMultiDrawArrays
         COMMAND MobileGLPathPerfTest --gtest_filter=MobileGLPathPerfFixture.AngelicaWorldOneshotMultiDrawArraysSmokeAndPerf)
add_test(NAME MobileGLPath.AngelicaWorldMultiDrawIndirect
         COMMAND MobileGLPathPerfTest --gtest_filter=MobileGLPathPerfFixture.AngelicaWorldMultiDrawIndirectSmokeAndPerf)
