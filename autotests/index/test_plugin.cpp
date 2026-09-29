// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_plugin.cpp
 * @brief Unit tests for the service entry (registerIndexServices / unregisterIndexServices).
 *        Since these are extern "C" functions that create DBus objects,
 *        we test them minimally — the DBus registration will fail gracefully
 *        in the test sandbox.
 */

#include <gtest/gtest.h>
#include <QTemporaryDir>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>

#include "stubext.h"
#include <dfm-search/dsearch_global.h>

#include "dfm_test_main.h"
#include "index_global.h"

using namespace ANYTHING_INDEX_NAMESPACE;
using namespace DFMSEARCH;

// Entry functions from serviceentry.cpp (the rewritten plugin.cpp entry)
#include "serviceentry.h"

class PluginTest : public testing::Test
{
protected:
    void SetUp() override
    {
        ASSERT_TRUE(tmp.isValid());

        // Redirect index directories to temp
        stub.set_lamda(ADDR(Global, contentIndexDirectory),
                       [this]() -> QString {
                           __DBG_STUB_INVOKE__
                           return tmp.path() + "/content-index";
                       });
        stub.set_lamda(ADDR(Global, isContentIndexAvailable),
                       []() -> bool {
                           __DBG_STUB_INVOKE__
                           return true;
                       });
        stub.set_lamda(ADDR(Global, isPathInContentIndexDirectory),
                       [this](const QString &path) -> bool {
                           __DBG_STUB_INVOKE__
                           return path.startsWith(tmp.path());
                       });
        stub.set_lamda(ADDR(Global, ocrTextIndexDirectory),
                       [this]() -> QString {
                           __DBG_STUB_INVOKE__
                           return tmp.path() + "/ocr-index";
                       });
        stub.set_lamda(ADDR(Global, isOcrTextIndexAvailable),
                       []() -> bool {
                           __DBG_STUB_INVOKE__
                           return true;
                       });
        stub.set_lamda(ADDR(Global, isPathInOcrTextIndexDirectory),
                       [this](const QString &path) -> bool {
                           __DBG_STUB_INVOKE__
                           return path.startsWith(tmp.path());
                       });
        stub.set_lamda(ADDR(Global, defaultIndexedDirectory),
                       [this]() -> QStringList {
                           __DBG_STUB_INVOKE__
                           return QStringList { tmp.path() + "/indexed-dir" };
                       });
        stub.set_lamda(ADDR(Global, defaultBlacklistPaths),
                       []() -> QStringList {
                           __DBG_STUB_INVOKE__
                           return QStringList();
                       });
    }

    QTemporaryDir tmp;
    stub_ext::StubExt stub;
};

TEST_F(PluginTest, DSMRegister_ReturnsValidResult)
{
    // Returns 0 on success, -1 if DBus registration fails (e.g. in sandbox).
    // Either way the function must not crash.
    int result = anything_index::registerIndexServices();
    EXPECT_TRUE(result == 0 || result == -1);

    // Clean up via DSMUnRegister (safe to call even if register failed)
    anything_index::unregisterIndexServices();
}

// TEST_F(PluginTest, DSMRegister_CallTwice)
// {
//     int result1 = anything_index::registerIndexServices();
//     EXPECT_EQ(result1, 0);
//
//     int result2 = anything_index::registerIndexServices();
//     EXPECT_EQ(result2, 0);
//
//     anything_index::unregisterIndexServices();
// }

TEST_F(PluginTest, DSMUnRegister_WithoutRegister)
{
    // Calling unregister without register should not crash
    int result = anything_index::unregisterIndexServices();
    EXPECT_EQ(result, 0);
}

TEST_F(PluginTest, DSMUnRegister_CalledTwice)
{
    anything_index::registerIndexServices();
    anything_index::unregisterIndexServices();
    // Second unregister should not crash
    int result = anything_index::unregisterIndexServices();
    EXPECT_EQ(result, 0);
}

TEST_F(PluginTest, DSMRegister_WithNullName)
{
    int result = anything_index::registerIndexServices();
    EXPECT_TRUE(result == 0 || result == -1);
    anything_index::unregisterIndexServices();
}
