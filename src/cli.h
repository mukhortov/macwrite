#pragma once

#include <QString>
#include <QStringList>
#include <optional>

// Command line handling that runs before the GUI starts, so `macwrite --help`
// and `macwrite --version` answer in the terminal instead of opening a window.
namespace Cli {

QString usage();

// The exit code main should return when the arguments are answered without
// starting the app, or nothing when Macwrite should open as usual. Anything
// else, Qt's own options included, is left for the app.
std::optional<int> handleArguments(const QStringList &arguments, const QString &version);
std::optional<int> handleArguments(int argc, char *argv[], const QString &version);

}
