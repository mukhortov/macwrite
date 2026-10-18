#pragma once

#include <QString>

// Write's own fonts, of which Qt and AppKit each keep a list.
namespace MacFonts {

// Adds the font file to both: Qt sets the text in it, and the Fonts panel
// offers it.
bool addApplicationFont(const QString &path);

}
