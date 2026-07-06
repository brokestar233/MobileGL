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
        constexpr int kRegionCount = 192;
        constexpr int kFacesPerRegion = 7;
        constexpr int kIndirectRegionCount = 32;
        constexpr int kChunksPerIndirectRegion = 6;
        constexpr int kFacesPerIndirectChunk = 7;

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

        struct RegionBatchBuffers {
            GLuint Vao = 0;
            GLuint Vbo = 0;
            std::array<GLint, kFacesPerRegion> Firsts = {};
            std::array<GLsizei, kFacesPerRegion> Counts = {};
            std::array<GLfloat, 4> ModelOffset = {0.0f, 0.0f, 0.0f, 0.0f};
        };

        struct RegionedMeshBuffers {
            std::vector<RegionBatchBuffers> Regions;
        };

        struct IndirectRegionBuffers {
            GLuint Vao = 0;
            GLuint VertexBuffer = 0;
            GLsizei DrawCount = 0;
            GLsizeiptr CommandByteOffset = 0;
        };

        struct RegionedIndirectMeshBuffers {
            GLuint UniformBuffer = 0;
            GLuint IndirectBuffer = 0;
            std::vector<std::array<GLfloat, 4>> ModelOffsets;
            std::vector<DrawArraysIndirectCommand> Commands;
            std::vector<IndirectRegionBuffers> Regions;
        };

        struct InstancedQuadBuffers {
            GLuint Vao = 0;
            GLuint VertexBuffer = 0;
            GLuint InstanceBuffer = 0;
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

        OffscreenHarness::Pixel MakeExpectedPixel(float red, float green, float blue, float alpha) {
            auto toByte = [](float value) -> uint8_t {
                const float clamped = std::clamp(value, 0.0f, 1.0f);
                return static_cast<uint8_t>(std::lround(clamped * 255.0f));
            };

            return {toByte(red), toByte(green), toByte(blue), toByte(alpha)};
        }

        bool PixelDiffersFrom(const OffscreenHarness::Pixel& pixel,
                              const OffscreenHarness::Pixel& expected,
                              uint8_t threshold = 8) {
            const auto absDiff = [](uint8_t a, uint8_t b) -> uint8_t {
                return static_cast<uint8_t>(a > b ? (a - b) : (b - a));
            };

            return absDiff(pixel[0], expected[0]) > threshold ||
                   absDiff(pixel[1], expected[1]) > threshold ||
                   absDiff(pixel[2], expected[2]) > threshold ||
                   absDiff(pixel[3], expected[3]) > threshold;
        }

        OffscreenHarness::Pixel ReadPixelAtNdc(const OffscreenHarness& harness, float ndcX, float ndcY);

        bool AnyPixelDiffersFrom(const OffscreenHarness& harness,
                                 std::initializer_list<std::array<float, 2>> ndcPoints,
                                 const OffscreenHarness::Pixel& expected,
                                 uint8_t threshold = 8) {
            for (const auto& point : ndcPoints) {
                if (PixelDiffersFrom(ReadPixelAtNdc(harness, point[0], point[1]), expected, threshold)) {
                    return true;
                }
            }
            return false;
        }

        OffscreenHarness::Pixel ReadPixelAtNdc(const OffscreenHarness& harness, float ndcX, float ndcY) {
            const auto clampCoord = [](GLint value, GLint limit) -> GLint {
                return std::clamp(value, 0, std::max(0, limit - 1));
            };

            const float normX = std::clamp(ndcX * 0.5f + 0.5f, 0.0f, 1.0f);
            const float normY = std::clamp(ndcY * 0.5f + 0.5f, 0.0f, 1.0f);
            const GLint x = clampCoord(static_cast<GLint>(std::lround(normX * static_cast<float>(harness.GetWidth() - 1))),
                                       harness.GetWidth());
            const GLint y = clampCoord(static_cast<GLint>(std::lround(normY * static_cast<float>(harness.GetHeight() - 1))),
                                       harness.GetHeight());
            return harness.ReadPixel(x, y);
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
layout(location = 3) in vec4 dModelOffset;
out vec4 vColor;
out vec2 vTexCoord;
void main() {
    gl_Position = vec4(aPos + dModelOffset.xyz, 1.0);
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

        RegionedMeshBuffers CreateRegionedMeshBuffers(int regionCount, int facesPerRegion) {
            RegionedMeshBuffers mesh{};
            mesh.Regions.reserve(static_cast<size_t>(regionCount));

            const int gridWidth = std::max(1, static_cast<int>(std::ceil(std::sqrt(static_cast<float>(regionCount)))));
            const float regionStep = 0.14f;
            const float localQuadWidth = 0.028f;
            const float localQuadHeight = 0.028f;
            const float localGap = 0.004f;

            for (int regionIndex = 0; regionIndex < regionCount; ++regionIndex) {
                RegionBatchBuffers region{};
                std::vector<Vertex> vertices;
                vertices.reserve(static_cast<size_t>(facesPerRegion) * 4u);

                const int gx = regionIndex % gridWidth;
                const int gy = regionIndex / gridWidth;
                region.ModelOffset = {
                    -0.92f + static_cast<float>(gx) * regionStep,
                    -0.92f + static_cast<float>(gy) * regionStep,
                    0.0f,
                    0.0f,
                };

                for (int faceIndex = 0; faceIndex < facesPerRegion; ++faceIndex) {
                    const int fx = faceIndex % 3;
                    const int fy = faceIndex / 3;
                    const float x = static_cast<float>(fx) * (localQuadWidth + localGap);
                    const float y = static_cast<float>(fy) * (localQuadHeight + localGap);
                    const GLubyte red = static_cast<GLubyte>(96 + ((regionIndex * 19 + faceIndex * 11) % 128));
                    const GLubyte green = static_cast<GLubyte>(128 + ((regionIndex * 13 + faceIndex * 7) % 96));
                    const GLubyte blue = static_cast<GLubyte>(160 + ((regionIndex * 17 + faceIndex * 5) % 80));

                    region.Firsts[faceIndex] = static_cast<GLint>(vertices.size());
                    region.Counts[faceIndex] = 4;

                    const Vertex quad[4] = {
                        {{x, y, 0.0f}, {red, green, blue, 255}, {0.0f, 0.0f}},
                        {{x, y + localQuadHeight, 0.0f}, {red, green, blue, 255}, {0.0f, 1.0f}},
                        {{x + localQuadWidth, y + localQuadHeight, 0.0f}, {red, green, blue, 255}, {1.0f, 1.0f}},
                        {{x + localQuadWidth, y, 0.0f}, {red, green, blue, 255}, {1.0f, 0.0f}},
                    };
                    vertices.insert(vertices.end(), std::begin(quad), std::end(quad));
                }

                glGenVertexArrays(1, &region.Vao);
                glBindVertexArray(region.Vao);

                glGenBuffers(1, &region.Vbo);
                glBindBuffer(GL_ARRAY_BUFFER, region.Vbo);
                glBufferData(GL_ARRAY_BUFFER,
                             static_cast<GLsizeiptr>(vertices.size() * sizeof(Vertex)),
                             vertices.data(),
                             GL_STATIC_DRAW);

                glEnableVertexAttribArray(0);
                glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                                      reinterpret_cast<const void*>(offsetof(Vertex, Position)));
                glEnableVertexAttribArray(1);
                glVertexAttribPointer(1, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(Vertex),
                                      reinterpret_cast<const void*>(offsetof(Vertex, Color)));
                glEnableVertexAttribArray(2);
                glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                                      reinterpret_cast<const void*>(offsetof(Vertex, TexCoord)));

                glBindVertexArray(0);
                glBindBuffer(GL_ARRAY_BUFFER, 0);

                mesh.Regions.push_back(region);
            }

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

        void DestroyRegionedMeshBuffers(RegionedMeshBuffers* mesh) {
            if (mesh == nullptr) {
                return;
            }

            for (auto& region : mesh->Regions) {
                if (region.Vbo != 0) {
                    glDeleteBuffers(1, &region.Vbo);
                    region.Vbo = 0;
                }
                if (region.Vao != 0) {
                    glDeleteVertexArrays(1, &region.Vao);
                    region.Vao = 0;
                }
            }

            mesh->Regions.clear();
        }

        RegionedIndirectMeshBuffers CreateRegionedIndirectMeshBuffers(int regionCount, int chunksPerRegion, int facesPerChunk) {
            RegionedIndirectMeshBuffers mesh{};
            const int totalChunkCount = regionCount * chunksPerRegion;
            const int gridWidth = std::max(1, static_cast<int>(std::ceil(std::sqrt(static_cast<float>(totalChunkCount)))));
            const float chunkStep = 0.14f;
            const float localQuadWidth = 0.026f;
            const float localQuadHeight = 0.026f;
            const float localGap = 0.004f;

            mesh.ModelOffsets.reserve(static_cast<size_t>(totalChunkCount));
            mesh.Commands.reserve(static_cast<size_t>(totalChunkCount * facesPerChunk));
            mesh.Regions.reserve(static_cast<size_t>(regionCount));

            glGenBuffers(1, &mesh.UniformBuffer);
            glBindBuffer(GL_ARRAY_BUFFER, mesh.UniformBuffer);
            glBufferData(GL_ARRAY_BUFFER,
                         static_cast<GLsizeiptr>(totalChunkCount * sizeof(mesh.ModelOffsets[0])),
                         nullptr,
                         GL_STREAM_DRAW);

            glGenBuffers(1, &mesh.IndirectBuffer);
            glBindBuffer(GL_DRAW_INDIRECT_BUFFER, mesh.IndirectBuffer);
            glBufferData(GL_DRAW_INDIRECT_BUFFER,
                         static_cast<GLsizeiptr>(totalChunkCount * facesPerChunk * sizeof(DrawArraysIndirectCommand)),
                         nullptr,
                         GL_STREAM_DRAW);
            glBindBuffer(GL_DRAW_INDIRECT_BUFFER, 0);
            glBindBuffer(GL_ARRAY_BUFFER, 0);

            for (int regionIndex = 0; regionIndex < regionCount; ++regionIndex) {
                IndirectRegionBuffers region{};
                std::vector<Vertex> vertices;
                vertices.reserve(static_cast<size_t>(chunksPerRegion * facesPerChunk) * 4u);
                region.CommandByteOffset = static_cast<GLsizeiptr>(mesh.Commands.size() * sizeof(DrawArraysIndirectCommand));

                for (int chunkIndex = 0; chunkIndex < chunksPerRegion; ++chunkIndex) {
                    const int globalChunkIndex = regionIndex * chunksPerRegion + chunkIndex;
                    const int gx = globalChunkIndex % gridWidth;
                    const int gy = globalChunkIndex / gridWidth;

                    mesh.ModelOffsets.push_back({
                        -0.92f + static_cast<float>(gx) * chunkStep,
                        -0.92f + static_cast<float>(gy) * chunkStep,
                        0.0f,
                        0.0f,
                    });

                    for (int faceIndex = 0; faceIndex < facesPerChunk; ++faceIndex) {
                        const int fx = faceIndex % 3;
                        const int fy = faceIndex / 3;
                        const float x = (static_cast<float>(fx) - 1.0f) * (localQuadWidth + localGap);
                        const float y = (static_cast<float>(fy) - 1.0f) * (localQuadHeight + localGap);
                        const GLubyte red = static_cast<GLubyte>(96 + ((globalChunkIndex * 19 + faceIndex * 11) % 128));
                        const GLubyte green = static_cast<GLubyte>(128 + ((globalChunkIndex * 13 + faceIndex * 7) % 96));
                        const GLubyte blue = static_cast<GLubyte>(160 + ((globalChunkIndex * 17 + faceIndex * 5) % 80));

                        const GLuint first = static_cast<GLuint>(vertices.size());
                        const Vertex quad[4] = {
                            {{x, y, 0.0f}, {red, green, blue, 255}, {0.0f, 0.0f}},
                            {{x, y + localQuadHeight, 0.0f}, {red, green, blue, 255}, {0.0f, 1.0f}},
                            {{x + localQuadWidth, y + localQuadHeight, 0.0f}, {red, green, blue, 255}, {1.0f, 1.0f}},
                            {{x + localQuadWidth, y, 0.0f}, {red, green, blue, 255}, {1.0f, 0.0f}},
                        };

                        vertices.insert(vertices.end(), std::begin(quad), std::end(quad));
                        mesh.Commands.push_back({
                            .Count = 4u,
                            .InstanceCount = 1u,
                            .First = first,
                            .BaseInstance = static_cast<GLuint>(globalChunkIndex),
                        });
                        ++region.DrawCount;
                    }
                }

                glGenVertexArrays(1, &region.Vao);
                glBindVertexArray(region.Vao);

                glGenBuffers(1, &region.VertexBuffer);
                glBindBuffer(GL_ARRAY_BUFFER, region.VertexBuffer);
                glBufferData(GL_ARRAY_BUFFER,
                             static_cast<GLsizeiptr>(vertices.size() * sizeof(Vertex)),
                             vertices.data(),
                             GL_STATIC_DRAW);

                glEnableVertexAttribArray(0);
                glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                                      reinterpret_cast<const void*>(offsetof(Vertex, Position)));
                glEnableVertexAttribArray(1);
                glVertexAttribPointer(1, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(Vertex),
                                      reinterpret_cast<const void*>(offsetof(Vertex, Color)));
                glEnableVertexAttribArray(2);
                glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                                      reinterpret_cast<const void*>(offsetof(Vertex, TexCoord)));

                glBindBuffer(GL_ARRAY_BUFFER, mesh.UniformBuffer);
                glEnableVertexAttribArray(3);
                glVertexAttribPointer(3, 4, GL_FLOAT, GL_FALSE, static_cast<GLsizei>(sizeof(mesh.ModelOffsets[0])), nullptr);
                glVertexAttribDivisor(3, 1);

                glBindBuffer(GL_ARRAY_BUFFER, 0);
                glBindVertexArray(0);

                mesh.Regions.push_back(region);
            }

            return mesh;
        }

        void DestroyRegionedIndirectMeshBuffers(RegionedIndirectMeshBuffers* mesh) {
            if (mesh == nullptr) {
                return;
            }

            for (auto& region : mesh->Regions) {
                if (region.VertexBuffer != 0) {
                    glDeleteBuffers(1, &region.VertexBuffer);
                    region.VertexBuffer = 0;
                }
                if (region.Vao != 0) {
                    glDeleteVertexArrays(1, &region.Vao);
                    region.Vao = 0;
                }
            }

            if (mesh->IndirectBuffer != 0) {
                glDeleteBuffers(1, &mesh->IndirectBuffer);
                mesh->IndirectBuffer = 0;
            }
            if (mesh->UniformBuffer != 0) {
                glDeleteBuffers(1, &mesh->UniformBuffer);
                mesh->UniformBuffer = 0;
            }

            mesh->Regions.clear();
            mesh->Commands.clear();
            mesh->ModelOffsets.clear();
        }

        InstancedQuadBuffers CreateInstancedQuadBuffers() {
            InstancedQuadBuffers buffers{};

            const Vertex vertices[4] = {
                {{-0.12f, -0.12f, 0.0f}, {255, 255, 255, 255}, {0.0f, 0.0f}},
                {{-0.12f,  0.12f, 0.0f}, {255, 255, 255, 255}, {0.0f, 1.0f}},
                {{ 0.12f, -0.12f, 0.0f}, {255, 255, 255, 255}, {1.0f, 0.0f}},
                {{ 0.12f,  0.12f, 0.0f}, {255, 255, 255, 255}, {1.0f, 1.0f}},
            };
            const GLfloat instanceOffsets[2][4] = {
                {-0.45f, 0.0f, 0.0f, 0.0f},
                { 0.45f, 0.0f, 0.0f, 0.0f},
            };

            glGenVertexArrays(1, &buffers.Vao);
            glBindVertexArray(buffers.Vao);

            glGenBuffers(1, &buffers.VertexBuffer);
            glBindBuffer(GL_ARRAY_BUFFER, buffers.VertexBuffer);
            glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);

            glEnableVertexAttribArray(0);
            glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                                  reinterpret_cast<const void*>(offsetof(Vertex, Position)));
            glEnableVertexAttribArray(1);
            glVertexAttribPointer(1, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(Vertex),
                                  reinterpret_cast<const void*>(offsetof(Vertex, Color)));
            glEnableVertexAttribArray(2);
            glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                                  reinterpret_cast<const void*>(offsetof(Vertex, TexCoord)));

            glGenBuffers(1, &buffers.InstanceBuffer);
            glBindBuffer(GL_ARRAY_BUFFER, buffers.InstanceBuffer);
            glBufferData(GL_ARRAY_BUFFER, sizeof(instanceOffsets), instanceOffsets, GL_STATIC_DRAW);
            glEnableVertexAttribArray(3);
            glVertexAttribPointer(3, 4, GL_FLOAT, GL_FALSE, sizeof(instanceOffsets[0]), nullptr);
            glVertexAttribDivisor(3, 1);

            glBindBuffer(GL_ARRAY_BUFFER, 0);
            glBindVertexArray(0);

            return buffers;
        }

        void DestroyInstancedQuadBuffers(InstancedQuadBuffers* buffers) {
            if (buffers == nullptr) {
                return;
            }

            if (buffers->InstanceBuffer != 0) {
                glDeleteBuffers(1, &buffers->InstanceBuffer);
                buffers->InstanceBuffer = 0;
            }
            if (buffers->VertexBuffer != 0) {
                glDeleteBuffers(1, &buffers->VertexBuffer);
                buffers->VertexBuffer = 0;
            }
            if (buffers->Vao != 0) {
                glDeleteVertexArrays(1, &buffers->Vao);
                buffers->Vao = 0;
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
            RegionedMesh = CreateRegionedMeshBuffers(kRegionCount, kFacesPerRegion);
            RegionedIndirectMesh = CreateRegionedIndirectMeshBuffers(kIndirectRegionCount,
                                                                     kChunksPerIndirectRegion,
                                                                     kFacesPerIndirectChunk);
        }

        static void TearDownTestSuite() {
            DestroyRegionedIndirectMeshBuffers(&RegionedIndirectMesh);
            DestroyRegionedMeshBuffers(&RegionedMesh);
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
        static RegionedMeshBuffers RegionedMesh;
        static RegionedIndirectMeshBuffers RegionedIndirectMesh;
    };

    OffscreenHarness MobileGLPathPerfFixture::HarnessInstance;
    std::string MobileGLPathPerfFixture::Error;
    GLuint MobileGLPathPerfFixture::Texture = 0;
    ProgramBundle MobileGLPathPerfFixture::Program;
    MeshBuffers MobileGLPathPerfFixture::Mesh;
    RegionedMeshBuffers MobileGLPathPerfFixture::RegionedMesh;
    RegionedIndirectMeshBuffers MobileGLPathPerfFixture::RegionedIndirectMesh;

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
        EXPECT_TRUE(PixelDiffersFrom(ReadPixelAtNdc(HarnessInstance, -0.925f, -0.925f),
                                     MakeExpectedPixel(0.06f, 0.10f, 0.16f, 1.0f)));
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
        EXPECT_TRUE(PixelDiffersFrom(ReadPixelAtNdc(HarnessInstance, -0.925f, -0.925f),
                                     MakeExpectedPixel(0.06f, 0.10f, 0.16f, 1.0f)));
        const auto result = MeasureScenario(HarnessInstance, "AngelicaWorldMultiDrawIndirect", kDrawCount, frameFn);
        EXPECT_GT(result.FrameNanoseconds, 0.0);
    }

    TEST_F(MobileGLPathPerfFixture, AngelicaWorldRegionedOneshotMultiDrawArraysSmokeAndPerf) {
        const auto frameFn = [&]() {
            ASSERT_TRUE(HarnessInstance.Clear(0.06f, 0.10f, 0.16f, 1.0f, &Error)) << Error;
            glUseProgram(Program.Program);
            if (Program.SamplerLocation >= 0) {
                glUniform1i(Program.SamplerLocation, 0);
            }
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, Texture);

            for (const auto& region : RegionedMesh.Regions) {
                glBindVertexArray(region.Vao);
                glVertexAttrib4f(3,
                                 region.ModelOffset[0],
                                 region.ModelOffset[1],
                                 region.ModelOffset[2],
                                 region.ModelOffset[3]);
                glMultiDrawArrays(GL_QUADS,
                                  region.Firsts.data(),
                                  region.Counts.data(),
                                  static_cast<GLsizei>(region.Counts.size()));
                glBindVertexArray(0);
            }

            glUseProgram(0);
        };

        frameFn();
        HarnessInstance.Finish();
        EXPECT_TRUE(PixelDiffersFrom(ReadPixelAtNdc(HarnessInstance, -0.906f, -0.906f),
                                     MakeExpectedPixel(0.06f, 0.10f, 0.16f, 1.0f)));
        const auto result = MeasureScenario(HarnessInstance,
                                            "AngelicaWorldRegionedOneshotMultiDrawArrays",
                                            kRegionCount * kFacesPerRegion,
                                            frameFn);
        EXPECT_GT(result.FrameNanoseconds, 0.0);
    }

    TEST_F(MobileGLPathPerfFixture, AngelicaWorldRegionedMultiDrawIndirectSmokeAndPerf) {
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

            glBindBuffer(GL_ARRAY_BUFFER, RegionedIndirectMesh.UniformBuffer);
            glBufferData(GL_ARRAY_BUFFER,
                         static_cast<GLsizeiptr>(RegionedIndirectMesh.ModelOffsets.size() * sizeof(RegionedIndirectMesh.ModelOffsets[0])),
                         RegionedIndirectMesh.ModelOffsets.data(),
                         GL_STREAM_DRAW);
            glBindBuffer(GL_ARRAY_BUFFER, 0);

            glBindBuffer(GL_DRAW_INDIRECT_BUFFER, RegionedIndirectMesh.IndirectBuffer);
            glBufferData(GL_DRAW_INDIRECT_BUFFER,
                         static_cast<GLsizeiptr>(RegionedIndirectMesh.Commands.size() * sizeof(DrawArraysIndirectCommand)),
                         RegionedIndirectMesh.Commands.data(),
                         GL_STREAM_DRAW);

            for (const auto& region : RegionedIndirectMesh.Regions) {
                glBindVertexArray(region.Vao);
                glMultiDrawArraysIndirect(GL_QUADS,
                                          reinterpret_cast<const void*>(region.CommandByteOffset),
                                          region.DrawCount,
                                          0);
                glBindVertexArray(0);
            }

            glBindBuffer(GL_DRAW_INDIRECT_BUFFER, 0);
            glUseProgram(0);
        };

        frameFn();
        HarnessInstance.Finish();
        EXPECT_TRUE(AnyPixelDiffersFrom(
            HarnessInstance,
            {
                {-0.937f, -0.937f},
                {-0.797f, -0.937f},
                {-0.657f, -0.937f},
                {-0.937f, -0.797f},
            },
            MakeExpectedPixel(0.06f, 0.10f, 0.16f, 1.0f)));
        const auto result = MeasureScenario(HarnessInstance,
                                            "AngelicaWorldRegionedMultiDrawIndirect",
                                            kIndirectRegionCount * kChunksPerIndirectRegion * kFacesPerIndirectChunk,
                                            frameFn);
        EXPECT_GT(result.FrameNanoseconds, 0.0);
    }

    TEST_F(MobileGLPathPerfFixture, DrawArraysInstancedBaseInstanceUsesInstancedAttributeOffset) {
        auto quad = CreateInstancedQuadBuffers();

        ASSERT_TRUE(HarnessInstance.Clear(0.06f, 0.10f, 0.16f, 1.0f, &Error)) << Error;
        glUseProgram(Program.Program);
        if (Program.SamplerLocation >= 0) {
            glUniform1i(Program.SamplerLocation, 0);
        }
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, Texture);
        glBindVertexArray(quad.Vao);

        glDrawArraysInstancedBaseInstance(GL_TRIANGLE_STRIP, 0, 4, 1, 0);
        glDrawArraysInstancedBaseInstance(GL_TRIANGLE_STRIP, 0, 4, 1, 1);

        glBindVertexArray(0);
        glUseProgram(0);
        HarnessInstance.Finish();

        const auto clearPixel = MakeExpectedPixel(0.06f, 0.10f, 0.16f, 1.0f);
        EXPECT_TRUE(PixelDiffersFrom(ReadPixelAtNdc(HarnessInstance, -0.45f, 0.0f),
                                     clearPixel));
        EXPECT_TRUE(PixelDiffersFrom(ReadPixelAtNdc(HarnessInstance, 0.45f, 0.0f),
                                     clearPixel));

        DestroyInstancedQuadBuffers(&quad);
    }

    TEST_F(MobileGLPathPerfFixture, MultiDrawArraysIndirectUsesInstancedAttributeOffset) {
        if (!SupportsMultiDrawIndirect()) {
            GTEST_SKIP() << "glMultiDrawArraysIndirect is not available on this MobileGL path";
        }

        auto quad = CreateInstancedQuadBuffers();
        const DrawArraysIndirectCommand commands[2] = {
            {.Count = 4u, .InstanceCount = 1u, .First = 0u, .BaseInstance = 0u},
            {.Count = 4u, .InstanceCount = 1u, .First = 0u, .BaseInstance = 1u},
        };
        GLuint indirectBuffer = 0;
        glGenBuffers(1, &indirectBuffer);
        glBindBuffer(GL_DRAW_INDIRECT_BUFFER, indirectBuffer);
        glBufferData(GL_DRAW_INDIRECT_BUFFER, sizeof(commands), commands, GL_STREAM_DRAW);

        ASSERT_TRUE(HarnessInstance.Clear(0.06f, 0.10f, 0.16f, 1.0f, &Error)) << Error;
        glUseProgram(Program.Program);
        if (Program.SamplerLocation >= 0) {
            glUniform1i(Program.SamplerLocation, 0);
        }
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, Texture);
        glBindVertexArray(quad.Vao);

        glMultiDrawArraysIndirect(GL_TRIANGLE_STRIP, reinterpret_cast<const void*>(0), 2, 0);

        glBindVertexArray(0);
        glBindBuffer(GL_DRAW_INDIRECT_BUFFER, 0);
        glUseProgram(0);
        HarnessInstance.Finish();

        const auto clearPixel = MakeExpectedPixel(0.06f, 0.10f, 0.16f, 1.0f);
        EXPECT_TRUE(PixelDiffersFrom(ReadPixelAtNdc(HarnessInstance, -0.45f, 0.0f),
                                     clearPixel));
        EXPECT_TRUE(PixelDiffersFrom(ReadPixelAtNdc(HarnessInstance, 0.45f, 0.0f),
                                     clearPixel));

        if (indirectBuffer != 0) {
            glDeleteBuffers(1, &indirectBuffer);
        }
        DestroyInstancedQuadBuffers(&quad);
    }
} // namespace MobileGL::MG_Test::RenderPath::MobileGLPath
