import AVFAudio
import MediaPlayer
import SwiftUI
import UIKit
import WebKit

/// Svelte owns responsive presentation while native C++ owns tracker state
/// and rendering. Indexed dirty framebuffer regions feed the existing canvas.
@MainActor
final class NativeHybridAppModel: ObservableObject {
    @Published private(set) var webURL: URL?
    @Published private(set) var isWebReady = false
    @Published private(set) var errorMessage: String?

    let webView: WKWebView
    private let nativeCore: NullPeratorNativeBridge
    private var nativeCommandBridge: NativeCommandBridge!
    private var controllerBridge: GameControllerBridge!
    private var batteryBridge: BatteryBridge!
    private var readinessTask: Task<Void, Never>?
    private let launchStartedAt = ProcessInfo.processInfo.systemUptime

    init() {
        Self.activateAudioSession()
        nativeCore = NullPeratorNativeBridge()

        let configuration = NullPeratorWebView.makeConfiguration()
        NativeCommandBridge.installBootstrapScript(
            in: configuration.userContentController,
            nativeCore: true
        )
        webView = WKWebView(frame: .zero, configuration: configuration)
        webView.isOpaque = false
        webView.backgroundColor = .black
        webView.scrollView.backgroundColor = .black
        webView.scrollView.bounces = false
        webView.scrollView.contentInsetAdjustmentBehavior = .never
        webView.allowsLinkPreview = false

        nativeCommandBridge = NativeCommandBridge(
            webView: webView,
            nativeCore: nativeCore
        )
        controllerBridge = GameControllerBridge(webView: webView)
        batteryBridge = BatteryBridge(webView: webView, nativeCore: nativeCore)
        nativeCommandBridge.start()
        controllerBridge.start()
        batteryBridge.start()

        guard nativeCore.isInitialized else {
            errorMessage = "The native NullPerator core could not start."
            return
        }
        guard let indexURL = URL(
            string: "\(BundledWebSchemeHandler.scheme)://app/index.html"
        ) else {
            errorMessage = "Bundled NullPerator UI is missing."
            return
        }
        webURL = indexURL
    }

    func webNavigationDidFinish() {
        isWebReady = false
        readinessTask?.cancel()
        readinessTask = Task { [weak self] in
            guard let self else { return }
            for _ in 0..<160 {
                if Task.isCancelled { return }
                let script = """
                (() => {
                  const canvas = document.querySelector('#nullperator-canvas');
                  const play = document.querySelector('[data-action="play"]');
                  const rect = canvas?.getBoundingClientRect();
                  return globalThis.__nullPeratorNativeCore === true
                    && canvas?.dataset?.frameContent === 'native'
                    && Number(canvas?.dataset?.nativeFrameSequence) > 0
                    && play?.disabled === false
                    && rect?.width > 0 && rect?.height > 0;
                })()
                """
                if let ready = try? await webView.evaluateJavaScript(script) as? Bool,
                   ready {
                    batteryBridge.pushState()
                    controllerBridge.pushState()
                    isWebReady = true
                    NSLog(
                        "NullPerator native UI ready in %.3f seconds",
                        ProcessInfo.processInfo.systemUptime - launchStartedAt
                    )
                    return
                }
                try? await Task.sleep(for: .milliseconds(50))
            }
            errorMessage = "NullPerator native UI did not become ready."
        }
    }

    func releaseInput() {
        nativeCore.releaseAllActions()
        webView.evaluateJavaScript("window.__nullPeratorHost?.releaseAll();")
    }

    func refreshAfterForeground() {
        Self.activateAudioSession()
        batteryBridge.pushState()
        controllerBridge.pushState()
        nativeCommandBridge.pushMidiState()
    }

    private static func activateAudioSession() {
        let session = AVAudioSession.sharedInstance()
        do {
            try session.setCategory(
                .playback,
                mode: .default,
                options: [.allowAirPlay]
            )
            try session.setPreferredSampleRate(44_100)
            try session.setActive(true)
            MPNowPlayingInfoCenter.default().nowPlayingInfo = nil
        } catch {
            NSLog("NullPerator native audio session failed: %@", error.localizedDescription)
        }
    }

}

struct NativeContentView: View {
    @StateObject private var model = NativeHybridAppModel()
    @Environment(\.scenePhase) private var scenePhase
    @State private var minimumSplashElapsed = false

    var body: some View {
        ZStack(alignment: .topLeading) {
            Color.black.ignoresSafeArea()

            if let url = model.webURL {
                NullPeratorWebView(
                    webView: model.webView,
                    url: url,
                    onNavigationFinished: model.webNavigationDidFinish
                )
                .opacity(contentVisible ? 1 : 0)
            } else if let message = model.errorMessage {
                Text(message)
                    .font(.system(.footnote, design: .monospaced))
                    .foregroundStyle(.red)
                    .padding()
            }

            if !contentVisible && model.errorMessage == nil {
                NativeSplashScreen()
                    .transition(.opacity)
                    .allowsHitTesting(false)
            }
        }
        .animation(.easeOut(duration: 0.25), value: contentVisible)
        .background(Color.black)
        .ignoresSafeArea()
        .persistentSystemOverlays(.hidden)
        .task {
            try? await Task.sleep(for: .milliseconds(1_200))
            minimumSplashElapsed = true
        }
        .onChange(of: scenePhase) { _, phase in
            if phase == .active { model.refreshAfterForeground() }
            else { model.releaseInput() }
        }
    }

    private var contentVisible: Bool {
        model.isWebReady && minimumSplashElapsed
    }
}

private struct NativeSplashScreen: View {
    var body: some View {
        GeometryReader { proxy in
            let brandEdge = min(176, max(0, min(proxy.size.width, proxy.size.height) - 120))
            let wordmarkWidth = min(240, max(0, proxy.size.width - 80))
            let systemsWidth = min(210, max(0, proxy.size.width - 80))

            ZStack {
                VStack(spacing: 28) {
                    Image("BrandLogo")
                        .resizable()
                        .interpolation(.high)
                        .scaledToFit()
                        .frame(width: brandEdge, height: brandEdge)

                    Image("NullPeratorWordmark")
                        .resizable()
                        .interpolation(.high)
                        .scaledToFit()
                        .frame(width: wordmarkWidth)
                }
                .offset(y: -12)

                VStack {
                    Spacer()
                    Image("SystemsWordmark")
                        .resizable()
                        .interpolation(.high)
                        .scaledToFit()
                        .frame(width: systemsWidth)
                        .padding(.bottom, max(34, proxy.safeAreaInsets.bottom + 22))
                }
            }
            .frame(maxWidth: .infinity, maxHeight: .infinity)
            .accessibilityHidden(true)
        }
        .background(Color.black)
        .ignoresSafeArea()
    }
}
