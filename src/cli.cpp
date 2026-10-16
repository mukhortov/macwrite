#include "cli.h"

#include <QTextStream>

QString Cli::usage() {
    return QStringLiteral(
        "Usage: Write [FILE]\n"
        "\n"
        "Opens FILE, a Markdown file, or an empty document without one. Write\n"
        "is a graphical app and keeps running until it is quit.\n"
        "\n"
        "Options:\n"
        "  -h, --help     Show this message and exit\n"
        "  -v, --version  Show the version and exit\n");
}

std::optional<int> Cli::handleArguments(const QStringList &arguments, const QString &version) {
    if (arguments.size() < 2)
        return std::nullopt;

    const QString &first = arguments.at(1);
    if (first == QLatin1String("-h") || first == QLatin1String("--help")) {
        QTextStream(stdout) << usage();
        return 0;
    }
    if (first == QLatin1String("-v") || first == QLatin1String("--version")) {
        QTextStream(stdout) << QStringLiteral("Write %1\n").arg(version);
        return 0;
    }

    return std::nullopt;
}

std::optional<int> Cli::handleArguments(int argc, char *argv[], const QString &version) {
    QStringList arguments;
    arguments.reserve(argc);
    for (int index = 0; index < argc; ++index)
        arguments.append(QString::fromLocal8Bit(argv[index]));

    return handleArguments(arguments, version);
}
