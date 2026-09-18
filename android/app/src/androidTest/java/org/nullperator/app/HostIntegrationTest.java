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
    @Test public void testNativeMidiLoopbackRoutesAndLifecycle() throws Exception {
        HandlerThread thread = new HandlerThread("MIDI integration core"); thread.start();
        Handler core = new Handler(thread.getLooper());
        var context = InstrumentationRegistry.getInstrumentation().getTargetContext();
        File root = new File(context.getCacheDir(), "midi-integration"); root.mkdirs();
        CompletableFuture<Boolean> initialized = new CompletableFuture<>();
        core.post(() -> initialized.complete(NativeCore.init(root.getAbsolutePath())));
        assertTrue(initialized.get(10, TimeUnit.SECONDS));
        assertEquals(BuildConfig.VERSION_NAME, NativeCore.productVersion());
        var preferences = context.getSharedPreferences("midi-test-routes", 0);
        preferences.edit().clear().commit();
        MidiRouter router = new MidiRouter(context, core, preferences);
        try {
            router.setActive(true);
            JSONObject state = request(router, "midiAccess", null);
            String input = endpoint(state.getJSONArray("inputs")), output = endpoint(state.getJSONArray("outputs"));
            assertNotNull("Test MIDI source missing", input); assertNotNull("Test MIDI destination missing", output);
            request(router, "midiSelectInput", input); request(router, "midiSelectOutput", output);
            for (int i = 0; i < 100; i++) {
                state = request(router, "midiRefresh", null);
                if (state.getBoolean("inputConnected") && state.getBoolean("outputConnected")) break;
                Thread.sleep(30);
            }
            assertTrue(state.toString(), state.getBoolean("inputConnected"));
            assertTrue(state.toString(), state.getBoolean("outputConnected"));
            // Real platform MIDI ports, test service echo, bounded native input queue.
            router.send(new int[]{0, 0, 3, 0x90, 60, 80, 3, 0x80, 60, 0});
            for (int i = 0; i < 100; i++) {
                state = request(router, "midiRefresh", null);
                if (state.getLong("receivedBytes") >= 6) break;
                Thread.sleep(20);
            }
            assertTrue(state.toString(), state.getLong("sentBytes") >= 6);
            assertTrue(state.toString(), state.getLong("receivedBytes") >= 6);
            router.setActive(false);
            state = request(router, "midiRefresh", null);
            assertFalse(state.getBoolean("inputConnected")); assertFalse(state.getBoolean("outputConnected"));
            router.setActive(true);
            for (int i = 0; i < 100; i++) {
                state = request(router, "midiRefresh", null);
                if (state.getBoolean("inputConnected") && state.getBoolean("outputConnected")) break;
                Thread.sleep(20);
            }
            assertTrue(state.toString(), state.getBoolean("inputConnected"));
            assertTrue(state.toString(), state.getBoolean("outputConnected"));
            request(router, "midiSelectInput", null); request(router, "midiSelectOutput", null);
            state = request(router, "midiRefresh", null);
            assertFalse(state.getBoolean("inputConnected")); assertFalse(state.getBoolean("outputConnected"));
        } finally {
            request(router, "midiStop", null); router.close();
            preferences.edit().clear().commit();
            core.post(NativeCore::shutdown); thread.quitSafely(); thread.join(3000);
        }
    }
    private static String endpoint(JSONArray ports) throws Exception {
        for (int i=0; i<ports.length(); i++) { JSONObject port = ports.getJSONObject(i); if (port.getString("manufacturer").equals("NullPerator Tests")) return port.getString("id"); }
        return null;
    }
    private static JSONObject request(MidiRouter router, String command, String id) throws Exception {
        CompletableFuture<JSONObject> result = new CompletableFuture<>();
        router.command(command, id, (value,error) -> { if (error == null) result.complete(value); else result.completeExceptionally(new IOException(error)); });
        return result.get(5, TimeUnit.SECONDS);
    }
}
