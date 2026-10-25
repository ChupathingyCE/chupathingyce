import Darwin
import ExpoModulesCore
import os
import UIKit

/*
The game's native side, as the iOS launcher sees it. For now it only
measures what the game's memory needs of iOS, which decides how the game is
built for it:

- the 64-bit builds' Xbox address space (chupathingyce's
  source/cseries/xbox_address.h, port/linux/src/xbox_memory.c): 4 GB
  reserved, aligned to 4 GB, by reserving 8 GB and keeping the aligned half,
  as xbox_memory.c does where it cannot ask for a fixed address; then the
  desktop's contiguous memory (PLATFORM_CONTIGUOUS_SIZE, 512 MB) committed
  read-write inside it, never touched, so nothing is really used;
- the console window (XBOX_WINDOW_LOW to XBOX_WINDOW_HIGH, 256 MB), for the
  case where 4 GB is not given: where it lands (it must be above 4 GB here,
  as iOS keeps the low 4 GB of a 64-bit app);
- the largest single reservation iOS gives, to the nearest 256 MB;
- the memory the system lets the app use before it is stopped
  (os_proc_available_memory), and the device's.

Every reservation is unmapped before the next.
*/

private let gigabyte: UInt64 = 1 << 30
private let addressSpaceSize: UInt64 = 4 * gigabyte
private let contiguousSize: UInt64 = 0x2000_0000
private let windowSize: UInt64 = 0x1000_0000
private let failed = UnsafeMutableRawPointer(bitPattern: -1)

private func reserve(_ size: UInt64) -> (UnsafeMutableRawPointer?, Int32) {
  let result = mmap(nil, Int(size), PROT_NONE, MAP_PRIVATE | MAP_ANON, -1, 0)
  if result == nil || result == failed {
    return (nil, errno)
  }
  return (result, 0)
}

private func release(_ pointer: UnsafeMutableRawPointer, _ size: UInt64) {
  munmap(pointer, Int(size))
}

private func describe(_ error: Int32) -> String {
  return String(cString: strerror(error))
}

private func hex(_ pointer: UnsafeMutableRawPointer) -> String {
  return "0x" + String(UInt(bitPattern: pointer), radix: 16)
}

/* the 64-bit builds' address space: 8 GB reserved, the 4 GB-aligned half kept */
private func probeAddressSpace() -> [String: Any] {
  let (twice, error) = reserve(2 * addressSpaceSize)
  guard let twice else {
    return ["ok": false, "step": "reserve 8 GB", "error": describe(error)]
  }
  let start = UInt64(UInt(bitPattern: twice))
  let base = (start + addressSpaceSize - 1) & ~(addressSpaceSize - 1)
  let end = start + 2 * addressSpaceSize
  if base > start {
    munmap(twice, Int(base - start))
  }
  if base + addressSpaceSize < end {
    munmap(UnsafeMutableRawPointer(bitPattern: UInt(base + addressSpaceSize)), Int(end - (base + addressSpaceSize)))
  }
  let space = UnsafeMutableRawPointer(bitPattern: UInt(base))!
  defer { release(space, addressSpaceSize) }

  /* the contiguous memory, at 0x80000000 in it as on the Xbox */
  let contiguous = space.advanced(by: 0x8000_0000)
  let committed = mmap(contiguous, Int(contiguousSize), PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON | MAP_FIXED, -1, 0)
  if committed != contiguous {
    return ["ok": false, "step": "commit 512 MB in it", "error": describe(errno), "base": hex(space)]
  }
  return ["ok": true, "base": hex(space)]
}

/* the console window: 256 MB wherever there is room */
private func probeWindow() -> [String: Any] {
  let (window, error) = reserve(windowSize)
  guard let window else {
    return ["ok": false, "error": describe(error)]
  }
  defer { release(window, windowSize) }
  let address = UInt64(UInt(bitPattern: window))
  return ["ok": true, "base": hex(window), "belowFourGigabytes": address + windowSize <= addressSpaceSize]
}

/* the largest single reservation, to the nearest 256 MB, up to 64 GB */
private func probeLargest() -> UInt64 {
  var low: UInt64 = 0
  var high: UInt64 = 64 * gigabyte / windowSize
  while low < high {
    let middle = (low + high + 1) / 2
    let (pointer, _) = reserve(middle * windowSize)
    if let pointer {
      release(pointer, middle * windowSize)
      low = middle
    } else {
      high = middle - 1
    }
  }
  return low * windowSize
}

public final class HaloNativeModule: Module {
  public func definition() -> ModuleDefinition {
    Name("HaloNative")

    AsyncFunction("probeMemory") { () -> [String: Any] in
      return [
        "addressSpace": probeAddressSpace(),
        "window": probeWindow(),
        "largestReservation": Double(probeLargest()),
        "availableMemory": Double(os_proc_available_memory()),
        "physicalMemory": Double(ProcessInfo.processInfo.physicalMemory),
        "system": "\(UIDevice.current.systemName) \(UIDevice.current.systemVersion)",
        "model": deviceModel(),
      ]
    }
  }
}

/* the model's identifier (iPhone14,8 is an iPhone 14 Plus) */
private func deviceModel() -> String {
  var info = utsname()
  uname(&info)
  return withUnsafeBytes(of: &info.machine) { bytes in
    String(decoding: bytes.prefix(while: { $0 != 0 }), as: UTF8.self)
  }
}
