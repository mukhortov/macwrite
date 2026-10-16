#include "systemtheme.h"

#include <QGuiApplication>
#include <QStyleHints>

SystemTheme::SystemTheme(QObject *parent) : QObject(parent) {
    m_darkMode = detectDarkMode();

    if (QGuiApplication::styleHints()) {
        connect(QGuiApplication::styleHints(), &QStyleHints::colorSchemeChanged,
                this, &SystemTheme::refresh);
    }
}

void SystemTheme::refresh() {
    const bool darkMode = detectDarkMode();
    if (m_darkMode == darkMode)
        return;

    m_darkMode = darkMode;
    emit darkModeChanged(m_darkMode);
}

// Dark unless the system says light, as an unknown scheme is rare enough that
// the editor's own dark look is the safer guess.
bool SystemTheme::detectDarkMode() {
    if (!QGuiApplication::styleHints())
        return true;

    return QGuiApplication::styleHints()->colorScheme() != Qt::ColorScheme::Light;
}
