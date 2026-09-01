import Foundation
@preconcurrency import GameController
import WebKit

@MainActor
final class GameControllerBridge {
    private weak var webView: WKWebView?
    private var observers: [NSObjectProtocol] = []
    private var scanTimer: Timer?

    init(webView: WKWebView) { self.webView = webView }

    func start() {
        observers.append(NotificationCenter.default.addObserver(
            forName: .GCControllerDidConnect, object: nil, queue: .main
        ) { [weak self] _ in
            Task { @MainActor in
                self?.rescan()
                try? await Task.sleep(for: .milliseconds(200))
                self?.rescan()
            }
        })
        observers.append(NotificationCenter.default.addObserver(
            forName: .GCControllerDidDisconnect, object: nil, queue: .main
        ) { [weak self] _ in
            Task { @MainActor in
                self?.evaluate("window.__nullPeratorHost?.releaseAll()")
                self?.pushState()
            }
        })
        GCController.controllers().forEach(configure)
        pushState()
        GCController.startWirelessControllerDiscovery(completionHandler: nil)
        scanTimer = Timer.scheduledTimer(withTimeInterval: 1, repeats: true) {
            [weak self] _ in
            Task { @MainActor in self?.rescan() }
        }
    }

    func rescan() {
        GCController.controllers().forEach(configure)
        pushState()
    }

    func pushState() {
        let controllers = GCController.controllers().filter {
            $0.extendedGamepad != nil || $0.microGamepad != nil
        }
        let names = controllers.map { $0.vendorName ?? "Game Controller" }
        guard
            let jsonData = try? JSONSerialization.data(withJSONObject: [
                "connected": !controllers.isEmpty,
                "count": controllers.count,
                "names": names,
            ]),
            let json = String(data: jsonData, encoding: .utf8)
        else { return }
        evaluate("""
        (() => {
          const state = Object.freeze(\(json));
          globalThis.__nullPeratorControllerState = state;
          globalThis.dispatchEvent(new CustomEvent(
            'nullperator-controller-change',
            { detail: state }
          ));
        })()
        """)
    }

    private func configure(_ controller: GCController) {
        let vendor = controller.vendorName ?? "unknown"
        print("[Controller] configure vendor=\(vendor) category=\(controller.productCategory)")
        if let gamepad = controller.extendedGamepad {
            bind(gamepad.dpad.up, action: "up")
            bind(gamepad.dpad.down, action: "down")
            bind(gamepad.dpad.left, action: "left")
            bind(gamepad.dpad.right, action: "right")
            bind(gamepad.leftThumbstick.up, action: "up")
            bind(gamepad.leftThumbstick.down, action: "down")
            bind(gamepad.leftThumbstick.left, action: "left")
            bind(gamepad.leftThumbstick.right, action: "right")
            // Face buttons use raw Web Gamepad indices (B0/B1) so iOS cannot
            // reinterpret Nintendo-style labels as Xbox-style positions.
        } else if let gamepad = controller.microGamepad {
            bind(gamepad.dpad.up, action: "up")
            bind(gamepad.dpad.down, action: "down")
            bind(gamepad.dpad.left, action: "left")
            bind(gamepad.dpad.right, action: "right")
            // Face buttons use raw Web Gamepad indices (B0/B1).
        }

        // Bind Start/Select from the physical profile as well. Compact
        // controllers often expose these buttons there but omit them from
        // `microGamepad` entirely.
        bindSystemButton(
            controller.physicalInputProfile.buttons[GCInputButtonMenu],
            action: "play",
            source: "start/menu"
        )
        let select = controller.physicalInputProfile.buttons[GCInputButtonOptions]
            ?? controller.physicalInputProfile.buttons[GCInputButtonShare]
        bindSystemButton(select, action: "shift", source: "select/options/share")
    }

    private func bindSystemButton(
        _ button: GCControllerButtonInput?,
        action: String,
        source: String
    ) {
        guard let button else { return }
        button.preferredSystemGestureState = .disabled
        bind(button, action: action, source: source)
    }

    private func bind(
        _ button: GCControllerButtonInput,
        action: String,
        source: String? = nil
    ) {
        button.pressedChangedHandler = { [weak self] _, _, pressed in
            Task { @MainActor in
                let method = pressed ? "press" : "release"
                let name = source ?? button.unmappedLocalizedName ?? button.localizedName ?? "button"
                print("[Controller] \(name) \(method) -> \(action)")
                self?.evaluate("window.__nullPeratorHost?.\(method)('\(action)')")
            }
        }
    }

    private func evaluate(_ script: String) {
        webView?.evaluateJavaScript(script)
    }
}
