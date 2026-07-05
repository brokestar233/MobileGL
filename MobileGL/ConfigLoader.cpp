// MobileGL - MobileGL/ConfigLoader.cpp
// Copyright (c) 2025-2026 MobileGL-Dev
// Licensed under the GNU Lesser General Public License v3.0:
//   https://www.gnu.org/licenses/gpl-3.0.txt
//   https://www.gnu.org/licenses/lgpl-3.0.txt
// SPDX-License-Identifier: LGPL-3.0-only
// End of Source File Header

#include "Config.h"
#include <fstream>

#ifndef _WIN32
extern char** environ;
#endif

namespace MobileGL::MG_ConfigLoader {
    static UniquePtr<UnorderedMap<String, String>> acceptedEnvVariablesMap;
    namespace {
        constexpr const char* kPluginConfigPath = "/storage/emulated/0/FCL/mobilegl-plugin.cfg";

        String Trim(String value) {
            const auto begin = value.find_first_not_of(" \t\r\n");
            if (begin == String::npos) {
                return {};
            }
            const auto end = value.find_last_not_of(" \t\r\n");
            return value.substr(begin, end - begin + 1);
        }

        String QueryPluginConfigValue(const String& desiredKey) {
            std::ifstream configStream(kPluginConfigPath);
            if (!configStream.is_open()) {
                return {};
            }

            String line;
            while (std::getline(configStream, line)) {
                const auto commentPos = line.find('#');
                if (commentPos != String::npos) {
                    line.erase(commentPos);
                }

                const auto eqPos = line.find('=');
                if (eqPos == String::npos) {
                    continue;
                }

                const auto key = Trim(line.substr(0, eqPos));
                if (key != desiredKey) {
                    continue;
                }
                return Trim(line.substr(eqPos + 1));
            }
            return {};
        }

        Bool ParseBoolValue(const String& rawValue, Bool defaultValue = false) {
            if (rawValue.empty()) {
                return defaultValue;
            }

            String lowered = rawValue;
            std::transform(lowered.begin(), lowered.end(), lowered.begin(), [](unsigned char ch) {
                return static_cast<char>(std::tolower(ch));
            });

            if (lowered == "1" || lowered == "true" || lowered == "yes" || lowered == "on") {
                return true;
            }
            if (lowered == "0" || lowered == "false" || lowered == "no" || lowered == "off") {
                return false;
            }
            return defaultValue;
        }
    }

    static Bool IsAcceptedPrefix(const String& key) {
        return (key.compare(0, 6, "LIBGL_") == 0 || key.compare(0, 9, "MOBILEGL_") == 0);
    }

    inline void InitializeAcceptedEnvVariables() {
        if (!acceptedEnvVariablesMap) {
            acceptedEnvVariablesMap = MakeUnique<UnorderedMap<String, String>>();
        } else {
            acceptedEnvVariablesMap->clear();
        }

        char** envPtr = nullptr;

#ifdef _WIN32
        envPtr = _environ;
#else // POSIX
        envPtr = ::environ;
#endif

        if (envPtr == nullptr) return;

        for (char** env = envPtr; *env != nullptr; ++env) {
            String entry(*env);
            SizeT pos = entry.find('=');
            if (pos != String::npos) {
                String key = entry.substr(0, pos);
                String value = entry.substr(pos + 1);

                if (IsAcceptedPrefix(key)) {
                    (*acceptedEnvVariablesMap)[key] = value;
                    MGLOG_D("Config: Accepted env variable: %s=%s", key.c_str(), value.c_str());
                }
            }
        }
    }

    inline void QueryEnvVariable(const String& key, String& outValue, const String& defaultValue) {
        auto it = acceptedEnvVariablesMap->find(key);
        if (it != acceptedEnvVariablesMap->end()) {
            outValue = it->second;
        } else {
            outValue = defaultValue;
        }
    }

    inline void InitBackendType() {
        String backendTypeStr;
        QueryEnvVariable("MOBILEGL_BACKEND_TYPE", backendTypeStr, "");
#if defined(ANDROID)
        if (backendTypeStr.empty()) {
            backendTypeStr = QueryPluginConfigValue("backend");
        }
#endif
        if (backendTypeStr.empty()) {
            backendTypeStr = "DirectGLES";
        }
#define ENTRY(backendType)                                                                                             \
    if (backendTypeStr == #backendType) {                                                                              \
        MG_Config::ActiveBackendType = BackendType::backendType;                                                       \
        MGLOG_I("Config: Active backend type set to " #backendType);                                                   \
        return;                                                                                                        \
    }
        ENTRY(DirectGLES)
        ENTRY(DirectVulkan)
        ENTRY(Unknown)
        MG_Config::ActiveBackendType = BackendType::Unknown;
#undef ENTRY
    }

    void Init() {
        MGLOG_D("Loading configuration from environment variables...");
        InitializeAcceptedEnvVariables();

        InitBackendType();

        // Destroy the map since we won't need it anymore
        acceptedEnvVariablesMap.reset();
    }

    Bool IsAndroidDebugLogEnabled() {
#if defined(ANDROID)
        return ParseBoolValue(QueryPluginConfigValue("debug_log"), false);
#else
        return false;
#endif
    }
} // namespace MobileGL::MG_ConfigLoader
