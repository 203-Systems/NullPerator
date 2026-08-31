import UIKit
import AVFAudio
import CoreAudioKit
import MediaPlayer
@preconcurrency import CoreMIDI
@preconcurrency import WebKit

@MainActor
final class NativeCommandBridge: NSObject, WKScriptMessageHandlerWithReply {
    static let handlerName = "nullPeratorNative"
    private static let nativeSettingsKey = "NullPeratorSettings.v1"
    private static let webSettingsKey = "nullperator.native.settings.v1"
    private static let maxSettingsBytes = 8 * 1024

    private weak var webView: WKWebView?
    private let midiBridge: NativeMidiBridge
    private let nativeCore: NullPeratorNativeBridge?

    init(
        webView: WKWebView,
        nativeCore: NullPeratorNativeBridge? = nil
    ) {
        self.webView = webView
        self.midiBridge = NativeMidiBridge(webView: webView)
        self.nativeCore = nativeCore
        super.init()
    }

    static func installBootstrapScript(
        in userContentController: WKUserContentController,
        nativeCore: Bool = false
    ) {
        installSettingsBootstrapScript(in: userContentController)
        installMidiBootstrapScript(in: userContentController)
        if nativeCore {
            userContentController.addUserScript(WKUserScript(
                source: "globalThis.__nullPeratorNativeCore = true;",
                injectionTime: .atDocumentStart,
                forMainFrameOnly: true
            ))
        }
    }

    private static func installMidiBootstrapScript(
        in userContentController: WKUserContentController
    ) {
        userContentController.addUserScript(WKUserScript(
            source: NativeMidiBridge.bootstrapJavaScript,
            injectionTime: .atDocumentStart,
            forMainFrameOnly: true
        ))
    }

    private static func installSettingsBootstrapScript(
        in userContentController: WKUserContentController
    ) {
        let stored = UserDefaults.standard.string(forKey: Self.nativeSettingsKey)
        let values: [Any] = [Self.webSettingsKey, stored ?? NSNull()]
        guard
            let data = try? JSONSerialization.data(withJSONObject: values),
            let json = String(data: data, encoding: .utf8)
        else { return }
        userContentController.addUserScript(WKUserScript(
            source: """
            (() => {
              const [settingsKey, nativeSettings] = \(json);
              if (typeof nativeSettings === 'string') {
                localStorage.setItem(settingsKey, nativeSettings);
              }
              const originalSetItem = Storage.prototype.setItem;
              Storage.prototype.setItem = function(key, value) {
                originalSetItem.call(this, key, value);
                if (this !== localStorage || key !== settingsKey) return;
                try {
                  const reply = globalThis.webkit?.messageHandlers?.nullPeratorNative
                    ?.postMessage({ command: 'saveSettings', value: String(value) });
                  reply?.catch?.(() => {});
                } catch {}
              };
            })();
            """,
            injectionTime: .atDocumentStart,
            forMainFrameOnly: true
        ))
    }

    func start() {
        webView?.configuration.userContentController.addScriptMessageHandler(
            self,
            contentWorld: .page,
            name: Self.handlerName
        )
        midiBridge.start()
    }

    func pushMidiState() {
        midiBridge.pushState()
    }

    func userContentController(
        _ userContentController: WKUserContentController,
        didReceive message: WKScriptMessage,
        replyHandler: @escaping @MainActor @Sendable (Any?, String?) -> Void
    ) {
        guard
            message.name == Self.handlerName,
            let body = message.body as? [String: Any],
            let command = body["command"] as? String
        else {
            replyHandler(nil, "Invalid native command")
            return
        }

        if command == "nativeReady" {
            guard let nativeCore, nativeCore.isInitialized else {
                replyHandler(nil, "Native C++ core is unavailable")
                return
            }
            let info = Bundle.main.infoDictionary
            replyHandler([
                "runtime": "native-cpp",
                "platform": "ios",
                "version": 1,
                "iosVersion": info?["CFBundleShortVersionString"] as? String ?? "Unknown",
                "iosBuild": info?["CFBundleVersion"] as? String ?? "Unknown",
                "nullPeratorVersion": nativeCore.nullPeratorVersion,
                "buildHash": nativeCore.buildHash,
                "buildTime": nativeCore.buildTime,
            ], nil)
            return
        }

        if command == "nativeAction" {
            guard
                let nativeCore,
                let number = body["action"] as? NSNumber,
                let action = NPTrackerAction(rawValue: number.intValue),
                let pressed = body["pressed"] as? Bool
            else {
                replyHandler(nil, "Invalid native input payload")
                return
            }
            let repeated = body["repeat"] as? Bool ?? false
            nativeCore.setAction(action, pressed: pressed, repeated: repeated)
            replyHandler(["ok": true], nil)
            return
        }

        if command == "nativeReleaseAll" {
            guard let nativeCore else {
                replyHandler(nil, "Native C++ core is unavailable")
                return
            }
            nativeCore.releaseAllActions()
            replyHandler(["ok": true], nil)
            return
        }

        if command == "nativeFrame" {
            guard
                let nativeCore,
                let after = body["after"] as? NSNumber,
                after.doubleValue >= 0,
                after.doubleValue <= Double(UInt32.max)
            else {
                replyHandler(nil, "Invalid native frame request")
                return
            }
            replyHandler(
                nativeCore.framePacket(since: UInt(after.uint32Value)),
                nil
            )
            return
        }

        if command == "nativeMidiInput" {
            guard
                let nativeCore,
                let rawBytes = body["bytes"] as? [Any],
                let timestamp = body["timestamp"] as? NSNumber,
                timestamp.doubleValue.isFinite,
                timestamp.doubleValue >= 0
            else {
                replyHandler(nil, "Invalid native MIDI input payload")
                return
            }
            let bytes = rawBytes.compactMap { value -> UInt8? in
                guard let number = value as? NSNumber else { return nil }
                let integer = number.intValue
                guard integer >= 0 && integer <= 255 else { return nil }
                return UInt8(integer)
            }
            guard bytes.count == rawBytes.count, !bytes.isEmpty, bytes.count <= 1_024 else {
                replyHandler(nil, "Invalid native MIDI input bytes")
                return
            }
            let accepted = nativeCore.submitMidiData(
                Data(bytes),
                timestamp: timestamp.doubleValue
            )
            replyHandler(["accepted": accepted], nil)
            return
        }

        if command == "nativeMidiDrain" {
            guard let nativeCore else {
                replyHandler(nil, "Native C++ core is unavailable")
                return
            }
            replyHandler(nativeCore.drainMidi(), nil)
            return
        }

        if command == "nativeMidiDisconnect" {
            guard
                let nativeCore,
                let directions = body["directions"] as? NSNumber,
                directions.intValue >= 1,
                directions.intValue <= 3
            else {
                replyHandler(nil, "Invalid native MIDI disconnect payload")
                return
            }
            nativeCore.disconnectMidiDirections(UInt(directions.uintValue))
            replyHandler(["ok": true], nil)
            return
        }

        if command == "nativeMidiOutputConnected" {
            guard let nativeCore, let connected = body["connected"] as? Bool else {
                replyHandler(nil, "Invalid native MIDI output state")
                return
            }
            nativeCore.setMidiOutputConnected(connected)
            replyHandler(["ok": true], nil)
            return
        }

        if command == "midiAccess" || command == "midiRefresh" {
            replyHandler(midiBridge.snapshot(), nil)
            return
        }

        if command == "midiSend" {
            guard
                let id = body["id"] as? String,
                let rawData = body["data"] as? [Any]
            else {
                replyHandler(nil, "Invalid MIDI output payload")
                return
            }
            let bytes = rawData.compactMap { value -> UInt8? in
                guard let number = value as? NSNumber else { return nil }
                let integer = number.intValue
                guard integer >= 0 && integer <= 255 else { return nil }
                return UInt8(integer)
            }
            guard bytes.count == rawData.count, !bytes.isEmpty, bytes.count <= 65_536 else {
                replyHandler(nil, "Invalid MIDI output bytes")
                return
            }
            do {
                try midiBridge.send(bytes, to: id)
                replyHandler(["ok": true], nil)
            } catch {
                replyHandler(nil, error.localizedDescription)
            }
            return
        }

        if command == "openBluetoothMidi" {
            midiBridge.presentBluetoothBrowser()
            replyHandler(["ok": true], nil)
            return
        }

        if command == "midiOpen" || command == "midiClose" {
            replyHandler(["ok": true], nil)
            return
        }

        if command == "purchaseHardware" {
            guard let productURL = URL(
                string: "https://203.io/products/operator-deposit"
            ) else {
                replyHandler(nil, "Invalid product URL")
                return
            }
            UIApplication.shared.open(productURL)
            replyHandler(["ok": true], nil)
            return
        }

        if command == "openPrivacyPolicy" {
            guard let privacyURL = URL(
                string: "https://203.io/pages/nullperator-privacy-policy"
            ) else {
                replyHandler(nil, "Invalid privacy policy URL")
                return
            }
            UIApplication.shared.open(privacyURL)
            replyHandler(["ok": true], nil)
            return
        }

        if command == "activateAudio" {
            do {
                let session = AVAudioSession.sharedInstance()
                try session.setCategory(
                    .playback,
                    mode: .default,
                    options: [.allowAirPlay]
                )
                try session.setActive(true)
                replyHandler(["ok": true], nil)
            } catch {
                replyHandler(nil, error.localizedDescription)
            }
            return
        }

        if command == "clearNowPlaying" {
            MPNowPlayingInfoCenter.default().nowPlayingInfo = nil
            let commands = MPRemoteCommandCenter.shared()
            commands.playCommand.isEnabled = false
            commands.pauseCommand.isEnabled = false
            commands.togglePlayPauseCommand.isEnabled = false
            replyHandler(["ok": true], nil)
            return
        }

        if command == "saveSettings" {
            guard
                let value = body["value"] as? String,
                value.utf8.count <= Self.maxSettingsBytes,
                let data = value.data(using: .utf8),
                (try? JSONSerialization.jsonObject(with: data)) is [String: Any]
            else {
                replyHandler(nil, "Invalid settings payload")
                return
            }
            UserDefaults.standard.set(value, forKey: Self.nativeSettingsKey)
            replyHandler(["ok": true], nil)
            return
        }

        guard command == "openFiles" else {
            replyHandler(nil, "Unknown native command: \(command)")
            return
        }

        let documents = FileManager.default.urls(
            for: .documentDirectory,
            in: .userDomainMask
        )[0]
        try? FileManager.default.createDirectory(
            at: documents,
            withIntermediateDirectories: true
        )
        guard let filesURL = URL(
            string: "shareddocuments://\(documents.path)"
        ) else {
            replyHandler(nil, "Unable to create Files URL")
            return
        }
        UIApplication.shared.open(filesURL)
        replyHandler(["ok": true], nil)
    }

}

@MainActor
private final class NativeMidiBridge {
    static let bootstrapJavaScript = #"""
    (() => {
      const nativeHandler = () => globalThis.webkit?.messageHandlers?.nullPeratorNative;
      const post = (command, payload = {}) => {
        const handler = nativeHandler();
        if (!handler) return Promise.reject(new Error('Native MIDI bridge is unavailable'));
        return Promise.resolve(handler.postMessage({ command, ...payload }));
      };

      let access = null;
      let pendingState = { inputs: [], outputs: [] };

      class NativeMIDIPort {
        constructor(info, type) {
          this.type = type;
          this.connection = 'closed';
          this.onstatechange = null;
          this.update(info);
        }
        update(info) {
          this.id = String(info.id);
          this.name = String(info.name || info.id);
          this.manufacturer = String(info.manufacturer || '');
          this.state = info.state === 'connected' ? 'connected' : 'disconnected';
          if (this.state !== 'connected') this.connection = 'closed';
        }
        async open() {
          await post('midiOpen', { id: this.id, type: this.type });
          this.connection = 'open';
          return this;
        }
        async close() {
          await post('midiClose', { id: this.id, type: this.type });
          this.connection = 'closed';
          return this;
        }
      }

      class NativeMIDIInput extends NativeMIDIPort {
        constructor(info) {
          super(info, 'input');
          this.onmidimessage = null;
        }
      }

      class NativeMIDIOutput extends NativeMIDIPort {
        constructor(info) { super(info, 'output'); }
        send(data, timestamp = 0) {
          const bytes = Array.from(data instanceof Uint8Array
            ? data
            : ArrayBuffer.isView(data)
              ? new Uint8Array(data.buffer, data.byteOffset, data.byteLength)
              : new Uint8Array(data));
          const dispatch = () => { void post('midiSend', { id: this.id, data: bytes }).catch(() => {}); };
          const delay = Number.isFinite(timestamp) ? timestamp - performance.now() : 0;
          if (delay > 1) setTimeout(dispatch, delay);
          else dispatch();
        }
        clear() {}
      }

      const syncMap = (map, items, Port) => {
        const live = new Set();
        for (const info of items || []) {
          const id = String(info.id);
          live.add(id);
          const existing = map.get(id);
          if (existing) existing.update(info);
          else map.set(id, new Port(info));
        }
        for (const id of [...map.keys()]) {
          if (!live.has(id)) map.delete(id);
        }
      };

      class NativeMIDIAccess {
        constructor() {
          this.inputs = new Map();
          this.outputs = new Map();
          this.onstatechange = null;
          this.sysexEnabled = false;
        }
        update(state) {
          syncMap(this.inputs, state.inputs, NativeMIDIInput);
          syncMap(this.outputs, state.outputs, NativeMIDIOutput);
          this.onstatechange?.(new Event('statechange'));
        }
      }

      const update = (state) => {
        pendingState = state || pendingState;
        access?.update(pendingState);
      };

      const requestMIDIAccess = async (options = {}) => {
        if (options.sysex) throw new DOMException('SysEx is disabled', 'SecurityError');
        const state = await post('midiAccess');
        if (!access) access = new NativeMIDIAccess();
        update(state);
        return access;
      };

      try {
        Object.defineProperty(navigator, 'requestMIDIAccess', {
          configurable: true,
          value: requestMIDIAccess,
        });
      } catch {
        Object.defineProperty(Navigator.prototype, 'requestMIDIAccess', {
          configurable: true,
          value: requestMIDIAccess,
        });
      }

      globalThis.__nullPeratorNativeMIDI = Object.freeze({
        receive(payload) {
          if (!payload || typeof payload !== 'object') return;
          if (payload.type === 'state') {
            update(payload);
            return;
          }
          if (payload.type !== 'message') return;
          const port = access?.inputs.get(String(payload.id));
          if (!port || typeof port.onmidimessage !== 'function') return;
          const data = Uint8Array.from(payload.data || []);
          port.onmidimessage({
            data,
            timeStamp: performance.now(),
            target: port,
            currentTarget: port,
          });
        },
      });
    })();
    """#

    private weak var webView: WKWebView?
    private var client = MIDIClientRef()
    private var inputPort = MIDIPortRef()
    private var outputPort = MIDIPortRef()
    private var sources: [String: MIDIEndpointRef] = [:]
    private var destinations: [String: MIDIEndpointRef] = [:]
    private var started = false

    init(webView: WKWebView) {
        self.webView = webView
    }

    func start() {
        guard !started else { return }
        started = true
        let clientStatus = MIDIClientCreateWithBlock(
            "NullPerator" as CFString,
            &client
        ) { [weak self] _ in
            Task { @MainActor [weak self] in self?.refreshEndpoints() }
        }
        guard clientStatus == noErr else {
            NSLog("NullPerator MIDI client creation failed: %d", clientStatus)
            return
        }

        let inputStatus = MIDIInputPortCreateWithBlock(
            client,
            "NullPerator Input" as CFString,
            &inputPort
        ) { [weak self] packetList, sourceConnection in
            let endpoint = MIDIEndpointRef(
                UInt32(truncatingIfNeeded: UInt(bitPattern: sourceConnection))
            )
            let messages = Self.packetBytes(packetList)
            Task { @MainActor [weak self] in
                self?.emitInput(from: endpoint, messages: messages)
            }
        }
        if inputStatus != noErr {
            NSLog("NullPerator MIDI input port creation failed: %d", inputStatus)
        }

        let outputStatus = MIDIOutputPortCreate(
            client,
            "NullPerator Output" as CFString,
            &outputPort
        )
        if outputStatus != noErr {
            NSLog("NullPerator MIDI output port creation failed: %d", outputStatus)
        }
        refreshEndpoints()
    }

    func snapshot() -> [String: Any] {
        refreshEndpoints(push: false)
        return statePayload()
    }

    func pushState() {
        guard started else { return }
        refreshEndpoints(push: true)
    }

    func send(_ bytes: [UInt8], to id: String) throws {
        guard !bytes.isEmpty, bytes.count <= 256 else {
            throw NativeMidiError("MIDI packet must contain between 1 and 256 bytes")
        }
        refreshEndpoints(push: false)
        guard let destination = destinations[id] else {
            throw NativeMidiError("The selected MIDI output is no longer connected")
        }
        var packetList = MIDIPacketList()
        let packet = MIDIPacketListInit(&packetList)
        let added = bytes.withUnsafeBytes { rawBuffer -> UnsafeMutablePointer<MIDIPacket>? in
            guard let baseAddress = rawBuffer.bindMemory(to: UInt8.self).baseAddress else {
                return nil
            }
            return MIDIPacketListAdd(
                &packetList,
                MemoryLayout<MIDIPacketList>.size,
                packet,
                0,
                bytes.count,
                baseAddress
            )
        }
        guard added != nil else { throw NativeMidiError("Unable to encode MIDI packet") }
        let status = MIDISend(outputPort, destination, &packetList)
        guard status == noErr else {
            throw NativeMidiError("CoreMIDI send failed (\(status))")
        }
    }

    func presentBluetoothBrowser() {
        guard let presenter = Self.topViewController() else { return }
        let browser = CABTMIDICentralViewController()
        presenter.present(browser, animated: true)
    }

    private func refreshEndpoints(push: Bool = true) {
        guard started, client != 0 else { return }
        var nextSources: [String: MIDIEndpointRef] = [:]
        for index in 0..<MIDIGetNumberOfSources() {
            let endpoint = MIDIGetSource(index)
            guard endpoint != 0 else { continue }
            let id = Self.endpointID(endpoint, direction: "input")
            nextSources[id] = endpoint
            if sources[id] != endpoint, inputPort != 0 {
                let connection = UnsafeMutableRawPointer(
                    bitPattern: UInt(endpoint)
                )
                MIDIPortConnectSource(inputPort, endpoint, connection)
            }
        }
        if inputPort != 0 {
            for (id, endpoint) in sources where nextSources[id] == nil {
                MIDIPortDisconnectSource(inputPort, endpoint)
            }
        }
        sources = nextSources

        var nextDestinations: [String: MIDIEndpointRef] = [:]
        for index in 0..<MIDIGetNumberOfDestinations() {
            let endpoint = MIDIGetDestination(index)
            guard endpoint != 0 else { continue }
            nextDestinations[Self.endpointID(endpoint, direction: "output")] = endpoint
        }
        destinations = nextDestinations
        if push { emit(statePayload()) }
    }

    private func statePayload() -> [String: Any] {
        let inputList = sources.map { id, endpoint in
            Self.endpointPayload(endpoint, id: id)
        }.sorted { ($0["name"] as? String ?? "") < ($1["name"] as? String ?? "") }
        let outputList = destinations.map { id, endpoint in
            Self.endpointPayload(endpoint, id: id)
        }.sorted { ($0["name"] as? String ?? "") < ($1["name"] as? String ?? "") }
        return [
            "type": "state",
            "inputs": inputList,
            "outputs": outputList,
            "bluetoothAvailable": true,
        ]
    }

    private func emitInput(from endpoint: MIDIEndpointRef, messages: [[UInt8]]) {
        let id = Self.endpointID(endpoint, direction: "input")
        guard sources[id] != nil else { return }
        for bytes in messages where !bytes.isEmpty {
            emit(["type": "message", "id": id, "data": bytes])
        }
    }

    private func emit(_ payload: [String: Any]) {
        guard
            JSONSerialization.isValidJSONObject(payload),
            let data = try? JSONSerialization.data(withJSONObject: payload),
            let json = String(data: data, encoding: .utf8)
        else { return }
        webView?.evaluateJavaScript(
            "globalThis.__nullPeratorNativeMIDI?.receive(\(json));"
        )
    }

    nonisolated private static func packetBytes(
        _ packetList: UnsafePointer<MIDIPacketList>
    ) -> [[UInt8]] {
        var result: [[UInt8]] = []
        var packet = packetList.pointee.packet
        for _ in 0..<packetList.pointee.numPackets {
            let bytes = withUnsafeBytes(of: &packet.data) { buffer in
                Array(buffer.prefix(Int(packet.length)))
            }
            result.append(bytes)
            packet = MIDIPacketNext(&packet).pointee
        }
        return result
    }

    nonisolated private static func endpointID(
        _ endpoint: MIDIEndpointRef,
        direction: String
    ) -> String {
        var uniqueID = MIDIUniqueID()
        if MIDIObjectGetIntegerProperty(
            endpoint,
            kMIDIPropertyUniqueID,
            &uniqueID
        ) == noErr, uniqueID != 0 {
            return "native-\(direction)-\(uniqueID)"
        }
        return "native-\(direction)-endpoint-\(endpoint)"
    }

    nonisolated private static func endpointPayload(
        _ endpoint: MIDIEndpointRef,
        id: String
    ) -> [String: Any] {
        [
            "id": id,
            "name": stringProperty(endpoint, kMIDIPropertyDisplayName)
                ?? stringProperty(endpoint, kMIDIPropertyName)
                ?? "MIDI \(endpoint)",
            "manufacturer": stringProperty(endpoint, kMIDIPropertyManufacturer) ?? "",
            "state": "connected",
            "connection": "closed",
        ]
    }

    nonisolated private static func stringProperty(
        _ object: MIDIObjectRef,
        _ property: CFString
    ) -> String? {
        var value: Unmanaged<CFString>?
        guard MIDIObjectGetStringProperty(object, property, &value) == noErr else {
            return nil
        }
        return value?.takeRetainedValue() as String?
    }

    private static func topViewController(
        from root: UIViewController? = UIApplication.shared.connectedScenes
            .compactMap { $0 as? UIWindowScene }
            .flatMap(\.windows)
            .first(where: \.isKeyWindow)?
            .rootViewController
    ) -> UIViewController? {
        if let presented = root?.presentedViewController {
            return topViewController(from: presented)
        }
        if let navigation = root as? UINavigationController {
            return topViewController(from: navigation.visibleViewController)
        }
        if let tab = root as? UITabBarController {
            return topViewController(from: tab.selectedViewController)
        }
        return root
    }
}

private struct NativeMidiError: LocalizedError {
    let message: String
    init(_ message: String) { self.message = message }
    var errorDescription: String? { message }
}
