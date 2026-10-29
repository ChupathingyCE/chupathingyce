/*
iPhone Duo's fold, for the touch controls (port/ios/src/ios_touch.m): held
part open, the fold is a reserved region of the "division" kind, and the
overlay keeps its controls off it. A hinge interaction on the overlay tells
it to look again as the hinge moves (Apple's hinge is for effects, not
layout: the region is what places the controls).

Apple's API for it is the iOS 27.1 SDK's (Xcode 27.1). Built with an older
one, as GitHub's runners have until they get 27.1, this is left out and
the overlay sees no fold; the app is iPhone Duo's all the same, its picture
following the window's shape as the phone folds and unfolds
(0002-ios-window-shape.patch). The workflow turns it on by itself
(IOS_27_1_SDK, .github/workflows/ios.yml) once the SDK is there.
*/

import UIKit

#if IOS_27_1_SDK

enum Fold {
  static func install() {
    halo_ios_set_fold_proc(foldFrame)
  }
}

/* (a C function for the overlay: nothing captured) */
private let foldFrame: halo_ios_fold_proc = { view, frame in
  guard let view, let frame else { return 0 }
  guard #available(iOS 27.1, *) else { return 0 }
  let overlay = Unmanaged<UIView>.fromOpaque(view).takeUnretainedValue()
  watchHinge(overlay)
  guard let fold = overlay.reservedRegions(kind: .division).first(where: { $0.isActive }) else { return 0 }
  frame[0] = Double(fold.frame.origin.x)
  frame[1] = Double(fold.frame.origin.y)
  frame[2] = Double(fold.frame.size.width)
  frame[3] = Double(fold.frame.size.height)
  return 1
}

/* the overlay looks again whenever the hinge moves: once per view */
@available(iOS 27.1, *)
private func watchHinge(_ view: UIView) {
  if view.interactions.contains(where: { $0 is UIHingeInteraction }) { return }
  view.addInteraction(UIHingeInteraction { _, _ in halo_ios_fold_changed() })
}

#else

enum Fold {
  static func install() {}
}

#endif
