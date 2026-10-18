#include "macfonts.h"

#include <QFontDatabase>

#include <CoreText/CoreText.h>
#include <Foundation/Foundation.h>

namespace MacFonts {

// Qt reads the file for itself and tells AppKit nothing; without CoreText's
// registration the Fonts panel neither lists the font nor knows it is the
// text's, and reports the system's in its place.
bool addApplicationFont(const QString &path) {
    if (QFontDatabase::addApplicationFont(path) == -1)
        return false;
    NSURL *url = [NSURL fileURLWithPath:path.toNSString()];
    return CTFontManagerRegisterFontsForURL((__bridge CFURLRef)url, kCTFontManagerScopeProcess,
                                            nullptr);
}

}
