package top.mobilegl.plugin;

import android.os.Environment;

import java.io.File;

final class PluginConstants {
    static final String FCL_DIRECTORY = Environment.getExternalStorageDirectory().getAbsolutePath() + "/FCL";
    static final String CONFIG_FILE_NAME = "mobilegl-plugin.cfg";
    static final String CONFIG_FILE_PATH = FCL_DIRECTORY + "/" + CONFIG_FILE_NAME;
    static final String MOBILEGL_LOG_FILE_PATH = FCL_DIRECTORY + "/mobilegl-latest.log";
    static final String MOBILEGL_NATIVE_LOG_FILE_PATH = FCL_DIRECTORY + "/mobilegl-native.log";

    private PluginConstants() {}

    static File getFclDirectoryFile() {
        return new File(FCL_DIRECTORY);
    }
}
