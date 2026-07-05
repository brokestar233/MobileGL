// MobileGL - MobileGL/MG_Test/Backend/DirectGLES/GpuHarness.cpp
// Copyright (c) 2025-2026 MobileGL-Dev
// Licensed under the GNU Lesser General Public License v3.0:
//   https://www.gnu.org/licenses/gpl-3.0.txt
//   https://www.gnu.org/licenses/lgpl-3.0.txt
// SPDX-License-Identifier: LGPL-3.0-only
// End of Source File Header

#include "GpuHarness.h"

#include <sstream>
#include <vector>

namespace MobileGL::MG_Test::DirectGLES {
    namespace {
        std::string BuildShaderInfoLog(GLuint object, bool isProgram) {
            GLint logLength = 0;
            if (isProgram) {
                glGetProgramiv(object, GL_INFO_LOG_LENGTH, &logLength);
            } else {
                glGetShaderiv(object, GL_INFO_LOG_LENGTH, &logLength);
            }

            if (logLength <= 1) {
                return {};
            }

            std::vector<GLchar> log(static_cast<size_t>(logLength));
            if (isProgram) {
                glGetProgramInfoLog(object, logLength, nullptr, log.data());
            } else {
                glGetShaderInfoLog(object, logLength, nullptr, log.data());
            }
            return std::string(log.data());
        }
    } // namespace

    OffscreenGpuHarness::~OffscreenGpuHarness() {
        Reset();
    }

    bool OffscreenGpuHarness::Initialize(GLint width, GLint height, std::string* outError) {
        Reset();

        m_display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
        if (m_display == EGL_NO_DISPLAY) {
            if (outError) *outError = "eglGetDisplay(EGL_DEFAULT_DISPLAY) failed";
            return false;
        }
        if (!eglInitialize(m_display, nullptr, nullptr)) {
            if (outError) *outError = "eglInitialize failed";
            Reset();
            return false;
        }
        if (!eglBindAPI(EGL_OPENGL_ES_API)) {
            if (outError) *outError = "eglBindAPI(EGL_OPENGL_ES_API) failed";
            Reset();
            return false;
        }

        const EGLint configAttribs[] = {
            EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
            EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT,
            EGL_RED_SIZE, 8,
            EGL_GREEN_SIZE, 8,
            EGL_BLUE_SIZE, 8,
            EGL_ALPHA_SIZE, 8,
            EGL_NONE,
        };
        EGLint configCount = 0;
        if (!eglChooseConfig(m_display, configAttribs, &m_config, 1, &configCount) || configCount < 1 ||
            m_config == nullptr) {
            if (outError) *outError = "eglChooseConfig for GLES3 pbuffer failed";
            Reset();
            return false;
        }

        const EGLint pbufferAttribs[] = {EGL_WIDTH, width, EGL_HEIGHT, height, EGL_NONE};
        m_surface = eglCreatePbufferSurface(m_display, m_config, pbufferAttribs);
        if (m_surface == EGL_NO_SURFACE) {
            if (outError) *outError = "eglCreatePbufferSurface failed";
            Reset();
            return false;
        }

        const EGLint contextAttribs[] = {EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE};
        m_context = eglCreateContext(m_display, m_config, EGL_NO_CONTEXT, contextAttribs);
        if (m_context == EGL_NO_CONTEXT) {
            if (outError) *outError = "eglCreateContext(GLES3) failed";
            Reset();
            return false;
        }

        if (!eglMakeCurrent(m_display, m_surface, m_surface, m_context)) {
            if (outError) *outError = "eglMakeCurrent failed";
            Reset();
            return false;
        }

        glGenVertexArrays(1, &m_dummyVao);
        glBindVertexArray(m_dummyVao);
        glDisable(GL_DITHER);
        glViewport(0, 0, width, height);

        m_width = width;
        m_height = height;
        m_initialized = CheckNoError(outError, "Initialize");
        if (!m_initialized) {
            Reset();
        }
        return m_initialized;
    }

    void OffscreenGpuHarness::Reset() {
        if (m_display != EGL_NO_DISPLAY && m_context != EGL_NO_CONTEXT) {
            eglMakeCurrent(m_display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        }
        if (m_dummyVao != 0) {
            glDeleteVertexArrays(1, &m_dummyVao);
            m_dummyVao = 0;
        }
        if (m_context != EGL_NO_CONTEXT) {
            eglDestroyContext(m_display, m_context);
            m_context = EGL_NO_CONTEXT;
        }
        if (m_surface != EGL_NO_SURFACE) {
            eglDestroySurface(m_display, m_surface);
            m_surface = EGL_NO_SURFACE;
        }
        if (m_display != EGL_NO_DISPLAY) {
            eglTerminate(m_display);
            m_display = EGL_NO_DISPLAY;
        }
        m_config = nullptr;
        m_width = 0;
        m_height = 0;
        m_initialized = false;
    }

    GLuint OffscreenGpuHarness::CompileShader(GLenum type, const char* source, std::string* outError) const {
        const GLuint shader = glCreateShader(type);
        glShaderSource(shader, 1, &source, nullptr);
        glCompileShader(shader);

        GLint compileStatus = GL_FALSE;
        glGetShaderiv(shader, GL_COMPILE_STATUS, &compileStatus);
        if (compileStatus == GL_TRUE) {
            return shader;
        }

        if (outError) {
            std::ostringstream oss;
            oss << "Shader compile failed: " << BuildShaderInfoLog(shader, false);
            *outError = oss.str();
        }
        glDeleteShader(shader);
        return 0;
    }

    GLuint OffscreenGpuHarness::CreateProgram(const char* vertexSource, const char* fragmentSource,
                                              std::string* outError) const {
        const GLuint vertexShader = CompileShader(GL_VERTEX_SHADER, vertexSource, outError);
        if (vertexShader == 0) return 0;

        const GLuint fragmentShader = CompileShader(GL_FRAGMENT_SHADER, fragmentSource, outError);
        if (fragmentShader == 0) {
            glDeleteShader(vertexShader);
            return 0;
        }

        const GLuint program = glCreateProgram();
        glAttachShader(program, vertexShader);
        glAttachShader(program, fragmentShader);
        glLinkProgram(program);
        glDeleteShader(vertexShader);
        glDeleteShader(fragmentShader);

        GLint linkStatus = GL_FALSE;
        glGetProgramiv(program, GL_LINK_STATUS, &linkStatus);
        if (linkStatus == GL_TRUE) {
            return program;
        }

        if (outError) {
            std::ostringstream oss;
            oss << "Program link failed: " << BuildShaderInfoLog(program, true);
            *outError = oss.str();
        }
        glDeleteProgram(program);
        return 0;
    }

    void OffscreenGpuHarness::DestroyProgram(GLuint program) const {
        if (program != 0) {
            glDeleteProgram(program);
        }
    }

    bool OffscreenGpuHarness::Clear(float red, float green, float blue, float alpha, std::string* outError) const {
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glViewport(0, 0, m_width, m_height);
        glClearColor(red, green, blue, alpha);
        glClear(GL_COLOR_BUFFER_BIT);
        return CheckNoError(outError, "Clear");
    }

    bool OffscreenGpuHarness::DrawFullscreenTriangle(GLuint program, std::string* outError) const {
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glViewport(0, 0, m_width, m_height);
        glUseProgram(program);
        glBindVertexArray(m_dummyVao);
        glDrawArrays(GL_TRIANGLES, 0, 3);
        return CheckNoError(outError, "DrawFullscreenTriangle");
    }

    OffscreenGpuHarness::Pixel OffscreenGpuHarness::ReadPixel(GLint x, GLint y) const {
        Pixel pixel = {0, 0, 0, 0};
        glReadPixels(x, y, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel.data());
        return pixel;
    }

    void OffscreenGpuHarness::Finish() const {
        glFinish();
    }

    bool OffscreenGpuHarness::CheckNoError(std::string* outError, const char* stage) const {
        const GLenum err = glGetError();
        if (err == GL_NO_ERROR) {
            return true;
        }
        if (outError) {
            std::ostringstream oss;
            oss << stage << " failed with GL error 0x" << std::hex << err;
            *outError = oss.str();
        }
        return false;
    }
} // namespace MobileGL::MG_Test::DirectGLES
