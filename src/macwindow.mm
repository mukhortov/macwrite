#include "macwindow.h"

#import <AppKit/AppKit.h>

#include <QGuiApplication>
#include <QWidget>

namespace {

// The window AppKit draws, when there is one: offscreen and in other
// platforms a window has no NSView behind it.
NSWindow *nativeWindow(QWidget *window) {
    if (QGuiApplication::platformName() != QLatin1String("cocoa"))
        return nil;
    auto *view = reinterpret_cast<NSView *>(window->winId());
    return view.window;
}

}

void MacWindow::setTitleBarColour(QWidget *window, const QColor &colour) {
    NSWindow *native = nativeWindow(window);
    if (!native)
        return;
    const QColor rgb = colour.toRgb();
    native.titlebarAppearsTransparent = YES;
    native.titlebarSeparatorStyle = NSTitlebarSeparatorStyleNone;
    native.backgroundColor = [NSColor colorWithSRGBRed:rgb.redF() green:rgb.greenF()
                                                  blue:rgb.blueF() alpha:1];
}

QColor MacWindow::titleBarColour(QWidget *window) {
    NSWindow *native = nativeWindow(window);
    if (!native || !native.titlebarAppearsTransparent)
        return {};
    NSColor *colour = [native.backgroundColor colorUsingColorSpace:NSColorSpace.sRGBColorSpace];
    if (!colour)
        return {};
    return QColor::fromRgbF(float(colour.redComponent), float(colour.greenComponent),
                            float(colour.blueComponent));
}
