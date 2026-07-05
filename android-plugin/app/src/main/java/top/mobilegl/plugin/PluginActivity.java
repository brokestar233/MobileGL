package top.mobilegl.plugin;

import android.Manifest;
import android.app.Activity;
import android.content.Intent;
import android.content.pm.PackageManager;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.os.Environment;
import android.provider.DocumentsContract;
import android.view.View;
import android.widget.AdapterView;
import android.widget.ArrayAdapter;
import android.widget.Button;
import android.widget.Spinner;
import android.widget.TextView;
import android.widget.Toast;

import androidx.appcompat.widget.SwitchCompat;

import com.google.android.material.dialog.MaterialAlertDialogBuilder;

import java.io.File;
import java.io.IOException;

public final class PluginActivity extends Activity {
    private static final int REQUEST_CODE_SAF = 2000;
    private static final int REQUEST_CODE_STORAGE = 2001;

    private FolderPermissionManager folderPermissionManager;
    private Uri fclDirectoryUri;
    private PluginConfig config;

    private TextView pathStatusView;
    private TextView configPathView;
    private TextView debugLogHintView;
    private Button accessButton;
    private Spinner backendSpinner;
    private Spinner glModeSpinner;
    private SwitchCompat debugLogSwitch;
    private View optionsContainer;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(R.layout.activity_plugin);

        folderPermissionManager = new FolderPermissionManager(getContentResolver());

        pathStatusView = findViewById(R.id.path_status);
        configPathView = findViewById(R.id.config_path);
        debugLogHintView = findViewById(R.id.debug_log_hint);
        accessButton = findViewById(R.id.access_button);
        backendSpinner = findViewById(R.id.backend_spinner);
        glModeSpinner = findViewById(R.id.gl_mode_spinner);
        debugLogSwitch = findViewById(R.id.debug_log_switch);
        optionsContainer = findViewById(R.id.options_container);
        findViewById(R.id.info).setOnClickListener(v -> showInfoDialog());

        ArrayAdapter<String> backendAdapter = new ArrayAdapter<>(
                this,
                R.layout.spinner,
                new String[]{"DirectVulkan", "DirectGLES"}
        );
        backendAdapter.setDropDownViewResource(android.R.layout.simple_spinner_dropdown_item);
        backendSpinner.setAdapter(backendAdapter);

        ArrayAdapter<String> glModeAdapter = new ArrayAdapter<>(
                this,
                R.layout.spinner,
                new String[]{
                        getString(R.string.gl_mode_compat),
                        getString(R.string.gl_mode_direct)
                }
        );
        glModeAdapter.setDropDownViewResource(android.R.layout.simple_spinner_dropdown_item);
        glModeSpinner.setAdapter(glModeAdapter);

        configPathView.setText(PluginConstants.CONFIG_FILE_PATH);
        debugLogHintView.setText(getString(
                R.string.debug_log_hint,
                PluginConstants.MOBILEGL_LOG_FILE_PATH,
                PluginConstants.MOBILEGL_NATIVE_LOG_FILE_PATH
        ));

        accessButton.setOnClickListener(v -> {
            if (hasConfigAccess()) {
                folderPermissionManager.clearAllPermissions();
                refreshState();
            } else {
                requestAccess();
            }
        });
        backendSpinner.setOnItemSelectedListener(new AdapterView.OnItemSelectedListener() {
            @Override
            public void onItemSelected(AdapterView<?> parent, View view, int position, long id) {
                if (config == null) {
                    return;
                }
                config.backendType = position == 0 ? PluginConfig.BACKEND_VULKAN : PluginConfig.BACKEND_GLES;
                saveConfigSilently();
            }

            @Override
            public void onNothingSelected(AdapterView<?> parent) {
            }
        });
        glModeSpinner.setOnItemSelectedListener(new AdapterView.OnItemSelectedListener() {
            @Override
            public void onItemSelected(AdapterView<?> parent, View view, int position, long id) {
                if (config == null) {
                    return;
                }
                config.glMode = position == 1 ? PluginConfig.GL_MODE_DIRECT : PluginConfig.GL_MODE_COMPAT;
                saveConfigSilently();
            }

            @Override
            public void onNothingSelected(AdapterView<?> parent) {
            }
        });
        debugLogSwitch.setOnCheckedChangeListener((buttonView, isChecked) -> {
            if (config == null) {
                return;
            }
            if (config.debugLogEnabled == isChecked) {
                return;
            }
            config.debugLogEnabled = isChecked;
            saveConfigSilently();
        });
    }

    @Override
    protected void onResume() {
        super.onResume();
        refreshState();
    }

    @Override
    protected void onActivityResult(int requestCode, int resultCode, Intent data) {
        super.onActivityResult(requestCode, resultCode, data);
        if (requestCode != REQUEST_CODE_SAF || resultCode != RESULT_OK || data == null) {
            return;
        }

        Uri treeUri = data.getData();
        if (treeUri == null) {
            return;
        }

        File expectedDir = PluginConstants.getFclDirectoryFile();
        if (!folderPermissionManager.isUriMatchingFilePath(treeUri, expectedDir)) {
            new MaterialAlertDialogBuilder(this)
                    .setTitle(R.string.app_name)
                    .setMessage(getString(R.string.warning_path_selection_error,
                            expectedDir.getAbsolutePath(),
                            String.valueOf(folderPermissionManager.getFileByUri(treeUri))))
                    .setPositiveButton(android.R.string.ok, null)
                    .show();
            return;
        }

        getContentResolver().takePersistableUriPermission(
                treeUri,
                Intent.FLAG_GRANT_READ_URI_PERMISSION | Intent.FLAG_GRANT_WRITE_URI_PERMISSION
        );
        refreshState();
    }

    @Override
    public void onRequestPermissionsResult(int requestCode, String[] permissions, int[] grantResults) {
        super.onRequestPermissionsResult(requestCode, permissions, grantResults);
        if (requestCode != REQUEST_CODE_STORAGE) {
            return;
        }
        if (grantResults.length > 0 && grantResults[0] == PackageManager.PERMISSION_GRANTED) {
            refreshState();
        }
    }

    private void refreshState() {
        fclDirectoryUri = folderPermissionManager.getFclFolderUri();
        if (!hasConfigAccess()) {
            hideOptions(
                    Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q
                            ? getString(R.string.status_need_fcl_access)
                            : getString(R.string.status_need_storage_permission)
            );
            return;
        }

        config = PluginConfig.load(this, fclDirectoryUri);
        showOptions();
    }

    private void hideOptions(String statusText) {
        optionsContainer.setVisibility(View.GONE);
        accessButton.setVisibility(View.VISIBLE);
        accessButton.setText(R.string.grant_fcl_access);
        pathStatusView.setText(statusText);
    }

    private void showOptions() {
        optionsContainer.setVisibility(View.VISIBLE);
        accessButton.setVisibility(View.VISIBLE);
        accessButton.setText(R.string.clear_permission);
        pathStatusView.setText(getString(R.string.status_ready, PluginConstants.FCL_DIRECTORY));
        backendSpinner.setSelection(PluginConfig.BACKEND_VULKAN.equals(config.backendType) ? 0 : 1, false);
        glModeSpinner.setSelection(PluginConfig.GL_MODE_DIRECT.equals(config.glMode) ? 1 : 0, false);
        debugLogSwitch.setOnCheckedChangeListener(null);
        debugLogSwitch.setChecked(config.debugLogEnabled);
        debugLogSwitch.setOnCheckedChangeListener((buttonView, isChecked) -> {
            if (config == null) {
                return;
            }
            if (config.debugLogEnabled == isChecked) {
                return;
            }
            config.debugLogEnabled = isChecked;
            saveConfigSilently();
        });
    }

    private boolean hasConfigAccess() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) {
            return fclDirectoryUri != null;
        }
        return checkSelfPermission(Manifest.permission.READ_EXTERNAL_STORAGE) == PackageManager.PERMISSION_GRANTED
                && checkSelfPermission(Manifest.permission.WRITE_EXTERNAL_STORAGE) == PackageManager.PERMISSION_GRANTED;
    }

    private void requestAccess() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) {
            Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT_TREE);
            intent.putExtra(
                    DocumentsContract.EXTRA_INITIAL_URI,
                    Uri.parse(Environment.getExternalStorageDirectory() + "/FCL")
            );
            startActivityForResult(intent, REQUEST_CODE_SAF);
            return;
        }

        requestPermissions(
                new String[]{
                        Manifest.permission.WRITE_EXTERNAL_STORAGE,
                        Manifest.permission.READ_EXTERNAL_STORAGE
                },
                REQUEST_CODE_STORAGE
        );
    }

    private void saveConfigSilently() {
        try {
            config.save(this, fclDirectoryUri);
            Toast.makeText(this, R.string.config_saved, Toast.LENGTH_SHORT).show();
        } catch (IOException e) {
            Toast.makeText(this, getString(R.string.config_save_failed, e.getMessage()), Toast.LENGTH_LONG).show();
        }
    }

    private void showInfoDialog() {
        new AppInfoDialogBuilder(this).show();
    }
}
