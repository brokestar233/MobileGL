// MobileGL - MobileGL/MG_Test/Compat/SFPEW/SfpewHarness.h
// Copyright (c) 2025-2026 MobileGL-Dev
// Licensed under the GNU Lesser General Public License v3.0:
//   https://www.gnu.org/licenses/gpl-3.0.txt
//   https://www.gnu.org/licenses/lgpl-3.0.txt
// SPDX-License-Identifier: LGPL-3.0-only
// End of Source File Header

#pragma once

#include <EGL/egl.h>
#ifndef GL_GLEXT_PROTOTYPES
#define GL_GLEXT_PROTOTYPES
#endif
#include <GL/gl.h>
#include <GL/glext.h>

#include <array>
#include <chrono>
#include <string>

namespace MobileGL::MG_Test::Compat::SFPEW {
    class OffscreenFixedPipelineHarness {
    public:
        using Pixel = std::array<GLubyte, 4>;

        OffscreenFixedPipelineHarness() = default;
        ~OffscreenFixedPipelineHarness();

        bool Initialize(GLint width, GLint height, std::string* outError);
        void Reset();

        bool Clear(float red, float green, float blue, float alpha, std::string* outError) const;
        Pixel ReadPixel(GLint x, GLint y) const;
        void Finish() const;

        template <typename Fn>
        double MeasureFrameNanoseconds(Fn&& fn, int warmupFrames, int timedFrames) const {
            using Clock = std::chrono::steady_clock;

            for (int i = 0; i < warmupFrames; ++i) {
                fn();
                Finish();
            }

            const auto begin = Clock::now();
            for (int i = 0; i < timedFrames; ++i) {
                fn();
                Finish();
            }
            const auto end = Clock::now();
            return std::chrono::duration<double, std::nano>(end - begin).count() /
                   static_cast<double>(timedFrames);
        }

        GLint GetWidth() const { return m_width; }
        GLint GetHeight() const { return m_height; }
        bool IsReady() const { return m_initialized; }

    private:
        bool CheckNoError(std::string* outError, const char* stage) const;

        EGLDisplay m_display = EGL_NO_DISPLAY;
        EGLConfig m_config = nullptr;
        EGLSurface m_surface = EGL_NO_SURFACE;
        EGLContext m_context = EGL_NO_CONTEXT;
        GLint m_width = 0;
        GLint m_height = 0;
        bool m_initialized = false;
    };
} // namespace MobileGL::MG_Test::Compat::SFPEW
