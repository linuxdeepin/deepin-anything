// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later
#include "textindexconfig.h"

#include <QDebug>

ANYTHING_INDEX_BEGIN_NAMESPACE

TextIndexConfig &TextIndexConfig::instance()
{
    static TextIndexConfig self;
    return self;
}

TextIndexConfig::TextIndexConfig(QObject *parent)
    : QObject(parent),
      m_dconfig(Dtk::Core::DConfig::create("org.deepin.dde.file-manager",
                                           Defines::DConf::kTextIndexSchema,
                                           QString(), this))
{
    if (!m_dconfig || !m_dconfig->isValid())
        qWarning() << "TextIndexConfig: Failed to load DConfig schema:" << Defines::DConf::kTextIndexSchema;

    if (m_dconfig) {
        loadAllConfigs();
        setupConnections();
    }
}

void TextIndexConfig::setupConnections()
{
    // Connect to DConfig's signal for changes in our schema
    // This allows automatic reloading if the config is changed externally (e.g., by dconf-editor)
    connect(m_dconfig, &Dtk::Core::DConfig::valueChanged, this,
            [this](const QString &key) {
                qDebug() << "TextIndexConfig: DConfig changed key:" << key;
                // Reloading all is simpler and often acceptable.
                loadAllConfigs();
                emit configChanged();
            });
}

void TextIndexConfig::loadAllConfigs()
{
    QMutexLocker locker(&m_mutex);
    qDebug() << "TextIndexConfig: Loading text index configurations";

    // Auto Index Update Interval (FSEventCollector event collection interval)
    m_autoIndexUpdateInterval = m_dconfig->value(
                                                        Defines::DConf::kAutoIndexUpdateInterval,
                                                        DEFAULT_AUTO_INDEX_UPDATE_INTERVAL)
                                        .toInt();
    // Validate autoIndexUpdateInterval
    if (m_autoIndexUpdateInterval < 1 || m_autoIndexUpdateInterval > 3600) {
        qWarning() << "TextIndexConfig: Invalid autoIndexUpdateInterval value:" << m_autoIndexUpdateInterval
                    << ", using default:" << DEFAULT_AUTO_INDEX_UPDATE_INTERVAL;
        m_autoIndexUpdateInterval = DEFAULT_AUTO_INDEX_UPDATE_INTERVAL;
    }

    // Monitoring Start Delay (FSEventController monitoring start delay)
    m_monitoringStartDelaySeconds = m_dconfig->value(
                                                            Defines::DConf::kMonitoringStartDelaySeconds,
                                                            DEFAULT_MONITORING_START_DELAY_SECONDS)
                                            .toInt();
    // Validate monitoringStartDelaySeconds
    if (m_monitoringStartDelaySeconds < 0 || m_monitoringStartDelaySeconds > 3600) {
        qWarning() << "TextIndexConfig: Invalid monitoringStartDelaySeconds value:" << m_monitoringStartDelaySeconds
                    << ", using default:" << DEFAULT_MONITORING_START_DELAY_SECONDS;
        m_monitoringStartDelaySeconds = DEFAULT_MONITORING_START_DELAY_SECONDS;
    }

    // Silent Index Update Delay (FSEventController first start delay)
    m_silentIndexUpdateDelay = m_dconfig->value(
                                                       Defines::DConf::kSilentIndexUpdateDelay,
                                                       DEFAULT_SILENT_INDEX_UPDATE_DELAY)
                                       .toInt();
    // Validate silentIndexUpdateDelay
    if (m_silentIndexUpdateDelay < 1 || m_silentIndexUpdateDelay > 3600) {
        qWarning() << "TextIndexConfig: Invalid silentIndexUpdateDelay value:" << m_silentIndexUpdateDelay
                    << ", using default:" << DEFAULT_SILENT_INDEX_UPDATE_DELAY;
        m_silentIndexUpdateDelay = DEFAULT_SILENT_INDEX_UPDATE_DELAY;
    }

    // Inotify Resource Cleanup Delay
    m_inotifyResourceCleanupDelayMs = m_dconfig->value(
                                                              Defines::DConf::kInotifyResourceCleanupDelay,   // Ensure this matches JSON and Defines.h
                                                              DEFAULT_INOTIFY_RESOURCE_CLEANUP_DELAY)
                                              .toLongLong();

    // Max Index File Size MB
    m_maxIndexTextFileSizeMB = m_dconfig->value(
                                                   Defines::DConf::kMaxIndexTextFileSizeMB,
                                                   DEFAULT_MAX_INDEX_FILE_SIZE_MB)
                                   .toInt();

    // Max Index File Truncation Size MB
    m_maxIndexFileTruncationSizeMB = m_dconfig->value(
                                                             Defines::DConf::kMaxIndexFileTruncationSizeMB,
                                                             DEFAULT_MAX_INDEX_FILE_TRUNCATION_SIZE_MB)
                                             .toInt();
    // Validate and apply default if value is invalid (negative, zero, or too large)
    if (m_maxIndexFileTruncationSizeMB <= 0 || m_maxIndexFileTruncationSizeMB > 1024) {
        qWarning() << "TextIndexConfig: Invalid maxIndexFileTruncationSizeMB value:" << m_maxIndexFileTruncationSizeMB << ", using default:" << DEFAULT_MAX_INDEX_FILE_TRUNCATION_SIZE_MB;
        m_maxIndexFileTruncationSizeMB = DEFAULT_MAX_INDEX_FILE_TRUNCATION_SIZE_MB;
    }

    // Supported File Extensions
    const QStringList defaultSupportedExtensions = {
        "rtf", "odt", "ods", "odp", "odg", "docx",
        "xlsx", "pptx", "ppsx", "md", "xls", "xlsb",
        "doc", "dot", "wps", "ppt", "pps", "txt",
        "pdf", "dps", "sh", "html", "htm", "xml",
        "xhtml", "dhtml", "shtm", "shtml", "json",
        "css", "yaml", "ini", "bat", "js", "sql",
        "uof", "ofd"
    };
    m_supportedTextFileExtensions = m_dconfig->value(
                                                        Defines::DConf::kSupportedTextFileExtensions,
                                                        QVariant::fromValue(defaultSupportedExtensions))   // Pass QVariant holding QStringList
                                        .toStringList();

    const QStringList defaultSupportedOcrImageExtensions = {
        "ani", "bmp", "jpe", "jpeg", "jpg", "pcx", "png", "psd",
        "tga", "tif", "tiff", "webp", "wmf", "heic", "heif", "raw"
    };
    m_supportedOcrImageExtensions = m_dconfig->value(
                                                                QStringLiteral("supportedOcrImageExtensions"),
                                                                QVariant::fromValue(defaultSupportedOcrImageExtensions))
                                            .toStringList();

    m_maxOcrImageSizeMB = m_dconfig->value(
                                                  QStringLiteral("maxOcrImageSizeMB"),
                                                  DEFAULT_MAX_OCR_IMAGE_SIZE_MB)
                                  .toInt();
    if (m_maxOcrImageSizeMB <= 0 || m_maxOcrImageSizeMB > 1024) {
        qWarning() << "TextIndexConfig: Invalid maxOcrImageSizeMB value:" << m_maxOcrImageSizeMB
                    << ", using default:" << DEFAULT_MAX_OCR_IMAGE_SIZE_MB;
        m_maxOcrImageSizeMB = DEFAULT_MAX_OCR_IMAGE_SIZE_MB;
    }

    // Index Hidden Files
    m_indexHiddenFiles = m_dconfig->value(
                                                 Defines::DConf::kIndexHiddenFiles,
                                                 DEFAULT_INDEX_HIDDEN_FILES)
                                 .toBool();

    // Folder Exclude Filters
    const QStringList defaultFolderExcludeFilters = {
        ".git", ".svn", ".hg", ".cache", ".local/share/Trash", ".Trash",
        ".thumbnails", "thumbnails", ".mozilla", "CMakeFiles",
        "CMakeTmp", "CMakeTmpQmake", "lost+found"
    };
    m_folderExcludeFilters = m_dconfig->value(
                                                     Defines::DConf::kFolderExcludeFilters,
                                                     QVariant::fromValue(defaultFolderExcludeFilters))   // Pass QVariant holding QStringList
                                     .toStringList();

    // CPU isage limit percent
    m_cpuUsageLimitPercent = m_dconfig->value(
                                                     Defines::DConf::kCpuUsageLimitPercent,
                                                     DEFAULT_CPU_USAGE_LIMIT_PERCENT)
                                     .toInt();
    if (m_cpuUsageLimitPercent < 10 || m_cpuUsageLimitPercent >= 100) {
        m_cpuUsageLimitPercent = DEFAULT_CPU_USAGE_LIMIT_PERCENT;
    }

    // Inotify watches coefficient
    m_inotifyWatchesCoefficient = m_dconfig->value(
                                                          Defines::DConf::kInotifyWatchesCoefficient,
                                                          DEFAULT_INOTIFY_WATCHES_COEFFICIENT)
                                          .toDouble();
    if (m_inotifyWatchesCoefficient < 0.1 || m_inotifyWatchesCoefficient > 1.0) {
        m_inotifyWatchesCoefficient = DEFAULT_INOTIFY_WATCHES_COEFFICIENT;
    }

    // Batch commit interval
    m_batchCommitInterval = m_dconfig->value(
                                                    Defines::DConf::kBatchCommitInterval,
                                                    DEFAULT_BATCH_COMMIT_INTERVAL)
                                    .toInt();
    if (m_batchCommitInterval < 100 || m_batchCommitInterval > 10000) {
        m_batchCommitInterval = DEFAULT_BATCH_COMMIT_INTERVAL;
    }

    // --- Strategy optimization config keys (environment detection) ---

    m_idleThresholdSeconds = m_dconfig->value(Defines::DConf::kIdleThresholdSeconds,
            DEFAULT_IDLE_THRESHOLD_SECONDS).toInt();
    if (m_idleThresholdSeconds < 5 || m_idleThresholdSeconds > 600)
        m_idleThresholdSeconds = DEFAULT_IDLE_THRESHOLD_SECONDS;

    m_loadSampleIntervalSeconds = m_dconfig->value(Defines::DConf::kLoadSampleIntervalSeconds,
            DEFAULT_LOAD_SAMPLE_INTERVAL_SECONDS).toInt();
    if (m_loadSampleIntervalSeconds < 1 || m_loadSampleIntervalSeconds > 60)
        m_loadSampleIntervalSeconds = DEFAULT_LOAD_SAMPLE_INTERVAL_SECONDS;

    m_cpuLoadThresholdPercent = m_dconfig->value(Defines::DConf::kCpuLoadThresholdPercent,
            DEFAULT_CPU_LOAD_THRESHOLD_PERCENT).toInt();
    if (m_cpuLoadThresholdPercent < 1 || m_cpuLoadThresholdPercent > 100)
        m_cpuLoadThresholdPercent = DEFAULT_CPU_LOAD_THRESHOLD_PERCENT;

    m_diskBusyThresholdPercent = m_dconfig->value(Defines::DConf::kDiskBusyThresholdPercent,
            DEFAULT_DISK_BUSY_THRESHOLD_PERCENT).toInt();
    if (m_diskBusyThresholdPercent < 1 || m_diskBusyThresholdPercent > 100)
        m_diskBusyThresholdPercent = DEFAULT_DISK_BUSY_THRESHOLD_PERCENT;

    // --- Strategy optimization config keys (task grading thresholds) ---

    m_lightIncrementFileCountThreshold = m_dconfig->value(Defines::DConf::kLightIncrementFileCountThreshold,
            DEFAULT_LIGHT_INCREMENT_FILE_COUNT_THRESHOLD).toInt();
    if (m_lightIncrementFileCountThreshold < 1 || m_lightIncrementFileCountThreshold > 100000)
        m_lightIncrementFileCountThreshold = DEFAULT_LIGHT_INCREMENT_FILE_COUNT_THRESHOLD;

    m_lightIncrementOcrFileCountThreshold = m_dconfig->value(Defines::DConf::kLightIncrementOcrFileCountThreshold,
            DEFAULT_LIGHT_INCREMENT_OCR_FILE_COUNT_THRESHOLD).toInt();
    if (m_lightIncrementOcrFileCountThreshold < 1 || m_lightIncrementOcrFileCountThreshold > 100000)
        m_lightIncrementOcrFileCountThreshold = DEFAULT_LIGHT_INCREMENT_OCR_FILE_COUNT_THRESHOLD;

    m_lightIncrementSizeThresholdMB = m_dconfig->value(Defines::DConf::kLightIncrementSizeThresholdMB,
            DEFAULT_LIGHT_INCREMENT_SIZE_THRESHOLD_MB).toLongLong();
    if (m_lightIncrementSizeThresholdMB < 1 || m_lightIncrementSizeThresholdMB > 10240)
        m_lightIncrementSizeThresholdMB = DEFAULT_LIGHT_INCREMENT_SIZE_THRESHOLD_MB;

    qDebug() << "TextIndexConfig: Text index configurations loaded successfully";
    // You might want to print the loaded values here for debugging if needed
    // qDebug() << "AutoIndexUpdateInterval:" << m_autoIndexUpdateInterval;
    // ... and so on
}

void TextIndexConfig::reloadConfig()
{
    loadAllConfigs();
    emit configChanged();
}

// --- Getter Implementations ---
int TextIndexConfig::autoIndexUpdateInterval() const
{
    QMutexLocker locker(&m_mutex);
    return m_autoIndexUpdateInterval;
}

int TextIndexConfig::monitoringStartDelaySeconds() const
{
    QMutexLocker locker(&m_mutex);
    return m_monitoringStartDelaySeconds;
}

int TextIndexConfig::silentIndexUpdateDelay() const
{
    QMutexLocker locker(&m_mutex);
    return m_silentIndexUpdateDelay;
}

qint64 TextIndexConfig::inotifyResourceCleanupDelayMs() const
{
    QMutexLocker locker(&m_mutex);
    return m_inotifyResourceCleanupDelayMs;
}

int TextIndexConfig::maxIndexTextFileSizeMB() const
{
    QMutexLocker locker(&m_mutex);
    return m_maxIndexTextFileSizeMB;
}

int TextIndexConfig::maxIndexFileTruncationSizeMB() const
{
    QMutexLocker locker(&m_mutex);
    return m_maxIndexFileTruncationSizeMB;
}

QStringList TextIndexConfig::supportedTextFileExtensions() const
{
    QMutexLocker locker(&m_mutex);
    return m_supportedTextFileExtensions;
}

QStringList TextIndexConfig::supportedOcrImageExtensions() const
{
    QMutexLocker locker(&m_mutex);
    return m_supportedOcrImageExtensions;
}

int TextIndexConfig::maxOcrImageSizeMB() const
{
    QMutexLocker locker(&m_mutex);
    return m_maxOcrImageSizeMB;
}

bool TextIndexConfig::indexHiddenFiles() const
{
    QMutexLocker locker(&m_mutex);
    return m_indexHiddenFiles;
}

QStringList TextIndexConfig::folderExcludeFilters() const
{
    QMutexLocker locker(&m_mutex);
    return m_folderExcludeFilters;
}

int TextIndexConfig::cpuUsageLimitPercent() const
{
    QMutexLocker locker(&m_mutex);
    return m_cpuUsageLimitPercent;
}

double TextIndexConfig::inotifyWatchesCoefficient() const
{
    QMutexLocker locker(&m_mutex);
    return m_inotifyWatchesCoefficient;
}

int TextIndexConfig::batchCommitInterval() const
{
    QMutexLocker locker(&m_mutex);
    return m_batchCommitInterval;
}

int TextIndexConfig::idleThresholdSeconds() const
{
    QMutexLocker locker(&m_mutex);
    return m_idleThresholdSeconds;
}

int TextIndexConfig::loadSampleIntervalSeconds() const
{
    QMutexLocker locker(&m_mutex);
    return m_loadSampleIntervalSeconds;
}

int TextIndexConfig::cpuLoadThresholdPercent() const
{
    QMutexLocker locker(&m_mutex);
    return m_cpuLoadThresholdPercent;
}

int TextIndexConfig::diskBusyThresholdPercent() const
{
    QMutexLocker locker(&m_mutex);
    return m_diskBusyThresholdPercent;
}

int TextIndexConfig::lightIncrementFileCountThreshold() const
{
    QMutexLocker locker(&m_mutex);
    return m_lightIncrementFileCountThreshold;
}

int TextIndexConfig::lightIncrementOcrFileCountThreshold() const
{
    QMutexLocker locker(&m_mutex);
    return m_lightIncrementOcrFileCountThreshold;
}

qint64 TextIndexConfig::lightIncrementSizeThresholdMB() const
{
    QMutexLocker locker(&m_mutex);
    return m_lightIncrementSizeThresholdMB;
}

ANYTHING_INDEX_END_NAMESPACE
