package org.nullperator.app;

import android.content.Context;
import android.content.SharedPreferences;
import android.media.midi.*;
import android.os.*;
import org.json.*;
import java.io.IOException;
import java.util.*;
import java.util.concurrent.atomic.AtomicInteger;

/** Native MIDI I/O; no MIDI bytes cross the WebView bridge. All port ownership is
 * on the MIDI thread, and the bounded handoff to the core rejects stale routes. */
final class MidiRouter {
    interface Reply { void complete(JSONObject snapshot, String error); }
    private final MidiManager manager;
    private final SharedPreferences preferences;
    private final Handler core;
    private final HandlerThread thread = new HandlerThread("NullPerator MIDI");
    private final Handler handler;
    private final AtomicInteger inputEpoch = new AtomicInteger(), outputEpoch = new AtomicInteger();
    private final AtomicInteger pendingInput = new AtomicInteger(), pendingOutput = new AtomicInteger();
    private final Map<String, Endpoint> inputs = new LinkedHashMap<>(), outputs = new LinkedHashMap<>();
    private static final class DeviceHandle {
        MidiDevice device;
        int references;
        final List<java.util.function.Consumer<MidiDevice>> waiting = new ArrayList<>();
    }
    private final Map<Integer, DeviceHandle> devices = new HashMap<>();
    private MidiDevice inputDevice, outputDevice;
    private MidiOutputPort inputPort; // Android names ports from the external device's perspective.
    private MidiInputPort outputPort;
    private String inputId, outputId, inputKey, outputKey, error;
    private boolean active, enabled, closed, openingInput, openingOutput;
    private long droppedInput, droppedNormal, droppedRealtime, receivedBytes, sentBytes;

    private static final class Endpoint {
        final MidiDeviceInfo device;
        final int port;
        final String id, key, name, manufacturer;
        Endpoint(MidiDeviceInfo device, MidiDeviceInfo.PortInfo port) {
            this.device = device; this.port = port.getPortNumber();
            Bundle props = device.getProperties();
            manufacturer = props.getString(MidiDeviceInfo.PROPERTY_MANUFACTURER, "");
            String deviceName = props.getString(MidiDeviceInfo.PROPERTY_NAME, "MIDI device");
            name = deviceName + " · " + (port.getName() == null || port.getName().isEmpty() ? "Port " + (this.port + 1) : port.getName());
            id = device.getId() + ":" + port.getType() + ":" + this.port;
            key = device.getType() + "|" + manufacturer + "|" + props.getString(MidiDeviceInfo.PROPERTY_PRODUCT, "")
                + "|" + deviceName + "|" + props.getString(MidiDeviceInfo.PROPERTY_SERIAL_NUMBER, "") + "|" + port.getType() + ":" + this.port;
        }
        JSONObject json(boolean connected) throws JSONException {
            return new JSONObject().put("id", id).put("name", name).put("manufacturer", manufacturer)
                .put("state", "connected").put("connection", connected ? "open" : "closed");
        }
    }
    MidiRouter(Context context, Handler core) {
        this(context, core, context.getSharedPreferences("midi-routes", Context.MODE_PRIVATE));
    }
    MidiRouter(Context context, Handler core, SharedPreferences preferences) {
        this.core = core;
        manager = (MidiManager)context.getSystemService(Context.MIDI_SERVICE);
        this.preferences = preferences;
        inputKey = preferences.getString("input", null); outputKey = preferences.getString("output", null);
        thread.start(); handler = new Handler(thread.getLooper());
        handler.post(() -> { if (manager != null) { manager.registerDeviceCallback(callback, handler); refresh(); } });
    }
    private final MidiManager.DeviceCallback callback = new MidiManager.DeviceCallback() {
        @Override public void onDeviceAdded(MidiDeviceInfo info) { refresh(); }
        @Override public void onDeviceRemoved(MidiDeviceInfo info) { refresh(); }
    };
    private void refresh() {
        if (closed || manager == null) return;
        inputs.clear(); outputs.clear();
        // The legacy byte-stream API covers USB and virtual MIDI 1.0 endpoints.
        // Bluetooth discovery/routing is deliberately excluded from this host.
        for (MidiDeviceInfo info : manager.getDevices()) {
            if (info.getType() == MidiDeviceInfo.TYPE_BLUETOOTH) continue;
            for (MidiDeviceInfo.PortInfo port : info.getPorts()) {
                Endpoint endpoint = new Endpoint(info, port);
                (port.getType() == MidiDeviceInfo.PortInfo.TYPE_OUTPUT ? inputs : outputs).put(endpoint.id, endpoint);
            }
        }
        if (inputId != null && !inputs.containsKey(inputId)) closeInput();
        if (outputId != null && !outputs.containsKey(outputId)) closeOutput();
        restore(true); restore(false);
    }
    private void restore(boolean input) {
        if (!enabled || !active || (input ? inputPort != null || openingInput : outputPort != null || openingOutput)) return;
        String key = input ? inputKey : outputKey;
        if (key == null) return;
        List<Endpoint> matches = new ArrayList<>();
        for (Endpoint endpoint : (input ? inputs : outputs).values()) if (key.equals(endpoint.key)) matches.add(endpoint);
        // Never guess between indistinguishable devices after reconnecting.
        if (matches.size() == 1) open(input, matches.get(0));
    }
    void command(String command, String id, Reply reply) {
        handler.post(() -> {
            try {
                if (closed) throw new IOException("MIDI host is closed");
                switch (command) {
                    case "midiAccess":
                        if (manager == null) throw new IOException("MIDI is unavailable on this device");
                        enabled = true; error = null; refresh(); break;
                    case "midiRefresh": break;
                    case "midiSelectInput": select(true, id); break;
                    case "midiSelectOutput": select(false, id); break;
                    case "midiStop": enabled = false; closeInput(); closeOutput(); break;
                    default: throw new IOException("Unknown MIDI command");
                }
                reply.complete(snapshot(), null);
            } catch (Exception failure) { reply.complete(null, failure.getMessage()); }
        });
    }
    private void select(boolean input, String id) throws IOException {
        Map<String, Endpoint> endpoints = input ? inputs : outputs;
        Endpoint endpoint = id == null || id.isEmpty() ? null : endpoints.get(id);
        if (id != null && !id.isEmpty() && endpoint == null) throw new IOException("MIDI device disconnected");
        if (input) { closeInput(); inputKey = endpoint == null ? null : endpoint.key; }
        else { closeOutput(); outputKey = endpoint == null ? null : endpoint.key; }
        preferences.edit().putString(input ? "input" : "output", endpoint == null ? null : endpoint.key).apply();
        error = null;
        if (endpoint != null && enabled && active) open(input, endpoint);
    }
    private void open(boolean input, Endpoint endpoint) {
        int epoch = (input ? inputEpoch : outputEpoch).get();
        if (input) { openingInput = true; inputId = endpoint.id; }
        else { openingOutput = true; outputId = endpoint.id; }
        try {
            acquireDevice(endpoint.device, device -> {
                if (closed || !active || !enabled || epoch != (input ? inputEpoch : outputEpoch).get()) { releaseDevice(device); return; }
                if (input) openingInput = false; else openingOutput = false;
                try {
                    if (device == null) throw new IOException("Cannot open MIDI device");
                    if (input) {
                        inputDevice = device; inputPort = device.openOutputPort(endpoint.port);
                        if (inputPort == null) throw new IOException("MIDI input port is unavailable");
                        inputPort.connect(new MidiReceiver(1024) {
                            @Override public void onSend(byte[] data, int offset, int count, long timestamp) {
                                if (epoch != inputEpoch.get()) return;
                                if (pendingInput.incrementAndGet() > 64) {
                                    pendingInput.decrementAndGet();
                                    handler.post(() -> failInput(epoch, count)); return;
                                }
                                byte[] bytes = Arrays.copyOfRange(data, offset, offset + count);
                                core.post(() -> {
                                    try { if (epoch == inputEpoch.get()) {
                                        boolean accepted = NativeCore.midiInput(bytes, SystemClock.elapsedRealtimeNanos() / 1e6);
                                        handler.post(() -> { if (accepted) receivedBytes += count; else failInput(epoch, count); });
                                    } }
                                    finally { pendingInput.decrementAndGet(); }
                                });
                            }
                        });
                    } else {
                        outputDevice = device; outputPort = device.openInputPort(endpoint.port);
                        if (outputPort == null) throw new IOException("MIDI output port is busy or unavailable");
                        core.post(() -> { if (epoch == outputEpoch.get()) NativeCore.midiOutputConnected(true); });
                    }
                } catch (IOException failure) {
                    error = failure.getMessage();
                    if (input) closeInput(); else closeOutput();
                }
            });
        } catch (Exception failure) {
            error = failure.getMessage();
            if (input) closeInput(); else closeOutput();
        }
    }
    private void failInput(int epoch, int bytes) {
        if (epoch != inputEpoch.get()) return;
        droppedInput += bytes; error = "MIDI input overflow; reselect the source to reconnect"; closeInput();
    }
    // Called by the core tick, not by animation frames in the WebView.
    void send(int[] batch) {
        if (batch == null) return;
        int epoch = outputEpoch.get();
        if (pendingOutput.incrementAndGet() > 64) {
            pendingOutput.decrementAndGet();
            handler.post(() -> { if (epoch == outputEpoch.get()) { droppedNormal++; error = "MIDI output overflow; reselect the destination"; closeOutput(); } });
            return;
        }
        handler.post(() -> {
            try {
                droppedNormal += Integer.toUnsignedLong(batch[0]); droppedRealtime += Integer.toUnsignedLong(batch[1]);
                if (epoch != outputEpoch.get() || outputPort == null) return;
                for (int i = 2; i + 3 < batch.length; i += 4) {
                    byte[] bytes = { (byte)batch[i+1], (byte)batch[i+2], (byte)batch[i+3] };
                    outputPort.send(bytes, 0, batch[i]);
                    sentBytes += batch[i];
                }
            } catch (IOException failure) { error = failure.getMessage(); closeOutput(); }
            finally { pendingOutput.decrementAndGet(); }
        });
    }
    void setActive(boolean value) {
        handler.post(() -> {
            if (closed) return;
            active = value;
            if (!value) { closeInput(); closeOutput(); }
            else { restore(true); restore(false); }
        });
    }
    private void closeInput() {
        inputEpoch.incrementAndGet(); openingInput = false;
        close(inputPort); releaseDevice(inputDevice); inputPort = null; inputDevice = null; inputId = null;
        core.post(() -> NativeCore.midiDisconnect(1));
    }
    private void closeOutput() {
        outputEpoch.incrementAndGet(); openingOutput = false;
        if (outputPort != null) {
            try { // Release external sustained notes before changing routes/backgrounding.
                for (int channel = 0; channel < 16; channel++) {
                    for (int control : new int[]{64, 123, 120}) outputPort.send(new byte[]{(byte)(0xb0 | channel), (byte)control, 0}, 0, 3);
                }
            } catch (IOException ignored) { }
        }
        close(outputPort); releaseDevice(outputDevice); outputPort = null; outputDevice = null; outputId = null;
        core.post(() -> NativeCore.midiDisconnect(2));
    }
    // Opening a virtual device twice while its service is binding can fail on
    // Android. Share one handle across both directions, including pending opens.
    private void acquireDevice(MidiDeviceInfo info, java.util.function.Consumer<MidiDevice> callback) {
        DeviceHandle existing = devices.get(info.getId());
        if (existing != null) {
            if (existing.device != null) { existing.references++; callback.accept(existing.device); }
            else existing.waiting.add(callback);
            return;
        }
        DeviceHandle handle = new DeviceHandle();
        handle.waiting.add(callback); devices.put(info.getId(), handle);
        try {
            manager.openDevice(info, device -> {
                if (devices.get(info.getId()) != handle) { close(device); return; }
                completeOpen(info.getId(), handle, device);
            }, handler);
            handler.postDelayed(() -> {
                if (devices.get(info.getId()) == handle && handle.device == null) completeOpen(info.getId(), handle, null);
            }, 5000);
        } catch (Exception failure) { completeOpen(info.getId(), handle, null); }
    }
    private void completeOpen(int id, DeviceHandle handle, MidiDevice device) {
        List<java.util.function.Consumer<MidiDevice>> waiting = new ArrayList<>(handle.waiting);
        handle.waiting.clear(); handle.device = device; handle.references = waiting.size();
        if (device == null) devices.remove(id);
        for (var callback : waiting) callback.accept(device);
        maybeQuit();
    }
    private void releaseDevice(MidiDevice device) {
        if (device == null) return;
        int id = device.getInfo().getId();
        DeviceHandle handle = devices.get(id);
        if (handle != null && handle.device == device && --handle.references == 0) { devices.remove(id); close(device); }
    }
    private void maybeQuit() { if (closed && devices.isEmpty()) thread.quitSafely(); }
    private static void close(java.io.Closeable resource) { if (resource != null) try { resource.close(); } catch (IOException ignored) { } }
    private JSONObject snapshot() throws JSONException {
        JSONArray in = new JSONArray(), out = new JSONArray();
        for (Endpoint e : inputs.values()) in.put(e.json(e.id.equals(inputId) && inputPort != null));
        for (Endpoint e : outputs.values()) out.put(e.json(e.id.equals(outputId) && outputPort != null));
        return new JSONObject().put("state", manager == null ? "unsupported" : enabled ? "ready" : "idle")
            .put("error", error == null ? JSONObject.NULL : error).put("inputs", in).put("outputs", out)
            .put("selectedInputId", inputId == null ? JSONObject.NULL : inputId).put("selectedOutputId", outputId == null ? JSONObject.NULL : outputId)
            .put("inputConnected", inputPort != null).put("outputConnected", outputPort != null)
            .put("receivedBytes", receivedBytes).put("sentBytes", sentBytes)
            .put("droppedInputBytes", droppedInput).put("droppedNormal", droppedNormal).put("droppedRealtime", droppedRealtime);
    }
    void close() {
        handler.post(() -> { closed = true; if (manager != null) manager.unregisterDeviceCallback(callback); closeInput(); closeOutput(); maybeQuit(); });
    }
}
