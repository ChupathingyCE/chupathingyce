import Darwin
import ExpoModulesCore
import os
import UIKit

/*
The game's native side, as the iOS launcher sees it: it starts the game
(port/ios/src/ios_main.c, linked in from lib/libhalo.a), and it measures
what the game's memory needs of iOS. With the game linked in, its Xbox
address space is already reserved when the check runs (xbox_memory.c, as
the app loads), so the check then shows what is left beside it.

The check:

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
  return commitContiguous(space)
}

/* the contiguous memory, at 0x80000000 in a reserved address space as on
the Xbox, committed read-write (never touched) */
private func commitContiguous(_ space: UnsafeMutableRawPointer) -> [String: Any] {
  let contiguous = space.advanced(by: 0x8000_0000)
  let committed = mmap(contiguous, Int(contiguousSize), PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON | MAP_FIXED, -1, 0)
  let aligned = UInt64(UInt(bitPattern: space)) & (addressSpaceSize - 1) == 0
  if committed != contiguous {
    return ["ok": false, "step": "commit 512 MB in it", "error": describe(errno), "base": hex(space), "aligned": aligned]
  }
  return ["ok": true, "base": hex(space), "aligned": aligned]
}

/* 4 GB wherever iOS puts it: Xbox address X is then base + X, as the
64-bit builds' conversions compute it, but not the pointer's low 32 bits */
private func probeDirect() -> [String: Any] {
  let (space, error) = reserve(addressSpaceSize)
  guard let space else {
    return ["ok": false, "step": "reserve 4 GB", "error": describe(error)]
  }
  defer { release(space, addressSpaceSize) }
  return commitContiguous(space)
}

/* 4 GB at a 4 GB-aligned address, asked for as a hint at each one from
8 GB up: what the 64-bit builds' layout gives elsewhere */
private func probeAlignedHint() -> [String: Any] {
  var tried = 0
  for index in UInt64(2)...UInt64(64) {
    let wanted = UnsafeMutableRawPointer(bitPattern: UInt(index * addressSpaceSize))
    let result = mmap(wanted, Int(addressSpaceSize), PROT_NONE, MAP_PRIVATE | MAP_ANON, -1, 0)
    tried += 1
    if result == nil || result == failed {
      continue
    }
    if result != wanted {
      munmap(result, Int(addressSpaceSize))
      continue
    }
    defer { release(result!, addressSpaceSize) }
    var report = commitContiguous(result!)
    report["tried"] = tried
    return report
  }
  return ["ok": false, "step": "no aligned 4 GB from 8 to 256 GB", "error": "tried \(tried)"]
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

/* port/ios/include/halo_ios.h's entry point, in lib/libhalo.a. Declared
here rather than through a header, so the pod needs no module map; its
arguments are pointers and a 32-bit integer, passed alike by Swift and C */
@_silgen_name("halo_ios_run")
private func halo_ios_run(
  _ dataRoot: UnsafePointer<CChar>, _ hardwareId: UnsafePointer<CChar>,
  _ error: UnsafeMutablePointer<CChar>, _ errorSize: Int32) -> Int32

public final class HaloNativeModule: Module {
  public func definition() -> ModuleDefinition {
    Name("HaloNative")

    /* starts the game, on the main thread: it does not return once the
    game is running (the process ends with it), so the promise is only
    rejected, with the reason the game could not start */
    AsyncFunction("start") { (promise: Promise) in
      DispatchQueue.main.async {
        let documents = FileManager.default.urls(for: .documentDirectory, in: .userDomainMask)[0].path
        let hardwareId = UIDevice.current.identifierForVendor?.uuidString ?? ""
        var error = [CChar](repeating: 0, count: 1024)
        _ = halo_ios_run(documents, hardwareId, &error, Int32(error.count))
        promise.reject("ERR_HALO_START", String(cString: error))
      }
    }

    AsyncFunction("probeMemory") { () -> [String: Any] in
      return [
        "addressSpace": probeAddressSpace(),
        "direct": probeDirect(),
        "alignedHint": probeAlignedHint(),
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
