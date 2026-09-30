// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef INDEX_GLOBAL_H
#define INDEX_GLOBAL_H

#include <QDebug>
#include <QString>
#include <dfm-search/dsearch_global.h>

#define ANYTHING_INDEX_NAMESPACE anything_index
#define ANYTHING_INDEX_BEGIN_NAMESPACE namespace ANYTHING_INDEX_NAMESPACE {
#define ANYTHING_INDEX_END_NAMESPACE }
#define ANYTHING_INDEX_USE_NAMESPACE using namespace ANYTHING_INDEX_NAMESPACE;

ANYTHING_INDEX_BEGIN_NAMESPACE

namespace Defines {

// The systemd user unit that hosts this service. CPU quota throttling
// (SystemdCpuUtils) targets this unit.
inline const QString kIndexSystemdUnitName = QLatin1String("deepin-anything-index.service");
inline const QString kAnythingDirType = QLatin1String("dir");
inline const QString kAnythingDocType = QLatin1String("doc");
inline const QString kAnythingPicType = QLatin1String("pic");
inline const QString kTextIndexDBusService = QLatin1String("org.deepin.Filemanager.TextIndex");
inline const QString kTextIndexDBusObjectPath = QLatin1String("/org/deepin/Filemanager/TextIndex");
inline const QString kOcrIndexDBusService = QLatin1String("org.deepin.Filemanager.OcrIndex");
inline const QString kOcrIndexDBusObjectPath = QLatin1String("/org/deepin/Filemanager/OcrIndex");
inline const QString kFileNameIndexDBusService = QLatin1String("org.deepin.Filemanager.FileNameIndex");
inline const QString kFileNameIndexDBusObjectPath = QLatin1String("/org/deepin/Filemanager/FileNameIndex");

// Dconfig
namespace DConf {
inline const QString kTextIndexSchema = QLatin1String("org.deepin.dde.file-manager.textindex");
inline const QString kAutoIndexUpdateInterval = QLatin1String("autoIndexUpdateInterval");
inline const QString kMonitoringStartDelaySeconds = QLatin1String("monitoringStartDelaySeconds");
inline const QString kSilentIndexUpdateDelay = QLatin1String("silentIndexUpdateDelay");
inline const QString kInotifyResourceCleanupDelay = QLatin1String("inotifyResourceCleanupDelay");
inline const QString kMaxIndexTextFileSizeMB = QLatin1String("maxIndexFileSizeMB");
inline const QString kMaxIndexFileTruncationSizeMB = QLatin1String("maxIndexFileTruncationSizeMB");
inline const QString kSupportedTextFileExtensions = QLatin1String("supportedFileExtensions");
inline const QString kIndexHiddenFiles = QLatin1String("indexHiddenFiles");
inline const QString kFolderExcludeFilters = QLatin1String("folderExcludeFilters");
inline const QString kCpuUsageLimitPercent = QLatin1String("cpuUsageLimitPercent");
inline const QString kInotifyWatchesCoefficient = QLatin1String("inotifyWatchesCoefficient");
inline const QString kBatchCommitInterval = QLatin1String("batchCommitInterval");

// Strategy optimization – environment detection
inline const QString kIdleThresholdSeconds = QLatin1String("idleThresholdSeconds");
inline const QString kLoadSampleIntervalSeconds = QLatin1String("loadSampleIntervalSeconds");
inline const QString kCpuLoadThresholdPercent = QLatin1String("cpuLoadThresholdPercent");
inline const QString kDiskBusyThresholdPercent = QLatin1String("diskBusyThresholdPercent");

// Strategy optimization – task grading thresholds
inline const QString kLightIncrementFileCountThreshold = QLatin1String("lightIncrementFileCountThreshold");
inline const QString kLightIncrementOcrFileCountThreshold = QLatin1String("lightIncrementOcrFileCountThreshold");
inline const QString kLightIncrementSizeThresholdMB = QLatin1String("lightIncrementSizeThresholdMB");

}   // namesapce DConf

// NOTE: The version number must be upgraded
// when the index contents are changed to ensure
// that the index can be rebuilt!!!
// History:
// Version 1: add "filename" filed
// Version 2: add new filed "ancestor_paths"
// Version 3: add new time-related fields
// Version 4: switch content index analyzer to NGramAnalyzer(2,2)
// Version 5: switch content index analyzer to LowerCaseNGramAnalyzer(1,2)
// Version 6: add new field "file_ext"
inline constexpr int kTextIndexVersion { 6 };

// OCR index version history:
// Version 0: initial OCR text index schema
// Version 1: switch OCR index analyzer to NGramAnalyzer(2,2)
// Version 2: switch OCR index analyzer to LowerCaseNGramAnalyzer(1,2)
// Version 3: add new field "file_ext"
inline constexpr int kOcrIndexVersion { 3 };

// Filename index version history:
// Version 1: initial filename index schema
inline constexpr int kFilenameIndexVersion { 1 };

// json - key
inline const QString kTextVersionKey = QLatin1String("version");
inline const QString kOcrVersionKey = QLatin1String("version");
inline const QString kFilenameVersionKey = QLatin1String("version");
inline const QString kLastUpdateTimeKey = QLatin1String("lastUpdateTime");
inline const QString kStateKey = QLatin1String("state");
inline const QString kNeedsRebuildKey = QLatin1String("needsRebuild");
inline const QString kCreateInProgressKey = QLatin1String("createInProgress");
inline const QString kUpdateInProgressKey = QLatin1String("updateInProgress");
inline const QString kBacklogExceededKey = QLatin1String("backlogExceeded");
inline const QString kDisabledKey = QLatin1String("disabled");

// json - value
inline const QString kStateClean = QLatin1String("clean");   // state
inline const QString kStateDirty = QLatin1String("dirty");   // state
}   // namespace Defines


ANYTHING_INDEX_END_NAMESPACE

#endif   // INDEX_GLOBAL_H
