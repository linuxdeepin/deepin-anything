// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "extractorapp.h"

#include "processprioritymanager.h"

#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QDebug>

EXTRACTOR_PLUGIN_BEGIN_NAMESPACE

ExtractorApp::ExtractorApp(QObject *parent)
    : QObject(parent), m_pluginLoader(new PluginLoader(this)), m_workerPipe(new EXTRACTOR_NAMESPACE::WorkerPipe(this)), m_idleTimer(new QTimer(this))
{
    m_idleTimer->setSingleShot(true);
    connect(m_idleTimer, &QTimer::timeout, this, []() {
        qInfo() << "ExtractorApp: No batch received for" << kIdleTimeoutMs
                 << "ms, exiting idle extractor process";
        QCoreApplication::quit();
    });
}

ExtractorApp::~ExtractorApp()
{
}

bool ExtractorApp::initialize(const QString &pluginPath)
{
    qInfo() << "ExtractorApp: Initializing with plugin path:" << pluginPath;

    // Lower process priority to avoid impacting user experience
    ProcessPriorityManager::lowerAllAvailablePriorities(true);

    // Load plugins
    int pluginCount = m_pluginLoader->loadPlugins(pluginPath);
    if (pluginCount == 0) {
        qWarning() << "ExtractorApp: No plugins loaded";
        return false;
    }

    qInfo() << "ExtractorApp: Initialization complete, loaded" << pluginCount << "plugins";
    return true;
}

void ExtractorApp::run()
{
    qInfo() << "ExtractorApp: Starting main loop";

    if (!m_workerPipe->initialize()) {
        qCritical() << "ExtractorApp: Failed to initialize worker pipe";
        return;
    }

    // Connect signals
    connect(m_workerPipe.get(), &EXTRACTOR_NAMESPACE::WorkerPipe::batchReceived,
            this, [this](const QVector<QString> &filePaths) {
                processBatch(filePaths);
            });

    connect(m_workerPipe.get(), &EXTRACTOR_NAMESPACE::WorkerPipe::activityDetected,
            this, &ExtractorApp::resetIdleTimer);

    connect(m_workerPipe.get(), &EXTRACTOR_NAMESPACE::WorkerPipe::stdinClosed,
            this, []() {
                qInfo() << "ExtractorApp: Stdin closed, exiting";
                QCoreApplication::quit();
            });

    resetIdleTimer();
    qInfo() << "ExtractorApp: Ready to process requests";

    // Enter event loop
    QCoreApplication::exec();

    qInfo() << "ExtractorApp: Exiting";
}

void ExtractorApp::processBatch(const QVector<QString> &filePaths)
{
    qInfo() << "ExtractorApp: Processing batch of" << filePaths.size() << "files";

    for (const QString &filePath : filePaths) {
        qDebug() << "ExtractorApp: Processing file:" << filePath;

        // Send started notification
        m_workerPipe->sendStarted(filePath);

        // Check if file exists
        if (!QFile::exists(filePath)) {
            qWarning() << "ExtractorApp: File does not exist:" << filePath;
            m_workerPipe->sendFailed(filePath, "File does not exist");
            continue;
        }

        // Find appropriate plugin
        auto plugin = m_pluginLoader->findPlugin(filePath);
        if (!plugin) {
            qWarning() << "ExtractorApp: No plugin can handle file:" << filePath;
            m_workerPipe->sendFailed(filePath, "No plugin available for this file type");
            continue;
        }

        // Extract content
        const auto &result = plugin->extract(filePath);
        if (!result.has_value()) {
            qWarning() << "ExtractorApp: Extraction failed for file:" << filePath;
            m_workerPipe->sendFailed(filePath, "Extraction failed");
            continue;
        }

        // Send extracted data
        qDebug() << "ExtractorApp: Successfully extracted" << result->size()
                  << "bytes from:" << filePath;
        m_workerPipe->sendData(filePath, result.value());
    }

    // Send batch done notification
    m_workerPipe->sendBatchDone();

    qInfo() << "ExtractorApp: Batch processing complete";
}

void ExtractorApp::resetIdleTimer()
{
    m_idleTimer->start(kIdleTimeoutMs);
}

EXTRACTOR_PLUGIN_END_NAMESPACE
