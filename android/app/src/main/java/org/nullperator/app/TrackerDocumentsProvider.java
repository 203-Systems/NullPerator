package org.nullperator.app;

import android.database.Cursor;
import android.database.MatrixCursor;
import android.os.CancellationSignal;
import android.os.Handler;
import android.os.Looper;
import android.os.ParcelFileDescriptor;
import android.provider.DocumentsContract;
import android.provider.DocumentsContract.Document;
import android.provider.DocumentsContract.Root;
import android.provider.DocumentsProvider;
import android.webkit.MimeTypeMap;
import java.io.*;
import java.util.Arrays;
import java.util.Locale;

/** Exposes the tracker library in Android Files, protected by SAF URI grants. */
public final class TrackerDocumentsProvider extends DocumentsProvider {
    public static final String AUTHORITY = "org.nullperator.app.documents";
    private static final String[] ROOT_COLUMNS = { Root.COLUMN_ROOT_ID, Root.COLUMN_DOCUMENT_ID,
        Root.COLUMN_TITLE, Root.COLUMN_FLAGS, Root.COLUMN_MIME_TYPES, Root.COLUMN_ICON, Root.COLUMN_AVAILABLE_BYTES };
    private static final String[] DOCUMENT_COLUMNS = { Document.COLUMN_DOCUMENT_ID, Document.COLUMN_DISPLAY_NAME,
        Document.COLUMN_MIME_TYPE, Document.COLUMN_FLAGS, Document.COLUMN_SIZE, Document.COLUMN_LAST_MODIFIED };
    private DocumentFiles files;
    @Override public boolean onCreate() {
        try { files = new DocumentFiles(getContext().getFilesDir()); return true; }
        catch (IOException error) { return false; }
    }
    @Override public Cursor queryRoots(String[] projection) {
        MatrixCursor cursor = new MatrixCursor(projection == null ? ROOT_COLUMNS : projection);
        cursor.newRow().add(Root.COLUMN_ROOT_ID, DocumentFiles.ROOT).add(Root.COLUMN_DOCUMENT_ID, DocumentFiles.ROOT)
            .add(Root.COLUMN_TITLE, "NullPerator").add(Root.COLUMN_FLAGS, Root.FLAG_SUPPORTS_CREATE | Root.FLAG_SUPPORTS_IS_CHILD)
            .add(Root.COLUMN_MIME_TYPES, "*/*").add(Root.COLUMN_ICON, R.drawable.app_icon)
            .add(Root.COLUMN_AVAILABLE_BYTES, getContext().getFilesDir().getUsableSpace());
        return cursor;
    }
    @Override public Cursor queryDocument(String id, String[] projection) throws FileNotFoundException {
        MatrixCursor cursor = new MatrixCursor(projection == null ? DOCUMENT_COLUMNS : projection);
        include(cursor, id);
        return cursor;
    }
    @Override public Cursor queryChildDocuments(String id, String[] projection, String sortOrder) throws FileNotFoundException {
        MatrixCursor cursor = new MatrixCursor(projection == null ? DOCUMENT_COLUMNS : projection);
        File[] children = files.resolve(id).listFiles();
        if (children == null) throw new FileNotFoundException("Cannot read folder");
        Arrays.sort(children, (a,b) -> a.getName().compareToIgnoreCase(b.getName()));
        for (File child : children) {
            try { include(cursor, files.id(child)); } catch (IOException ignored) { /* Hidden/staging files. */ }
        }
        cursor.setNotificationUri(getContext().getContentResolver(), DocumentsContract.buildChildDocumentsUri(AUTHORITY, id));
        return cursor;
    }
    private void include(MatrixCursor cursor, String id) throws FileNotFoundException {
        File file = files.resolve(id);
        if (!file.exists()) throw new FileNotFoundException(id);
        int flags = file.isDirectory() ? Document.FLAG_DIR_SUPPORTS_CREATE : Document.FLAG_SUPPORTS_WRITE;
        if (!DocumentFiles.ROOT.equals(id)) flags |= Document.FLAG_SUPPORTS_DELETE | Document.FLAG_SUPPORTS_RENAME;
        cursor.newRow().add(Document.COLUMN_DOCUMENT_ID, id).add(Document.COLUMN_DISPLAY_NAME, DocumentFiles.ROOT.equals(id) ? "NullPerator" : file.getName())
            .add(Document.COLUMN_MIME_TYPE, mime(file)).add(Document.COLUMN_FLAGS, flags)
            .add(Document.COLUMN_SIZE, file.isDirectory() ? null : file.length()).add(Document.COLUMN_LAST_MODIFIED, file.lastModified());
    }
    private static String mime(File file) {
        if (file.isDirectory()) return Document.MIME_TYPE_DIR;
        String extension = file.getName().substring(file.getName().lastIndexOf('.') + 1).toLowerCase(Locale.ROOT);
        String type = MimeTypeMap.getSingleton().getMimeTypeFromExtension(extension);
        return type == null ? "application/octet-stream" : type;
    }
    @Override public boolean isChildDocument(String parent, String child) {
        try { files.resolve(parent); files.resolve(child); return child.startsWith(parent + "/"); }
        catch (FileNotFoundException error) { return false; }
    }
    @Override public ParcelFileDescriptor openDocument(String id, String mode, CancellationSignal signal) throws FileNotFoundException {
        if (signal != null) signal.throwIfCanceled();
        File file = files.resolve(id);
        if (!file.isFile()) throw new FileNotFoundException(id);
        try {
            if (mode.equals("r")) return ParcelFileDescriptor.open(file, ParcelFileDescriptor.MODE_READ_ONLY);
            return ParcelFileDescriptor.open(file, ParcelFileDescriptor.parseMode(mode), new Handler(Looper.getMainLooper()), error -> changed(id));
        } catch (IOException error) { throw missing(error); }
    }
    @Override public String createDocument(String parent, String mime, String name) throws FileNotFoundException {
        try { String id = files.id(files.create(parent, name, Document.MIME_TYPE_DIR.equals(mime))); changed(id); return id; }
        catch (IOException error) { throw missing(error); }
    }
    @Override public String renameDocument(String id, String name) throws FileNotFoundException {
        try { String next = files.id(files.rename(id, name)); changed(id); return next; }
        catch (IOException error) { throw missing(error); }
    }
    @Override public void deleteDocument(String id) throws FileNotFoundException {
        try { files.delete(id); changed(id); }
        catch (IOException error) { throw missing(error); }
    }
    private void changed(String id) {
        String parent = id.contains("/") ? id.substring(0, id.lastIndexOf('/')) : DocumentFiles.ROOT;
        getContext().getContentResolver().notifyChange(DocumentsContract.buildChildDocumentsUri(AUTHORITY, parent), null);
        getContext().getContentResolver().notifyChange(DocumentsContract.buildDocumentUri(AUTHORITY, id), null);
    }
    private static FileNotFoundException missing(IOException error) { return new FileNotFoundException(error.getMessage()); }
}
