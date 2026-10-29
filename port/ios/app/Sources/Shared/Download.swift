/*
Downloads a .zip of maps (or a .map) from an address the player gives, into
the app's caches, for MapImport to import and then delete. The app knows no
address of its own: the player brings the file, as with the disc image.
*/

import Foundation

final class Download: NSObject, URLSessionDownloadDelegate {
  /* the address as one to download from, or why it is not */
  static func check(_ text: String) -> Result<URL, ImportError> {
    guard let url = URL(string: text.trimmingCharacters(in: .whitespacesAndNewlines)), url.host != nil else {
      return .failure(ImportError("That is not a web address."))
    }
    guard url.scheme == "https" || url.scheme == "http" else {
      return .failure(ImportError("The address must start with https://."))
    }
    return .success(url)
  }

  /* the name the import goes by: the address's, a .map kept as one and
  anything else taken as a .zip (the importer decides by the name) */
  static func name(for url: URL) -> String {
    /* (an address of a site's root has "/" for its last part) */
    var last = url.lastPathComponent == "/" ? "" : url.lastPathComponent
    last = last.removingPercentEncoding ?? last
    last = String(last.unicodeScalars.map {
      CharacterSet.alphanumerics.contains($0) && $0.isASCII || "._ -".unicodeScalars.contains($0) ? Character($0) : "_"
    })
    while last.hasPrefix(".") { last.removeFirst() }
    let lower = last.lowercased()
    if lower.hasSuffix(".map") || lower.hasSuffix(".zip") { return last }
    return (last.isEmpty ? "download" : last) + ".zip"
  }

  private let name: String
  private let progress: (Int64, Int64) -> Void
  private let finished: (Result<URL, ImportError>?) -> Void
  private var session: URLSession!
  private var task: URLSessionDownloadTask?
  private var tooLarge: Int64 = 0
  private var cancelled = false

  /* Starts the download. progress gets the bytes so far and the total (-1
  when the server does not say); finished the downloaded file, nil when
  cancelled, or why it failed; both on the main queue. A file larger than
  half the free room is stopped as soon as its size is known: its maps take
  that much again. */
  init(
    _ url: URL, progress: @escaping (Int64, Int64) -> Void,
    finished: @escaping (Result<URL, ImportError>?) -> Void
  ) {
    name = Self.name(for: url)
    self.progress = progress
    self.finished = finished
    super.init()
    session = URLSession(configuration: .default, delegate: self, delegateQueue: .main)
    task = session.downloadTask(with: url)
    task?.resume()
  }

  func cancel() {
    cancelled = true
    task?.cancel()
  }

  func urlSession(
    _ session: URLSession, downloadTask: URLSessionDownloadTask, didWriteData bytesWritten: Int64,
    totalBytesWritten: Int64, totalBytesExpectedToWrite: Int64
  ) {
    let free = GameData.availableSpace
    if tooLarge == 0 && totalBytesExpectedToWrite > 0 && free > 0
      && Double(totalBytesExpectedToWrite) * 2.05 > Double(free + totalBytesWritten)
    {
      tooLarge = totalBytesExpectedToWrite
      downloadTask.cancel()
      return
    }
    progress(totalBytesWritten, totalBytesExpectedToWrite)
  }

  func urlSession(_ session: URLSession, downloadTask: URLSessionDownloadTask, didFinishDownloadingTo location: URL) {
    /* (the file is the session's until this returns: moved out now) */
    let destination = FileManager.default.temporaryDirectory
      .appendingPathComponent("download-\(Int(Date().timeIntervalSince1970))-\(name)")
    if let response = downloadTask.response as? HTTPURLResponse, !(200..<300).contains(response.statusCode) {
      finish(.failure(ImportError("The download failed: the server answered \(response.statusCode).")))
      return
    }
    do {
      try FileManager.default.moveItem(at: location, to: destination)
      finish(.success(destination))
    } catch {
      finish(.failure(ImportError("The download could not be kept: \(error.localizedDescription)")))
    }
  }

  func urlSession(_ session: URLSession, task: URLSessionTask, didCompleteWithError error: Error?) {
    guard let error else { return }
    if tooLarge > 0 {
      let free = Double(GameData.availableSpace) / 1e9
      finish(.failure(ImportError(String(
        format: "The file is %.1f GB: the download and its maps need about %.1f GB free, and the device has %.1f GB.",
        Double(tooLarge) / 1e9, Double(tooLarge) * 2.05 / 1e9, free))))
    } else if cancelled {
      finish(nil)
    } else {
      finish(.failure(ImportError("The download failed: \(error.localizedDescription)")))
    }
  }

  private func finish(_ result: Result<URL, ImportError>?) {
    session.finishTasksAndInvalidate()
    finished(result)
  }
}
