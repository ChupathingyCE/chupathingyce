/*
The Apple TV has no Files app and no picker: the game's data comes from a
phone or a computer on the same network instead. While the launcher is up,
the app answers on port 8080 (the TV shows its address): the page there
picks files and sends them over, one at a time, each as its own PUT with
the file as the body, which is written straight to the data folder's
upload/ folder, then handed to the launcher to import (a disc image, a .zip
of maps or a .map) and deleted.

Plain HTTP, on the local network only, as the app's own small server: one
upload at a time, only names the importer takes, and only what fits on the
TV. Nothing is served but the page, and whether the TV is busy.
*/

import Foundation
import Network

final class UploadServer {
  static let port: UInt16 = 8080
  /* what the importer takes (MapImport), by the name's ending */
  static let accepted = [".iso", ".xiso", ".zip", ".map"]
  /* the request line and headers, at most */
  private static let headerLimit = 16 * 1024

  /* a file came in whole; done, called once it is imported, lets the next in */
  var received: ((URL, _ done: @escaping () -> Void) -> Void)?
  /* bytes so far and in all, while one comes in */
  var progress: ((String, Int64, Int64) -> Void)?
  /* why the server could not start (the port taken), or nil once listening */
  var stateChanged: ((String?) -> Void)?

  private let queue = DispatchQueue(label: "upload-server")
  private var listener: NWListener?
  private var busy = false

  func start() {
    guard listener == nil else { return }
    do {
      let listener = try NWListener(using: .tcp, on: NWEndpoint.Port(rawValue: Self.port)!)
      listener.stateUpdateHandler = { [weak self] state in
        let message: String?
        switch state {
        case .ready: message = nil
        case let .failed(error): message = "The upload page could not start: \(error)"
        default: return
        }
        DispatchQueue.main.async { self?.stateChanged?(message) }
      }
      listener.newConnectionHandler = { [weak self] connection in self?.accept(connection) }
      listener.start(queue: queue)
      self.listener = listener
    } catch {
      stateChanged?("The upload page could not start: \(error)")
    }
  }

  func stop() {
    listener?.cancel()
    listener = nil
  }

  /* the TV's address on the network (IPv4, Wi-Fi or Ethernet), for the screen */
  static var address: String? {
    var list: UnsafeMutablePointer<ifaddrs>?
    guard getifaddrs(&list) == 0, let first = list else { return nil }
    defer { freeifaddrs(list) }
    var found: String?
    for entry in sequence(first: first, next: { $0.pointee.ifa_next }) {
      let flags = Int32(entry.pointee.ifa_flags)
      guard let address = entry.pointee.ifa_addr, address.pointee.sa_family == UInt8(AF_INET),
        flags & IFF_UP != 0, flags & IFF_LOOPBACK == 0
      else { continue }
      var host = [CChar](repeating: 0, count: Int(NI_MAXHOST))
      guard getnameinfo(address, socklen_t(address.pointee.sa_len), &host, socklen_t(host.count), nil, 0, NI_NUMERICHOST) == 0
      else { continue }
      let name = String(cString: entry.pointee.ifa_name)
      found = String(cString: host)
      /* (en0, Ethernet or Wi-Fi, before any other) */
      if name == "en0" { break }
    }
    return found
  }

  // MARK: - a connection

  private struct Request {
    let method: String
    let path: String
    let query: [String: String]
    let length: Int64?
  }

  private func accept(_ connection: NWConnection) {
    connection.start(queue: queue)
    readHeader(connection, Data())
  }

  private func readHeader(_ connection: NWConnection, _ buffer: Data) {
    connection.receive(minimumIncompleteLength: 1, maximumLength: Self.headerLimit) { [weak self] data, _, complete, error in
      guard let self else { return }
      var buffer = buffer
      if let data { buffer.append(data) }
      if let end = buffer.range(of: Data("\r\n\r\n".utf8)) {
        let head = String(decoding: buffer[..<end.lowerBound], as: UTF8.self)
        let rest = buffer[end.upperBound...]
        guard let request = Self.parse(head) else {
          self.reply(connection, 400, "That is not a request this page sends.")
          return
        }
        self.handle(connection, request, Data(rest))
      } else if buffer.count > Self.headerLimit || complete || error != nil {
        self.reply(connection, 400, "That is not a request this page sends.")
      } else {
        self.readHeader(connection, buffer)
      }
    }
  }

  private static func parse(_ head: String) -> Request? {
    let lines = head.components(separatedBy: "\r\n")
    let parts = lines.first?.split(separator: " ") ?? []
    guard parts.count >= 2, let target = URLComponents(string: String(parts[1])) else { return nil }
    var query: [String: String] = [:]
    for item in target.queryItems ?? [] { query[item.name] = item.value ?? "" }
    var length: Int64?
    for line in lines.dropFirst() {
      let pair = line.split(separator: ":", maxSplits: 1)
      if pair.count == 2, pair[0].trimmingCharacters(in: .whitespaces).lowercased() == "content-length" {
        length = Int64(pair[1].trimmingCharacters(in: .whitespaces))
      }
    }
    return Request(method: String(parts[0]), path: target.path, query: query, length: length)
  }

  private func handle(_ connection: NWConnection, _ request: Request, _ body: Data) {
    switch (request.method, request.path) {
    case ("GET", "/"):
      reply(connection, 200, Self.page, type: "text/html; charset=utf-8")
    case ("GET", "/busy"):
      /* (the page asks before each file: a refused upload would be sent whole first) */
      reply(connection, 200, busy ? "1" : "0")
    case ("PUT", "/upload"):
      upload(connection, request, body)
    default:
      reply(connection, 404, "Nothing here.")
    }
  }

  private func upload(_ connection: NWConnection, _ request: Request, _ body: Data) {
    let name = request.query["name"] ?? ""
    guard MapImport.isPlainName(name), Self.accepted.contains(where: { name.lowercased().hasSuffix($0) }) else {
      reply(connection, 415, "Send a disc image (.iso, .xiso), a .zip of maps or a .map.")
      return
    }
    guard let length = request.length, length > 0 else {
      reply(connection, 411, "The upload gave no size.")
      return
    }
    /* (with room left for what the import makes of it) */
    let free = GameData.availableSpace
    if free > 0 && length > free - 64_000_000 {
      reply(connection, 507, String(format: "The file is %.1f GB, and the Apple TV has %.1f GB free.",
        Double(length) / 1e9, Double(free) / 1e9))
      return
    }
    guard !busy else {
      reply(connection, 409, "Another file is coming in or being imported: try again once it is done.")
      return
    }
    let folder = GameData.root.appendingPathComponent("upload")
    let partial = folder.appendingPathComponent(name + ".part")
    let target = folder.appendingPathComponent(name)
    let output: FileHandle
    do {
      try FileManager.default.createDirectory(at: folder, withIntermediateDirectories: true)
      _ = FileManager.default.createFile(atPath: partial.path, contents: nil)
      output = try FileHandle(forWritingTo: partial)
    } catch {
      reply(connection, 500, "The Apple TV could not keep the file: \(error.localizedDescription)")
      return
    }
    busy = true
    var written: Int64 = 0
    let report = { [weak self] in
      let done = written
      DispatchQueue.main.async { self?.progress?(name, done, length) }
    }
    let failed = { [weak self] (status: Int, message: String) in
      try? output.close()
      try? FileManager.default.removeItem(at: partial)
      self?.busy = false
      DispatchQueue.main.async { self?.progress?("", 0, 0) }
      self?.reply(connection, status, message)
    }

    func write(_ chunk: Data) -> Bool {
      let take = chunk.prefix(Int(min(Int64(chunk.count), length - written)))
      do {
        try output.write(contentsOf: take)
      } catch {
        failed(500, "The Apple TV could not keep the file: \(error.localizedDescription)")
        return false
      }
      written += Int64(take.count)
      return true
    }

    func finish() {
      do {
        try output.close()
        try? FileManager.default.removeItem(at: target)
        try FileManager.default.moveItem(at: partial, to: target)
      } catch {
        failed(500, "The Apple TV could not keep the file: \(error.localizedDescription)")
        return
      }
      reply(connection, 200, "\(name) is on the Apple TV: importing it there now.")
      DispatchQueue.main.async { [weak self] in
        guard let self else { return }
        if let received = self.received {
          received(target) { self.queue.async { self.busy = false } }
        } else {
          try? FileManager.default.removeItem(at: target)
          self.queue.async { self.busy = false }
        }
      }
    }

    var lastReport = Date.distantPast
    func receive() {
      connection.receive(minimumIncompleteLength: 1, maximumLength: 1 << 20) { data, _, complete, error in
        if let data, !data.isEmpty, !write(data) { return }
        if written >= length {
          report()
          finish()
        } else if complete || error != nil {
          failed(400, "The upload stopped part way.")
        } else {
          if Date().timeIntervalSince(lastReport) > 0.25 {
            lastReport = Date()
            report()
          }
          receive()
        }
      }
    }

    if !body.isEmpty, !write(body) { return }
    report()
    if written >= length { finish() } else { receive() }
  }

  private func reply(_ connection: NWConnection, _ status: Int, _ text: String, type: String = "text/plain; charset=utf-8") {
    let reasons = [200: "OK", 400: "Bad Request", 404: "Not Found", 409: "Conflict", 411: "Length Required",
                   415: "Unsupported Media Type", 500: "Internal Server Error", 507: "Insufficient Storage"]
    let body = Data(text.utf8)
    let head = "HTTP/1.1 \(status) \(reasons[status] ?? "Error")\r\nContent-Type: \(type)\r\n"
      + "Content-Length: \(body.count)\r\nConnection: close\r\nCache-Control: no-store\r\n\r\n"
    connection.send(content: Data(head.utf8) + body, completion: .contentProcessed { _ in connection.cancel() })
  }

  // MARK: - the page

  private static let page = """
    <!doctype html>
    <html lang="en"><head><meta charset="utf-8">
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <title>ChupathingyCE on the Apple TV</title>
    <style>
    :root { color-scheme: dark; }
    body { margin: 0; padding: 24px 16px; background: #0d1114; color: #e8eef2;
      font: 16px/1.5 -apple-system, system-ui, sans-serif; }
    main { max-width: 560px; margin: 0 auto; }
    h1 { font-size: 22px; margin: 0 0 8px; }
    p { color: #8a9aa6; margin: 0 0 16px; }
    label { display: block; background: #5fb35f; color: #0b0f0b; font-weight: 600; text-align: center;
      padding: 14px; border-radius: 10px; cursor: pointer; }
    input { display: none; }
    progress { width: 100%; height: 10px; margin-top: 16px; accent-color: #5fb35f; }
    #log div { margin-top: 8px; overflow-wrap: anywhere; }
    .error { color: #e06c5f; }
    </style></head>
    <body><main>
    <h1>Send the game's data to the Apple TV</h1>
    <p>Pick an Xbox disc image of Halo (.iso or .xiso), a .zip of the disc's maps folder, or Halo PC maps
    (.map or .zip). Keep this page and the app on the TV open until each file is in. Bring your own copy.</p>
    <label>Pick files<input id="files" type="file" multiple accept=".iso,.xiso,.zip,.map"></label>
    <progress id="bar" max="1" value="0" hidden></progress>
    <div id="log"></div>
    </main>
    <script>
    const log = (text, error) => {
      const line = document.createElement("div");
      line.textContent = text;
      if (error) line.className = "error";
      document.getElementById("log").append(line);
    };
    const send = file => new Promise(done => {
      const request = new XMLHttpRequest();
      const bar = document.getElementById("bar");
      bar.hidden = false;
      bar.value = 0;
      request.upload.onprogress = event => { if (event.lengthComputable) bar.value = event.loaded / event.total; };
      request.onload = () => {
        if (request.status !== 409) log(request.responseText, request.status !== 200);
        done(request.status);
      };
      request.onerror = () => { log(file.name + ": the upload stopped (is the app still open on the TV?)", true); done(0); };
      request.open("PUT", "/upload?name=" + encodeURIComponent(file.name));
      request.send(file);
    });
    const wait = ms => new Promise(go => setTimeout(go, ms));
    document.getElementById("files").addEventListener("change", async event => {
      for (const file of event.target.files) {
        log("Sending " + file.name + "...");
        /* (the TV imports one before it takes the next) */
        do {
          while ((await (await fetch("/busy", { cache: "no-store" })).text()) === "1") await wait(3000);
        } while (await send(file) === 409);
      }
      event.target.value = "";
    });
    </script>
    </body></html>
    """
}
