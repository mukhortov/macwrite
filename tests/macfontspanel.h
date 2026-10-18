#pragma once

#include <QString>

// The writer's hands on AppKit's shared Fonts panel, for tests on Cocoa.
namespace MacFontsPanel {

bool isVisible();
// Whether the panel offers the family.
bool lists(const QString &family);
// Picks the family in the panel, as a click in its list does.
void choose(const QString &family);
// The panel's close button.
void close();

}
