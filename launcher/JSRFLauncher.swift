//
//  JSRF Unleashed - launcher
//
//  Double-click, and the game runs. The first launch asks where the game's
//  files are; after that it remembers, and only asks again if they have gone.
//
//  What it remembers is a security-scoped bookmark rather than a path, so a
//  folder the user renames or moves still resolves and the question is not
//  asked a second time for no reason. "Ask again" is reserved for the case
//  where the files are genuinely not there any more, which is the only case
//  the user can actually do anything about.
//

import AppKit
import Foundation

// MARK: - where the game files are

enum GameFolder {
    static let bookmarkKey = "gameFolderBookmark"
    static let pathKey     = "gameFolderPath"

    /// A folder is the game if the XBE is in it. Accept the enclosing folder
    /// too: extraction tools drop the disc contents one level down as often
    /// as not, and making the user find the exact level is a question the
    /// launcher can answer itself.
    static func resolve(_ url: URL) -> URL? {
        let fm = FileManager.default
        if fm.fileExists(atPath: url.appendingPathComponent("default.xbe").path) {
            return url
        }
        guard let kids = try? fm.contentsOfDirectory(
            at: url, includingPropertiesForKeys: [.isDirectoryKey],
            options: [.skipsHiddenFiles]) else { return nil }
        for kid in kids where (try? kid.resourceValues(forKeys: [.isDirectoryKey]))?.isDirectory == true {
            if fm.fileExists(atPath: kid.appendingPathComponent("default.xbe").path) {
                return kid
            }
        }
        return nil
    }

    /// The remembered folder, or nil if it was never set or is gone.
    static func remembered() -> URL? {
        let d = UserDefaults.standard
        if let data = d.data(forKey: bookmarkKey) {
            var stale = false
            if let url = try? URL(resolvingBookmarkData: data,
                                  options: [.withSecurityScope],
                                  relativeTo: nil,
                                  bookmarkDataIsStale: &stale) {
                _ = url.startAccessingSecurityScopedResource()
                if let game = resolve(url) {
                    if stale { remember(url) }
                    return game
                }
                url.stopAccessingSecurityScopedResource()
            }
        }
        // A bookmark can fail for reasons that have nothing to do with the
        // files (an unsigned rebuild, a different volume). The plain path is
        // the fallback, and it is also what the "Game folder" menu shows.
        if let p = d.string(forKey: pathKey) {
            return resolve(URL(fileURLWithPath: p))
        }
        return nil
    }

    static func remember(_ url: URL) {
        let d = UserDefaults.standard
        d.set(url.path, forKey: pathKey)
        if let data = try? url.bookmarkData(options: [.withSecurityScope],
                                            includingResourceValuesForKeys: nil,
                                            relativeTo: nil) {
            d.set(data, forKey: bookmarkKey)
        }
    }

    static func forget() {
        let d = UserDefaults.standard
        d.removeObject(forKey: bookmarkKey)
        d.removeObject(forKey: pathKey)
    }
}

// MARK: - discs

/// Where an extracted disc is kept, so the same ISO is never unpacked twice.
enum Library {
    /// Where the run logs go.
    ///
    /// Application Support is the correct place for them and the wrong place
    /// to have to read them from: it is a protected location, so anyone
    /// helping to debug this remotely -- which is how most of this port has
    /// been built -- cannot open it, and every crash report has to be copied
    /// out by hand. The logs are diagnostic output about the game, not user
    /// data, so they belong beside the app, in the folder the game already
    /// lives in. Falls back to Application Support if that folder is not
    /// writable (a read-only volume, or the app moved somewhere odd).
    static var logRoot: URL {
        let beside = Bundle.main.bundleURL.deletingLastPathComponent()
        if FileManager.default.isWritableFile(atPath: beside.path) { return beside }
        return root
    }

    static var root: URL {
        let base = FileManager.default.urls(for: .applicationSupportDirectory,
                                            in: .userDomainMask)[0]
            .appendingPathComponent("JSRF Unleashed")
        try? FileManager.default.createDirectory(at: base, withIntermediateDirectories: true)
        return base
    }

    static func extractedFolder(for iso: URL) -> URL {
        var name = iso.deletingPathExtension().lastPathComponent
        if name.lowercased().hasSuffix(".xiso") { name = String(name.dropLast(5)) }
        return root.appendingPathComponent(name)
    }

    /// The disc images directly in a folder -- where a download lands, and
    /// what someone picks when they were asked for "the game".
    static func images(in folder: URL) -> [URL] {
        let kids = (try? FileManager.default.contentsOfDirectory(
            at: folder, includingPropertiesForKeys: nil, options: [.skipsHiddenFiles])) ?? []
        return kids.filter { ["iso", "xiso"].contains($0.pathExtension.lowercased()) }
                   .sorted { $0.lastPathComponent < $1.lastPathComponent }
    }
    // A disc image is unpacked by the engine itself (--unpack-iso: the
    // unpacker the iPhone, Android and Windows apps use, port/ios/xiso.c).
    // It used to be handed to extract-xiso, which is not part of macOS: a
    // player without Homebrew had no way to use an image at all.
}

// MARK: - the game

enum Game {
    /// The recompiled binary, beside this launcher inside the bundle.
    static var binary: URL {
        Bundle.main.bundleURL
            .appendingPathComponent("Contents/MacOS/jsrf_recomp")
    }

    /// Whether some files are the game the engine is made from, asked of the
    /// engine itself (port/xbe_check.c, through port/main.c) so there is one
    /// copy of the check: `--check-game <folder>` or `--check-iso <image>`.
    /// nil means yes, or that the engine could not be asked -- then it checks
    /// again at startup and stops with the same explanation in its log. A
    /// string is what to tell the player: what the files are instead (another
    /// build of the game, another game's disc, a zip, not a disc image).
    static func refusal(_ args: [String]) -> String? {
        let p = Process()
        p.executableURL = binary
        p.arguments = args
        let out = Pipe()
        p.standardOutput = out
        p.standardError = FileHandle.nullDevice
        do { try p.run() } catch { return nil }
        let data = out.fileHandleForReading.readDataToEndOfFile()
        p.waitUntilExit()
        // 2: no default.xbe; 3: another build or another game; 4: not an Xbox
        // disc image. Anything else is the engine failing, not the files.
        guard [2, 3, 4].contains(p.terminationStatus) else { return nil }
        let text = String(data: data, encoding: .utf8)?
            .trimmingCharacters(in: .whitespacesAndNewlines) ?? ""
        return text.isEmpty ? "These are not the files of the standalone US disc of Jet Set Radio Future." : text
    }

    /// The flags the port needs. Kept here rather than in a shell script so
    /// there is one place they are true, and overridable from `defaults` for
    /// anyone who wants to experiment without a rebuild:
    ///   defaults write com.jumatt.jsrf-unleashed env -array RECOMP_GL_SCALE=2
    static func environment(gameDir: URL) -> [String: String] {
        var env = ProcessInfo.processInfo.environment
        let base = [
            "RECOMP_USB": "1",
            "RECOMP_GL": "1",
            "RECOMP_PB_EXEC": "1",
            "RECOMP_VBLANK": "1",
            "RECOMP_AC97_READY_NOTRAP": "1",
            "RECOMP_TRACE_BUDGET": "0",
            "RECOMP_GL_READBACK": "0",
            "RECOMP_FPS": "60",
            "RECOMP_ABI_RESTORE": "1",
            // CRI's ADX idle thread runs at idle priority and does real file
            // and decode work; outside the guest lock it raced the threads
            // CRI assumes it cannot preempt, and the music stream stalled
            // (and could corrupt CRI's state). Idle threads take the lock.
            "RECOMP_LOCK_IDLE": "1",
            // WORKAROUND, not a fix. One full-screen black quad -- four
            // vertices covering the viewport, no texture, diffuse RGB
            // zero, alpha around 0.9, alpha-blended -- is drawn over the
            // finished scene and takes the frame from a mean brightness
            // of 80 to 5, which is black to the eye. The HUD is drawn
            // after it and survives, which is exactly what the tutorial
            // looked like. Skipping it makes the level visible again.
            // The cost is that genuine fades between scenes now cut
            // rather than fade. Why the title asks for a near-opaque
            // curtain on a frame it means you to see is still open.
            "RECOMP_GL_CURTAIN": "skip",
            "RECOMP_MUTE": "0",
            // Without this the engine renders offscreen and opens nothing.
            // It is how the diagnostic runs work -- they dump frames to disk
            // and never want a window -- and copying that list into a launcher
            // produced an app that played the soundtrack to an empty screen.
            "RECOMP_WINDOW": "1",
        ]
        // Deliberately absent: RECOMP_GL_SCALE and the window size. The engine
        // reads the resolution from the Video menu's stored choice and sizes
        // its window to match; setting them here would silently overrule the
        // menu, which is the one setting a player can actually reach.

        for (k, v) in base { env[k] = v }
        for entry in UserDefaults.standard.stringArray(forKey: "env") ?? [] {
            let parts = entry.split(separator: "=", maxSplits: 1).map(String.init)
            if parts.count == 2 { env[parts[0]] = parts[1] }
        }
        env["JSRF_GAME_DIR"] = gameDir.path
        // Saves belong to the player, not to the disc they were made from:
        // the game folder may be read-only, may be a mounted image, and is
        // not somewhere anyone expects their progress to be kept.
        let saves = Library.root.appendingPathComponent("save")
        try? FileManager.default.createDirectory(at: saves, withIntermediateDirectories: true)
        env["JSRF_SAVE_DIR"] = saves.path
        return env
    }
}

// MARK: - app

final class Launcher: NSObject, NSApplicationDelegate {
    private var logURL: URL?

    func applicationDidFinishLaunching(_ note: Notification) {
        NSApp.setActivationPolicy(.regular)
        guard FileManager.default.isExecutableFile(atPath: Game.binary.path) else {
            fail("This app is missing the game engine.",
                 "Expected it at:\n\(Game.binary.path)\n\nRebuild the app with build_app.sh.")
            return
        }
        launched = true
        // A path on the command line, as the Windows and Linux launchers take one.
        let arg = CommandLine.arguments.dropFirst()
            .first { !$0.hasPrefix("-") && FileManager.default.fileExists(atPath: $0) }
        if let url = opened ?? arg.map({ URL(fileURLWithPath: $0) }) {   // dropped on the app, or opened with it
            opened = nil
            used = url.standardizedFileURL
            use(url)
        } else if let folder = GameFolder.remembered() {
            start(folder)
        } else {
            ask(reason: nil)
        }
    }

    /// A folder or a disc image dropped on the app's icon, or opened with it:
    /// used as if it had been picked. When the app is started that way this
    /// comes before applicationDidFinishLaunching, which then takes it.
    private var opened: URL?
    private var used: URL?
    private var launched = false
    func application(_ sender: NSApplication, open urls: [URL]) {
        guard let url = urls.first else { return }
        if !launched { opened = url; return }
        if url.standardizedFileURL == used { return }    // the command line's, delivered again
        use(url)
    }

    /// For the release tests: every question the launcher puts on screen,
    /// appended to the file JSRF_LAUNCHER_TRACE names. Nothing else changes.
    private func trace(_ what: String) {
        guard let path = ProcessInfo.processInfo.environment["JSRF_LAUNCHER_TRACE"],
              let data = (what.replacingOccurrences(of: "\n", with: " / ") + "\n").data(using: .utf8)
        else { return }
        if let h = FileHandle(forWritingAtPath: path) {
            h.seekToEndOfFile(); h.write(data); h.closeFile()
        } else {
            FileManager.default.createFile(atPath: path, contents: data)
        }
    }

    // MARK: choosing

    private func ask(reason: String?) {
        if let reason = reason {
            let a = NSAlert()
            a.messageText = "Where is Jet Set Radio Future?"
            a.informativeText = reason
            a.addButton(withTitle: "Choose…")
            a.addButton(withTitle: "Quit")
            trace("[alert] \(a.messageText) | \(reason)")
            if a.runModal() != .alertFirstButtonReturn { NSApp.terminate(nil); return }
        }

        let panel = NSOpenPanel()
        panel.title = "Choose the game"
        panel.message = "Pick the folder of the game's extracted files, or an image of its disc (.iso)."
        panel.prompt = "Use This"
        panel.canChooseDirectories = true
        panel.canChooseFiles = true
        panel.allowsMultipleSelection = false
        // No file-type filter: with allowedFileTypes set, macOS lets the panel
        // choose only such files -- "Use This" on a folder opens it, and inside
        // it the button stays grey, so no folder can be picked at all. Nor are
        // other files greyed out: a file someone takes for the game and cannot
        // even select tells them nothing. Anything can be chosen, and use()
        // says what it is if it is not the game.
        panel.treatsFilePackagesAsDirectories = true

        trace("[picker] \(panel.message ?? "")")
        guard panel.runModal() == .OK, let picked = panel.url else {
            NSApp.terminate(nil); return
        }
        use(picked)
    }

    /// Whatever was picked: the game's folder (or the folder above it), a
    /// folder with the disc image in it, the image, default.xbe itself -- or
    /// something else, which the engine names.
    private func use(_ picked: URL) {
        var isDir: ObjCBool = false
        FileManager.default.fileExists(atPath: picked.path, isDirectory: &isDir)
        if isDir.boolValue {
            if let folder = GameFolder.resolve(picked) {
                start(folder, remembering: picked)
                return
            }
            let images = Library.images(in: picked)
            if images.count == 1 { extract(images[0]); return }
            ask(reason: images.isEmpty
                ? "That folder has no default.xbe in it, so it isn't the game's files: the game's "
                  + "folder has default.xbe and Media in it. Pick that folder, or an image of the "
                  + "disc (.iso)."
                : "That folder has \(images.count) disc images in it. Pick the one of Jet Set Radio Future.")
            return
        }
        if picked.lastPathComponent.lowercased() == "default.xbe" {
            use(picked.deletingLastPathComponent())
            return
        }
        extract(picked)     // a disc image, or the engine says what it is instead
    }

    /// The files are not the game: say what they are, and offer the picker
    /// again. What is remembered is left alone -- a wrong file dropped on the
    /// app says nothing about the folder that worked yesterday.
    private func refuse(_ why: String) {
        NSApp.activate(ignoringOtherApps: true)
        let a = NSAlert()
        a.alertStyle = .warning
        a.messageText = "These files won't work with JSRF Unleashed"
        a.informativeText = why
        a.addButton(withTitle: "Choose Other Files…")
        a.addButton(withTitle: "Quit")
        trace("[alert] \(a.messageText) | \(why)")
        if a.runModal() == .alertFirstButtonReturn { ask(reason: nil) } else { NSApp.terminate(nil) }
    }

    private func extract(_ iso: URL) {
        let dest = Library.extractedFolder(for: iso)
        if let folder = GameFolder.resolve(dest) {   // unpacked on an earlier run
            start(folder, remembering: dest)
            return
        }
        // What the file is, before minutes and gigabytes go into unpacking it:
        // the game's disc, another build of the game, another game, a zip...
        if let why = Game.refusal(["--check-iso", iso.path]) {
            refuse(why)
            return
        }

        let progress = NSAlert()
        progress.messageText = "Unpacking \(iso.lastPathComponent)…"
        progress.informativeText = "This happens once. The files go to:\n\(dest.path)"
        let box = NSView(frame: NSRect(x: 0, y: 0, width: 320, height: 42))
        let bar = NSProgressIndicator(frame: NSRect(x: 0, y: 20, width: 320, height: 20))
        bar.style = .bar
        bar.isIndeterminate = true
        bar.minValue = 0
        bar.maxValue = 1
        bar.startAnimation(nil)
        let label = NSTextField(labelWithString: "")
        label.frame = NSRect(x: 0, y: 0, width: 320, height: 16)
        label.font = NSFont.systemFont(ofSize: NSFont.smallSystemFontSize)
        label.textColor = .secondaryLabelColor
        box.addSubview(bar)
        box.addSubview(label)
        progress.accessoryView = box
        progress.addButton(withTitle: "Cancel")

        try? FileManager.default.removeItem(at: dest)     // what a cancelled run left
        let p = Process()
        p.executableURL = Game.binary
        p.arguments = ["--unpack-iso", iso.path, dest.path]
        let out = Pipe()
        p.standardOutput = out
        p.standardError = FileHandle.nullDevice
        do { try p.run() } catch {
            fail("The disc image couldn't be unpacked.", error.localizedDescription); return
        }

        // The engine prints "progress <done> <total>" lines, then its answer.
        // Read on a thread of its own; the bar is moved on the main thread,
        // which is inside the alert's modal loop and still runs the queue.
        var result: (status: Int32, said: [String])? = nil
        var cancelled = false
        let reader = out.fileHandleForReading
        DispatchQueue.global(qos: .userInitiated).async {
            var pending = Data(), said: [String] = []
            while true {
                let chunk = reader.availableData
                if chunk.isEmpty { break }
                pending.append(chunk)
                while let nl = pending.firstIndex(of: 0x0A) {
                    let line = String(decoding: pending[pending.startIndex..<nl], as: UTF8.self)
                    pending.removeSubrange(pending.startIndex...nl)
                    let f = line.split(separator: " ")
                    if f.count == 3, f[0] == "progress", let done = Double(f[1]),
                       let total = Double(f[2]), total > 0 {
                        DispatchQueue.main.async {
                            bar.isIndeterminate = false
                            bar.doubleValue = done / total
                            label.stringValue = String(format: "%.1f of %.1f GB", done / 1e9, total / 1e9)
                            self.trace("[progress] \(label.stringValue)")
                        }
                    } else {
                        said.append(line)
                    }
                }
            }
            p.waitUntilExit()
            let status = p.terminationStatus
            DispatchQueue.main.async {
                if cancelled { return }          // the alert is gone; nothing to end
                result = (status, said)
                NSApp.abortModal()
            }
        }
        trace("[alert] \(progress.messageText) | \(progress.informativeText)")
        _ = progress.runModal()

        guard let finished = result else {     // Cancel
            cancelled = true
            p.terminate()
            p.waitUntilExit()
            try? FileManager.default.removeItem(at: dest)
            ask(reason: nil)
            return
        }
        let status = finished.status, said = finished.said
        let text = said.filter { $0 != "ok" }.joined(separator: "\n")
            .trimmingCharacters(in: .whitespacesAndNewlines)
        if status == 0, let folder = GameFolder.resolve(dest) {
            start(folder, remembering: dest)
            return
        }
        try? FileManager.default.removeItem(at: dest)
        if [2, 3, 4].contains(status) {
            refuse(text.isEmpty ? "That disc image isn't the game's." : text)
        } else {
            fail("The disc image couldn't be unpacked.",
                 text.isEmpty ? "The unpacker stopped (status \(status))." : text)
        }
    }

    // MARK: running

    /// Hand this process over to the engine.
    ///
    /// Not a subprocess. macOS decides whether a process may put a window on
    /// screen from the bundle it was launched as, and only the bundle's main
    /// executable qualifies -- anything else started out of Contents/MacOS is
    /// a helper tool, and its request to become a foreground application is
    /// refused without an error anyone can see. The engine then runs happily,
    /// plays its music, and draws nothing: sound has no such rule.
    ///
    /// exec replaces the image but keeps the process, so what carries on
    /// running IS the application the system launched, with its window rights
    /// intact. This is the usual shape of a wrapper that has a question to ask
    /// before the real program starts.
    ///
    /// remembering: what to remember once the files pass the check (the
    /// folder that was picked, which may be the one above the game's, or
    /// where an image was unpacked); nil when they are the remembered ones.
    private func start(_ folder: URL, remembering: URL? = nil) {
        // Another build of the game (European, Japanese, a Sega GT 2002 combo
        // disc) loads and then crashes a few seconds in: the code is the
        // standalone US disc's. Say what the files are and offer the picker
        // again rather than start it.
        if let why = Game.refusal(["--check-game", folder.path]) {
            if remembering == nil { GameFolder.forget() }     // they were the remembered ones
            refuse(why)
            return
        }
        if let r = remembering { GameFolder.remember(r) }

        // Write the log where the game lives.
        //
        // logRoot prefers the folder beside the .app, which is right in
        // principle and silently produced nothing in practice -- no log ever
        // appeared there, so every crash still had to be described from
        // memory rather than read. The game folder is the one directory this
        // process is certain it can reach: it was just opened, its contents
        // were just listed, and the engine is about to chdir into it. Use it
        // first, keep the other two as fallbacks, and record which one won.
        let candidates = [folder, Library.logRoot, Library.root]
        let base = candidates.first {
            FileManager.default.isWritableFile(atPath: $0.path)
        } ?? Library.root
        let log = base.appendingPathComponent("last-run.log")
        logURL = log

        // Keep the previous run's log instead of truncating it.
        //
        // The engine's diagnosis of a crash is written here, and the next
        // launch used to open the same path with "w" -- so the ordinary
        // reaction to a crash, launching again, destroyed the only record of
        // it. That happened twice in one afternoon: a crash reported, the app
        // relaunched, and by the time anyone went looking the backtrace, the
        // register dump and the guest call chain were gone.
        //
        // Each run's log is moved aside under a timestamp before the new one
        // opens, and the five most recent are kept: they live in the player's
        // game folder, and a run's log can reach a few tens of MB.
        let fm = FileManager.default
        let history = base.appendingPathComponent("logs")
        try? fm.createDirectory(at: history, withIntermediateDirectories: true)
        if fm.fileExists(atPath: log.path) {
            let stamp = DateFormatter()
            stamp.dateFormat = "yyyyMMdd-HHmmss"
            let name = "run-" + stamp.string(from: Date()) + ".log"
            try? fm.moveItem(at: log, to: history.appendingPathComponent(name))
        }
        if let kept = try? fm.contentsOfDirectory(atPath: history.path)
                              .filter({ $0.hasPrefix("run-") }).sorted(),
           kept.count > 5 {
            for old in kept.prefix(kept.count - 5) {
                try? fm.removeItem(at: history.appendingPathComponent(old))
            }
        }

        for (k, v) in Game.environment(gameDir: folder) { setenv(k, v, 1) }
        FileManager.default.changeCurrentDirectoryPath(folder.path)

        // The engine writes its diagnosis to stdout and stderr; after exec
        // there is nobody left to collect them, so point them at the log now.
        // Both append. "w" twice gave two writers at offset zero; "w" for
        // stdout and "a" for stderr was still one writer at its own offset,
        // and the engine's few stdout lines at startup landed on top of the
        // stderr lines already there. The previous log was moved aside above,
        // so appending starts a new file.
        freopen(log.path, "a", stdout)
        freopen(log.path, "a", stderr)

        let exe = Game.binary.path
        var argv: [UnsafeMutablePointer<CChar>?] =
            [strdup(exe), strdup(folder.path), nil]
        execv(exe, &argv)

        // Only reached if exec failed, which leaves this process intact.
        let err = String(cString: strerror(errno))
        fail("The game wouldn't start.", "\(exe)\n\n\(err)")
    }

    private func fail(_ message: String, _ detail: String) {
        NSApp.activate(ignoringOtherApps: true)
        let a = NSAlert()
        a.alertStyle = .warning
        a.messageText = message
        a.informativeText = detail
        a.addButton(withTitle: "Choose Game…")
        a.addButton(withTitle: "Quit")
        trace("[alert] \(message) | \(detail)")
        if a.runModal() == .alertFirstButtonReturn {
            GameFolder.forget()
            ask(reason: nil)
        } else {
            NSApp.terminate(nil)
        }
    }

}

let app = NSApplication.shared
let delegate = Launcher()
app.delegate = delegate
app.run()
