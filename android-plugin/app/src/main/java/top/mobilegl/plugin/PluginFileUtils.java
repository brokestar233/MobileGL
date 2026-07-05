package top.mobilegl.plugin;

import android.content.ContentResolver;
import android.content.Context;
import android.net.Uri;
import android.provider.DocumentsContract;
import android.database.Cursor;

import java.io.BufferedOutputStream;
import java.io.ByteArrayOutputStream;
import java.io.FileNotFoundException;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.Comparator;
import java.util.List;

final class PluginFileUtils {
    static final class ChildDocumentInfo {
        final Uri uri;
        final String displayName;
        final long lastModified;

        ChildDocumentInfo(Uri uri, String displayName, long lastModified) {
            this.uri = uri;
            this.displayName = displayName;
            this.lastModified = lastModified;
        }

        boolean isExactName(String fileName) {
            return fileName.equals(displayName);
        }
    }

    private PluginFileUtils() {}

    static String readText(Context context, Uri uri) throws IOException {
        try (InputStream in = context.getContentResolver().openInputStream(uri)) {
            if (in == null) {
                throw new IOException("Failed to open input stream");
            }
            ByteArrayOutputStream result = new ByteArrayOutputStream();
            byte[] buffer = new byte[1024];
            int length;
            while ((length = in.read(buffer)) != -1) {
                result.write(buffer, 0, length);
            }
            return result.toString(StandardCharsets.UTF_8.name());
        }
    }

    static void writeText(Context context, Uri directoryUri, String fileName, String text, String mimeType) throws IOException {
        ContentResolver resolver = context.getContentResolver();
        ChildDocumentInfo existing = findExactChildDocument(resolver, directoryUri, fileName);
        Uri fileUri = existing != null ? existing.uri : null;

        if (fileUri != null) {
            try (OutputStream out = resolver.openOutputStream(fileUri, "wt")) {
                if (out != null) {
                    try (BufferedOutputStream bufferedOut = new BufferedOutputStream(out)) {
                        bufferedOut.write(text.getBytes(StandardCharsets.UTF_8));
                    }
                    cleanupLegacyConfigDocuments(resolver, directoryUri, fileName, fileUri);
                    return;
                }
            } catch (IOException | RuntimeException ignored) {
                // Fall through to createDocument path.
            }
        }

        String baseDocId = DocumentsContract.getTreeDocumentId(directoryUri);
        Uri parentDocumentUri = DocumentsContract.buildDocumentUriUsingTree(directoryUri, baseDocId);
        Uri newFileUri = DocumentsContract.createDocument(resolver, parentDocumentUri, mimeType, fileName);
        if (newFileUri == null) {
            throw new IOException("Failed to create document: " + fileName);
        }

        try (OutputStream out = resolver.openOutputStream(newFileUri, "wt")) {
            if (out == null) {
                throw new IOException("Failed to open output stream");
            }
            try (BufferedOutputStream bufferedOut = new BufferedOutputStream(out)) {
                bufferedOut.write(text.getBytes(StandardCharsets.UTF_8));
            }
        }

        cleanupLegacyConfigDocuments(resolver, directoryUri, fileName, newFileUri);
    }

    static ChildDocumentInfo findBestConfigDocument(ContentResolver resolver, Uri directoryUri, String fileName) {
        List<ChildDocumentInfo> matches = listChildDocuments(resolver, directoryUri);
        ChildDocumentInfo exact = null;
        List<ChildDocumentInfo> legacyMatches = new ArrayList<>();
        for (ChildDocumentInfo info : matches) {
            if (info.isExactName(fileName)) {
                exact = info;
                break;
            }
            if (isLegacyConfigName(fileName, info.displayName)) {
                legacyMatches.add(info);
            }
        }
        if (exact != null) {
            return exact;
        }
        return legacyMatches.stream()
                .max(Comparator.comparingLong(info -> info.lastModified))
                .orElse(null);
    }

    private static ChildDocumentInfo findExactChildDocument(ContentResolver resolver, Uri directoryUri, String fileName) {
        for (ChildDocumentInfo info : listChildDocuments(resolver, directoryUri)) {
            if (info.isExactName(fileName)) {
                return info;
            }
        }
        return null;
    }

    private static List<ChildDocumentInfo> listChildDocuments(ContentResolver resolver, Uri directoryUri) {
        List<ChildDocumentInfo> results = new ArrayList<>();
        String treeDocumentId = DocumentsContract.getTreeDocumentId(directoryUri);
        Uri childrenUri = DocumentsContract.buildChildDocumentsUriUsingTree(directoryUri, treeDocumentId);
        String[] projection = {
                DocumentsContract.Document.COLUMN_DOCUMENT_ID,
                DocumentsContract.Document.COLUMN_DISPLAY_NAME,
                DocumentsContract.Document.COLUMN_LAST_MODIFIED
        };
        try (Cursor cursor = resolver.query(childrenUri, projection, null, null, null)) {
            if (cursor == null) {
                return results;
            }
            int idIndex = cursor.getColumnIndexOrThrow(DocumentsContract.Document.COLUMN_DOCUMENT_ID);
            int nameIndex = cursor.getColumnIndexOrThrow(DocumentsContract.Document.COLUMN_DISPLAY_NAME);
            int modifiedIndex = cursor.getColumnIndexOrThrow(DocumentsContract.Document.COLUMN_LAST_MODIFIED);
            while (cursor.moveToNext()) {
                String documentId = cursor.getString(idIndex);
                String displayName = cursor.getString(nameIndex);
                long lastModified = cursor.isNull(modifiedIndex) ? 0L : cursor.getLong(modifiedIndex);
                Uri uri = DocumentsContract.buildDocumentUriUsingTree(directoryUri, documentId);
                results.add(new ChildDocumentInfo(uri, displayName, lastModified));
            }
        } catch (RuntimeException ignored) {
            return results;
        }
        return results;
    }

    private static boolean isLegacyConfigName(String fileName, String displayName) {
        if (displayName == null) {
            return false;
        }
        if (displayName.equals(fileName + ".txt")) {
            return true;
        }
        return displayName.startsWith(fileName + " (") && displayName.endsWith(").txt");
    }

    private static void cleanupLegacyConfigDocuments(ContentResolver resolver, Uri directoryUri, String fileName, Uri keepUri) {
        for (ChildDocumentInfo info : listChildDocuments(resolver, directoryUri)) {
            if (keepUri.equals(info.uri)) {
                continue;
            }
            if (!isLegacyConfigName(fileName, info.displayName)) {
                continue;
            }
            try {
                DocumentsContract.deleteDocument(resolver, info.uri);
            } catch (FileNotFoundException | RuntimeException ignored) {
                // Best-effort cleanup only.
            }
        }
    }
}
