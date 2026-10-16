#pragma once

#include <QObject>

// Follows the macOS appearance, light or dark, as it changes.
class SystemTheme : public QObject {
    Q_OBJECT

public:
    explicit SystemTheme(QObject *parent = nullptr);

    bool darkMode() const { return m_darkMode; }

signals:
    void darkModeChanged(bool darkMode);

public slots:
    void refresh();

private:
    static bool detectDarkMode();

    bool m_darkMode = true;
};
