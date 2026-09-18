package org.nullperator.app;

import androidx.test.ext.junit.runners.AndroidJUnit4;
import androidx.test.platform.app.InstrumentationRegistry;
import org.junit.Test;
import org.junit.runner.RunWith;
import static org.junit.Assert.*;
import android.os.*;
import android.provider.DocumentsContract;
import android.net.Uri;
import org.json.*;
import java.io.*;
import java.nio.charset.StandardCharsets;
import java.util.concurrent.*;

@RunWith(AndroidJUnit4.class)
public final class HostIntegrationTest {
    @Test public void testDocumentsProviderRoundTripAndBoundary() throws Exception {
        var resolver = InstrumentationRegistry.getInstrumentation().getTargetContext().getContentResolver();
        Uri root = DocumentsContract.buildDocumentUri(TrackerDocumentsProvider.AUTHORITY, "root");
        Uri directory = DocumentsContract.createDocument(resolver, root, DocumentsContract.Document.MIME_TYPE_DIR, "AndroidHostTest-" + System.nanoTime());
        assertNotNull(directory);
        try {
            Uri file = DocumentsContract.createDocument(resolver, directory, "text/plain", "example.txt");
            try (var out = resolver.openOutputStream(file, "wt")) { out.write("round trip".getBytes(StandardCharsets.UTF_8)); }
            try (var in = resolver.openInputStream(file)) { assertEquals("round trip", new String(in.readAllBytes(), StandardCharsets.UTF_8)); }
            Uri renamed = DocumentsContract.renameDocument(resolver, file, "renamed.txt");
            assertNotNull(renamed);
            try (var cursor = resolver.query(renamed, null, null, null, null)) {
                assertTrue(cursor.moveToFirst());
                assertEquals("renamed.txt", cursor.getString(cursor.getColumnIndexOrThrow(DocumentsContract.Document.COLUMN_DISPLAY_NAME)));
            }
            Uri traversal = DocumentsContract.buildDocumentUri(TrackerDocumentsProvider.AUTHORITY, "root/../shared_prefs");
            try { resolver.openFileDescriptor(traversal, "r"); fail("Traversal accepted"); } catch (FileNotFoundException expected) { }
        } finally { assertTrue(DocumentsContract.deleteDocument(resolver, directory)); }
    }
}
