package org.nullperator.app;

import android.Manifest;
import android.app.Activity;
import android.app.AlertDialog;
import android.content.Intent;
import android.content.IntentFilter;
import android.content.pm.PackageManager;
import android.media.AudioAttributes;
import android.media.AudioFocusRequest;
import android.media.AudioManager;
import android.net.Uri;
import android.provider.DocumentsContract;
import android.provider.OpenableColumns;
import android.os.*;
import android.util.Base64;
import android.webkit.*;
import android.widget.EditText;
import androidx.webkit.WebViewAssetLoader;
import org.json.*;
import java.io.*;
import java.nio.*;
import java.util.zip.*;

public final class MainActivity extends Activity {
    private static final String ORIGIN = "https://appassets.androidplatform.net";
    private static final int IMPORT = 1, MICROPHONE = 2, EXPORT = 3;
    private static final HandlerThread CORE_THREAD = new HandlerThread("NullPerator core", android.os.Process.THREAD_PRIORITY_AUDIO);
    static { CORE_THREAD.start(); }
    private static final Handler CORE = new Handler(CORE_THREAD.getLooper());
    private final Handler ui = new Handler(Looper.getMainLooper());
    private WebView web;
    private boolean foreground, ready, focusGranted;
    private volatile boolean destroyed;
    private String importProject = "";
    private AudioManager audioManager;
    private AudioFocusRequest audioFocus;

    @Override public void onCreate(Bundle state) {
        super.onCreate(state);
        if (Build.VERSION.SDK_INT >= 33) {
            getOnBackInvokedDispatcher().registerOnBackInvokedCallback(
                android.window.OnBackInvokedDispatcher.PRIORITY_DEFAULT, this::navigateBack);
        }
        if (state != null) importProject = state.getString("importProject", "");
        audioManager = (AudioManager)getSystemService(AUDIO_SERVICE);
        setVolumeControlStream(AudioManager.STREAM_MUSIC);
        audioFocus = new AudioFocusRequest.Builder(AudioManager.AUDIOFOCUS_GAIN)
            .setAudioAttributes(new AudioAttributes.Builder().setUsage(AudioAttributes.USAGE_MEDIA)
                .setContentType(AudioAttributes.CONTENT_TYPE_MUSIC).build())
            .setOnAudioFocusChangeListener(change -> CORE.post(() -> {
                focusGranted = change == AudioManager.AUDIOFOCUS_GAIN;
                if (ready) NativeCore.suspend(!foreground || !focusGranted);
            })).build();
        web = new WebView(this);
        web.setBackgroundColor(0xff111111);
        web.getSettings().setJavaScriptEnabled(true);
        web.getSettings().setDomStorageEnabled(true);
        web.getSettings().setAllowFileAccess(false);
        web.getSettings().setAllowContentAccess(false);
        web.getSettings().setMixedContentMode(WebSettings.MIXED_CONTENT_NEVER_ALLOW);
        web.setWebChromeClient(new WebChromeClient());
        WebView.setWebContentsDebuggingEnabled(BuildConfig.DEBUG);
        var assets = new WebViewAssetLoader.Builder()
            .addPathHandler("/", new WebViewAssetLoader.AssetsPathHandler(this)).build();
        web.setWebViewClient(new WebViewClient() {
            @Override public WebResourceResponse shouldInterceptRequest(WebView view, WebResourceRequest request) {
                if (ORIGIN.equals(request.getUrl().getScheme()+"://"+request.getUrl().getHost())) {
                    WebResourceResponse response = assets.shouldInterceptRequest(request.getUrl());
                    if (response != null) return response;
                }
                return new WebResourceResponse("text/plain", "UTF-8", 404, "Not found", null, new ByteArrayInputStream(new byte[0]));
            }
            @Override public boolean shouldOverrideUrlLoading(WebView view, WebResourceRequest request) {
                // The bridge must never be reachable from navigated external content.
                return true;
            }
        });
        web.addJavascriptInterface(new Bridge(), "NullPeratorAndroid");
        setContentView(web);
        web.setOnApplyWindowInsetsListener((v, insets) -> {
            v.setPadding(insets.getSystemWindowInsetLeft(), insets.getSystemWindowInsetTop(),
                insets.getSystemWindowInsetRight(), insets.getSystemWindowInsetBottom());
            return insets.consumeSystemWindowInsets();
        });
        CORE.post(() -> {
            try {
                ready = NativeCore.init(getFilesDir().getAbsolutePath());
                if (!ready) throw new IOException("Could not initialize the native audio engine.");
                ui.post(() -> { if (!destroyed) web.loadUrl(ORIGIN+"/web/index.html"); });
            } catch (Throwable error) { showError(error); }
        });
    }
    private final Runnable tick = new Runnable() {
        public void run() {
            if (!foreground || destroyed) return;
            if (ready) {
                NativeCore.tick();
                if (NativeCore.needsPermission()) ui.post(() -> requestPermissions(new String[]{Manifest.permission.RECORD_AUDIO}, MICROPHONE));
                String project = NativeCore.takeImport();
                if (project != null) ui.post(() -> {
                    importProject = project;
                    try { startActivityForResult(new Intent(Intent.ACTION_OPEN_DOCUMENT).setType("audio/*").addCategory(Intent.CATEGORY_OPENABLE), IMPORT); }
                    catch (Exception error) { CORE.post(() -> NativeCore.importResult(3, "")); showError(error); }
                });
            }
            CORE.postDelayed(this, 16);
        }
    };
    @Override protected void onStart() {
        super.onStart();
        int permission = checkSelfPermission(Manifest.permission.RECORD_AUDIO) == PackageManager.PERMISSION_GRANTED ? 1 : -2;
        Intent battery = registerReceiver(null, new IntentFilter(Intent.ACTION_BATTERY_CHANGED));
        if (battery != null) {
            int percent = battery.getIntExtra(BatteryManager.EXTRA_LEVEL, 0) * 100 / Math.max(1, battery.getIntExtra(BatteryManager.EXTRA_SCALE, 100));
            boolean charging = battery.getIntExtra(BatteryManager.EXTRA_PLUGGED, 0) != 0;
            CORE.post(() -> NativeCore.battery(percent, charging));
        }
        boolean focus = audioManager.requestAudioFocus(audioFocus) == AudioManager.AUDIOFOCUS_REQUEST_GRANTED;
        CORE.post(() -> { foreground = true; focusGranted = focus; NativeCore.permission(permission); if (ready) NativeCore.suspend(!focus); CORE.removeCallbacks(tick); CORE.post(tick); });
        web.onResume();
    }
    @Override protected void onStop() {
        CORE.post(() -> { foreground = false; CORE.removeCallbacks(tick); if (ready) NativeCore.suspend(true); });
        audioManager.abandonAudioFocusRequest(audioFocus);
        web.onPause();
        super.onStop();
    }
    @Override protected void onDestroy() {
        destroyed = true; CORE.removeCallbacks(tick); web.removeJavascriptInterface("NullPeratorAndroid"); web.destroy(); super.onDestroy();
    }
    @Override protected void onSaveInstanceState(Bundle state) {
        state.putString("importProject", importProject);
        super.onSaveInstanceState(state);
    }
    @Override public void onRequestPermissionsResult(int request, String[] permissions, int[] results) {
        super.onRequestPermissionsResult(request, permissions, results);
        if (request == MICROPHONE) CORE.post(() -> NativeCore.permission(results.length > 0 && results[0] == PackageManager.PERMISSION_GRANTED ? 1 : -1));
    }
    @Override public void onBackPressed() { navigateBack(); }
    private void navigateBack() {
        if (destroyed) return;
        web.evaluateJavascript("(() => { if (!document.querySelector('.settings-sheet')) return false; window.dispatchEvent(new KeyboardEvent('keydown', {key:'Escape'})); return true; })()", handled -> {
            if (!"true".equals(handled)) CORE.post(this::backInTracker);
        });
    }
    private void backInTracker() {
        // Route Back through the same core action as Shift+Left, including dirty-edit warnings.
        NativeCore.action(4, true, false); NativeCore.action(0, true, false);
        NativeCore.action(0, false, false); NativeCore.action(4, false, false);
    }
    private final class Bridge {
        @JavascriptInterface public void postMessage(String text) {
            if (text.length() > 16384) return;
            CORE.post(() -> {
                int id = -1;
                try {
                    JSONObject message = new JSONObject(text); id = message.getInt("id");
                    String command = message.getString("command");
                    final int requestId = id;
                    if (command.equals("openFiles") || command.equals("exportFiles") || command.equals("openWiki")
                            || command.equals("openDiscord") || command.equals("openPrivacyPolicy") || command.equals("purchaseHardware")) {
                        ui.post(() -> { try { openExternal(command); reply(requestId, true, null); }
                            catch (Exception error) { reply(requestId, null, error.getMessage()); } });
                        return;
                    }
                    reply(id, command(message), null);
                } catch (Exception error) { reply(id, null, error.getMessage()); }
            });
        }
    }
    private Object command(JSONObject message) throws Exception {
        switch (message.getString("command")) {
            case "nativeReady":
                if (!ready) throw new IOException("Native engine is not ready");
                return new JSONObject().put("runtime", "native-cpp").put("platform", "android").put("version", 1)
                    .put("appVersion", BuildConfig.VERSION_NAME).put("appBuild", BuildConfig.VERSION_CODE)
                    .put("nullPeratorVersion", NativeCore.productVersion()).put("buildHash", NativeCore.buildHash()).put("buildTime", NativeCore.buildTime());
            case "nativeAction": NativeCore.action(message.getInt("action"), message.getBoolean("pressed"), message.optBoolean("repeat")); return true;
            case "nativeReleaseAll": NativeCore.release(); return true;
            case "nativeFrame": return frame(message.optInt("after"));
            case "nativeMidiDrain": return new JSONObject().put("packets", new JSONArray()).put("droppedNormal", 0).put("droppedRealtime", 0);
            case "nativeMidiDisconnect": case "nativeMidiOutputConnected": return false;
            default: throw new IOException("Command is unavailable on Android: " + message.getString("command"));
        }
    }
    private void openExternal(String command) {
        if (destroyed) return;
        if (command.equals("openFiles")) {
            Uri root = DocumentsContract.buildRootUri(TrackerDocumentsProvider.AUTHORITY, DocumentFiles.ROOT);
            try { startActivity(new Intent(Intent.ACTION_VIEW).setDataAndType(root, "vnd.android.document/root")); }
            catch (android.content.ActivityNotFoundException error) {
                startActivityForResult(new Intent(Intent.ACTION_OPEN_DOCUMENT).setType("*/*")
                    .addCategory(Intent.CATEGORY_OPENABLE).putExtra(DocumentsContract.EXTRA_INITIAL_URI,
                        DocumentsContract.buildDocumentUri(TrackerDocumentsProvider.AUTHORITY, DocumentFiles.ROOT)), 4);
            }
            return;
        }
        if (command.equals("exportFiles")) {
            startActivityForResult(new Intent(Intent.ACTION_CREATE_DOCUMENT).setType("application/zip")
                .addCategory(Intent.CATEGORY_OPENABLE).putExtra(Intent.EXTRA_TITLE, "NullPerator-backup.zip"), EXPORT);
            return;
        }
        String url = switch (command) {
            case "openWiki" -> "https://np-wiki.203.io";
            case "openDiscord" -> "https://discord.gg/rRVCBHHPfw";
            case "openPrivacyPolicy" -> "https://203.io/pages/nullperator-privacy-policy";
            case "purchaseHardware" -> "https://203.io/products/operator-deposit";
            default -> throw new IllegalArgumentException("Unknown link");
        };
        startActivity(new Intent(Intent.ACTION_VIEW, Uri.parse(url)));
    }
    private JSONObject frame(int after) throws Exception {
        byte[] data = NativeCore.frame(after);
        JSONObject result = new JSONObject().put("version", 1).put("changed", data != null);
        if (data == null) return result.put("sequence", after);
        ByteBuffer bytes = ByteBuffer.wrap(data).order(ByteOrder.LITTLE_ENDIAN);
        result.put("sequence", Integer.toUnsignedLong(bytes.getInt())).put("width", 240).put("height", 240);
        byte[] palette = new byte[bytes.getInt()]; bytes.get(palette);
        result.put("palette", Base64.encodeToString(palette, Base64.NO_WRAP));
        JSONArray regions = new JSONArray(); int count = bytes.getInt();
        for (int i = 0; i < count; ++i) {
            JSONObject region = new JSONObject().put("x", bytes.getInt()).put("y", bytes.getInt()).put("width", bytes.getInt()).put("height", bytes.getInt());
            byte[] indices = new byte[bytes.getInt()]; bytes.get(indices);
            regions.put(region.put("indices", Base64.encodeToString(indices, Base64.NO_WRAP)));
        }
        return result.put("regions", regions);
    }
    private void reply(int id, Object result, String error) {
        JSONArray values = new JSONArray().put(id)
            .put(result == null ? JSONObject.NULL : result)
            .put(error == null ? JSONObject.NULL : error);
        String script = "globalThis.__nullPeratorAndroidReply.apply(null," + values + ")";
        ui.post(() -> { if (!destroyed) web.evaluateJavascript(script, null); });
    }
    private void showError(Throwable error) {
        ui.post(() -> { if (!destroyed) new AlertDialog.Builder(this).setTitle("NullPerator").setMessage(error.getMessage()).setPositiveButton("OK", null).show(); });
    }
    @Override protected void onActivityResult(int request, int result, Intent data) {
        super.onActivityResult(request, result, data);
        if (request == IMPORT) {
            if (result != RESULT_OK || data == null || data.getData() == null) { CORE.post(() -> NativeCore.importResult(2, "")); return; }
            nameSample(data.getData());
        } else if (request == EXPORT && result == RESULT_OK && data != null && data.getData() != null) {
            Uri uri = data.getData();
            if (TrackerDocumentsProvider.AUTHORITY.equals(uri.getAuthority())) {
                showError(new IOException("Choose a backup destination outside the NullPerator folder"));
                return;
            }
            // Serialize the snapshot with the engine so project writes cannot race it.
            CORE.post(() -> {
                NativeCore.suspend(true);
                try (ZipOutputStream zip = new ZipOutputStream(getContentResolver().openOutputStream(uri, "wt"))) { exportDirectory(getFilesDir(), "", zip); }
                catch (Exception error) { showError(error); }
                finally { NativeCore.suspend(!foreground || !focusGranted); }
            });
        }
    }
    private void nameSample(Uri uri) {
        String suggested = "Sample";
        try (var cursor = getContentResolver().query(uri, new String[]{OpenableColumns.DISPLAY_NAME}, null, null, null)) {
            if (cursor != null && cursor.moveToFirst()) suggested = cursor.getString(0).replaceFirst("(?i)\\.wav$", "");
        } catch (Exception ignored) { }
        EditText name = new EditText(this); name.setSingleLine(true); name.setText(suggested); name.selectAll();
        AlertDialog dialog = new AlertDialog.Builder(this).setTitle("Import WAV").setView(name)
            .setNegativeButton("Cancel", (d,w) -> CORE.post(() -> NativeCore.importResult(2, "")))
            .setPositiveButton("Import", null).setOnCancelListener(d -> CORE.post(() -> NativeCore.importResult(2, ""))).create();
        dialog.setOnShowListener(d -> dialog.getButton(AlertDialog.BUTTON_POSITIVE).setOnClickListener(v -> {
            final String filename;
            try { filename = SampleFiles.filename(name.getText().toString()); }
            catch (IOException error) { name.setError(error.getMessage()); return; }
            File library = new File(getFilesDir(), "samples");
            File project = new File(getFilesDir(), "projects/"+importProject+"/samples");
            if (SampleFiles.exists(library, filename) || SampleFiles.exists(project, filename)) { name.setError("A sample with this name already exists"); return; }
            name.setEnabled(false);
            dialog.setCancelable(false);
            dialog.getButton(AlertDialog.BUTTON_POSITIVE).setEnabled(false);
            dialog.getButton(AlertDialog.BUTTON_NEGATIVE).setEnabled(false);
            new Thread(() -> {
                try {
                    SampleFiles.importWav(getContentResolver().openInputStream(uri), library, project, filename);
                    CORE.post(() -> NativeCore.importResult(1, "/samples/"+filename));
                    ui.post(() -> { if (!destroyed) dialog.dismiss(); });
                } catch (Exception error) {
                    ui.post(() -> {
                        if (destroyed) { CORE.post(() -> NativeCore.importResult(3, "")); return; }
                        name.setEnabled(true); name.setError(error.getMessage());
                        dialog.setCancelable(true);
                        dialog.getButton(AlertDialog.BUTTON_POSITIVE).setEnabled(true);
                        dialog.getButton(AlertDialog.BUTTON_NEGATIVE).setEnabled(true);
                    });
                }
            }, "WAV import").start();
        }));
        dialog.show();
    }
    private static void exportDirectory(File directory, String prefix, ZipOutputStream zip) throws IOException {
        File[] files = directory.listFiles(); if (files == null) return;
        byte[] buffer = new byte[65536];
        for (File file : files) {
            if (file.getName().startsWith(".") || java.nio.file.Files.isSymbolicLink(file.toPath())) continue;
            if (file.isDirectory()) { exportDirectory(file, prefix+file.getName()+"/", zip); continue; }
            zip.putNextEntry(new ZipEntry(prefix+file.getName()));
            try (InputStream in = new FileInputStream(file)) { int n; while ((n=in.read(buffer)) != -1) zip.write(buffer,0,n); }
            zip.closeEntry();
        }
    }
}
