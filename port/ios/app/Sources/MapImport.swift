/*
Imports what the player picks: an Xbox disc image, through the engine's own
extractor (chupathingyce's port/linux/src/xiso.c, in libhalo.a), or .map
files and .zip archives of them, as the Android launcher's MapImporter.java
does, Xbox maps included, so that a zip of the maps folder of the player's
own disc can stand in for the disc image.

Each map goes in its family's folder (map_families.c in chupathingyce), by
its header: an Xbox map (cache version 5) in maps; a Custom Edition map
(609) in maps_ce; a Halo PC 1.0 map (7) in maps_md if it is named as
HaloMD's are (<name>_<number>.map), else in maps_pc. Custom Edition's
resource maps (bitmaps.map, sounds.map, loc.map) go in maps_ce. A map
replaces a file of the same name in its folder; it is written as
<name>.part first and renamed once whole, so the game never reads a
half-copied map. From a zip, only the .map files are taken, by their names
alone (never a folder from the archive).

Everything here runs off the main thread; progress is called from there too.
*/

import Foundation

struct ImportError: Error, CustomStringConvertible {
  let description: String
  init(_ description: String) { self.description = description }
}

/* what an import did, for the player */
struct ImportReport {
  var imported: [String] = []
  var skipped: [String] = []

  var summary: String {
    var text =
      imported.isEmpty
      ? "No maps were imported."
      : "Imported \(imported.count) \(imported.count == 1 ? "map" : "maps"):\n" + imported.joined(separator: "\n")
    if !skipped.isEmpty { text += "\n\nLeft out:\n" + skipped.joined(separator: "\n") }
    return text
  }
}

/* status, bytes done and bytes in all */
typealias ImportProgress = (String, Int64, Int64) -> Void

enum MapImport {
  private static let cacheSignature: UInt32 = 0x6865_6164  // 'head'
  private static let xboxVersion: UInt32 = 5
  private static let haloPCVersion: UInt32 = 7
  private static let customEditionVersion: UInt32 = 609
  /* a map file, at most (the largest community maps are a few hundred MB) */
  private static let maximumMapSize: Int64 = 2048 << 20

  /* whether the app writes a file by this name: printable ASCII, none of
  the characters a path or the Files app treats otherwise */
  static func isPlainName(_ name: String) -> Bool {
    if name.isEmpty || name == "." || name == ".." { return false }
    return name.unicodeScalars.allSatisfy { $0.value >= 0x20 && $0.value <= 0x7e && !"/\\:*?\"<>|".unicodeScalars.contains($0) }
  }

  /* the folder a map belongs in, by its name and header; nil to leave it out */
  static func folder(for name: String, header: Data) -> String? {
    let lower = name.lowercased()
    if lower == "bitmaps.map" || lower == "sounds.map" || lower == "loc.map" { return "maps_ce" }
    guard header.count >= 8 else { return nil }
    let bytes = [UInt8](header.prefix(8))
    func field(_ at: Int) -> UInt32 {
      UInt32(bytes[at]) | UInt32(bytes[at + 1]) << 8 | UInt32(bytes[at + 2]) << 16 | UInt32(bytes[at + 3]) << 24
    }
    guard field(0) == cacheSignature else { return nil }
    switch field(4) {
    case xboxVersion: return "maps"
    case customEditionVersion: return "maps_ce"
    case haloPCVersion: return lower.range(of: "_[0-9]+\\.map$", options: .regularExpression) != nil ? "maps_md" : "maps_pc"
    default: return nil
    }
  }

  /* (thrown from a map's first bytes: the rest of it is not read) */
  private struct NotAMap: Error {}

  /* One map's data into its folder, a chunk at a time: feed gets a writer
  to call with each chunk in turn. The first 8 bytes decide the folder. */
  private static func importMap(
    named name: String, from: String, report: inout ImportReport, status: @escaping (String) -> Void,
    feed: ((Data) throws -> Void) throws -> Void
  ) throws {
    let manager = FileManager.default
    var output: FileHandle?
    var partial: URL?
    var target: URL?
    var written: Int64 = 0
    /* (a header in pieces: kept until 8 bytes are in) */
    var head = Data()

    /* the map's folder and its .part file, by its header */
    func open(_ header: Data) throws {
      guard let folder = folder(for: name, header: header) else { throw NotAMap() }
      let folderURL = GameData.root.appendingPathComponent(folder)
      try manager.createDirectory(at: folderURL, withIntermediateDirectories: true)
      target = folderURL.appendingPathComponent(name)
      partial = folderURL.appendingPathComponent(name + ".part")
      status("Copying \(name) to \(folder)")
      guard manager.createFile(atPath: partial!.path, contents: nil) else {
        throw ImportError("it could not be written to \(folder)")
      }
      output = try FileHandle(forWritingTo: partial!)
      written = Int64(header.count)
      try output!.write(contentsOf: header)
    }

    do {
      try feed { chunk in
        guard let output else {
          head.append(chunk)
          if head.count >= 8 { try open(head) }
          return
        }
        written += Int64(chunk.count)
        if written > maximumMapSize { throw ImportError("larger than any map") }
        try output.write(contentsOf: chunk)
      }
      /* (shorter than a header: only a resource map's name can place it) */
      if output == nil { try open(head) }
      try output!.close()
      _ = try? manager.removeItem(at: target!)
      try manager.moveItem(at: partial!, to: target!)
      report.imported.append("\(target!.deletingLastPathComponent().lastPathComponent)/\(name)")
    } catch is NotAMap {
      report.skipped.append("\(from)\(name): not a Halo map")
    } catch {
      try? output?.close()
      if let partial { try? manager.removeItem(at: partial) }
      throw error
    }
  }

  /* Imports every .map in the picked files (a .map, or a .zip of them) into
  its folder. The report says what went where and what was left out, and
  why. */
  static func importMaps(_ files: [URL], progress: @escaping ImportProgress) -> ImportReport {
    var report = ImportReport()
    let sizes = files.map { Int64((try? $0.resourceValues(forKeys: [.fileSizeKey]).fileSize) ?? 0) }
    let total = sizes.reduce(0, +)
    var done: Int64 = 0
    var statusText = ""
    let consumed = { (bytes: Int) in
      done += Int64(bytes)
      progress(statusText, done, total)
    }
    let show = { (text: String) in
      statusText = text
      progress(statusText, done, total)
    }

    for (file, size) in zip(files, sizes) {
      let name = file.lastPathComponent
      let lower = name.lowercased()
      let start = done
      do {
        let handle = try FileHandle(forReadingFrom: file)
        defer { try? handle.close() }
        if lower.hasSuffix(".zip") {
          show("Reading \(name)")
          let archive = ZipReader(handle: handle, size: UInt64(size))
          let entries: [ZipEntry]
          do {
            entries = try archive.entries()
          } catch {
            throw ImportError(String(describing: error))
          }
          var found = 0
          for entry in entries {
            let entryName = String(entry.name.split(separator: "/", omittingEmptySubsequences: false).last ?? "")
            if entry.directory || !entryName.lowercased().hasSuffix(".map") { continue }
            found += 1
            guard isPlainName(entryName) else {
              report.skipped.append("\(name): \(entry.name): not a name the app writes")
              continue
            }
            do {
              try importMap(named: entryName, from: "\(name): ", report: &report, status: show) { write in
                try archive.readEntry(entry, consumed: consumed, output: write)
              }
            } catch {
              report.skipped.append("\(name): \(entryName): \(error)")
            }
          }
          if found == 0 { report.skipped.append("\(name): no .map files in it") }
        } else if lower.hasSuffix(".map") {
          guard isPlainName(name) else { throw ImportError("not a name the app writes") }
          try importMap(named: name, from: "", report: &report, status: show) { write in
            while let data = try handle.read(upToCount: ZipReader.readChunkSize), !data.isEmpty {
              consumed(data.count)
              try write(data)
            }
          }
        } else {
          report.skipped.append("\(name): not a .map or .zip file")
        }
      } catch {
        report.skipped.append("\(name): \(error)")
      }
      /* (what was skipped in it counts as done) */
      done = start + size
      progress(statusText, done, total)
    }
    return report
  }

  /* The disc's maps folder, then its movies, into the data folder (xiso.c's
  xiso_extract_maps); throws with the reason, for the player */
  static func importDisc(_ image: URL, progress: @escaping ImportProgress) throws {
    guard xiso_is_image(image.path) != 0 else {
      throw ImportError("\(image.lastPathComponent) is not an Xbox disc image.")
    }
    let space = GameData.availableSpace
    if space > 0 && space < GameData.spaceNeeded {
      throw ImportError(
        String(
          format: "The game's data needs about %.1f GB free, and the device has %.1f GB.",
          Double(GameData.spaceNeeded) / 1e9, Double(space) / 1e9))
    }
    final class Context {
      let progress: ImportProgress
      init(_ progress: @escaping ImportProgress) { self.progress = progress }
    }
    let context = Context(progress)
    var error = [CChar](repeating: 0, count: 512)
    let succeeded = withExtendedLifetime(context) {
      xiso_extract_maps(
        image.path, GameData.root.path,
        { context, file, done, total in
          guard let context else { return }
          let box = Unmanaged<Context>.fromOpaque(context).takeUnretainedValue()
          box.progress(file.map { String(cString: $0) } ?? "", Int64(done), Int64(total))
        }, Unmanaged.passUnretained(context).toOpaque(), &error, Int32(error.count))
    }
    if succeeded == 0 { throw ImportError(String(cString: error)) }
  }
}
