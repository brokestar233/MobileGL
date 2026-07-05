cmake_minimum_required(VERSION 3.14)

message(STATUS "Generating build files for SimpleFPEWrapper GPU Perf Test...")

if (WIN32 OR APPLE)
    message(STATUS "Skipping SimpleFPEWrapper GPU perf tests on this platform")
    return()
endif()

if (NOT TARGET SimpleFPEWrapper OR NOT TARGET MobileGL)
    message(STATUS "Skipping SimpleFPEWrapper GPU perf tests because required targets are missing")
    return()
endif()

add_executable(
    SFPEWPerfTest
    SfpewHarness.cpp
    SfpewPerformanceTest.cpp
)

target_include_directories(SFPEWPerfTest PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}
)

target_link_libraries(
    SFPEWPerfTest PRIVATE
    GTest::gtest_main
    SimpleFPEWrapper
    MobileGL
)

set_target_properties(SFPEWPerfTest PROPERTIES
    BUILD_RPATH "$<TARGET_FILE_DIR:SimpleFPEWrapper>;$<TARGET_FILE_DIR:MobileGL>"
)

add_test(NAME SFPEW.VanillaImmediateTriangleStripFont
         COMMAND SFPEWPerfTest --gtest_filter=SFPEWPerfFixture.VanillaImmediateTriangleStripFontSmokeAndPerf)
add_test(NAME SFPEW.VanillaClientArrayGuiQuads4
         COMMAND SFPEWPerfTest --gtest_filter=SFPEWPerfFixture.VanillaClientArrayGuiQuads4SmokeAndPerf)
add_test(NAME SFPEW.VanillaClientArrayGuiQuads12
         COMMAND SFPEWPerfTest --gtest_filter=SFPEWPerfFixture.VanillaClientArrayGuiQuads12SmokeAndPerf)
add_test(NAME SFPEW.CMMImmediateMenuQuads
         COMMAND SFPEWPerfTest --gtest_filter=SFPEWPerfFixture.CMMImmediateMenuQuadsSmokeAndPerf)
add_test(NAME SFPEW.CMMPanoramaClientArrayQuads
         COMMAND SFPEWPerfTest --gtest_filter=SFPEWPerfFixture.CMMPanoramaClientArrayQuadsSmokeAndPerf)
add_test(NAME SFPEW.VanillaDisplayListCompileAndCallGuiQuads
         COMMAND SFPEWPerfTest --gtest_filter=SFPEWPerfFixture.VanillaDisplayListCompileAndCallGuiQuadsSmokeAndPerf)
add_test(NAME SFPEW.AngelicaBatchedFontDrawElements
         COMMAND SFPEWPerfTest --gtest_filter=SFPEWPerfFixture.AngelicaBatchedFontDrawElementsSmokeAndPerf)
