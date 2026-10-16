# Homebrew installs Qt and its libraries built for the macOS it runs on, and
# many of them, Qt Quick among them, need that release, newer than Qt's own
# minimum. So the app needs the macOS it was built on, and says so.
QMAKE_MACOSX_DEPLOYMENT_TARGET = $$system(sw_vers -productVersion | cut -d. -f1).0
