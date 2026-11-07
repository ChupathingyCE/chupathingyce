/*
The Apple TV's launcher: the iPhone's (LauncherViewController), for the
remote and the big screen. The game's data comes in from a phone or a
computer on the same network (UploadServer: the page's address is on the
screen), or from an address typed in, as on the iPhone; then Play. The
game itself is played with a controller: the remote has too few buttons.
*/

import UIKit

private enum Colors {
  static let background = UIColor(red: 0x0d / 255, green: 0x11 / 255, blue: 0x14 / 255, alpha: 1)
  static let card = UIColor(red: 24 / 255, green: 32 / 255, blue: 38 / 255, alpha: 0.92)
  static let text = UIColor(red: 0xe8 / 255, green: 0xee / 255, blue: 0xf2 / 255, alpha: 1)
  static let muted = UIColor(red: 0x8a / 255, green: 0x9a / 255, blue: 0xa6 / 255, alpha: 1)
  static let accent = UIColor(red: 0x5f / 255, green: 0xb3 / 255, blue: 0x5f / 255, alpha: 1)
  /* the garage sign's pink (art/background.svg) */
  static let neon = UIColor(red: 1, green: 0x8f / 255, blue: 0xb0 / 255, alpha: 1)
  static let error = UIColor(red: 0xe0 / 255, green: 0x6c / 255, blue: 0x5f / 255, alpha: 1)
}

private func gigabytes(_ bytes: Int64) -> String {
  String(format: "%.2f GB", Double(bytes) / 1e9)
}

final class TVLauncherViewController: UIViewController {
  private enum State {
    case idle(error: String?)
    case importing(title: String, status: String, done: Int64, total: Int64, cancel: (() -> Void)?)
  }

  private var state = State.idle(error: nil) {
    didSet { render() }
  }
  private let server = UploadServer()
  private var serverError: String?
  private var download: Download?
  private var lastReport = Date.distantPast
  private let stack = UIStackView()

  override func viewDidLoad() {
    super.viewDidLoad()
    view.backgroundColor = Colors.background

    /* art/background.svg, rendered */
    let background = UIImageView(image: UIImage(named: "background.png"))
    background.contentMode = .scaleAspectFill
    background.clipsToBounds = true
    background.alpha = 0.6
    background.translatesAutoresizingMaskIntoConstraints = false
    view.addSubview(background)

    stack.axis = .vertical
    stack.spacing = 24
    stack.translatesAutoresizingMaskIntoConstraints = false
    view.addSubview(stack)

    NSLayoutConstraint.activate([
      background.topAnchor.constraint(equalTo: view.topAnchor),
      background.bottomAnchor.constraint(equalTo: view.bottomAnchor),
      background.leadingAnchor.constraint(equalTo: view.leadingAnchor),
      background.trailingAnchor.constraint(equalTo: view.trailingAnchor),
      stack.centerXAnchor.constraint(equalTo: view.centerXAnchor),
      stack.centerYAnchor.constraint(equalTo: view.centerYAnchor),
      stack.widthAnchor.constraint(equalToConstant: 1100),
      stack.topAnchor.constraint(greaterThanOrEqualTo: view.safeAreaLayoutGuide.topAnchor),
    ])

    server.received = { [weak self] file, done in self?.imported(file, then: done) }
    server.progress = { [weak self] name, done, total in
      guard let self else { return }
      if total > 0 {
        self.report("Receiving \(name)", "from the upload page", done, total)
      } else if case .importing = self.state {
        self.state = .idle(error: "The upload stopped part way.")
      }
    }
    server.stateChanged = { [weak self] error in
      self?.serverError = error
      self?.render()
    }
    render()
  }

  override func viewDidAppear(_ animated: Bool) {
    super.viewDidAppear(animated)
    server.start()
  }

  override func viewDidDisappear(_ animated: Bool) {
    super.viewDidDisappear(animated)
    server.stop()
  }

  // MARK: - the screen

  private func label(_ text: String, size: CGFloat, color: UIColor, weight: UIFont.Weight = .regular) -> UILabel {
    let label = UILabel()
    label.text = text
    label.font = .systemFont(ofSize: size, weight: weight)
    label.textColor = color
    label.numberOfLines = 0
    return label
  }

  private func button(_ title: String, primary: Bool, action: @escaping () -> Void) -> UIButton {
    var configuration = primary ? UIButton.Configuration.filled() : UIButton.Configuration.bordered()
    configuration.title = title
    configuration.baseBackgroundColor = primary ? Colors.accent : Colors.background
    configuration.baseForegroundColor = primary ? UIColor(red: 0x0b / 255, green: 0x0f / 255, blue: 0x0b / 255, alpha: 1) : Colors.text
    configuration.cornerStyle = .medium
    return UIButton(configuration: configuration, primaryAction: UIAction { _ in action() })
  }

  private func card(_ views: [UIView]) -> UIView {
    let card = UIView()
    card.backgroundColor = Colors.card
    card.layer.cornerRadius = 24
    let inner = UIStackView(arrangedSubviews: views)
    inner.axis = .vertical
    inner.spacing = 20
    inner.translatesAutoresizingMaskIntoConstraints = false
    card.addSubview(inner)
    NSLayoutConstraint.activate([
      inner.topAnchor.constraint(equalTo: card.topAnchor, constant: 40),
      inner.bottomAnchor.constraint(equalTo: card.bottomAnchor, constant: -40),
      inner.leadingAnchor.constraint(equalTo: card.leadingAnchor, constant: 48),
      inner.trailingAnchor.constraint(equalTo: card.trailingAnchor, constant: -48),
    ])
    return card
  }

  private var uploadAddress: String {
    guard let address = UploadServer.address else { return "(the Apple TV is not on a network)" }
    return "http://\(address):\(UploadServer.port)"
  }

  private func render() {
    guard isViewLoaded else { return }
    for view in stack.arrangedSubviews {
      stack.removeArrangedSubview(view)
      view.removeFromSuperview()
    }
    let title = label("ChupathingyCE", size: 48, color: Colors.neon, weight: .bold)
    title.textAlignment = .center
    stack.addArrangedSubview(title)

    switch state {
    case let .importing(title, status, done, total, cancel):
      var views: [UIView] = [label(title, size: 38, color: Colors.text, weight: .semibold)]
      if !status.isEmpty { views.append(label(status, size: 29, color: Colors.muted)) }
      if total > 0 {
        let bar = UIProgressView(progressViewStyle: .default)
        bar.progress = Float(Double(done) / Double(total))
        bar.progressTintColor = Colors.accent
        views.append(bar)
        views.append(label("\(gigabytes(done)) of \(gigabytes(total))", size: 29, color: Colors.muted))
      } else {
        let spinner = UIActivityIndicatorView(style: .large)
        spinner.color = Colors.accent
        spinner.startAnimating()
        views.append(spinner)
      }
      views.append(label("Keep the app open until it is done.", size: 29, color: Colors.muted))
      if let cancel { views.append(button("Cancel", primary: false, action: cancel)) }
      stack.addArrangedSubview(card(views))

    case let .idle(error):
      var views: [UIView]
      if GameData.ready {
        views = [
          label("Ready", size: 38, color: Colors.text, weight: .semibold),
          label("The game's data is in place. Play with a controller (pair it in Settings, Remotes and Devices).",
                size: 29, color: Colors.muted),
        ]
        if let error { views.append(label(error, size: 29, color: Colors.error)) }
        views.append(button("Play", primary: true) { [weak self] in self?.play() })
      } else {
        views = [
          label("The game's data is not here yet", size: 38, color: Colors.text, weight: .semibold),
          label("Send an Xbox disc image of Halo: Combat Evolved (.iso or .xiso), or a .zip of its maps folder, from "
                + "a phone or computer on the same network. Bring your own copy: the app comes with none of the "
                + "game's files.", size: 29, color: Colors.muted),
        ]
        if let error { views.append(label(error, size: 29, color: Colors.error)) }
      }
      views.append(label("Open this on a phone or computer to send files:", size: 29, color: Colors.muted))
      let address = label(uploadAddress, size: 44, color: Colors.neon, weight: .semibold)
      views.append(address)
      if let serverError { views.append(label(serverError, size: 29, color: Colors.error)) }
      views.append(button("Download from a link", primary: !GameData.ready) { [weak self] in self?.askForLink() })
      stack.addArrangedSubview(card(views))
    }

    let footer = label("The Apple TV may clear the game's data when it runs short of room: send it again then.",
                       size: 23, color: Colors.muted)
    footer.textAlignment = .center
    stack.addArrangedSubview(footer)
  }

  private func report(_ title: String, _ status: String, _ done: Int64, _ total: Int64, cancel: (() -> Void)? = nil) {
    let now = Date()
    if now.timeIntervalSince(lastReport) < 0.25 && done < total { return }
    lastReport = now
    state = .importing(title: title, status: status, done: done, total: total, cancel: cancel)
  }

  private func alert(_ title: String, _ message: String) {
    let alert = UIAlertController(title: title, message: message, preferredStyle: .alert)
    alert.addAction(UIAlertAction(title: "OK", style: .default))
    present(alert, animated: true)
  }

  // MARK: - what the buttons do

  private func play() {
    server.stop()
    Game.start { [weak self] error in
      self?.server.start()
      self?.alert("The game could not start", error)
    }
  }

  /* a file the upload page sent, or a download: imported, then deleted */
  private func imported(_ file: URL, then finished: @escaping () -> Void) {
    let name = file.lastPathComponent
    let lower = name.lowercased()
    let disc = lower.hasSuffix(".iso") || lower.hasSuffix(".xiso")
    var report = ImportReport()
    let title = disc ? "Copying the game's data" : "Importing maps"
    state = .importing(title: title, status: name, done: 0, total: 0, cancel: nil)
    UIApplication.shared.isIdleTimerDisabled = true
    DispatchQueue.global(qos: .userInitiated).async {
      var failure: Error?
      let progress: ImportProgress = { status, done, total in
        DispatchQueue.main.async { [weak self] in self?.report(title, status, done, total) }
      }
      do {
        if disc {
          try MapImport.importDisc(file, progress: progress)
        } else {
          report = MapImport.importMaps([file], progress: progress)
        }
      } catch {
        failure = error
      }
      try? FileManager.default.removeItem(at: file)
      DispatchQueue.main.async { [weak self] in
        UIApplication.shared.isIdleTimerDisabled = false
        finished()
        guard let self else { return }
        if let failure {
          self.state = .idle(error: String(describing: failure))
        } else if disc {
          self.state = .idle(error: nil)
          self.alert("The game's data is in place", "The disc's maps and movies are copied. Play when ready.")
        } else {
          self.state = .idle(error: nil)
          self.alert(report.imported.isEmpty ? "Nothing imported" : "Maps imported", report.summary)
        }
      }
    }
  }

  private func askForLink() {
    let prompt = UIAlertController(
      title: "Download from a link",
      message: "The address of a .zip of maps (a maps folder, or Halo PC maps), or of a .map.",
      preferredStyle: .alert)
    prompt.addTextField { field in
      field.placeholder = "https://example.com/maps.zip"
      field.keyboardType = .URL
      field.autocapitalizationType = .none
      field.autocorrectionType = .no
    }
    prompt.addAction(UIAlertAction(title: "Cancel", style: .cancel))
    prompt.addAction(UIAlertAction(title: "Download", style: .default) { [weak self, weak prompt] _ in
      self?.startDownload(prompt?.textFields?.first?.text ?? "")
    })
    present(prompt, animated: true)
  }

  private func startDownload(_ address: String) {
    let url: URL
    switch Download.check(address) {
    case let .success(checked): url = checked
    case let .failure(error):
      state = .idle(error: error.description)
      return
    }
    let title = "Downloading"
    let host = url.host ?? ""
    UIApplication.shared.isIdleTimerDisabled = true
    let cancel: () -> Void = { [weak self] in self?.download?.cancel() }
    state = .importing(title: title, status: host, done: 0, total: 0, cancel: cancel)
    download = Download(
      url,
      progress: { [weak self] written, total in
        self?.report(title, host, written, max(total, 0), cancel: cancel)
      },
      finished: { [weak self] result in
        UIApplication.shared.isIdleTimerDisabled = false
        guard let self else { return }
        self.download = nil
        switch result {
        case nil:
          self.state = .idle(error: nil)
        case let .failure(error)?:
          self.state = .idle(error: error.description)
        case let .success(file)?:
          self.imported(file) {}
        }
      })
  }
}
