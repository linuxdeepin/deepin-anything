// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "extractorapp.h"

#include <QCommandLineParser>
#include <QDir>
#include <QDebug>
#include <QGuiApplication>
#include <QProcessEnvironment>
#include <sys/prctl.h>
#include <type_traits>

namespace {

// /proc/<pid>/comm (and thus the "process name" shown by top/htop/ps -o comm)
// is capped at TASK_COMM_LEN - 1 = 15 bytes. Which name each worker gets is the
// index profile's policy (IndexProfile::workerName), not this binary's; here we
// only enforce what the kernel accepts so a bad value cannot garble comm.
constexpr int kMaxCommLength = 15;

bool isValidProcessName(const QByteArray &name)
{
    if (name.isEmpty() || name.size() > kMaxCommLength)
        return false;

    for (char ch : name) {
        const auto uc = static_cast<unsigned char>(ch);
        if (uc <= 0x20 || uc >= 0x7f)   // printable ASCII, no whitespace
            return false;
    }

    return true;
}

void applyProcessName(const QString &processName)
{
    if (processName.isEmpty())
        return;

    const QByteArray name = processName.toUtf8();
    if (!isValidProcessName(name)) {
        qWarning() << "Extractor: ignoring invalid process name:" << processName;
        return;
    }

    // prctl() renames the calling thread; main() runs on the main thread, whose
    // comm is what the kernel reports as the process name.
    if (prctl(PR_SET_NAME, name.constData(), 0, 0, 0) != 0) {
        qWarning() << "Extractor: failed to set process name to:" << processName;
        return;
    }

    qInfo() << "Extractor: process name set to" << processName;
}

}   // namespace

// debug:
// 1. sudo sysctl kernel.yama.ptrace_scope=0
// 2. attach: gdb -p $(PID)

int main(int argc, char *argv[])
{
    // Fix: HTML extraction uses QTextDocument, which requires a GUI-capable Qt
    // application even in this headless worker process.
    if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM")
        && qEnvironmentVariableIsEmpty("DISPLAY")
        && qEnvironmentVariableIsEmpty("WAYLAND_DISPLAY")) {
        qputenv("QT_QPA_PLATFORM", QByteArrayLiteral("offscreen"));
    }

    QGuiApplication app(argc, argv);
    QGuiApplication::setApplicationName("deepin-anything-extractor");
    QGuiApplication::setApplicationVersion("1.0.0");

    // Parse command line arguments
    QCommandLineParser parser;
    parser.setApplicationDescription("Deepin Anything Content Extractor");
    parser.addHelpOption();
    parser.addVersionOption();

    QCommandLineOption pluginPathOption(
            "plugin-path",
            "Path to the plugin directory",
            "path");

    QCommandLineOption processNameOption(
            "process-name",
            "Short name to report as the process name (ps/top comm); the index "
            "profile supplies it",
            "name");

    parser.addOption(pluginPathOption);
    parser.addOption(processNameOption);
    parser.process(app);

    applyProcessName(parser.value(processNameOption));

    // Get plugin path
    QString pluginPath = parser.value(pluginPathOption);
    if (pluginPath.isEmpty()) {
        auto defaultPath = ANYTHING_EXTRACTOR_PLUGIN_DIR;
        static_assert(std::is_same<decltype(defaultPath), const char *>::value,
                      "ANYTHING_EXTRACTOR_PLUGIN_DIR is not a string.");

#ifdef QT_DEBUG
        const QString debugPluginPath = QDir(QGuiApplication::applicationDirPath()).absoluteFilePath("../plugins");
        if (QDir(debugPluginPath).exists()) {
            pluginPath = debugPluginPath;
        } else {
            pluginPath = QString::fromLocal8Bit(defaultPath);
        }
#else
        pluginPath = QString::fromLocal8Bit(defaultPath);
#endif
    }

    // Create and initialize extractor application
    anything_extractor_plugin::ExtractorApp extractor;
    if (!extractor.initialize(pluginPath)) {
        qCritical() << "Failed to initialize extractor with plugin path:" << pluginPath;
        return 1;
    }

    // Run the main loop
    extractor.run();

    return 0;
}
