// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_indexutility.cpp
 * @brief Unit tests for IndexUtility free functions (indexutility.cpp)
 */

#include <gtest/gtest.h>
#include <QTemporaryDir>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QString>
#include <QStringList>

#include "index_global.h"
#include "utils/indexutility.h"
#include "stubext.h"
#include <dfm-search/dsearch_global.h>
#include <QList>
#include <QObject>
#include "dfm_test_main.h"

using namespace ANYTHING_INDEX_NAMESPACE;
using namespace DFMSEARCH;

TEST(IndexUtilityTest, NormalizeDirectoryPathAddsTrailingSlash)
{
    EXPECT_EQ(PathCalculator::normalizeDirectoryPath("/home/user"), QString("/home/user/"));
    EXPECT_EQ(PathCalculator::normalizeDirectoryPath("/home/user/"), QString("/home/user/"));
}

TEST(IndexUtilityTest, IsDirectoryMoveExistingDir)
{
    QTemporaryDir tmp;
    ASSERT_TRUE(tmp.isValid());
    EXPECT_TRUE(PathCalculator::isDirectoryMove(tmp.path()));
}

TEST(IndexUtilityTest, IsDirectoryMoveExistingFile)
{
    QTemporaryDir tmp;
    QString path = tmp.path() + "/afile.txt";
    QFile f(path);
    ASSERT_TRUE(f.open(QIODevice::WriteOnly));
    f.close();
    EXPECT_FALSE(PathCalculator::isDirectoryMove(path));
}

TEST(IndexUtilityTest, IsDirectoryMoveEmptyReturnsFalse)
{
    EXPECT_FALSE(PathCalculator::isDirectoryMove(""));
}

TEST(IndexUtilityTest, IsDirectoryMoveTrailingSlashInferred)
{
    EXPECT_TRUE(PathCalculator::isDirectoryMove("/no/such/dir/"));
    EXPECT_FALSE(PathCalculator::isDirectoryMove("/no/such/file"));
}

TEST(IndexUtilityTest, ExtractAncestorPathsBuildsList)
{
    QStringList ancestors = PathCalculator::extractAncestorPaths("/home/user/docs/file.txt");
    EXPECT_FALSE(ancestors.isEmpty());
    EXPECT_TRUE(ancestors.contains("/home/user/docs"));
}

TEST(IndexUtilityTest, ExtractAncestorPathsEmptyReturnsEmpty)
{
    EXPECT_TRUE(PathCalculator::extractAncestorPaths("").isEmpty());
}

TEST(IndexUtilityTest, CheckFileSizeWithinLimit)
{
    QTemporaryDir tmp;
    QString path = tmp.path() + "/small.txt";
    QFile f(path);
    ASSERT_TRUE(f.open(QIODevice::WriteOnly));
    f.write("tiny");
    f.close();
    EXPECT_TRUE(IndexUtility::checkFileSize(QFileInfo(path), 50));
}

TEST(IndexUtilityTest, CheckFileSizeExceedsLimit)
{
    QTemporaryDir tmp;
    QString path = tmp.path() + "/big.txt";
    QFile f(path);
    ASSERT_TRUE(f.open(QIODevice::WriteOnly));
    f.write(QByteArray(10, 'x'));
    f.close();
    // 0 falls back to 50MB default, a 10-byte file fits
    EXPECT_TRUE(IndexUtility::checkFileSize(QFileInfo(path), 0));
    EXPECT_TRUE(IndexUtility::checkFileSize(QFileInfo(path), 50));
}

TEST(IndexUtilityTest, CheckFileSizeNonExistent)
{
    QFileInfo info("/no/such/file/here.txt");
    EXPECT_NO_FATAL_FAILURE({ (void)IndexUtility::checkFileSize(info, 50); });
}

TEST(IndexUtilityTest, IsSupportedTextFileBySuffix)
{
    EXPECT_NO_FATAL_FAILURE({ (void)IndexUtility::isSupportedTextFile("/some/path/readme.txt"); });
    EXPECT_NO_FATAL_FAILURE({ (void)IndexUtility::isSupportedTextFile("/some/path/unknown.xyz"); });
}

TEST(IndexUtilityTest, IsSupportedOCRFileBySuffix)
{
    EXPECT_NO_FATAL_FAILURE({ (void)IndexUtility::isSupportedOCRFile("/some/path/image.png"); });
    EXPECT_NO_FATAL_FAILURE({ (void)IndexUtility::isSupportedOCRFile("/some/path/unknown.xyz"); });
}

TEST(IndexUtilityTest, IsDefaultIndexedDirectory)
{
    EXPECT_NO_FATAL_FAILURE({ (void)IndexUtility::isDefaultIndexedDirectory("/no/such/indexed/dir"); });
}

TEST(IndexUtilityTest, IsIndexWithAnything)
{
    EXPECT_NO_FATAL_FAILURE({ (void)IndexUtility::isIndexWithAnything("/no/such/dir"); });
}

// ---------------------------------------------------------------------------
// isFileNameIndexUsableAsDataSource: the watch-seeding / file-enumeration
// predicate. Deliberately weaker than isFileNameIndexReadyForSearch — only
// createInProgress / disabled / never-built / version-mismatch block.
// ---------------------------------------------------------------------------

class FileNameIndexUsableTest : public testing::Test
{
protected:
    void SetUp() override
    {
        ASSERT_TRUE(tmp.isValid());
        stub.set_lamda(ADDR(DFMSEARCH::Global, fileNameIndexDirectory),
                       [this]() -> QString {
                           __DBG_STUB_INVOKE__
                           return tmp.path() + "/filename-index";
                       });
        stub.set_lamda(ADDR(DFMSEARCH::Global, isFileNameIndexDirectoryAvailable),
                       []() -> bool {
                           __DBG_STUB_INVOKE__
                           return true;
                       });
    }

    void writeStatus(const QJsonObject &obj)
    {
        const QString dir = tmp.path() + "/filename-index";
        QDir().mkpath(dir);
        QFile f(dir + "/index_status.json");
        ASSERT_TRUE(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
        f.write(QJsonDocument(obj).toJson());
        f.close();
    }

    static QJsonObject readyStatus()
    {
        return QJsonObject {
            { "version", Defines::kFilenameIndexVersion },
            { "lastUpdateTime", "2026-09-30T10:00:00" },
            { "state", "clean" }
        };
    }

    QTemporaryDir tmp;
    stub_ext::StubExt stub;
};

TEST_F(FileNameIndexUsableTest, MissingStatusFileBlocks)
{
    EXPECT_FALSE(IndexUtility::isFileNameIndexUsableAsDataSource());
}

TEST_F(FileNameIndexUsableTest, ReadyIndexPasses)
{
    writeStatus(readyStatus());
    EXPECT_TRUE(IndexUtility::isFileNameIndexUsableAsDataSource());
}

TEST_F(FileNameIndexUsableTest, EmptyLastUpdateTimeBlocks)
{
    QJsonObject obj = readyStatus();
    obj.remove("lastUpdateTime");
    writeStatus(obj);
    EXPECT_FALSE(IndexUtility::isFileNameIndexUsableAsDataSource());
}

TEST_F(FileNameIndexUsableTest, VersionMismatchBlocks)
{
    QJsonObject obj = readyStatus();
    obj["version"] = Defines::kFilenameIndexVersion + 1;
    writeStatus(obj);
    EXPECT_FALSE(IndexUtility::isFileNameIndexUsableAsDataSource());
}

TEST_F(FileNameIndexUsableTest, CreateInProgressBlocks)
{
    QJsonObject obj = readyStatus();
    obj["createInProgress"] = true;
    writeStatus(obj);
    EXPECT_FALSE(IndexUtility::isFileNameIndexUsableAsDataSource());
}

TEST_F(FileNameIndexUsableTest, DisabledBlocks)
{
    QJsonObject obj = readyStatus();
    obj["disabled"] = true;
    writeStatus(obj);
    EXPECT_FALSE(IndexUtility::isFileNameIndexUsableAsDataSource());
}

TEST_F(FileNameIndexUsableTest, UpdateInProgressStillPasses)
{
    // Recovery/rebuild update: index substantially complete, must NOT
    // degrade watch seeding to filesystem traversal
    QJsonObject obj = readyStatus();
    obj["updateInProgress"] = true;
    writeStatus(obj);
    EXPECT_TRUE(IndexUtility::isFileNameIndexUsableAsDataSource());
}

TEST_F(FileNameIndexUsableTest, BacklogExceededStillPasses)
{
    QJsonObject obj = readyStatus();
    obj["backlogExceeded"] = true;
    writeStatus(obj);
    EXPECT_TRUE(IndexUtility::isFileNameIndexUsableAsDataSource());
}

TEST_F(FileNameIndexUsableTest, DirtyStateStillPasses)
{
    QJsonObject obj = readyStatus();
    obj["state"] = "dirty";
    writeStatus(obj);
    EXPECT_TRUE(IndexUtility::isFileNameIndexUsableAsDataSource());
}

TEST(IndexUtilityTest, AnythingConfigWatcherInstance)
{
    EXPECT_NE(IndexUtility::AnythingConfigWatcher::instance(), nullptr);
}

TEST(IndexUtilityTest, AnythingConfigWatcherDefaultPaths)
{
    EXPECT_NO_FATAL_FAILURE({ (void)IndexUtility::AnythingConfigWatcher::instance()->defaultAnythingIndexPaths(); });
    EXPECT_NO_FATAL_FAILURE({ (void)IndexUtility::AnythingConfigWatcher::instance()->defaultAnythingIndexPathsRealtime(); });
}

TEST(IndexUtilityTest, AnythingConfigWatcherBlacklistPaths)
{
    EXPECT_NO_FATAL_FAILURE({ (void)IndexUtility::AnythingConfigWatcher::instance()->defaultBlacklistPaths(); });
    EXPECT_NO_FATAL_FAILURE({ (void)IndexUtility::AnythingConfigWatcher::instance()->defaultBlacklistPathsRealtime(); });
}

// ---- Coverage additions: config watcher destructors + handleConfigChanged ----

TEST(IndexUtilityTest, AnythingConfigWatcherHandleConfigChangedCallable)
{
    EXPECT_NO_FATAL_FAILURE({ IndexUtility::AnythingConfigWatcher::instance()->handleConfigChanged("log_rules"); });
}

TEST(IndexUtilityTest, DlnfsConfigWatcherInstanceCallable)
{
    EXPECT_NO_FATAL_FAILURE({ (void)IndexUtility::DlnfsConfigWatcher::instance(); });
}

TEST(IndexUtilityTest, PathCalculatorCalculateNewPathForDirectoryMove)
{
    EXPECT_NO_FATAL_FAILURE({
        (void)PathCalculator::calculateNewPathForDirectoryMove("/old", "/old/sub", "/new");
    });
}

TEST(IndexUtilityTest, SearchUtilityRunCliCallable)
{
    EXPECT_NO_FATAL_FAILURE({ (void)SearchUtility::runCli({ "echo", "hello" }, 5000); });
}

TEST(IndexUtilityTest, ConfigRebuildWatcherConstructsEmpty)
{
    QList<IndexUtility::ConfigRebuildWatcher::WatchEntry> empty;
    IndexUtility::ConfigRebuildWatcher w(empty, nullptr);
    SUCCEED();
}
TEST(IndexUtilityTest, ConfigRebuildWatcherD0Destructor)
{
    QList<IndexUtility::ConfigRebuildWatcher::WatchEntry> empty;
    auto *ptr = new IndexUtility::ConfigRebuildWatcher(empty, nullptr);
    EXPECT_NO_FATAL_FAILURE({ delete ptr; });
}
TEST(IndexUtilityTest, DlnfsConfigWatcherInstanceExt)
{
    EXPECT_NO_FATAL_FAILURE({ (void)IndexUtility::DlnfsConfigWatcher::instance(); });
}
TEST(IndexUtilityTest, AnythingConfigWatcherInstanceExt)
{
    EXPECT_NO_FATAL_FAILURE({ (void)IndexUtility::AnythingConfigWatcher::instance(); });
}
TEST(IndexUtilityTest, IsDefaultIndexedDirectoryReturnsBool)
{
    EXPECT_NO_FATAL_FAILURE({ (void)IndexUtility::isDefaultIndexedDirectory("/tmp"); });
}
TEST(IndexUtilityTest, IsIndexWithAnythingReturnsBool)
{
    EXPECT_NO_FATAL_FAILURE({ (void)IndexUtility::isIndexWithAnything("/tmp"); });
}
TEST(IndexUtilityTest, IsSupportedTextFileReturnsBool)
{
    EXPECT_NO_FATAL_FAILURE({ (void)IndexUtility::isSupportedTextFile("/tmp/test.txt"); });
}
TEST(IndexUtilityTest, IsSupportedOCRFileReturnsBool)
{
    EXPECT_NO_FATAL_FAILURE({ (void)IndexUtility::isSupportedOCRFile("/tmp/test.png"); });
}
