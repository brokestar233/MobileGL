// MobileGL - MobileGL/MG_Test/Compat/SFPEW/SfpewPerformanceTest.cpp
// Copyright (c) 2025-2026 MobileGL-Dev
// Licensed under the GNU Lesser General Public License v3.0:
//   https://www.gnu.org/licenses/gpl-3.0.txt
//   https://www.gnu.org/licenses/lgpl-3.0.txt
// SPDX-License-Identifier: LGPL-3.0-only
// End of Source File Header

#include "SfpewHarness.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace MobileGL::MG_Test::Compat::SFPEW {
    namespace {
        constexpr GLint kSurfaceSize = 512;
        constexpr int kWarmupFrames = 10;
        constexpr int kTimedFrames = 40;

        struct TrackInterleavedVertex {
            GLubyte Color[4];
            GLfloat TexCoord[2];
            GLfloat Position[3];
            GLfloat Padding[2];
        };

        struct TrackClientArrayBatch {
            std::vector<TrackInterleavedVertex> Vertices;
            GLsizei VertexCount = 0;
        };

        struct BatchedFontBuffers {
            std::vector<GLfloat> Positions;
            std::vector<GLubyte> Colors;
            std::vector<GLfloat> TexCoords;
            std::vector<GLushort> Indices;
            GLsizei IndexCount = 0;
        };

        struct BenchmarkResult {
            double FrameNanoseconds = 0.0;
            double DrawNanoseconds = 0.0;
        };

        GLuint CreateAlphaCheckerTexture() {
            GLuint texture = 0;
            glGenTextures(1, &texture);
            glBindTexture(GL_TEXTURE_2D, texture);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

            const GLubyte pixels[4 * 4 * 4] = {
                255,   0,   0, 255,   0, 255,   0,  64, 255,   0,   0, 255,   0, 255,   0,  64,
                  0,   0, 255, 255, 255, 255,   0, 255,   0,   0, 255, 255, 255, 255,   0, 255,
                255,   0,   0, 255,   0, 255,   0,  64, 255,   0,   0, 255,   0, 255,   0,  64,
                  0,   0, 255, 255, 255, 255,   0, 255,   0,   0, 255, 255, 255, 255,   0, 255,
            };
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 4, 4, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
            return texture;
        }

        void Setup2D(GLint width, GLint height) {
            glDisable(GL_CULL_FACE);
            glDisable(GL_DEPTH_TEST);
            glDisable(GL_LIGHTING);
            glDisable(GL_FOG);
            glDisable(GL_ALPHA_TEST);
            glDisable(GL_BLEND);
            glEnable(GL_TEXTURE_2D);
            glViewport(0, 0, width, height);
            glMatrixMode(GL_PROJECTION);
            glLoadIdentity();
            glOrtho(0.0, static_cast<GLdouble>(width), static_cast<GLdouble>(height), 0.0, -1.0, 1.0);
            glMatrixMode(GL_MODELVIEW);
            glLoadIdentity();
            glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
        }

        TrackClientArrayBatch BuildTrackClientArrayBatch(int quadsPerDraw, float quadWidth, float quadHeight, float gap) {
            TrackClientArrayBatch batch;
            batch.VertexCount = static_cast<GLsizei>(quadsPerDraw * 4);
            batch.Vertices.reserve(static_cast<size_t>(batch.VertexCount));

            const int gridWidth = std::max(1, static_cast<int>(std::ceil(std::sqrt(static_cast<float>(quadsPerDraw)))));
            for (int quadIndex = 0; quadIndex < quadsPerDraw; ++quadIndex) {
                const int gx = quadIndex % gridWidth;
                const int gy = quadIndex / gridWidth;
                const float x = gx * (quadWidth + gap);
                const float y = gy * (quadHeight + gap);
                const GLubyte red = static_cast<GLubyte>(96 + ((quadIndex * 23) % 128));
                const GLubyte green = static_cast<GLubyte>(128 + ((quadIndex * 41) % 96));
                const GLubyte blue = static_cast<GLubyte>(160 + ((quadIndex * 17) % 80));

                const TrackInterleavedVertex quad[4] = {
                    {{red, green, blue, 255}, {0.0f, 0.0f}, {x, y, 0.0f}, {0.0f, 0.0f}},
                    {{red, green, blue, 255}, {0.0f, 1.0f}, {x, y + quadHeight, 0.0f}, {0.0f, 0.0f}},
                    {{red, green, blue, 255}, {1.0f, 1.0f}, {x + quadWidth, y + quadHeight, 0.0f}, {0.0f, 0.0f}},
                    {{red, green, blue, 255}, {1.0f, 0.0f}, {x + quadWidth, y, 0.0f}, {0.0f, 0.0f}},
                };

                batch.Vertices.insert(batch.Vertices.end(), std::begin(quad), std::end(quad));
            }

            return batch;
        }

        BatchedFontBuffers BuildBatchedFontGlyphs(int glyphCount, float baseX, float baseY,
                                                  float glyphWidth, float glyphHeight) {
            BatchedFontBuffers buffers;
            buffers.Positions.reserve(static_cast<size_t>(glyphCount) * 8u);
            buffers.Colors.reserve(static_cast<size_t>(glyphCount) * 16u);
            buffers.TexCoords.reserve(static_cast<size_t>(glyphCount) * 8u);
            buffers.Indices.reserve(static_cast<size_t>(glyphCount) * 6u);

            const int lineWidth = std::max(1, static_cast<int>(std::sqrt(static_cast<float>(glyphCount))));
            for (int glyphIndex = 0; glyphIndex < glyphCount; ++glyphIndex) {
                const int gx = glyphIndex % lineWidth;
                const int gy = glyphIndex / lineWidth;
                const float x = baseX + gx * (glyphWidth + 1.0f);
                const float y = baseY + gy * (glyphHeight + 1.0f);
                const GLushort baseVertex = static_cast<GLushort>(glyphIndex * 4);
                const GLubyte shade = static_cast<GLubyte>(160 + ((glyphIndex * 7) % 80));

                const GLfloat positions[8] = {
                    x, y,
                    x, y + glyphHeight,
                    x + glyphWidth, y + glyphHeight,
                    x + glyphWidth, y,
                };
                const GLfloat texCoords[8] = {
                    0.0f, 0.0f,
                    0.0f, 1.0f,
                    1.0f, 1.0f,
                    1.0f, 0.0f,
                };
                const GLubyte colors[16] = {
                    shade, shade, shade, 255,
                    shade, shade, shade, 255,
                    shade, shade, shade, 255,
                    shade, shade, shade, 255,
                };
                const GLushort indices[6] = {
                    static_cast<GLushort>(baseVertex + 0u),
                    static_cast<GLushort>(baseVertex + 1u),
                    static_cast<GLushort>(baseVertex + 2u),
                    static_cast<GLushort>(baseVertex + 2u),
                    static_cast<GLushort>(baseVertex + 1u),
                    static_cast<GLushort>(baseVertex + 3u),
                };

                buffers.Positions.insert(buffers.Positions.end(), std::begin(positions), std::end(positions));
                buffers.TexCoords.insert(buffers.TexCoords.end(), std::begin(texCoords), std::end(texCoords));
                buffers.Colors.insert(buffers.Colors.end(), std::begin(colors), std::end(colors));
                buffers.Indices.insert(buffers.Indices.end(), std::begin(indices), std::end(indices));
            }

            buffers.IndexCount = static_cast<GLsizei>(buffers.Indices.size());
            return buffers;
        }

        void DrawImmediateTriangleStripGlyphs(int glyphCount, float baseX, float baseY) {
            const float glyphWidth = 10.0f;
            const float glyphHeight = 16.0f;
            const int lineWidth = std::max(1, static_cast<int>(std::sqrt(static_cast<float>(glyphCount))));

            for (int glyphIndex = 0; glyphIndex < glyphCount; ++glyphIndex) {
                const int gx = glyphIndex % lineWidth;
                const int gy = glyphIndex / lineWidth;
                const float x = baseX + gx * (glyphWidth + 1.0f);
                const float y = baseY + gy * (glyphHeight + 1.0f);

                glBegin(GL_TRIANGLE_STRIP);
                glTexCoord2f(0.0f, 0.0f);
                glVertex3f(x, y, 0.0f);
                glTexCoord2f(0.0f, 1.0f);
                glVertex3f(x, y + glyphHeight, 0.0f);
                glTexCoord2f(1.0f, 0.0f);
                glVertex3f(x + glyphWidth, y, 0.0f);
                glTexCoord2f(1.0f, 1.0f);
                glVertex3f(x + glyphWidth, y + glyphHeight, 0.0f);
                glEnd();
            }
        }

        void DrawImmediateMenuQuads(int quadCount, float baseX, float baseY, float quadSize) {
            const int gridWidth = std::max(1, static_cast<int>(std::sqrt(static_cast<float>(quadCount))));
            for (int quadIndex = 0; quadIndex < quadCount; ++quadIndex) {
                const int gx = quadIndex % gridWidth;
                const int gy = quadIndex / gridWidth;
                const float x = baseX + gx * (quadSize + 2.0f);
                const float y = baseY + gy * (quadSize + 2.0f);

                glPushMatrix();
                glTranslatef(x, y, 0.0f);
                glBegin(GL_QUADS);
                glTexCoord2f(0.0f, 0.0f);
                glVertex3f(0.0f, 0.0f, 0.0f);
                glTexCoord2f(0.0f, 1.0f);
                glVertex3f(0.0f, quadSize, 0.0f);
                glTexCoord2f(1.0f, 1.0f);
                glVertex3f(quadSize, quadSize, 0.0f);
                glTexCoord2f(1.0f, 0.0f);
                glVertex3f(quadSize, 0.0f, 0.0f);
                glEnd();
                glPopMatrix();
            }
        }

        void IssueTrackClientArrayDraw(const TrackClientArrayBatch& batch) {
            glEnableClientState(GL_TEXTURE_COORD_ARRAY);
            glEnableClientState(GL_COLOR_ARRAY);
            glEnableClientState(GL_VERTEX_ARRAY);

            const auto* base = batch.Vertices.data();
            glTexCoordPointer(2, GL_FLOAT, sizeof(TrackInterleavedVertex), &base[0].TexCoord[0]);
            glColorPointer(4, GL_UNSIGNED_BYTE, sizeof(TrackInterleavedVertex), &base[0].Color[0]);
            glVertexPointer(3, GL_FLOAT, sizeof(TrackInterleavedVertex), &base[0].Position[0]);
            glDrawArrays(GL_QUADS, 0, batch.VertexCount);

            glDisableClientState(GL_VERTEX_ARRAY);
            glDisableClientState(GL_TEXTURE_COORD_ARRAY);
            glDisableClientState(GL_COLOR_ARRAY);
        }

        void DrawTrackClientArrayRepeats(const TrackClientArrayBatch& batch, int repeats) {
            for (int i = 0; i < repeats; ++i) {
                IssueTrackClientArrayDraw(batch);
            }
        }

        void DrawPanoramaLikeClientArrayRepeats(const TrackClientArrayBatch& batch, int repeats, float radius) {
            const int gridWidth = std::max(1, static_cast<int>(std::sqrt(static_cast<float>(repeats))));
            for (int i = 0; i < repeats; ++i) {
                const int gx = i % gridWidth;
                const int gy = i / gridWidth;
                const float tx = ((static_cast<float>(gx) / static_cast<float>(gridWidth)) - 0.5f) * radius;
                const float ty = ((static_cast<float>(gy) / static_cast<float>(gridWidth)) - 0.5f) * radius;

                glPushMatrix();
                glTranslatef(tx, ty, 0.0f);
                glRotatef(20.0f + static_cast<float>(i % 7), 1.0f, 0.0f, 0.0f);
                glRotatef(static_cast<float>(i * 3), 0.0f, 1.0f, 0.0f);
                IssueTrackClientArrayDraw(batch);
                glPopMatrix();
            }
        }

        void IssueBatchedFontDraw(const BatchedFontBuffers& buffers) {
            glEnableClientState(GL_TEXTURE_COORD_ARRAY);
            glEnableClientState(GL_COLOR_ARRAY);
            glEnableClientState(GL_VERTEX_ARRAY);

            glTexCoordPointer(2, GL_FLOAT, 0, buffers.TexCoords.data());
            glColorPointer(4, GL_UNSIGNED_BYTE, 0, buffers.Colors.data());
            glVertexPointer(2, GL_FLOAT, 0, buffers.Positions.data());
            glDrawElements(GL_TRIANGLES, buffers.IndexCount, GL_UNSIGNED_SHORT, buffers.Indices.data());

            glDisableClientState(GL_VERTEX_ARRAY);
            glDisableClientState(GL_TEXTURE_COORD_ARRAY);
            glDisableClientState(GL_COLOR_ARRAY);
        }

        BenchmarkResult MeasureScenario(const OffscreenFixedPipelineHarness& harness,
                                        const char* label,
                                        int drawsPerFrame,
                                        const std::function<void()>& frameFn,
                                        int warmupFrames = kWarmupFrames,
                                        int timedFrames = kTimedFrames) {
            const double frameNanoseconds = harness.MeasureFrameNanoseconds(frameFn, warmupFrames, timedFrames);
            const double drawNanoseconds = frameNanoseconds / static_cast<double>(std::max(1, drawsPerFrame));
            std::printf("SFPEW_PERF case=%s frame_ns=%.2f draw_ns=%.2f draws_per_frame=%d fps=%.2f\n",
                        label,
                        frameNanoseconds,
                        drawNanoseconds,
                        drawsPerFrame,
                        1.0e9 / frameNanoseconds);
            return {.FrameNanoseconds = frameNanoseconds, .DrawNanoseconds = drawNanoseconds};
        }

        bool PixelHasVisibleColor(const OffscreenFixedPipelineHarness::Pixel& pixel) {
            return pixel[0] > 8 || pixel[1] > 8 || pixel[2] > 8;
        }
    } // namespace

    class SFPEWPerfFixture : public ::testing::Test {
    protected:
        static void SetUpTestSuite() {
            ASSERT_TRUE(Harness.Initialize(kSurfaceSize, kSurfaceSize, &Error)) << Error;
        }

        static void TearDownTestSuite() {
            Harness.Reset();
        }

        static OffscreenFixedPipelineHarness Harness;
        static std::string Error;
    };

    OffscreenFixedPipelineHarness SFPEWPerfFixture::Harness;
    std::string SFPEWPerfFixture::Error;

    TEST_F(SFPEWPerfFixture, VanillaImmediateTriangleStripFontSmokeAndPerf) {
        const GLuint texture = CreateAlphaCheckerTexture();
        Setup2D(Harness.GetWidth(), Harness.GetHeight());

        const auto frameFn = [&]() {
            ASSERT_TRUE(Harness.Clear(0.04f, 0.04f, 0.08f, 1.0f, &Error)) << Error;
            glBindTexture(GL_TEXTURE_2D, texture);
            DrawImmediateTriangleStripGlyphs(640, 12.0f, 12.0f);
        };

        frameFn();
        Harness.Finish();
        EXPECT_TRUE(PixelHasVisibleColor(Harness.ReadPixel(64, 64)));
        const auto result = MeasureScenario(Harness, "VanillaImmediateTriangleStripFont", 640, frameFn);
        EXPECT_GT(result.FrameNanoseconds, 0.0);

        glDeleteTextures(1, &texture);
    }

    TEST_F(SFPEWPerfFixture, VanillaClientArrayGuiQuads4SmokeAndPerf) {
        const GLuint texture = CreateAlphaCheckerTexture();
        const auto batch = BuildTrackClientArrayBatch(1, 24.0f, 24.0f, 0.0f);
        Setup2D(Harness.GetWidth(), Harness.GetHeight());

        const auto frameFn = [&]() {
            ASSERT_TRUE(Harness.Clear(0.06f, 0.08f, 0.10f, 1.0f, &Error)) << Error;
            glBindTexture(GL_TEXTURE_2D, texture);
            DrawTrackClientArrayRepeats(batch, 128);
        };

        frameFn();
        Harness.Finish();
        EXPECT_TRUE(PixelHasVisibleColor(Harness.ReadPixel(16, 16)));
        const auto result = MeasureScenario(Harness, "VanillaClientArrayGuiQuads4", 128, frameFn, 4, 20);
        EXPECT_GT(result.FrameNanoseconds, 0.0);

        glDeleteTextures(1, &texture);
    }

    TEST_F(SFPEWPerfFixture, VanillaClientArrayGuiQuads12SmokeAndPerf) {
        const GLuint texture = CreateAlphaCheckerTexture();
        const auto batch = BuildTrackClientArrayBatch(3, 20.0f, 20.0f, 2.0f);
        Setup2D(Harness.GetWidth(), Harness.GetHeight());

        const auto frameFn = [&]() {
            ASSERT_TRUE(Harness.Clear(0.06f, 0.08f, 0.10f, 1.0f, &Error)) << Error;
            glBindTexture(GL_TEXTURE_2D, texture);
            DrawTrackClientArrayRepeats(batch, 64);
        };

        frameFn();
        Harness.Finish();
        EXPECT_TRUE(PixelHasVisibleColor(Harness.ReadPixel(16, 16)));
        const auto result = MeasureScenario(Harness, "VanillaClientArrayGuiQuads12", 64, frameFn, 4, 20);
        EXPECT_GT(result.FrameNanoseconds, 0.0);

        glDeleteTextures(1, &texture);
    }

    TEST_F(SFPEWPerfFixture, CMMImmediateMenuQuadsSmokeAndPerf) {
        const GLuint texture = CreateAlphaCheckerTexture();
        Setup2D(Harness.GetWidth(), Harness.GetHeight());

        const auto frameFn = [&]() {
            ASSERT_TRUE(Harness.Clear(0.06f, 0.08f, 0.10f, 1.0f, &Error)) << Error;
            glBindTexture(GL_TEXTURE_2D, texture);
            DrawImmediateMenuQuads(256, 16.0f, 16.0f, 20.0f);
        };

        frameFn();
        Harness.Finish();
        EXPECT_TRUE(PixelHasVisibleColor(Harness.ReadPixel(24, 24)));
        const auto result = MeasureScenario(Harness, "CMMImmediateMenuQuads", 256, frameFn);
        EXPECT_GT(result.FrameNanoseconds, 0.0);

        glDeleteTextures(1, &texture);
    }

    TEST_F(SFPEWPerfFixture, CMMPanoramaClientArrayQuadsSmokeAndPerf) {
        const GLuint texture = CreateAlphaCheckerTexture();
        const auto batch = BuildTrackClientArrayBatch(1, 1.0f, 1.0f, 0.0f);
        Setup2D(Harness.GetWidth(), Harness.GetHeight());

        const auto frameFn = [&]() {
            ASSERT_TRUE(Harness.Clear(0.03f, 0.03f, 0.06f, 1.0f, &Error)) << Error;
            glBindTexture(GL_TEXTURE_2D, texture);
            DrawPanoramaLikeClientArrayRepeats(batch, 64, 0.25f);
        };

        frameFn();
        Harness.Finish();
        EXPECT_TRUE(PixelHasVisibleColor(Harness.ReadPixel(Harness.GetWidth() / 2, Harness.GetHeight() / 2)));
        const auto result = MeasureScenario(Harness, "CMMPanoramaClientArrayQuads", 64, frameFn, 4, 20);
        EXPECT_GT(result.FrameNanoseconds, 0.0);

        glDeleteTextures(1, &texture);
    }

    TEST_F(SFPEWPerfFixture, VanillaDisplayListCompileAndCallGuiQuadsSmokeAndPerf) {
        const GLuint texture = CreateAlphaCheckerTexture();
        Setup2D(Harness.GetWidth(), Harness.GetHeight());

        const GLuint listId = glGenLists(1);
        ASSERT_NE(listId, 0u);
        glNewList(listId, GL_COMPILE);
        glBindTexture(GL_TEXTURE_2D, texture);
        DrawImmediateMenuQuads(1, 32.0f, 32.0f, 128.0f);
        glEndList();

        const auto frameFn = [&]() {
            ASSERT_TRUE(Harness.Clear(0.04f, 0.04f, 0.08f, 1.0f, &Error)) << Error;
            glBindTexture(GL_TEXTURE_2D, texture);
            for (int i = 0; i < 256; ++i) {
                glCallList(listId);
            }
        };

        frameFn();
        Harness.Finish();
        EXPECT_TRUE(PixelHasVisibleColor(Harness.ReadPixel(96, 96)));
        const auto result = MeasureScenario(Harness, "VanillaDisplayListCompileAndCallGuiQuads", 256, frameFn);
        EXPECT_GT(result.FrameNanoseconds, 0.0);

        glDeleteLists(listId, 1);
        glDeleteTextures(1, &texture);
    }

    TEST_F(SFPEWPerfFixture, AngelicaBatchedFontDrawElementsSmokeAndPerf) {
        const GLuint texture = CreateAlphaCheckerTexture();
        const auto buffers = BuildBatchedFontGlyphs(192, 12.0f, 12.0f, 8.0f, 12.0f);
        Setup2D(Harness.GetWidth(), Harness.GetHeight());
        glEnable(GL_BLEND);
        glEnable(GL_ALPHA_TEST);
        glAlphaFunc(GL_GREATER, 0.1f);

        const auto frameFn = [&]() {
            ASSERT_TRUE(Harness.Clear(0.02f, 0.02f, 0.04f, 1.0f, &Error)) << Error;
            glBindTexture(GL_TEXTURE_2D, texture);
            IssueBatchedFontDraw(buffers);
        };

        frameFn();
        Harness.Finish();
        EXPECT_TRUE(PixelHasVisibleColor(Harness.ReadPixel(64, 64)));
        const auto result = MeasureScenario(Harness, "AngelicaBatchedFontDrawElements", 1, frameFn, 2, 8);
        EXPECT_GT(result.FrameNanoseconds, 0.0);

        glDeleteTextures(1, &texture);
    }

    TEST_F(SFPEWPerfFixture, VanillaWorldSizedClientArrayQuadsSmokeAndPerf) {
        const GLuint texture = CreateAlphaCheckerTexture();
        const auto batch68 = BuildTrackClientArrayBatch(17, 10.0f, 10.0f, 1.0f);
        const auto batch100 = BuildTrackClientArrayBatch(25, 8.0f, 8.0f, 1.0f);
        const auto batch132 = BuildTrackClientArrayBatch(33, 7.0f, 7.0f, 1.0f);
        const auto batch64 = BuildTrackClientArrayBatch(16, 10.0f, 10.0f, 1.0f);
        Setup2D(Harness.GetWidth(), Harness.GetHeight());

        const auto frameFn = [&]() {
            ASSERT_TRUE(Harness.Clear(0.10f, 0.12f, 0.16f, 1.0f, &Error)) << Error;
            glBindTexture(GL_TEXTURE_2D, texture);
            DrawTrackClientArrayRepeats(batch68, 72);
            DrawTrackClientArrayRepeats(batch100, 48);
            DrawTrackClientArrayRepeats(batch132, 8);
            DrawTrackClientArrayRepeats(batch64, 28);
        };

        frameFn();
        Harness.Finish();
        EXPECT_TRUE(PixelHasVisibleColor(Harness.ReadPixel(16, 16)));
        const auto result = MeasureScenario(Harness, "VanillaWorldSizedClientArrayQuads", 156, frameFn, 2, 10);
        EXPECT_GT(result.FrameNanoseconds, 0.0);

        glDeleteTextures(1, &texture);
    }
} // namespace MobileGL::MG_Test::Compat::SFPEW
