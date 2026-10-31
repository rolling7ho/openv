import Foundation
import CoreGraphics
let k = CGKeyCode(Int(CommandLine.arguments[1])!)
let src = CGEventSource(stateID: .hidSystemState)
CGEvent(keyboardEventSource: src, virtualKey: k, keyDown: true)!.post(tap: .cghidEventTap)
usleep(120000)
CGEvent(keyboardEventSource: src, virtualKey: k, keyDown: false)!.post(tap: .cghidEventTap)
