import CoreMIDI

enum MidiPacketReader {
    nonisolated static func bytes(
        from packetList: UnsafePointer<MIDIPacketList>
    ) -> [[UInt8]] {
        let count = Int(packetList.pointee.numPackets)
        guard count > 0 else { return [] }

        // CoreMIDI packets have variable lengths. Walk the original allocation;
        // MIDIPacketNext on a copied MIDIPacket would walk unrelated stack data.
        let packetOffset = MemoryLayout<MIDIPacketList>.offset(of: \.packet)!
        let dataOffset = MemoryLayout<MIDIPacket>.offset(of: \.data)!
        var packet = UnsafeRawPointer(packetList)
            .advanced(by: packetOffset)
            .assumingMemoryBound(to: MIDIPacket.self)
        var result: [[UInt8]] = []
        result.reserveCapacity(count)
        for index in 0..<count {
            let data = UnsafeRawPointer(packet)
                .advanced(by: dataOffset)
                .assumingMemoryBound(to: UInt8.self)
            // data's imported tuple has 256 elements, but a packet can be longer.
            result.append(Array(UnsafeBufferPointer(
                start: data, count: Int(packet.pointee.length)
            )))
            if index + 1 < count {
                packet = UnsafePointer(MIDIPacketNext(
                    UnsafeMutablePointer(mutating: packet)
                ))
            }
        }
        return result
    }
}
