import CoreMIDI

@main
struct MidiPacketReaderTests {
    static func read(_ messages: [[UInt8]]) -> [[UInt8]] {
        let capacity = 65_536
        let storage = UnsafeMutableRawPointer.allocate(
            byteCount: capacity, alignment: MemoryLayout<MIDIPacketList>.alignment
        )
        defer { storage.deallocate() }
        storage.initializeMemory(as: UInt8.self, repeating: 0, count: capacity)
        let list = storage.assumingMemoryBound(to: MIDIPacketList.self)
        var packet = MIDIPacketListInit(list)
        for (index, message) in messages.enumerated() {
            packet = message.withUnsafeBufferPointer { data in
                // Different timestamps prevent CoreMIDI from merging the packets.
                let next: UnsafeMutablePointer<MIDIPacket>? = MIDIPacketListAdd(
                    list, capacity, packet, MIDITimeStamp(index + 1),
                    data.count, data.baseAddress!
                )
                guard let next else { fatalError("Unable to construct MIDI fixture") }
                return next
            }
        }
        precondition(Int(list.pointee.numPackets) == messages.count)
        return MidiPacketReader.bytes(from: list)
    }

    static func main() {
        precondition(read([]).isEmpty)
        precondition(read([[0xF8]]) == [[0xF8]])
        let transport: [[UInt8]] = [[0xFA], [0xF8], [0xF8], [0xFC], [0xFB], [0xF8]]
        precondition(read(transport) == transport)

        // Exercise packet alignment and lengths beyond the imported data tuple.
        for length in [2, 3, 4, 5, 255, 256, 257, 513, 1_024] {
            let sysex: [UInt8] = [0xF0] + (0..<(length - 2)).map {
                UInt8($0 % 128)
            } + [0xF7]
            let messages: [[UInt8]] = [[0xFA], sysex, [0x90, 60, 100], [0xF8], [0xFC]]
            // Comparison happens after the input allocation has been freed:
            // the result must own its bytes before leaving the MIDI callback.
            precondition(read(messages) == messages, "Incorrect payload at length \(length)")
        }

        let clockBatch: [[UInt8]] = [[0xFA]] + Array(repeating: [0xF8], count: 96) + [[0xFC]]
        for _ in 0..<10_000 {
            precondition(read(clockBatch) == clockBatch)
        }
        print("CoreMIDI packet tests passed (including 980,000 transport/clock packets)")
    }
}
