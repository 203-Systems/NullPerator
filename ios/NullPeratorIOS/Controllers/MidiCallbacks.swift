import CoreMIDI

enum MidiCallbacks {
    typealias ReadBlock = @Sendable @convention(block) (
        UnsafePointer<MIDIPacketList>, UnsafeMutableRawPointer?
    ) -> Void
    typealias NotifyBlock = @Sendable @convention(block) (
        UnsafePointer<MIDINotification>
    ) -> Void

    // Device-change notifications also arrive on implementation-chosen threads.
    // Only schedule a refresh; never carry CoreMIDI's notification pointer across
    // the actor boundary.
    nonisolated static func notify(
        refresh: @escaping @MainActor @Sendable () -> Void
    ) -> NotifyBlock {
        { _ in
            Task { @MainActor in
                refresh()
            }
        }
    }

    // CoreMIDI invokes this block on its receive thread. Create it outside any
    // actor so Swift cannot inherit MainActor isolation from the caller.
    nonisolated static func read(
        deliver: @escaping @MainActor @Sendable (MIDIEndpointRef, [[UInt8]]) -> Void
    ) -> ReadBlock {
        { packetList, sourceConnection in
            let endpoint = MIDIEndpointRef(
                UInt32(truncatingIfNeeded: UInt(bitPattern: sourceConnection))
            )
            // The packet list belongs to CoreMIDI and expires when we return.
            let messages = MidiPacketReader.bytes(from: packetList)
            Task { @MainActor in
                deliver(endpoint, messages)
            }
        }
    }
}
