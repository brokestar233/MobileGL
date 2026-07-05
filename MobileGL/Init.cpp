// MobileGL - MobileGL/Init.cpp
// Copyright (c) 2025-2026 MobileGL-Dev
// Licensed under the GNU Lesser General Public License v3.0:
//   https://www.gnu.org/licenses/gpl-3.0.txt
//   https://www.gnu.org/licenses/lgpl-3.0.txt
// SPDX-License-Identifier: LGPL-3.0-only
// End of Source File Header

#include "Init.h"
#include "Config.h"
#include <MG_Backend/BackendObjects.h>
#include <MG_State/GLState/Core.h>
#include <MG_State/EGLState/Core.h>
#include <MG_Impl/GLImpl/Texture/ProxyTexture.h>
#include <MG_Impl/GLImpl/Framebuffer/GL_Framebuffer.h>
#include <cstdlib>
#if defined(__ANDROID__)
#include <fcntl.h>
#endif

namespace MobileGL {
    namespace {
        Bool g_isInitialized = false;

#if defined(__ANDROID__)
        constexpr const char* kAndroidFileLogPath = "/storage/emulated/0/FCL/mobilegl-latest.log";
        constexpr const char* kAndroidNativeLogPath = "/storage/emulated/0/FCL/mobilegl-native.log";

        void ConfigureAndroidLogSinks() {
            static Bool configured = false;
            if (configured) {
                return;
            }
            configured = true;

            const char* fileLogPath = std::getenv("MOBILEGL_LOG_FILE_PATH");
            const char* nativeLogPath = std::getenv("MOBILEGL_NATIVE_LOG_FILE_PATH");
            const Bool envOverridesLogging =
                (fileLogPath && *fileLogPath) || (nativeLogPath && *nativeLogPath);
            if (!envOverridesLogging && !MG_ConfigLoader::IsAndroidDebugLogEnabled()) {
                return;
            }

            if (!fileLogPath || !*fileLogPath) {
                setenv("MOBILEGL_LOG_FILE_PATH", kAndroidFileLogPath, 1);
            }

            if (!nativeLogPath || !*nativeLogPath) {
                nativeLogPath = kAndroidNativeLogPath;
            }

            const int fd = ::open(nativeLogPath, O_CREAT | O_WRONLY | O_TRUNC | O_CLOEXEC, 0644);
            if (fd < 0) {
                return;
            }

            ::dup2(fd, STDERR_FILENO);
            ::dup2(fd, STDOUT_FILENO);
            if (fd != STDERR_FILENO && fd != STDOUT_FILENO) {
                ::close(fd);
            }
        }
#endif

        void DestroyImpl(Bool logLifecycle) {
            if (!g_isInitialized) {
                return;
            }

            if (logLifecycle) {
                MGLOG_I("MobileGL closing...");
            }
            glslang::FinalizeProcess();
            MG_Backend::pActiveBackendObject.reset();
            MG_State::pGLContext.reset();
            MG_State::pEGLContext.reset();
            MG_Impl::GLImpl::TextureImpl::pProxyTextureManager.reset();
            MG_Impl::GLImpl::FramebufferImpl::pDefaultFramebufferInfo.reset();
            MG_Backend::gBackendFunctionsTable = {};
            g_isInitialized = false;
            if (logLifecycle) {
                MG_Util::Debug::Close();
            }

            // TODO: add and use Destroy functions for other subsystems
        }
    }

    void Initialize() {
        if (g_isInitialized) {
            MGLOG_D("MobileGL already initialized; skipping duplicate Initialize()");
            return;
        }

#if defined(__ANDROID__)
        ConfigureAndroidLogSinks();
#endif
        MG_Util::Debug::InitFile();
        MGLOG_I("Initializing MobileGL...");
        MG_ConfigLoader::Init();
        MGLOG_I("Config loaded");
        MG_State::Init();
        MGLOG_D("MG_State initialized");
        MG_Backend::Init();
        MGLOG_D("MG_Backend initialized");
        MG_Impl::Init();
        MGLOG_D("MG_Impl initialized");
        glslang::InitializeProcess();
        MGLOG_D("glslang initialized");
        g_isInitialized = true;
        MGLOG_I("MobileGL initialized");
    }

    void Destroy() {
        DestroyImpl(true);
    }

#if defined(__linux__) || defined(__APPLE__)
    __attribute__((constructor)) static void AutoInit() {
        Initialize();
    }

    __attribute__((destructor)) static void AutoDestroy() {
        if (std::getenv("MOBILEGL_TRACE_SKIP_AUTODESTROY") != nullptr) {
            return;
        }
#if defined(__APPLE__)
        // macOS injected dylibs can run destructors after logging/backend static state is already torn down.
        return;
#else
        DestroyImpl(false);
#endif
    }
#endif

#ifdef _WIN32
    BOOL WINAPI DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved) {
        switch (ul_reason_for_call) {
        case DLL_PROCESS_ATTACH:
            Initialize();
            break;

        case DLL_PROCESS_DETACH:
            Destroy();
            break;
        }
        return TRUE;
    }
#endif
} // namespace MobileGL
