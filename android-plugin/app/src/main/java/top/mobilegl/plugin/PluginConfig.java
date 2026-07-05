package top.mobilegl.plugin;

import android.content.Context;
import android.net.Uri;
import android.provider.DocumentsContract;

import java.io.IOException;
import java.util.LinkedHashMap;
import java.util.Map;

final class PluginConfig {
    static final String BACKEND_GLES = "DirectGLES";
    static final String BACKEND_VULKAN = "DirectVulkan";
    static final String GL_MODE_DIRECT = "direct";
    static final String GL_MODE_COMPAT = "compat";

    String backendType = BACKEND_VULKAN;
    String glMode = GL_MODE_COMPAT;
    boolean debugLogEnabled = false;

    static PluginConfig load(Context context, Uri fclDirUri) {
        PluginConfig config = new PluginConfig();
        if (fclDirUri == null) {
            return config;
        }

        try {
            PluginFileUtils.ChildDocumentInfo documentInfo =
                    PluginFileUtils.findBestConfigDocument(context.getContentResolver(), fclDirUri, PluginConstants.CONFIG_FILE_NAME);
            if (documentInfo == null) {
                return config;
            }
            String raw = PluginFileUtils.readText(context, documentInfo.uri);
            config.apply(raw);
            if (!documentInfo.isExactName(PluginConstants.CONFIG_FILE_NAME)) {
                try {
                    config.save(context, fclDirUri);
                } catch (IOException ignored) {
                    // Keep running with loaded legacy config even if migration fails.
                }
            }
            return config;
        } catch (IOException | RuntimeException ignored) {
            return config;
        }
    }

    void save(Context context, Uri fclDirUri) throws IOException {
        if (fclDirUri == null) {
            throw new IOException("FCL folder permission is not granted");
        }
        PluginFileUtils.writeText(
                context,
                fclDirUri,
                PluginConstants.CONFIG_FILE_NAME,
                serialize(),
                "application/octet-stream"
        );
    }

    private void apply(String raw) {
        Map<String, String> values = parse(raw);
        backendType = normalizeBackend(values.get("backend"));
        glMode = normalizeGlMode(values.get("gl_mode"));
        debugLogEnabled = normalizeBool(values.get("debug_log"));
    }

    private String serialize() {
        StringBuilder builder = new StringBuilder();
        builder.append("backend=").append(normalizeBackend(backendType)).append('\n');
        builder.append("gl_mode=").append(normalizeGlMode(glMode)).append('\n');
        builder.append("debug_log=").append(debugLogEnabled ? "1" : "0").append('\n');
        return builder.toString();
    }

    private static Map<String, String> parse(String raw) {
        Map<String, String> values = new LinkedHashMap<>();
        String[] lines = raw.split("\\r?\\n");
        for (String line : lines) {
            String trimmed = line.trim();
            if (trimmed.isEmpty() || trimmed.startsWith("#")) {
                continue;
            }
            int idx = trimmed.indexOf('=');
            if (idx <= 0) {
                continue;
            }
            String key = trimmed.substring(0, idx).trim();
            String value = trimmed.substring(idx + 1).trim();
            values.put(key, value);
        }
        return values;
    }

    private static String normalizeBackend(String value) {
        if (BACKEND_GLES.equals(value)) {
            return BACKEND_GLES;
        }
        return BACKEND_VULKAN;
    }

    private static String normalizeGlMode(String value) {
        if (GL_MODE_DIRECT.equalsIgnoreCase(value)) {
            return GL_MODE_DIRECT;
        }
        return GL_MODE_COMPAT;
    }

    private static boolean normalizeBool(String value) {
        if (value == null) {
            return false;
        }
        String normalized = value.trim().toLowerCase();
        return "1".equals(normalized)
                || "true".equals(normalized)
                || "yes".equals(normalized)
                || "on".equals(normalized);
    }
}
