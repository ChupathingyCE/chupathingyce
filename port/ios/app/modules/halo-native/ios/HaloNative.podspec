require 'json'

package = JSON.parse(File.read(File.join(__dir__, '..', 'package.json')))

Pod::Spec.new do |s|
  s.name           = 'HaloNative'
  s.version        = package['version']
  s.summary        = package['description']
  s.description    = package['description']
  s.license        = package['license']
  s.author         = package['author']
  s.homepage       = package['homepage']
  s.platforms      = {
    :ios => '16.4',
    :tvos => '16.4'
  }
  s.swift_version  = '5.9'
  s.source         = { git: 'https://github.com/JizzyRivers/warthog-ios.git' }
  s.static_framework = true

  s.dependency 'ExpoModulesCore'

  s.source_files = "*.{h,m,swift}"
  # the game (libhalo.a, port/ios/build.py) and SDL3, built for iOS; the app's
  # workflow puts them here (.github/workflows/ios.yml)
  s.vendored_libraries = 'lib/*.a'
  s.preserve_paths = 'lib/*.a'
  # what SDL3 and the game need of iOS (port/ios/build.py, IOS_FRAMEWORKS)
  s.frameworks = %w[
    Foundation UIKit OpenGLES QuartzCore CoreGraphics CoreVideo Metal
    CoreAudio AudioToolbox AVFoundation GameController CoreMotion CoreHaptics
    CoreBluetooth UniformTypeIdentifiers
  ]
  s.libraries = 'iconv'
  s.pod_target_xcconfig = {
    'DEFINES_MODULE' => 'YES',
    'SWIFT_COMPILATION_MODE' => 'wholemodule'
  }
end
