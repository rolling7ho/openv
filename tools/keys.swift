import CoreGraphics; import Foundation
// usage: keys.swift <keycode> <flags: cmd|ctrl|none>
let code = CGKeyCode(Int(CommandLine.arguments[1])!)
let f: CGEventFlags = CommandLine.arguments[2] == "cmd" ? .maskCommand : CommandLine.arguments[2] == "ctrl" ? .maskControl : []
let modKey: CGKeyCode? = CommandLine.arguments[2] == "cmd" ? 55 : CommandLine.arguments[2] == "ctrl" ? 59 : nil
let src = CGEventSource(stateID: .hidSystemState)
func post(_ k: CGKeyCode, _ d: Bool, _ fl: CGEventFlags) { let e = CGEvent(keyboardEventSource: src, virtualKey: k, keyDown: d)!; e.flags = fl; e.post(tap: .cghidEventTap); usleep(40000) }
if let m = modKey { post(m, true, f) }
post(code, true, f); post(code, false, f)
if let m = modKey { post(m, false, []) }
