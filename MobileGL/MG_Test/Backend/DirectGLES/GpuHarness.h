// MobileGL - MobileGL/MG_Test/Backend/DirectGLES/GpuHarness.h
// Copyright (c) 2025-2026 MobileGL-Dev
// Licensed under the GNU Lesser General Public License v3.0:
//   https://www.gnu.org/licenses/gpl-3.0.txt
//   https://www.gnu.org/licenses/lgpl-3.0.txt
// SPDX-License-Identifier: LGPL-3.0-only
// End of Source File Header

#pragma once

#include <EGL/egl.h>
#include <GLES3/gl3.h>

#include <array>
#include <chrono>
#include <functional>
#include <string>

namespace MobileGL::MG_Test::DirectGLES {
    class OffscreenGpuHarness {
    public:
        using Pixel = std::array<GLubyte, 4>;

        OffscreenGpuHarness() = default;
        ~OffscreenGpuHarness();

        bool Initialize(GLint width, GLint height, std::string* outError);
        void Reset();

        GLuint CreateProgram(const char* vertexSource, const char* fragmentSource, std::string* outError) const;
        void DestroyProgram(GLuint program) const;

        bool Clear(float red, float green, float blue, float alpha, std::string* outError) const;
        bool DrawFullscreenTriangle(GLuint program, std::string* outError) const;
        Pixel ReadPixel(GLint x, GLint y) const;
        void Finish() const;

        template <typename Fn>
        double MeasureGpuNanoseconds(Fn&& fn, int warmupIterations, int timedIterations) const {
            using Clock = std::chrono::steady_clock;

            for (int i = 0; i < warmupIterations; ++i) {
                fn();
                Finish();
            }

            const auto begin = Clock::now();
            for (int i = 0; i < timedIterations; ++i) {
                fn();
                Finish();
            }
            const auto end = Clock::now();
            return std::chrono::duration<double, std::nano>(end - begin).count() /
                   static_cast<double>(timedIterations);
        }

        GLint GetWidth() const { return m_width; }
        GLint GetHeight() const { return m_height; }
        bool IsReady() const { return m_initialized; }

    private:
        GLuint CompileShader(GLenum type, const char* source, std::string* outError) const;
        bool CheckNoError(std::string* outError, const char* stage) const;

        EGLDisplay m_display = EGL_NO_DISPLAY;
        EGLConfig m_config = nullptr;
        EGLSurface m_surface = EGL_NO_SURFACE;
        EGLContext m_context = EGL_NO_CONTEXT;
        GLuint m_dummyVao = 0;
        GLint m_width = 0;
        GLint m_height = 0;
        bool m_initialized = false;
    };
} // namespace MobileGL::MG_Test::DirectGLES
