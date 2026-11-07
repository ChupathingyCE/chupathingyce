/*
Reads the files of a .zip archive the player picks: the central directory
at its end (with zip64's, for archives and files past 4 GB), then each
file's data, stored or deflated (the system's zlib), checked against its
CRC-32 as it is read.

Encrypted files, and methods other than stored and deflate, are listed with
a reason and not read.
*/

import Foundation

struct ZipError: Error, CustomStringConvertible {
  let description: String
  init(_ description: String) { self.description = description }

  static let damaged = ZipError("The archive is damaged.")
  static let cutOff = ZipError("The archive is cut off (is it complete?).")
}

struct ZipEntry {
  /* the name as the archive has it, '/'-separated folders and all */
  var name: String
  var directory: Bool
  var method: UInt16
  var encrypted: Bool
  var crc: UInt32
  var compressedSize: UInt64
  var size: UInt64
  var localOffset: UInt64
}

/* little-endian fields of bytes read from the archive */
private extension Array where Element == UInt8 {
  func u16(_ at: Int) -> UInt16 { UInt16(self[at]) | UInt16(self[at + 1]) << 8 }
  func u32(_ at: Int) -> UInt32 { UInt32(u16(at)) | UInt32(u16(at + 2)) << 16 }
  func u64(_ at: Int) -> UInt64 { UInt64(u32(at)) | UInt64(u32(at + 4)) << 32 }
}

final class ZipReader {
  static let readChunkSize = 1 << 20

  private static let endSignature: UInt32 = 0x0605_4b50
  private static let zip64LocatorSignature: UInt32 = 0x0706_4b50
  private static let zip64EndSignature: UInt32 = 0x0606_4b50
  private static let centralSignature: UInt32 = 0x0201_4b50
  private static let localSignature: UInt32 = 0x0403_4b50
  private static let endSize = 22
  private static let maximumComment = 0xffff
  private static let centralHeaderSize = 46
  private static let localHeaderSize = 30
  private static let zip64Extra: UInt16 = 0x0001
  static let stored: UInt16 = 0
  static let deflate: UInt16 = 8
  /* central directories of a few thousand files are well under this */
  private static let maximumDirectorySize: UInt64 = 64 << 20

  private let handle: FileHandle
  let size: UInt64

  init(handle: FileHandle, size: UInt64) {
    self.handle = handle
    self.size = size
  }

  /* exactly length bytes at offset, or ZipError.cutOff */
  private func read(_ offset: UInt64, _ length: Int) throws -> [UInt8] {
    try handle.seek(toOffset: offset)
    var bytes = [UInt8]()
    bytes.reserveCapacity(length)
    while bytes.count < length {
      guard let data = try handle.read(upToCount: length - bytes.count), !data.isEmpty else {
        throw ZipError.cutOff
      }
      bytes.append(contentsOf: data)
    }
    return bytes
  }

  /* the central directory's offset, size and entry count */
  private func findDirectory() throws -> (UInt64, UInt64, UInt64) {
    let tailSize = Int(min(size, UInt64(Self.endSize + Self.maximumComment)))
    let tailStart = size - UInt64(tailSize)
    let tail = try read(tailStart, tailSize)
    var at = tailSize - Self.endSize
    while at >= 0 {
      defer { at -= 1 }
      if tail.u32(at) != Self.endSignature { continue }
      var count = UInt64(tail.u16(at + 10))
      var directorySize = UInt64(tail.u32(at + 12))
      var directoryOffset = UInt64(tail.u32(at + 16))
      /* zip64: the locator just before the end record gives its own */
      if count == 0xffff || directorySize == 0xffff_ffff || directoryOffset == 0xffff_ffff {
        guard tailStart + UInt64(at) >= 20 else { throw ZipError.damaged }
        let locator = try read(tailStart + UInt64(at) - 20, 20)
        guard locator.u32(0) == Self.zip64LocatorSignature else { throw ZipError.damaged }
        let end = try read(locator.u64(8), 56)
        guard end.u32(0) == Self.zip64EndSignature else { throw ZipError.damaged }
        count = end.u64(32)
        directorySize = end.u64(40)
        directoryOffset = end.u64(48)
      }
      guard directoryOffset <= size, directorySize <= size - directoryOffset,
        directorySize <= Self.maximumDirectorySize
      else { throw ZipError.damaged }
      return (directoryOffset, directorySize, count)
    }
    throw ZipError("This is not a .zip archive.")
  }

  /* the files and folders in the archive, in the order of its directory */
  func entries() throws -> [ZipEntry] {
    let (offset, directorySize, count) = try findDirectory()
    let directory = try read(offset, Int(directorySize))
    var entries: [ZipEntry] = []
    var at = 0
    for _ in 0..<count {
      guard at + Self.centralHeaderSize <= directory.count, directory.u32(at) == Self.centralSignature else {
        throw ZipError.damaged
      }
      let nameLength = Int(directory.u16(at + 28))
      let extraLength = Int(directory.u16(at + 30))
      let commentLength = Int(directory.u16(at + 32))
      let next = at + Self.centralHeaderSize + nameLength + extraLength + commentLength
      guard next <= directory.count else { throw ZipError.damaged }

      let nameStart = at + Self.centralHeaderSize
      /* (a name byte at a time, as Latin-1: only plain names are written) */
      let name = String(directory[nameStart..<nameStart + nameLength].map { Character(Unicode.Scalar($0)) })
      var entry = ZipEntry(
        name: name, directory: name.hasSuffix("/"), method: directory.u16(at + 10),
        encrypted: directory.u16(at + 8) & 1 != 0, crc: directory.u32(at + 16),
        compressedSize: UInt64(directory.u32(at + 20)), size: UInt64(directory.u32(at + 24)),
        localOffset: UInt64(directory.u32(at + 42)))

      /* zip64's extra field holds, in this order, those of the three that
      are 0xffffffff here */
      var extra = nameStart + nameLength
      let extraEnd = extra + extraLength
      while extra + 4 <= extraEnd {
        let id = directory.u16(extra)
        let length = Int(directory.u16(extra + 2))
        if id == Self.zip64Extra {
          var field = extra + 4
          func take() throws -> UInt64 {
            guard field + 8 <= extra + 4 + length, field + 8 <= directory.count else { throw ZipError.damaged }
            defer { field += 8 }
            return directory.u64(field)
          }
          if entry.size == 0xffff_ffff { entry.size = try take() }
          if entry.compressedSize == 0xffff_ffff { entry.compressedSize = try take() }
          if entry.localOffset == 0xffff_ffff { entry.localOffset = try take() }
        }
        extra += 4 + length
      }
      entries.append(entry)
      at = next
    }
    return entries
  }

  /* An entry's data, a chunk at a time, to output; consumed is told the
  archive's bytes read as it goes. Throws ZipError when the data does not
  inflate, is not the size the directory gives, or fails its CRC. */
  func readEntry(_ entry: ZipEntry, consumed: (Int) -> Void, output: (Data) throws -> Void) throws {
    if entry.encrypted { throw ZipError("it is encrypted") }
    guard entry.method == Self.stored || entry.method == Self.deflate else {
      throw ZipError("it is compressed with a method the app does not read (\(entry.method))")
    }

    let local = try read(entry.localOffset, Self.localHeaderSize)
    guard local.u32(0) == Self.localSignature else { throw ZipError.damaged }
    var offset = entry.localOffset + UInt64(Self.localHeaderSize) + UInt64(local.u16(26)) + UInt64(local.u16(28))
    var remaining = entry.compressedSize
    var crc: uLong = 0
    var written: UInt64 = 0

    func emit(_ chunk: Data) throws {
      written += UInt64(chunk.count)
      if written > entry.size { throw ZipError("it is larger than the archive says") }
      chunk.withUnsafeBytes { bytes in
        crc = crc32(crc, bytes.bindMemory(to: Bytef.self).baseAddress, uInt(bytes.count))
      }
      try output(chunk)
    }

    if entry.method == Self.stored {
      while remaining > 0 {
        let length = Int(min(remaining, UInt64(Self.readChunkSize)))
        let bytes = try read(offset, length)
        offset += UInt64(length)
        remaining -= UInt64(length)
        consumed(length)
        try emit(Data(bytes))
      }
    } else {
      var stream = z_stream()
      /* (raw deflate: a zip's entries have no zlib header) */
      guard inflateInit2_(&stream, -MAX_WBITS, ZLIB_VERSION, Int32(MemoryLayout<z_stream>.size)) == Z_OK else {
        throw ZipError("it could not be unpacked")
      }
      defer { inflateEnd(&stream) }
      var out = [UInt8](repeating: 0, count: Self.readChunkSize)
      var ended = false
      while !ended {
        /* (the archive's data used up before the deflate stream's end) */
        guard remaining > 0 else { throw ZipError("it is cut off in the archive") }
        let length = Int(min(remaining, UInt64(Self.readChunkSize)))
        var input = try read(offset, length)
        offset += UInt64(length)
        remaining -= UInt64(length)
        consumed(length)
        try input.withUnsafeMutableBufferPointer { inBuffer in
          stream.next_in = inBuffer.baseAddress
          stream.avail_in = uInt(inBuffer.count)
          defer { stream.next_in = nil }
          /* on until this input is used up and the output has room left */
          while true {
            let status: Int32 = out.withUnsafeMutableBufferPointer { outBuffer in
              stream.next_out = outBuffer.baseAddress
              stream.avail_out = uInt(outBuffer.count)
              return inflate(&stream, Z_NO_FLUSH)
            }
            let produced = out.count - Int(stream.avail_out)
            if produced > 0 { try emit(Data(out[0..<produced])) }
            if status == Z_STREAM_END {
              ended = true
              return
            }
            guard status == Z_OK || status == Z_BUF_ERROR else { throw ZipError("it is damaged in the archive") }
            if stream.avail_in == 0 && stream.avail_out != 0 { return }
          }
        }
      }
    }
    if written != entry.size { throw ZipError("it is cut off in the archive") }
    if UInt32(truncatingIfNeeded: crc) != entry.crc {
      throw ZipError("it is damaged in the archive (its CRC does not match)")
    }
  }
}
