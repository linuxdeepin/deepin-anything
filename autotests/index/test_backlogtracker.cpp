// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <gtest/gtest.h>

#include <QTemporaryDir>

#include "stubext.h"
#include <DConfig>

#include "profile/indexprofile.h"
#include "state/indexstatestore.h"
#include "task/backlogtracker.h"
#include "utils/indexutility.h"

using namespace ANYTHING_INDEX_NAMESPACE;
DCORE_USE_NAMESPACE

class FilenameBacklogTrackerTest : public testing::Test
{
protected:
    void SetUp() override
    {
        ASSERT_TRUE(directory.isValid());
        profile = IndexProfile({ IndexProfile::Type::Filename,
                                 "backlog-test",
                                 "backlog_status.json",
                                 "backlog_version",
                                 1 },
                               { [this]() { return directory.path(); },
                                 []() { return true; },
                                 [](const QString &) { return true; },
                                 [](const QString &) { return true; } });
        store = std::make_unique<IndexStateStore>(profile);
        tracker = std::make_unique<FilenameBacklogTracker>(store.get());
    }

    QTemporaryDir directory;
    IndexProfile profile;
    std::unique_ptr<IndexStateStore> store;
    std::unique_ptr<FilenameBacklogTracker> tracker;
};

TEST_F(FilenameBacklogTrackerTest, SetsAtDefaultThresholdAndClearsWhenDrained)
{
    // 屏蔽 DConfig::create：本机 dconfig 可能配置了 pending_events_trigger_updating
    // 覆盖值，置空后 threshold() 回落代码默认值 5000，测试不依赖机器配置。
    stub_ext::StubExt stub;
    stub.set_lamda(static_cast<DConfig *(*)(const QString &, const QString &, const QString &, QObject *)>(&DConfig::create),
                   [](const QString &, const QString &, const QString &, QObject *) -> DConfig * {
                       __DBG_STUB_INVOKE__
                       return nullptr;
                   });

    tracker->update(4999, 0, true);
    EXPECT_FALSE(store->isBacklogExceeded());

    tracker->update(5000, 0, true);
    EXPECT_TRUE(store->isBacklogExceeded());

    tracker->update(0, 0, true);
    EXPECT_TRUE(store->isBacklogExceeded());

    tracker->update(0, 0, false);
    EXPECT_FALSE(store->isBacklogExceeded());
}

TEST_F(FilenameBacklogTrackerTest, StartupKeepsDirtyBacklogAndResetsCleanBacklog)
{
    store->setBacklogExceeded(true);
    store->setIndexState(IndexUtility::IndexState::Dirty);
    tracker->onStartup();
    EXPECT_TRUE(store->isBacklogExceeded());

    store->setIndexState(IndexUtility::IndexState::Clean);
    tracker->onStartup();
    EXPECT_FALSE(store->isBacklogExceeded());
}

TEST_F(FilenameBacklogTrackerTest, FullScanCompletionClearsBacklog)
{
    store->setBacklogExceeded(true);
    tracker->onFullScanFinished();
    EXPECT_FALSE(store->isBacklogExceeded());
}
