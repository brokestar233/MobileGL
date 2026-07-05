// MobileGL - MobileGL/MG_Test/Backend/DirectGLES/AlphaCompatGpuTest.cpp
// Copyright (c) 2025-2026 MobileGL-Dev
// Licensed under the GNU Lesser General Public License v3.0:
//   https://www.gnu.org/licenses/gpl-3.0.txt
//   https://www.gnu.org/licenses/lgpl-3.0.txt
// SPDX-License-Identifier: LGPL-3.0-only
// End of Source File Header

#include "GpuHarness.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstdio>
#include <functional>
#include <numeric>
#include <string>
#include <vector>

namespace {
    constexpr const char* kFullscreenTriangleVertexShader = R"(#version 300 es
void main() {
    vec2 positions[3] = vec2[3](
        vec2(-1.0, -1.0),
        vec2( 3.0, -1.0),
        vec2(-1.0,  3.0)
    );
    gl_Position = vec4(positions[gl_VertexID], 0.0, 1.0);
}
)";

    struct PerfStats {
        double AverageNs = 0.0;
        double MinNs = 0.0;
        double MaxNs = 0.0;
    };

    std::string BuildLegacyAlphaCompatFragmentShader() {
        return R"(#version 300 es
precision mediump float;
uniform int mg_AlphaTestEnabled;
uniform int mg_AlphaTestFunc;
uniform float mg_AlphaTestRef;
out vec4 fragColor;

float mg_AlphaForLane(int lane) {
    if (lane == 0) return 0.0;
    if (lane == 1) return 0.25;
    if (lane == 2) return 0.4995;
    if (lane == 3) return 0.5;
    if (lane == 4) return 0.5005;
    if (lane == 5) return 0.75;
    return 1.0;
}

bool mg_PassesAlphaTest(float alpha) {
    const float mg_AlphaEpsilon = 0.001;
    if (mg_AlphaTestEnabled == 0) return true;
    if (mg_AlphaTestFunc == 512) return false;
    if (mg_AlphaTestFunc == 513) return alpha < mg_AlphaTestRef;
    if (mg_AlphaTestFunc == 514) return abs(alpha - mg_AlphaTestRef) < mg_AlphaEpsilon;
    if (mg_AlphaTestFunc == 515) return alpha <= mg_AlphaTestRef;
    if (mg_AlphaTestFunc == 516) return alpha > mg_AlphaTestRef;
    if (mg_AlphaTestFunc == 517) return abs(alpha - mg_AlphaTestRef) >= mg_AlphaEpsilon;
    if (mg_AlphaTestFunc == 518) return alpha >= mg_AlphaTestRef;
    return true;
}

void main() {
    int lane = int(floor(gl_FragCoord.x));
    float alpha = mg_AlphaForLane(lane);
    fragColor = vec4(1.0, 0.0, 0.0, alpha);
    if (!mg_PassesAlphaTest(fragColor.a)) {
        discard;
    }
    fragColor = vec4(1.0, 0.0, 0.0, 1.0);
}
)";
    }

    std::string BuildSpecializedAlphaCompatFragmentShader(GLenum func) {
        std::string predicate;
        switch (func) {
        case GL_NEVER:
            predicate = "false";
            break;
        case GL_LESS:
            predicate = "((alpha) < mg_AlphaTestRef)";
            break;
        case GL_EQUAL:
            predicate = "(abs((alpha) - mg_AlphaTestRef) < 0.001)";
            break;
        case GL_LEQUAL:
            predicate = "((alpha) <= mg_AlphaTestRef)";
            break;
        case GL_GREATER:
            predicate = "((alpha) > mg_AlphaTestRef)";
            break;
        case GL_NOTEQUAL:
            predicate = "(abs((alpha) - mg_AlphaTestRef) >= 0.001)";
            break;
        case GL_GEQUAL:
            predicate = "((alpha) >= mg_AlphaTestRef)";
            break;
        default:
            predicate = "true";
            break;
        }

        return std::string(R"(#version 300 es
precision mediump float;
uniform float mg_AlphaTestRef;
out vec4 fragColor;

float mg_AlphaForLane(int lane) {
    if (lane == 0) return 0.0;
    if (lane == 1) return 0.25;
    if (lane == 2) return 0.4995;
    if (lane == 3) return 0.5;
    if (lane == 4) return 0.5005;
    if (lane == 5) return 0.75;
    return 1.0;
}

#define MG_ALPHA_TEST_PASSES(alpha) )") + predicate + R"(

void main() {
    int lane = int(floor(gl_FragCoord.x));
    float alpha = mg_AlphaForLane(lane);
    fragColor = vec4(1.0, 0.0, 0.0, alpha);
    if (!MG_ALPHA_TEST_PASSES(fragColor.a)) {
        discard;
    }
    fragColor = vec4(1.0, 0.0, 0.0, 1.0);
}
)";
    }

    std::string BuildLegacyPerformanceFragmentShader() {
        return R"(#version 300 es
precision mediump float;
uniform int mg_AlphaTestEnabled;
uniform int mg_AlphaTestFunc;
uniform float mg_AlphaTestRef;
out vec4 fragColor;

bool mg_PassesAlphaTest(float alpha) {
    const float mg_AlphaEpsilon = 0.001;
    if (mg_AlphaTestEnabled == 0) return true;
    if (mg_AlphaTestFunc == 512) return false;
    if (mg_AlphaTestFunc == 513) return alpha < mg_AlphaTestRef;
    if (mg_AlphaTestFunc == 514) return abs(alpha - mg_AlphaTestRef) < mg_AlphaEpsilon;
    if (mg_AlphaTestFunc == 515) return alpha <= mg_AlphaTestRef;
    if (mg_AlphaTestFunc == 516) return alpha > mg_AlphaTestRef;
    if (mg_AlphaTestFunc == 517) return abs(alpha - mg_AlphaTestRef) >= mg_AlphaEpsilon;
    if (mg_AlphaTestFunc == 518) return alpha >= mg_AlphaTestRef;
    return true;
}

void main() {
    float alpha = fract(gl_FragCoord.x * 0.618034 + gl_FragCoord.y * 0.414214);
    fragColor = vec4(alpha, 0.0, 0.0, alpha);
    if (!mg_PassesAlphaTest(fragColor.a)) {
        discard;
    }
    fragColor = vec4(1.0, 0.0, 0.0, 1.0);
}
)";
    }

    std::string BuildSpecializedPerformanceFragmentShader() {
        return R"(#version 300 es
precision mediump float;
uniform float mg_AlphaTestRef;
out vec4 fragColor;

#define MG_ALPHA_TEST_PASSES(alpha) ((alpha) > mg_AlphaTestRef)

void main() {
    float alpha = fract(gl_FragCoord.x * 0.618034 + gl_FragCoord.y * 0.414214);
    fragColor = vec4(alpha, 0.0, 0.0, alpha);
    if (!MG_ALPHA_TEST_PASSES(fragColor.a)) {
        discard;
    }
    fragColor = vec4(1.0, 0.0, 0.0, 1.0);
}
)";
    }

    void SetLegacyAlphaUniforms(GLuint program, bool enabled, GLenum func, float ref) {
        glUseProgram(program);
        glUniform1i(glGetUniformLocation(program, "mg_AlphaTestEnabled"), enabled ? 1 : 0);
        glUniform1i(glGetUniformLocation(program, "mg_AlphaTestFunc"), static_cast<GLint>(func));
        glUniform1f(glGetUniformLocation(program, "mg_AlphaTestRef"), ref);
    }

    void SetSpecializedAlphaUniforms(GLuint program, float ref) {
        glUseProgram(program);
        glUniform1f(glGetUniformLocation(program, "mg_AlphaTestRef"), ref);
    }

    PerfStats MeasureProgramGpuTime(const MobileGL::MG_Test::DirectGLES::OffscreenGpuHarness& harness, GLuint program,
                                    int rounds, int warmupIterations, int timedIterations,
                                    const std::function<void()>& beforeDraw) {
        std::vector<double> samples;
        samples.reserve(static_cast<size_t>(rounds));
        for (int round = 0; round < rounds; ++round) {
            const double avgNs = harness.MeasureGpuNanoseconds(
                [&]() {
                    beforeDraw();
                    const bool ok = harness.DrawFullscreenTriangle(program, nullptr);
                    (void)ok;
                },
                warmupIterations,
                timedIterations);
            samples.push_back(avgNs);
        }

        PerfStats stats;
        stats.MinNs = *std::min_element(samples.begin(), samples.end());
        stats.MaxNs = *std::max_element(samples.begin(), samples.end());
        stats.AverageNs = std::accumulate(samples.begin(), samples.end(), 0.0) / static_cast<double>(samples.size());
        return stats;
    }

    bool IsRed(const MobileGL::MG_Test::DirectGLES::OffscreenGpuHarness::Pixel& pixel) {
        return pixel[0] > 200 && pixel[1] < 32 && pixel[2] < 32;
    }

    bool IsGreen(const MobileGL::MG_Test::DirectGLES::OffscreenGpuHarness::Pixel& pixel) {
        return pixel[0] < 32 && pixel[1] > 200 && pixel[2] < 32;
    }

    class DirectGLESGpuFixture : public ::testing::Test {
    protected:
        void SetUp() override {
            if (!m_harness.Initialize(4, 4, &m_error)) {
                GTEST_SKIP() << m_error;
            }
        }

        MobileGL::MG_Test::DirectGLES::OffscreenGpuHarness m_harness;
        std::string m_error;
    };
} // namespace

TEST_F(DirectGLESGpuFixture, OffscreenReadbackSmoke) {
    ASSERT_TRUE(m_harness.Clear(0.0f, 0.0f, 1.0f, 1.0f, &m_error)) << m_error;
    m_harness.Finish();

    const auto pixel = m_harness.ReadPixel(1, 1);
    EXPECT_LT(pixel[0], 16);
    EXPECT_LT(pixel[1], 16);
    EXPECT_GT(pixel[2], 240);
    EXPECT_GT(pixel[3], 240);
}

TEST_F(DirectGLESGpuFixture, FragmentDiscardAffectsPixels) {
    static constexpr const char* kFragmentShader = R"(#version 300 es
precision mediump float;
out vec4 fragColor;
void main() {
    float alpha = gl_FragCoord.x < 2.0 ? 0.25 : 0.75;
    if (!(alpha > 0.5)) {
        discard;
    }
    fragColor = vec4(1.0, 0.0, 0.0, 1.0);
}
)";

    const GLuint program =
        m_harness.CreateProgram(kFullscreenTriangleVertexShader, kFragmentShader, &m_error);
    ASSERT_NE(program, 0u) << m_error;

    ASSERT_TRUE(m_harness.Clear(0.0f, 1.0f, 0.0f, 1.0f, &m_error)) << m_error;
    ASSERT_TRUE(m_harness.DrawFullscreenTriangle(program, &m_error)) << m_error;
    m_harness.Finish();

    const auto leftPixel = m_harness.ReadPixel(0, 1);
    const auto rightPixel = m_harness.ReadPixel(3, 1);

    EXPECT_LT(leftPixel[0], 32);
    EXPECT_GT(leftPixel[1], 200);
    EXPECT_LT(leftPixel[2], 32);

    EXPECT_GT(rightPixel[0], 200);
    EXPECT_LT(rightPixel[1], 32);
    EXPECT_LT(rightPixel[2], 32);

    m_harness.DestroyProgram(program);
}

TEST(DirectGLESGpuBehavior, LegacyAndSpecializedAlphaCompatMatchPixels) {
    using Harness = MobileGL::MG_Test::DirectGLES::OffscreenGpuHarness;

    Harness harness;
    std::string error;
    ASSERT_TRUE(harness.Initialize(7, 1, &error)) << error;

    const GLuint legacyProgram =
        harness.CreateProgram(kFullscreenTriangleVertexShader, BuildLegacyAlphaCompatFragmentShader().c_str(), &error);
    ASSERT_NE(legacyProgram, 0u) << error;

    struct FuncCase {
        GLenum Func;
        std::array<bool, 7> PassMask;
    };

    const std::array<FuncCase, 4> cases = {{
        {GL_LESS, {true, true, true, false, false, false, false}},
        {GL_EQUAL, {false, false, true, true, true, false, false}},
        {GL_GREATER, {false, false, false, false, true, true, true}},
        {GL_NOTEQUAL, {true, true, false, false, false, true, true}},
    }};

    for (const auto& testCase : cases) {
        const GLuint specializedProgram = harness.CreateProgram(
            kFullscreenTriangleVertexShader, BuildSpecializedAlphaCompatFragmentShader(testCase.Func).c_str(), &error);
        ASSERT_NE(specializedProgram, 0u) << error;

        ASSERT_TRUE(harness.Clear(0.0f, 1.0f, 0.0f, 1.0f, &error)) << error;
        SetLegacyAlphaUniforms(legacyProgram, true, testCase.Func, 0.5f);
        ASSERT_TRUE(harness.DrawFullscreenTriangle(legacyProgram, &error)) << error;
        harness.Finish();

        std::array<Harness::Pixel, 7> legacyPixels{};
        for (int x = 0; x < 7; ++x) {
            legacyPixels[static_cast<size_t>(x)] = harness.ReadPixel(x, 0);
        }

        ASSERT_TRUE(harness.Clear(0.0f, 1.0f, 0.0f, 1.0f, &error)) << error;
        SetSpecializedAlphaUniforms(specializedProgram, 0.5f);
        ASSERT_TRUE(harness.DrawFullscreenTriangle(specializedProgram, &error)) << error;
        harness.Finish();

        for (int x = 0; x < 7; ++x) {
            const auto specializedPixel = harness.ReadPixel(x, 0);
            const auto& legacyPixel = legacyPixels[static_cast<size_t>(x)];
            SCOPED_TRACE(::testing::Message() << "func=" << testCase.Func << " lane=" << x);

            EXPECT_EQ(specializedPixel, legacyPixel);
            if (testCase.PassMask[static_cast<size_t>(x)]) {
                EXPECT_TRUE(IsRed(specializedPixel));
            } else {
                EXPECT_TRUE(IsGreen(specializedPixel));
            }
        }

        harness.DestroyProgram(specializedProgram);
    }

    harness.DestroyProgram(legacyProgram);
}

TEST(DirectGLESGpuPerformance, LegacyAndSpecializedAlphaCompatDrawTime) {
    using Harness = MobileGL::MG_Test::DirectGLES::OffscreenGpuHarness;

    Harness harness;
    std::string error;
    ASSERT_TRUE(harness.Initialize(512, 512, &error)) << error;

    const GLuint legacyProgram =
        harness.CreateProgram(kFullscreenTriangleVertexShader, BuildLegacyPerformanceFragmentShader().c_str(), &error);
    ASSERT_NE(legacyProgram, 0u) << error;

    const GLuint specializedProgram = harness.CreateProgram(
        kFullscreenTriangleVertexShader, BuildSpecializedPerformanceFragmentShader().c_str(), &error);
    ASSERT_NE(specializedProgram, 0u) << error;

    constexpr int kRounds = 6;
    constexpr int kWarmupIterations = 4;
    constexpr int kTimedIterations = 24;

    const auto legacyStats = MeasureProgramGpuTime(
        harness,
        legacyProgram,
        kRounds,
        kWarmupIterations,
        kTimedIterations,
        [&]() {
            harness.Clear(0.0f, 0.0f, 0.0f, 1.0f, nullptr);
            SetLegacyAlphaUniforms(legacyProgram, true, GL_GREATER, 0.5f);
        });
    const auto specializedStats = MeasureProgramGpuTime(
        harness,
        specializedProgram,
        kRounds,
        kWarmupIterations,
        kTimedIterations,
        [&]() {
            harness.Clear(0.0f, 0.0f, 0.0f, 1.0f, nullptr);
            SetSpecializedAlphaUniforms(specializedProgram, 0.5f);
        });

    const double speedup = specializedStats.AverageNs > 0.0 ? legacyStats.AverageNs / specializedStats.AverageNs : 0.0;
    std::printf(
        "[DirectGLES alpha compat gpu] legacy avg=%.2f ns min=%.2f ns max=%.2f ns | "
        "specialized avg=%.2f ns min=%.2f ns max=%.2f ns | ratio=%.3fx\n",
        legacyStats.AverageNs,
        legacyStats.MinNs,
        legacyStats.MaxNs,
        specializedStats.AverageNs,
        specializedStats.MinNs,
        specializedStats.MaxNs,
        speedup);

    RecordProperty("legacy_avg_ns", legacyStats.AverageNs);
    RecordProperty("specialized_avg_ns", specializedStats.AverageNs);
    RecordProperty("legacy_to_specialized_ratio", speedup);

    EXPECT_GT(legacyStats.AverageNs, 0.0);
    EXPECT_GT(specializedStats.AverageNs, 0.0);

    harness.DestroyProgram(specializedProgram);
    harness.DestroyProgram(legacyProgram);
}
