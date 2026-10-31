import CoreGraphics
let pids = Set(CommandLine.arguments.dropFirst().compactMap { Int($0) })
let l = CGWindowListCopyWindowInfo([.optionOnScreenOnly], kCGNullWindowID) as! [[String: Any]]
for w in l where pids.contains(w["kCGWindowOwnerPID"] as! Int) {
  let b = w["kCGWindowBounds"] as! [String: Any]
  print(w["kCGWindowNumber"]!, w["kCGWindowOwnerPID"]!, w["kCGWindowOwnerName"] ?? "-", w["kCGWindowName"] ?? "-", b["X"]!, b["Y"]!, b["Width"]!, b["Height"]!)
}
