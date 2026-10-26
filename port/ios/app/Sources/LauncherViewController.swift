/*
The launcher: imports the game's data on the first start (a disc image, or
a zip of the disc's maps folder), imports Halo PC maps, picked or downloaded
from an address, then starts the game.
*/

import UIKit
import UniformTypeIdentifiers

private enum Colors {
  static let background = UIColor(red: 0x0d / 255, green: 0x11 / 255, blue: 0x14 / 255, alpha: 1)
  /* a card over the background picture: the surface, the night showing through */
  static let card = UIColor(red: 24 / 255, green: 32 / 255, blue: 38 / 255, alpha: 0.9)
  static let text = UIColor(red: 0xe8 / 255, green: 0xee / 255, blue: 0xf2 / 255, alpha: 1)
  static let muted = UIColor(red: 0x8a / 255, green: 0x9a / 255, blue: 0xa6 / 255, alpha: 1)
  static let accent = UIColor(red: 0x5f / 255, green: 0xb3 / 255, blue: 0x5f / 255, alpha: 1)
  /* the garage sign's pink (art/background.svg) */
  static let neon = UIColor(red: 1, green: 0x8f / 255, blue: 0xb0 / 255, alpha: 1)
  static let error = UIColor(red: 0xe0 / 255, green: 0x6c / 255, blue: 0x5f / 255, alpha: 1)
}

/* a line at the top, a new one each time the app opens */
private let quips = [
  "Leave the plasma pistol. Take the gabagool.",
  "Fuhgeddaboudit. The maps are in good hands.",
  "Nobody touches ui.map. Capisce?",
  "Respawn? In this family, we drive.",
  "I got a buggy, a jacket and a bad feeling about this ring.",
  "Back in the day, a man copied his own disc.",
  "You come into my garage, on the day of the LAN party...",
  "With all due respect, the Flood are not family.",
]

private func gigabytes(_ bytes: Int64) -> String {
  String(format: "%.2f GB", Double(bytes) / 1e9)
}

final class LauncherViewController: UIViewController, UIDocumentPickerDelegate, UITextFieldDelegate {
  private enum State {
    case idle(error: String?)
    case importing(title: String, status: String, done: Int64, total: Int64, cancel: (() -> Void)?)
  }

  private enum Picking {
    case disc, maps
  }

  private var state = State.idle(error: nil) {
    didSet { render() }
  }
  private var picking = Picking.disc
  private var download: Download?
  private weak var downloadButton: UIButton?
  private var lastReport = Date.distantPast
  private let quip = quips.randomElement()!

  private let scroll = UIScrollView()
  private let stack = UIStackView()
  private lazy var addressField: UITextField = {
    let field = UITextField()
    field.placeholder = "https://example.com/maps.zip"
    field.attributedPlaceholder = NSAttributedString(
      string: "https://example.com/maps.zip", attributes: [.foregroundColor: Colors.muted])
    field.textColor = Colors.text
    field.backgroundColor = Colors.background
    field.layer.cornerRadius = 8
    field.font = .systemFont(ofSize: 15)
    field.keyboardType = .URL
    field.textContentType = .URL
    field.autocapitalizationType = .none
    field.autocorrectionType = .no
    field.returnKeyType = .go
    field.leftView = UIView(frame: CGRect(x: 0, y: 0, width: 12, height: 1))
    field.leftViewMode = .always
    field.heightAnchor.constraint(equalToConstant: 44).isActive = true
    field.delegate = self
    /* (only the button changes: a new screen would take the keyboard away) */
    field.addAction(UIAction { [weak self] _ in self?.updateDownloadButton() }, for: .editingChanged)
    return field
  }()

  override func viewDidLoad() {
    super.viewDidLoad()
    view.backgroundColor = Colors.background

    /* art/background.svg, rendered */
    let background = UIImageView(image: UIImage(named: "background.png"))
    background.contentMode = .scaleAspectFill
    background.clipsToBounds = true
    background.translatesAutoresizingMaskIntoConstraints = false
    view.addSubview(background)

    scroll.translatesAutoresizingMaskIntoConstraints = false
    scroll.alwaysBounceVertical = true
    scroll.keyboardDismissMode = .interactive
    view.addSubview(scroll)

    stack.axis = .vertical
    stack.spacing = 16
    stack.translatesAutoresizingMaskIntoConstraints = false
    scroll.addSubview(stack)

    let readable = stack.widthAnchor.constraint(equalTo: scroll.frameLayoutGuide.widthAnchor, constant: -32)
    readable.priority = .defaultHigh
    NSLayoutConstraint.activate([
      background.topAnchor.constraint(equalTo: view.topAnchor),
      background.bottomAnchor.constraint(equalTo: view.bottomAnchor),
      background.leadingAnchor.constraint(equalTo: view.leadingAnchor),
      background.trailingAnchor.constraint(equalTo: view.trailingAnchor),
      scroll.topAnchor.constraint(equalTo: view.safeAreaLayoutGuide.topAnchor),
      scroll.leadingAnchor.constraint(equalTo: view.safeAreaLayoutGuide.leadingAnchor),
      scroll.trailingAnchor.constraint(equalTo: view.safeAreaLayoutGuide.trailingAnchor),
      scroll.bottomAnchor.constraint(equalTo: view.keyboardLayoutGuide.topAnchor),
      stack.topAnchor.constraint(equalTo: scroll.contentLayoutGuide.topAnchor, constant: 16),
      stack.bottomAnchor.constraint(equalTo: scroll.contentLayoutGuide.bottomAnchor, constant: -16),
      stack.centerXAnchor.constraint(equalTo: scroll.frameLayoutGuide.centerXAnchor),
      stack.widthAnchor.constraint(lessThanOrEqualToConstant: 640),
      readable,
    ])
    render()
  }

  override var prefersStatusBarHidden: Bool { true }

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
    var configuration = primary ? UIButton.Configuration.filled() : UIButton.Configuration.plain()
    configuration.title = title
    configuration.baseBackgroundColor = Colors.accent
    configuration.baseForegroundColor = primary ? UIColor(red: 0x0b / 255, green: 0x0f / 255, blue: 0x0b / 255, alpha: 1) : Colors.accent
    configuration.cornerStyle = .medium
    configuration.contentInsets = NSDirectionalEdgeInsets(top: primary ? 14 : 10, leading: 12, bottom: primary ? 14 : 10, trailing: 12)
    configuration.titleTextAttributesTransformer = UIConfigurationTextAttributesTransformer { attributes in
      var attributes = attributes
      attributes.font = .systemFont(ofSize: primary ? 16 : 15, weight: primary ? .semibold : .regular)
      return attributes
    }
    return UIButton(configuration: configuration, primaryAction: UIAction { _ in action() })
  }

  private func card(_ views: [UIView]) -> UIView {
    let card = UIView()
    card.backgroundColor = Colors.card
    card.layer.cornerRadius = 12
    let inner = UIStackView(arrangedSubviews: views)
    inner.axis = .vertical
    inner.spacing = 12
    inner.translatesAutoresizingMaskIntoConstraints = false
    card.addSubview(inner)
    NSLayoutConstraint.activate([
      inner.topAnchor.constraint(equalTo: card.topAnchor, constant: 20),
      inner.bottomAnchor.constraint(equalTo: card.bottomAnchor, constant: -20),
      inner.leadingAnchor.constraint(equalTo: card.leadingAnchor, constant: 20),
      inner.trailingAnchor.constraint(equalTo: card.trailingAnchor, constant: -20),
    ])
    return card
  }

  private func render() {
    guard isViewLoaded else { return }
    for view in stack.arrangedSubviews {
      stack.removeArrangedSubview(view)
      view.removeFromSuperview()
    }
    let quipLabel = label(quip, size: 17, color: Colors.neon)
    quipLabel.font = .italicSystemFont(ofSize: 17)
    quipLabel.textAlignment = .center
    stack.addArrangedSubview(quipLabel)

    switch state {
    case let .importing(title, status, done, total, cancel):
      var views: [UIView] = [label(title, size: 20, color: Colors.text, weight: .semibold)]
      if !status.isEmpty { views.append(label(status, size: 15, color: Colors.muted)) }
      if total > 0 {
        let bar = UIProgressView(progressViewStyle: .bar)
        bar.progress = Float(Double(done) / Double(total))
        bar.progressTintColor = Colors.accent
        bar.trackTintColor = Colors.background
        views.append(bar)
        views.append(label("\(gigabytes(done)) of \(gigabytes(total))", size: 15, color: Colors.muted))
      } else {
        let spinner = UIActivityIndicatorView(style: .medium)
        spinner.color = Colors.accent
        spinner.startAnimating()
        views.append(spinner)
      }
      views.append(label("Keep the app open: iOS pauses it in the background.", size: 15, color: Colors.muted))
      if let cancel { views.append(button("Cancel", primary: false, action: cancel)) }
      stack.addArrangedSubview(card(views))

    case let .idle(error):
      var views: [UIView]
      if !GameData.ready {
        views = [
          label("The game's data is not here yet", size: 20, color: Colors.text, weight: .semibold),
          label(
            "Pick an Xbox disc image of Halo: Combat Evolved (.xiso or .iso, any version) and the app copies its "
              + "maps and movies, about \(gigabytes(GameData.spaceNeeded)) with room to spare. Or pick a .zip of the "
              + "maps folder from your own disc, or give the address of one below. Bring your own copy: the app "
              + "comes with none of the game's files.", size: 15, color: Colors.muted),
        ]
        if let error { views.append(label(error, size: 15, color: Colors.error)) }
        views.append(button("Pick the disc image", primary: true) { [weak self] in self?.pick(.disc) })
        views.append(button("Pick a zip of the maps folder", primary: false) { [weak self] in self?.pick(.maps) })
      } else {
        views = [
          label("Ready", size: 20, color: Colors.text, weight: .semibold),
          label("The game's data is in place.", size: 15, color: Colors.muted),
        ]
        if let error { views.append(label(error, size: 15, color: Colors.error)) }
        views.append(button("Play", primary: true) { [weak self] in self?.play() })
        views.append(button("Import maps (.map or .zip)", primary: false) { [weak self] in self?.pick(.maps) })
        views.append(button("Import a disc image again", primary: false) { [weak self] in self?.pick(.disc) })
      }
      stack.addArrangedSubview(card(views))

      let downloadButton = button("Download and import", primary: true) { [weak self] in self?.startDownload() }
      self.downloadButton = downloadButton
      updateDownloadButton()
      stack.addArrangedSubview(card([
        label("Download maps", size: 20, color: Colors.text, weight: .semibold),
        label(
          "The address of a .zip of maps (a maps folder, or Halo PC maps), or of a .map. It is downloaded, its "
            + "maps imported as a picked file's are, and the download deleted.", size: 15, color: Colors.muted),
        addressField,
        downloadButton,
      ]))
    }

    let footer = label("Data folder: \(GameData.root.path)", size: 12, color: Colors.muted)
    footer.textAlignment = .center
    stack.addArrangedSubview(footer)
  }

  private func updateDownloadButton() {
    let address = addressField.text?.trimmingCharacters(in: .whitespaces) ?? ""
    downloadButton?.isEnabled = !address.isEmpty
    downloadButton?.alpha = address.isEmpty ? 0.4 : 1
  }

  /* progress from an import, a few times a second at most (a report per
  megabyte is far more than the screen needs) */
  private func report(_ title: String, _ status: String, _ done: Int64, _ total: Int64, cancel: (() -> Void)? = nil) {
    let now = Date()
    if now.timeIntervalSince(lastReport) < 0.2 && done < total { return }
    lastReport = now
    state = .importing(title: title, status: status, done: done, total: total, cancel: cancel)
  }

  private func alert(_ title: String, _ message: String) {
    let alert = UIAlertController(title: title, message: message, preferredStyle: .alert)
    alert.addAction(UIAlertAction(title: "OK", style: .default))
    present(alert, animated: true)
  }

  /* work off the main thread, with the screen kept awake; done back on it
  with the error, if any */
  private func run(
    _ title: String, work: @escaping (@escaping ImportProgress) throws -> Void,
    done: @escaping (Error?) -> Void
  ) {
    state = .importing(title: title, status: "", done: 0, total: 0, cancel: nil)
    UIApplication.shared.isIdleTimerDisabled = true
    DispatchQueue.global(qos: .userInitiated).async {
      var failure: Error?
      do {
        try work { status, done, total in
          DispatchQueue.main.async { [weak self] in self?.report(title, status, done, total) }
        }
      } catch {
        failure = error
      }
      DispatchQueue.main.async {
        UIApplication.shared.isIdleTimerDisabled = false
        done(failure)
      }
    }
  }

  // MARK: - what the buttons do

  private func play() {
    Game.start { [weak self] error in
      self?.alert("The game could not start", error)
    }
  }

  private func pick(_ kind: Picking) {
    picking = kind
    /* (read where it is, not copied: a disc image is several GB) */
    let picker = UIDocumentPickerViewController(forOpeningContentTypes: [.item], asCopy: false)
    picker.allowsMultipleSelection = kind == .maps
    picker.delegate = self
    present(picker, animated: true)
  }

  func documentPicker(_ controller: UIDocumentPickerViewController, didPickDocumentsAt urls: [URL]) {
    guard !urls.isEmpty else { return }
    let accessed = urls.filter { $0.startAccessingSecurityScopedResource() }
    let release = { for url in accessed { url.stopAccessingSecurityScopedResource() } }
    switch picking {
    case .disc:
      run("Copying the game's data", work: { progress in try MapImport.importDisc(urls[0], progress: progress) }) {
        [weak self] error in
        release()
        guard let self else { return }
        if let error {
          self.state = .idle(error: String(describing: error))
        } else {
          self.state = .idle(error: nil)
          self.alert("The game's data is in place", "The disc's maps and movies are copied. Play when ready.")
        }
      }
    case .maps:
      importMaps(urls, then: release)
    }
  }

  private func importMaps(_ files: [URL], then finished: @escaping () -> Void) {
    var report = ImportReport()
    run("Importing maps", work: { progress in report = MapImport.importMaps(files, progress: progress) }) {
      [weak self] _ in
      finished()
      guard let self else { return }
      self.state = .idle(error: nil)
      self.alert(report.imported.isEmpty ? "Nothing imported" : "Maps imported", report.summary)
    }
  }

  private func startDownload() {
    addressField.resignFirstResponder()
    let url: URL
    switch Download.check(addressField.text ?? "") {
    case let .success(checked): url = checked
    case let .failure(error):
      state = .idle(error: error.description)
      return
    }
    let title = "Downloading"
    let host = url.host ?? ""
    UIApplication.shared.isIdleTimerDisabled = true
    let cancel = { [weak self] in self?.download?.cancel() }
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
          self.addressField.text = ""
          self.importMaps([file]) { try? FileManager.default.removeItem(at: file) }
        }
      })
  }

  func textFieldShouldReturn(_ textField: UITextField) -> Bool {
    startDownload()
    return true
  }
}
