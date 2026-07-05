// MobileGL - MobileGL/MG_Test/RenderPath/MobileGL/MobileGLPathPerfTest.cpp
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
#include <cstring>
#include <string>
#include <vector>

namespace MobileGL::MG_Test::RenderPath::MobileGLPath {
    using OffscreenHarness = MobileGL::MG_Test::Compat::SFPEW::OffscreenFixedPipelineHarness;

    namespace {
        constexpr GLint kSurfaceSize = 512;
        constexpr int kWarmupFrames = 8;
        constexpr int kTimedFrames = 30;
        constexpr int kDrawCount = 256;
        constexpr int kQuadsPerDraw = 3;

        struct Vertex {
            GLfloat Position[3];
            GLubyte Color[4];
            GLfloat TexCoord[2];
        };

        struct DrawArraysIndirectCommand {
            GLuint Count = 0;
            GLuint InstanceCount = 1;
            GLuint First = 0;
            GLuint BaseInstance = 0;
        };

        struct PathPerfResult {
            double FrameNanoseconds = 0.0;
            double DrawNanoseconds = 0.0;
        };

        struct MeshBuffers {
            GLuint Vao = 0;
            GLuint Vbo = 0;
            GLuint IndirectBuffer = 0;
            std::vector<GLint> Firsts;
            std::vector<GLsizei> Counts;
            std::vector<DrawArraysIndirectCommand> Commands;
        };

        struct ProgramBundle {
            GLuint Program = 0;
            GLint SamplerLocation = -1;
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
                255,   0,   0, 255,   0, 255,   0, 160, 255,   0,   0, 255,   0, 255,   0, 160,
                  0,   0, 255, 255, 255, 255,   0, 255,   0,   0, 255, 255, 255, 255,   0, 255,
                255,   0,   0, 255,   0, 255,   0, 160, 255,   0,   0, 255,   0, 255,   0, 160,
                  0,   0, 255, 255, 255, 255,   0, 255,   0,   0, 255, 255, 255, 255,   0, 255,
            };
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 4, 4, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
            return texture;
        }

        void SetupRenderState(GLint width, GLint height) {
            glDisable(GL_CULL_FACE);
            glDisable(GL_DEPTH_TEST);
            glDisable(GL_BLEND);
            glViewport(0, 0, width, height);
            glClearColor(0.06f, 0.10f, 0.16f, 1.0f);
        }

        bool PixelHasVisibleColor(const OffscreenHarness::Pixel& pixel) {
            return pixel[0] > 8 || pixel[1] > 8 || pixel[2] > 8;
        }

        GLuint CompileShader(GLenum type, const char* source) {
            const GLuint shader = glCreateShader(type);
            glShaderSource(shader, 1, &source, nullptr);
            glCompileShader(shader);

            GLint compileStatus = GL_FALSE;
            glGetShaderiv(shader, GL_COMPILE_STATUS, &compileStatus);
            if (compileStatus == GL_TRUE) {
                return shader;
            }

            GLchar log[1024] = {};
            glGetShaderInfoLog(shader, static_cast<GLsizei>(sizeof(log)), nullptr, log);
            std::fprintf(stderr, "MOBILEGL_PATH_SHADER_COMPILE_ERROR type=%u log=%s\n", type, log);
            glDeleteShader(shader);
            return 0;
        }

        ProgramBundle CreateProgram() {
            static constexpr const char* kVertexShader = R"(#version 330 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec4 aColor;
layout(location = 2) in vec2 aTexCoord;
out vec4 vColor;
out vec2 vTexCoord;
void main() {
    gl_Position = vec4(aPos, 1.0);
    vColor = aColor;
    vTexCoord = aTexCoord;
}
)";

            static constexpr const char* kFragmentShader = R"(#version 330 core
uniform sampler2D uTexture;
in vec4 vColor;
in vec2 vTexCoord;
out vec4 fragColor;
void main() {
    fragColor = texture(uTexture, vTexCoord) * vColor;
}
)";

            ProgramBundle bundle{};
            const GLuint vs = CompileShader(GL_VERTEX_SHADER, kVertexShader);
            const GLuint fs = CompileShader(GL_FRAGMENT_SHADER, kFragmentShader);
            if (vs == 0 || fs == 0) {
                if (vs != 0) {
                    glDeleteShader(vs);
                }
                if (fs != 0) {
                    glDeleteShader(fs);
                }
                return bundle;
            }

            bundle.Program = glCreateProgram();
            glAttachShader(bundle.Program, vs);
            glAttachShader(bundle.Program, fs);
            glLinkProgram(bundle.Program);
            glDeleteShader(vs);
            glDeleteShader(fs);

            GLint linkStatus = GL_FALSE;
            glGetProgramiv(bundle.Program, GL_LINK_STATUS, &linkStatus);
            if (linkStatus != GL_TRUE) {
                GLchar log[1024] = {};
                glGetProgramInfoLog(bundle.Program, static_cast<GLsizei>(sizeof(log)), nullptr, log);
                std::fprintf(stderr, "MOBILEGL_PATH_PROGRAM_LINK_ERROR log=%s\n", log);
                glDeleteProgram(bundle.Program);
                bundle.Program = 0;
                return bundle;
            }

            bundle.SamplerLocation = glGetUniformLocation(bundle.Program, "uTexture");
            return bundle;
        }

        MeshBuffers CreateMeshBuffers(int drawCount, int quadsPerDraw) {
            MeshBuffers mesh{};
            std::vector<Vertex> vertices;
            vertices.reserve(static_cast<size_t>(drawCount * quadsPerDraw * 4));
            mesh.Firsts.reserve(static_cast<size_t>(drawCount));
            mesh.Counts.reserve(static_cast<size_t>(drawCount));
            mesh.Commands.reserve(static_cast<size_t>(drawCount));

            const int gridWidth = std::max(1, static_cast<int>(std::sqrt(static_cast<float>(drawCount))));
            const float quadWidth = 0.05f;
            const float quadHeight = 0.05f;
            const float drawGapX = 0.12f;
            const float drawGapY = 0.12f;
            const float quadGap = 0.01f;

            for (int drawIndex = 0; drawIndex < drawCount; ++drawIndex) {
                const int gx = drawIndex % gridWidth;
                const int gy = drawIndex / gridWidth;
                const float baseX = -0.95f + static_cast<float>(gx) * drawGapX;
                const float baseY = -0.95f + static_cast<float>(gy) * drawGapY;
                const GLint first = static_cast<GLint>(vertices.size());

                for (int quadIndex = 0; quadIndex < quadsPerDraw; ++quadIndex) {
                    const float x = baseX + static_cast<float>(quadIndex) * (quadWidth + quadGap);
                    const float y = baseY;
                    const GLubyte red = static_cast<GLubyte>(120 + ((drawIndex + quadIndex) * 17) % 100);
                    const GLubyte green = static_cast<GLubyte>(140 + ((drawIndex + quadIndex) * 29) % 80);
                    const GLubyte blue = static_cast<GLubyte>(180 + ((drawIndex + quadIndex) * 11) % 60);

                    const Vertex quad[4] = {
                        {{x, y, 0.0f}, {red, green, blue, 255}, {0.0f, 0.0f}},
                        {{x, y + quadHeight, 0.0f}, {red, green, blue, 255}, {0.0f, 1.0f}},
                        {{x + quadWidth, y + quadHeight, 0.0f}, {red, green, blue, 255}, {1.0f, 1.0f}},
                        {{x + quadWidth, y, 0.0f}, {red, green, blue, 255}, {1.0f, 0.0f}},
                    };
                    vertices.insert(vertices.end(), std::begin(quad), std::end(quad));
                }

                const GLsizei count = static_cast<GLsizei>(quadsPerDraw * 4);
                mesh.Firsts.push_back(first);
                mesh.Counts.push_back(count);
                mesh.Commands.push_back({
                    .Count = static_cast<GLuint>(count),
                    .InstanceCount = 1u,
                    .First = static_cast<GLuint>(first),
                    .BaseInstance = 0u,
                });
            }

            glGenVertexArrays(1, &mesh.Vao);
            glBindVertexArray(mesh.Vao);

            glGenBuffers(1, &mesh.Vbo);
            glBindBuffer(GL_ARRAY_BUFFER, mesh.Vbo);
            glBufferData(GL_ARRAY_BUFFER,
                         static_cast<GLsizeiptr>(vertices.size() * sizeof(Vertex)),
                         vertices.data(),
                         GL_STATIC_DRAW);

            glEnableVertexAttribArray(0);
            glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), reinterpret_cast<const void*>(offsetof(Vertex, Position)));
            glEnableVertexAttribArray(1);
            glVertexAttribPointer(1, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(Vertex), reinterpret_cast<const void*>(offsetof(Vertex, Color)));
            glEnableVertexAttribArray(2);
            glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), reinterpret_cast<const void*>(offsetof(Vertex, TexCoord)));

            glGenBuffers(1, &mesh.IndirectBuffer);
            glBindBuffer(GL_DRAW_INDIRECT_BUFFER, mesh.IndirectBuffer);
            glBufferData(GL_DRAW_INDIRECT_BUFFER,
                         static_cast<GLsizeiptr>(mesh.Commands.size() * sizeof(DrawArraysIndirectCommand)),
                         mesh.Commands.data(),
                         GL_STATIC_DRAW);

            glBindBuffer(GL_DRAW_INDIRECT_BUFFER, 0);
            glBindBuffer(GL_ARRAY_BUFFER, 0);
            glBindVertexArray(0);

            return mesh;
        }

        void DestroyMeshBuffers(MeshBuffers* mesh) {
            if (mesh == nullptr) {
                return;
            }

            if (mesh->IndirectBuffer != 0) {
                glDeleteBuffers(1, &mesh->IndirectBuffer);
                mesh->IndirectBuffer = 0;
            }
            if (mesh->Vbo != 0) {
                glDeleteBuffers(1, &mesh->Vbo);
                mesh->Vbo = 0;
            }
            if (mesh->Vao != 0) {
                glDeleteVertexArrays(1, &mesh->Vao);
                mesh->Vao = 0;
            }
        }

        PathPerfResult MeasureScenario(const OffscreenHarness& harness, const char* label, int drawsPerFrame,
                                       const std::function<void()>& frameFn) {
            const double frameNanoseconds = harness.MeasureFrameNanoseconds(frameFn, kWarmupFrames, kTimedFrames);
            const double drawNanoseconds = frameNanoseconds / static_cast<double>(std::max(1, drawsPerFrame));
            std::printf("MOBILEGL_PATH_PERF case=%s frame_ns=%.2f draw_ns=%.2f draws_per_frame=%d fps=%.2f\n",
                        label,
                        frameNanoseconds,
                        drawNanoseconds,
                        drawsPerFrame,
                        1.0e9 / frameNanoseconds);
            return {.FrameNanoseconds = frameNanoseconds, .DrawNanoseconds = drawNanoseconds};
        }
    } // namespace

    class MobileGLPathPerfFixture : public ::testing::Test {
    protected:
        static void SetUpTestSuite() {
            ASSERT_TRUE(HarnessInstance.Initialize(kSurfaceSize, kSurfaceSize, &Error)) << Error;
            SetupRenderState(HarnessInstance.GetWidth(), HarnessInstance.GetHeight());

            Texture = CreateAlphaCheckerTexture();
            Program = CreateProgram();
            ASSERT_NE(Program.Program, 0u);
            Mesh = CreateMeshBuffers(kDrawCount, kQuadsPerDraw);
        }

        static void TearDownTestSuite() {
            DestroyMeshBuffers(&Mesh);
            if (Program.Program != 0) {
                glDeleteProgram(Program.Program);
                Program.Program = 0;
            }
            if (Texture != 0) {
                glDeleteTextures(1, &Texture);
                Texture = 0;
            }
            HarnessInstance.Reset();
        }

        static bool SupportsMultiDrawIndirect() {
            const auto* ext = reinterpret_cast<const char*>(glGetString(GL_EXTENSIONS));
            if (ext != nullptr && std::strstr(ext, "GL_ARB_multi_draw_indirect") != nullptr) {
                return true;
            }
            return eglGetProcAddress("glMultiDrawArraysIndirect") != nullptr;
        }

        static OffscreenHarness HarnessInstance;
        static std::string Error;
        static GLuint Texture;
        static ProgramBundle Program;
        static MeshBuffers Mesh;
    };

    OffscreenHarness MobileGLPathPerfFixture::HarnessInstance;
    std::string MobileGLPathPerfFixture::Error;
    GLuint MobileGLPathPerfFixture::Texture = 0;
    ProgramBundle MobileGLPathPerfFixture::Program;
    MeshBuffers MobileGLPathPerfFixture::Mesh;

    TEST_F(MobileGLPathPerfFixture, AngelicaWorldOneshotMultiDrawArraysSmokeAndPerf) {
        const auto frameFn = [&]() {
            ASSERT_TRUE(HarnessInstance.Clear(0.06f, 0.10f, 0.16f, 1.0f, &Error)) << Error;
            glUseProgram(Program.Program);
            if (Program.SamplerLocation >= 0) {
                glUniform1i(Program.SamplerLocation, 0);
            }
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, Texture);
            glBindVertexArray(Mesh.Vao);
            glMultiDrawArrays(GL_QUADS, Mesh.Firsts.data(), Mesh.Counts.data(), static_cast<GLsizei>(Mesh.Firsts.size()));
            glBindVertexArray(0);
            glUseProgram(0);
        };

        frameFn();
        HarnessInstance.Finish();
        EXPECT_TRUE(PixelHasVisibleColor(HarnessInstance.ReadPixel(HarnessInstance.GetWidth() / 2, HarnessInstance.GetHeight() / 2)));
        const auto result = MeasureScenario(HarnessInstance, "AngelicaWorldOneshotMultiDrawArrays", kDrawCount, frameFn);
        EXPECT_GT(result.FrameNanoseconds, 0.0);
    }

    TEST_F(MobileGLPathPerfFixture, AngelicaWorldMultiDrawIndirectSmokeAndPerf) {
        if (!SupportsMultiDrawIndirect()) {
            GTEST_SKIP() << "glMultiDrawArraysIndirect is not available on this MobileGL path";
        }

        const auto frameFn = [&]() {
            ASSERT_TRUE(HarnessInstance.Clear(0.06f, 0.10f, 0.16f, 1.0f, &Error)) << Error;
            glUseProgram(Program.Program);
            if (Program.SamplerLocation >= 0) {
                glUniform1i(Program.SamplerLocation, 0);
            }
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, Texture);
            glBindVertexArray(Mesh.Vao);
            glBindBuffer(GL_DRAW_INDIRECT_BUFFER, Mesh.IndirectBuffer);
            glMultiDrawArraysIndirect(GL_QUADS, reinterpret_cast<const void*>(0), static_cast<GLsizei>(Mesh.Commands.size()), 0);
            glBindBuffer(GL_DRAW_INDIRECT_BUFFER, 0);
            glBindVertexArray(0);
            glUseProgram(0);
        };

        frameFn();
        HarnessInstance.Finish();
        EXPECT_TRUE(PixelHasVisibleColor(HarnessInstance.ReadPixel(HarnessInstance.GetWidth() / 2, HarnessInstance.GetHeight() / 2)));
        const auto result = MeasureScenario(HarnessInstance, "AngelicaWorldMultiDrawIndirect", kDrawCount, frameFn);
        EXPECT_GT(result.FrameNanoseconds, 0.0);
    }
} // namespace MobileGL::MG_Test::RenderPath::MobileGLPath
