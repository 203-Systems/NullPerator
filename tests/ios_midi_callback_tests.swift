import CoreMIDI
import Foundation

@main
struct MidiCallbackTests {
    // Build the production block from MainActor, just as NativeMidiBridge does,
    // then invoke it on a background thread as CoreMIDI does on a real device.
    @MainActor
    static func receiveFromBackground(_ messages: [[UInt8]]) async {
        let expectedEndpoint: MIDIEndpointRef = 42
        await withCheckedContinuation { (done: CheckedContinuation<Void, Never>) in
            let callback = MidiCallbacks.read { endpoint, received in
                MainActor.preconditionIsolated()
                precondition(endpoint == expectedEndpoint)
                precondition(received == messages)
                done.resume()
            }
            Task.detached {
                precondition(!Thread.isMainThread)
                let capacity = 65_536
                let storage = UnsafeMutableRawPointer.allocate(
                    byteCount: capacity,
                    alignment: MemoryLayout<MIDIPacketList>.alignment
                )
                defer { storage.deallocate() }
                storage.initializeMemory(as: UInt8.self, repeating: 0, count: capacity)
                let list = storage.assumingMemoryBound(to: MIDIPacketList.self)
                var packet = MIDIPacketListInit(list)
                for (index, message) in messages.enumerated() {
                    packet = message.withUnsafeBufferPointer { bytes in
                        let next: UnsafeMutablePointer<MIDIPacket>? = MIDIPacketListAdd(
                            list, capacity, packet, MIDITimeStamp(index + 1),
                            bytes.count, bytes.baseAddress!
                        )
                        guard let next else { fatalError("Unable to construct MIDI fixture") }
                        return next
                    }
                }
                callback(list, UnsafeMutableRawPointer(bitPattern: UInt(expectedEndpoint)))
            }
        }
    }

    @MainActor
    static func main() async {
        await receiveFromBackground([])
        await receiveFromBackground([[0xF8]])
        let sysex: [UInt8] = [0xF0] + Array(repeating: 0x7F, count: 511) + [0xF7]
        await receiveFromBackground([[0xFA], sysex, [0x90, 60, 100], [0xFC]])
        let clockBatch: [[UInt8]] = [[0xFA]] + Array(repeating: [0xF8], count: 96) + [[0xFC]]
        for _ in 0..<100 {
            await receiveFromBackground(clockBatch)
        }
        print("CoreMIDI background input callbacks passed")
    }
}
