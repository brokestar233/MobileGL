package top.mobilegl.plugin;

import android.content.ContentResolver;
import android.content.Intent;
import android.content.UriPermission;
import android.net.Uri;
import android.os.Environment;
import android.provider.DocumentsContract;

import java.io.File;
import java.util.ArrayList;
import java.util.List;
import java.util.Objects;

final class FolderPermissionManager {
    private final ContentResolver contentResolver;

    FolderPermissionManager(ContentResolver contentResolver) {
        this.contentResolver = contentResolver;
    }

    List<Uri> getGrantedFolderUris() {
        List<Uri> uriList = new ArrayList<>();
        for (UriPermission permission : contentResolver.getPersistedUriPermissions()) {
            if (permission.isReadPermission() && permission.isWritePermission()) {
                uriList.add(permission.getUri());
            }
        }
        return uriList;
    }

    void clearAllPermissions() {
        for (UriPermission permission : contentResolver.getPersistedUriPermissions()) {
            contentResolver.releasePersistableUriPermission(
                    permission.getUri(),
                    Intent.FLAG_GRANT_READ_URI_PERMISSION | Intent.FLAG_GRANT_WRITE_URI_PERMISSION
            );
        }
    }

    File getFileByUri(Uri uri) {
        if (!"com.android.externalstorage.documents".equals(uri.getAuthority())) {
            return null;
        }

        String docId = DocumentsContract.isTreeUri(uri)
                ? DocumentsContract.getTreeDocumentId(uri)
                : DocumentsContract.getDocumentId(uri);

        String[] split = docId.split(":");
        if (split.length < 2) {
            return null;
        }

        if (!"primary".equalsIgnoreCase(split[0])) {
            return null;
        }

        return new File(Environment.getExternalStorageDirectory(), split[1]);
    }

    boolean isUriMatchingFilePath(Uri uri, File file) {
        File expectedFile = getFileByUri(uri);
        return expectedFile != null && Objects.equals(expectedFile.getAbsolutePath(), file.getAbsolutePath());
    }

    Uri getFclFolderUri() {
        File fclFolder = PluginConstants.getFclDirectoryFile();
        for (Uri uri : getGrantedFolderUris()) {
            if (isUriMatchingFilePath(uri, fclFolder)) {
                return uri;
            }
        }
        return null;
    }
}
