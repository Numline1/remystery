// ReMystery for macOS: hosts the scene (index.html) in a WKWebView inside a ScreenSaverView.
import ScreenSaver
import WebKit

@objc(ReMysteryView)
final class ReMysteryView: ScreenSaverView, WKNavigationDelegate {
    private static let moduleName = "io.github.remystery"
    private static weak var soundOwner: ReMysteryView?   // only one display plays sound

    private var webView: WKWebView?
    private var sheet: NSWindow?
    private var soundCheckbox: NSButton?

    private var defaults: ScreenSaverDefaults? { ScreenSaverDefaults(forModuleWithName: Self.moduleName) }
    private var soundEnabled: Bool {
        get { defaults?.object(forKey: "sound") as? Bool ?? true }
        set { defaults?.set(newValue, forKey: "sound"); defaults?.synchronize() }
    }

    override init?(frame: NSRect, isPreview: Bool) {
        super.init(frame: frame, isPreview: isPreview)
        setUp()
    }

    required init?(coder: NSCoder) {
        super.init(coder: coder)
        setUp()
    }

    private func setUp() {
        wantsLayer = true
        layer?.backgroundColor = NSColor.black.cgColor
        animationTimeInterval = 1.0      // the page animates itself
        // macOS 14+ may not call stopAnimation when the saver is dismissed.
        DistributedNotificationCenter.default().addObserver(
            self, selector: #selector(screensaverWillStop),
            name: NSNotification.Name("com.apple.screensaver.willstop"), object: nil)
    }

    deinit {
        DistributedNotificationCenter.default().removeObserver(self)
    }

    override func startAnimation() {
        super.startAnimation()
        guard webView == nil,
              let page = Bundle(for: ReMysteryView.self).url(forResource: "index", withExtension: "html")
        else { return }

        let config = WKWebViewConfiguration()
        config.mediaTypesRequiringUserActionForPlayback = []
        let web = WKWebView(frame: bounds, configuration: config)
        web.autoresizingMask = [.width, .height]
        web.navigationDelegate = self
        web.isHidden = true             // shown once loaded, avoids a white flash
        if #available(macOS 12.0, *) { web.underPageBackgroundColor = .black }
        addSubview(web)
        webView = web

        let sound = !isPreview && soundEnabled && claimSound()
        var url = URLComponents(url: page, resolvingAgainstBaseURL: false)!
        url.queryItems = [
            URLQueryItem(name: "sound", value: sound ? "1" : "0"),
            URLQueryItem(name: "preview", value: isPreview ? "1" : "0"),
        ]
        web.loadFileURL(url.url ?? page, allowingReadAccessTo: page.deletingLastPathComponent())
    }

    override func stopAnimation() {
        super.stopAnimation()
        tearDown()
    }

    @objc private func screensaverWillStop() {
        tearDown()
    }

    private func claimSound() -> Bool {
        if let owner = Self.soundOwner, owner !== self, owner.webView != nil { return false }
        Self.soundOwner = self
        return true
    }

    private func tearDown() {
        guard let web = webView else { return }
        web.evaluateJavaScript("window.reMystery && window.reMystery.stop()", completionHandler: nil)
        web.navigationDelegate = nil
        web.removeFromSuperview()
        webView = nil
        if Self.soundOwner === self { Self.soundOwner = nil }
    }

    func webView(_ webView: WKWebView, didFinish navigation: WKNavigation!) {
        webView.isHidden = false
    }

    override func draw(_ rect: NSRect) {
        NSColor.black.setFill()
        rect.fill()
    }

    override func animateOneFrame() {}

    // MARK: options sheet

    override var hasConfigureSheet: Bool { true }

    override var configureSheet: NSWindow? {
        if let sheet = sheet {
            soundCheckbox?.state = soundEnabled ? .on : .off
            return sheet
        }
        let window = NSWindow(contentRect: NSRect(x: 0, y: 0, width: 320, height: 130),
                              styleMask: [.titled], backing: .buffered, defer: true)
        window.title = "ReMystery"
        let content = NSView(frame: window.contentRect(forFrameRect: window.frame))

        let title = NSTextField(labelWithString: "ReMystery")
        title.font = .boldSystemFont(ofSize: 13)
        title.frame = NSRect(x: 20, y: 92, width: 280, height: 18)

        let checkbox = NSButton(checkboxWithTitle: "Play sounds", target: nil, action: nil)
        checkbox.state = soundEnabled ? .on : .off
        checkbox.frame = NSRect(x: 20, y: 60, width: 280, height: 20)
        soundCheckbox = checkbox

        let ok = NSButton(title: "OK", target: self, action: #selector(closeSheet))
        ok.keyEquivalent = "\r"
        ok.frame = NSRect(x: 220, y: 14, width: 84, height: 30)

        [title, checkbox, ok].forEach(content.addSubview)
        window.contentView = content
        sheet = window
        return window
    }

    @objc private func closeSheet() {
        soundEnabled = soundCheckbox?.state == .on
        if let sheet = sheet { sheet.sheetParent?.endSheet(sheet) }
    }
}
