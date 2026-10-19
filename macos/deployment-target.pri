# Homebrew installs Qt and its libraries built for the macOS it runs on, and
# many of them need that release, newer than Qt's own minimum. So an app built
# against Homebrew's Qt needs the macOS it was built on, and says so.
#
# Qt's own binaries keep Qt's minimum, macOS 14.4 for Qt 6.12, and carry both
# Mac architectures, so an app built against them, as bin/dmg's is, does too.
exists($$[QT_INSTALL_PREFIX]/Cellar) {
    QMAKE_MACOSX_DEPLOYMENT_TARGET = $$system(sw_vers -productVersion | cut -d. -f1).0
} else {
    QMAKE_APPLE_DEVICE_ARCHS = x86_64 arm64
}
