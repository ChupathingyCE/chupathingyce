/*
The app: its scene shows the launcher (LauncherViewController), which
imports the game's data and starts the game. Once started, the game opens a
window of its own (SDL's) over the launcher's, and the process ends with it.
*/

import UIKit

@main
final class AppDelegate: UIResponder, UIApplicationDelegate {
  func application(
    _ application: UIApplication,
    didFinishLaunchingWithOptions launchOptions: [UIApplication.LaunchOptionsKey: Any]?
  ) -> Bool {
    /* made on the first start, as the Android launcher does */
    GameData.makeFolders()
    return true
  }

  func application(
    _ application: UIApplication, configurationForConnecting connectingSceneSession: UISceneSession,
    options: UIScene.ConnectionOptions
  ) -> UISceneConfiguration {
    return UISceneConfiguration(name: "Default", sessionRole: connectingSceneSession.role)
  }
}

final class SceneDelegate: UIResponder, UIWindowSceneDelegate {
  var window: UIWindow?

  func scene(
    _ scene: UIScene, willConnectTo session: UISceneSession, options connectionOptions: UIScene.ConnectionOptions
  ) {
    guard let scene = scene as? UIWindowScene else { return }
    let window = UIWindow(windowScene: scene)
    window.rootViewController = LauncherViewController()
    window.makeKeyAndVisible()
    self.window = window
  }
}

/* The game's data folder: the app's Documents folder, which iOS keeps and
the Files app shows, laid out as the Android build's (chupathingyce's
port/android/README.md, "Game data"): maps and bink from the disc, maps_ce,
maps_md and maps_pc for Halo PC maps, save, logs and config.toml. */
enum GameData {
  static let folders = ["maps", "maps_ce", "maps_md", "maps_pc", "save", "logs"]

  /* what a disc's maps and movies take, with room to spare */
  static let spaceNeeded: Int64 = 2_200_000_000

  static var root: URL {
    FileManager.default.urls(for: .documentDirectory, in: .userDomainMask)[0]
  }

  static func makeFolders() {
    for name in folders {
      try? FileManager.default.createDirectory(
        at: root.appendingPathComponent(name), withIntermediateDirectories: true)
    }
  }

  /* whether the disc's maps are in place: maps/ui.map is the one the game
  needs first */
  static var ready: Bool {
    FileManager.default.fileExists(atPath: root.appendingPathComponent("maps/ui.map").path)
  }

  static var availableSpace: Int64 {
    let values = try? root.resourceValues(forKeys: [.volumeAvailableCapacityForImportantUsageKey])
    return values?.volumeAvailableCapacityForImportantUsage ?? 0
  }
}

enum Game {
  /* Starts the game (halo_ios_run, port/ios/src/ios_main.c) on the main
  thread. It does not return once the game is running; failed is called
  with the reason it could not start. It runs as a run loop block, not the
  main queue's: a main-queue block that never returns would hold the queue,
  and the GameController framework tells SDL of controllers through it (SDL's
  event pump runs the run loop, which drains the queue only outside its
  blocks). */
  static func start(failed: @escaping (String) -> Void) {
    let mainLoop = CFRunLoopGetMain()
    CFRunLoopPerformBlock(mainLoop, CFRunLoopMode.commonModes.rawValue) {
      let hardwareId = UIDevice.current.identifierForVendor?.uuidString ?? ""
      var error = [CChar](repeating: 0, count: 1024)
      _ = halo_ios_run(GameData.root.path, hardwareId, &error, Int32(error.count))
      failed(String(cString: error))
    }
    CFRunLoopWakeUp(mainLoop)
  }
}
