#include "macfontspanel.h"

#include <AppKit/AppKit.h>

namespace MacFontsPanel {

bool isVisible() {
    return [NSFontPanel sharedFontPanel].isVisible;
}

bool lists(const QString &family) {
    return [[[NSFontManager sharedFontManager] availableFontFamilies]
        containsObject:family.toNSString()];
}

void choose(const QString &family) {
    NSFontPanel *panel = [NSFontPanel sharedFontPanel];
    NSFontManager *fonts = [NSFontManager sharedFontManager];
    [panel setPanelFont:[fonts fontWithFamily:family.toNSString() traits:0 weight:5 size:20]
             isMultiple:NO];
    [fonts modifyFontViaPanel:panel];
}

void close() {
    [[NSFontPanel sharedFontPanel] performClose:nil];
}

}
