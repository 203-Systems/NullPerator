package org.nullperator.app;
import android.media.midi.*;
import java.io.IOException;
/** Test-only system MIDI endpoint: echoes each input packet to its output. */
public final class TestMidiService extends MidiDeviceService {
    @Override public MidiReceiver[] onGetInputPortReceivers() {
        return new MidiReceiver[]{new MidiReceiver() {
            @Override public void onSend(byte[] data, int offset, int count, long timestamp) throws IOException {
                for (MidiReceiver receiver : getOutputPortReceivers()) receiver.send(data, offset, count, timestamp);
            }
        }};
    }
}
