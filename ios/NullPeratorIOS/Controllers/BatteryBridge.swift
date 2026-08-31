import UIKit
import WebKit

@MainActor
final class BatteryBridge: NSObject {
    private weak var webView: WKWebView?
    private let device: UIDevice
    private weak var nativeCore: NullPeratorNativeBridge?
    private var started = false

    init(
        webView: WKWebView,
        device: UIDevice = .current,
        nativeCore: NullPeratorNativeBridge? = nil
    ) {
        self.webView = webView
        self.device = device
        self.nativeCore = nativeCore
        super.init()
    }

    func start() {
        guard !started else { return }
        started = true
        device.isBatteryMonitoringEnabled = true
        NotificationCenter.default.addObserver(
            self,
            selector: #selector(batteryDidChange),
            name: UIDevice.batteryLevelDidChangeNotification,
            object: device
        )
        NotificationCenter.default.addObserver(
            self,
            selector: #selector(batteryDidChange),
            name: UIDevice.batteryStateDidChangeNotification,
            object: device
        )
        pushState()
    }

    func pushState() {
        let level = device.batteryLevel
        let available = level >= 0
        let percentage = available
            ? min(100, max(0, Int((level * 100).rounded())))
            : 0
        let charging = available &&
            (device.batteryState == .charging || device.batteryState == .full)
        nativeCore?.setBatteryPercentage(
            percentage,
            charging: charging,
            available: available
        )
        let script = """
        (() => {
          const state = Object.freeze({
            percentage: \(percentage),
            charging: \(charging),
            available: \(available)
          });
          globalThis.__nullPeratorNativeBattery = state;
          globalThis.__nullPeratorHost?.setBattery?.(
            state.percentage,
            state.charging,
            state.available
          );
        })()
        """
        webView?.evaluateJavaScript(script)
    }

    @objc private func batteryDidChange() {
        pushState()
    }
}
