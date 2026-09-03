import SwiftUI
import WebKit

final class BundledWebSchemeHandler: NSObject, WKURLSchemeHandler {
    static let scheme = "nullperator"

    func webView(
        _ webView: WKWebView,
        start urlSchemeTask: any WKURLSchemeTask
    ) {
        guard
            let requestURL = urlSchemeTask.request.url,
            requestURL.host == "app"
        else {
            urlSchemeTask.didFailWithError(URLError(.badURL))
            return
        }
        let relativePath = requestURL.path.removingPercentEncoding?
            .trimmingCharacters(in: CharacterSet(charactersIn: "/")) ?? ""
        guard
            !relativePath.isEmpty,
            !relativePath.split(separator: "/").contains(".."),
            let resourceRoot = Bundle.main.resourceURL?
                .appendingPathComponent("WebApp", isDirectory: true)
        else {
            urlSchemeTask.didFailWithError(URLError(.fileDoesNotExist))
            return
        }
        let fileURL = resourceRoot.appendingPathComponent(relativePath)
        guard var data = try? Data(contentsOf: fileURL) else {
            urlSchemeTask.didFailWithError(URLError(.fileDoesNotExist))
            return
        }
        if relativePath == "index.html",
           var html = String(data: data, encoding: .utf8) {
            // The shared web workbench deliberately keeps a browser-friendly
            // viewport. The iOS shell fills the physical display and owns its
            // zoom gestures, so apply the native viewport before WebKit parses
            // the page. In particular, viewport-fit=cover is what makes the
            // CSS safe-area insets and the control spacing deterministic.
            let nativeViewport = """
            <meta name="viewport" content="width=device-width, initial-scale=1, minimum-scale=1, maximum-scale=1, user-scalable=no, viewport-fit=cover" />
            """
            if let viewportRange = html.range(
                of: #"<meta\s+name=[\"']viewport[\"'][^>]*>"#,
                options: [.regularExpression, .caseInsensitive]
            ) {
                html.replaceSubrange(viewportRange, with: nativeViewport)
            } else if let headRange = html.range(
                of: "<head>",
                options: .caseInsensitive
            ) {
                html.insert(contentsOf: "\n    \(nativeViewport)", at: headRange.upperBound)
            }
            data = Data(html.utf8)
        }
        let response = URLResponse(
            url: requestURL,
            mimeType: Self.mimeType(for: fileURL.pathExtension),
            expectedContentLength: data.count,
            textEncodingName: Self.isText(fileURL.pathExtension) ? "utf-8" : nil
        )
        urlSchemeTask.didReceive(response)
        urlSchemeTask.didReceive(data)
        urlSchemeTask.didFinish()
    }

    func webView(
        _ webView: WKWebView,
        stop urlSchemeTask: any WKURLSchemeTask
    ) {}

    private static func mimeType(for extensionName: String) -> String {
        switch extensionName.lowercased() {
        case "html": "text/html"
        case "css": "text/css"
        case "js", "mjs": "text/javascript"
        case "json", "map": "application/json"
        case "svg": "image/svg+xml"
        case "png": "image/png"
        default: "application/octet-stream"
        }
    }

    private static func isText(_ extensionName: String) -> Bool {
        ["html", "css", "js", "mjs", "json", "map", "svg"]
            .contains(extensionName.lowercased())
    }
}

struct NullPeratorWebView: UIViewRepresentable {
    let webView: WKWebView
    let url: URL
    let onNavigationFinished: () -> Void

    @MainActor
    final class Coordinator: NSObject, WKNavigationDelegate {
        let onNavigationFinished: () -> Void

        init(onNavigationFinished: @escaping () -> Void) {
            self.onNavigationFinished = onNavigationFinished
        }

        func webView(_ webView: WKWebView, didFinish navigation: WKNavigation!) {
            Self.disableSystemTextAndZoomGestures(in: webView)
            onNavigationFinished()
        }

        static func disableSystemTextAndZoomGestures(in webView: WKWebView) {
            func disable(in view: UIView) {
                view.gestureRecognizers?.forEach { recognizer in
                    if recognizer is UILongPressGestureRecognizer {
                        recognizer.isEnabled = false
                    }
                    if let tap = recognizer as? UITapGestureRecognizer,
                       tap.numberOfTapsRequired > 1 {
                        tap.isEnabled = false
                    }
                }
                view.subviews.forEach(disable)
            }
            webView.scrollView.minimumZoomScale = 1
            webView.scrollView.maximumZoomScale = 1
            webView.scrollView.pinchGestureRecognizer?.isEnabled = false
            disable(in: webView)
        }

    }

    static func makeConfiguration() -> WKWebViewConfiguration {
        let configuration = WKWebViewConfiguration()
        configuration.setURLSchemeHandler(
            BundledWebSchemeHandler(),
            forURLScheme: BundledWebSchemeHandler.scheme
        )
        configuration.allowsInlineMediaPlayback = true
        configuration.mediaTypesRequiringUserActionForPlayback = []
        configuration.preferences.javaScriptCanOpenWindowsAutomatically = false
        configuration.websiteDataStore = .default()
        let disableTextAndZoom = WKUserScript(
            source: """
            (() => {
              const style = document.createElement('style');
              style.textContent = `
                html, body, #app, #app * {
                  -webkit-user-select: none !important;
                  user-select: none !important;
                  -webkit-touch-callout: none !important;
                  -webkit-tap-highlight-color: transparent !important;
                }
                html, body, #app { touch-action: none !important; }
              `;
              document.documentElement.appendChild(style);
              const prevent = event => event.preventDefault();
              document.addEventListener('contextmenu', prevent, { capture: true });
              document.addEventListener('selectstart', prevent, { capture: true });
              document.addEventListener('dblclick', prevent, { capture: true });
              document.addEventListener('gesturestart', prevent, { capture: true, passive: false });
              document.addEventListener('gesturechange', prevent, { capture: true, passive: false });
              document.addEventListener('gestureend', prevent, { capture: true, passive: false });
              try {
                // Treat WebAudio as a real-time instrument rather than media
                // playback so iOS does not publish browser-style Now Playing metadata.
                if ('audioSession' in navigator) navigator.audioSession.type = 'play-and-record';
              } catch {}
            })();
            """,
            injectionTime: .atDocumentStart,
            forMainFrameOnly: true
        )
        configuration.userContentController.addUserScript(disableTextAndZoom)
        return configuration
    }
    func makeCoordinator() -> Coordinator {
        Coordinator(onNavigationFinished: onNavigationFinished)
    }

    func makeUIView(context: Context) -> WKWebView {
        webView.navigationDelegate = context.coordinator
        Coordinator.disableSystemTextAndZoomGestures(in: webView)
        load(url, in: webView)
        return webView
    }

    func updateUIView(_ uiView: WKWebView, context: Context) {
        guard uiView.url == nil else { return }
        load(url, in: uiView)
    }

    private func load(_ url: URL, in webView: WKWebView) {
        webView.load(URLRequest(
            url: url,
            cachePolicy: .reloadIgnoringLocalCacheData
        ))
    }

    static func dismantleUIView(_ uiView: WKWebView, coordinator: Coordinator) {
        uiView.stopLoading()
        uiView.navigationDelegate = nil
    }
}
