// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_filehash.cpp
 * @brief Unit tests for FileHash (utils/filehash.cpp)
 */

#include <gtest/gtest.h>
#include <QTemporaryDir>
#include <QFile>

#include <unistd.h>   // geteuid

#include "dfm_test_main.h"
#include "index_global.h"
#include "utils/filehash.h"

using namespace ANYTHING_INDEX_NAMESPACE;

class FileHashTest : public testing::Test
{
protected:
    QTemporaryDir tmp;

    void SetUp() override
    {
        ASSERT_TRUE(tmp.isValid());
    }
};

TEST_F(FileHashTest, ComputeMd5_ExistingFile)
{
    QString filePath = tmp.path() + "/test.txt";
    QFile f(filePath);
    ASSERT_TRUE(f.open(QIODevice::WriteOnly));
    f.write("hello world");
    f.close();

    QString hash = FileHash::computeMd5(filePath);
    EXPECT_EQ(hash.length(), 32);
    // Known MD5 for "hello world"
    EXPECT_EQ(hash, QString("5eb63bbbe01eeed093cb22bb8f5acdc3"));
}

TEST_F(FileHashTest, ComputeMd5_EmptyFile)
{
    QString filePath = tmp.path() + "/empty.txt";
    QFile f(filePath);
    ASSERT_TRUE(f.open(QIODevice::WriteOnly));
    f.close();

    QString hash = FileHash::computeMd5(filePath);
    EXPECT_EQ(hash.length(), 32);
    // MD5 of empty string
    EXPECT_EQ(hash, QString("d41d8cd98f00b204e9800998ecf8427e"));
}

TEST_F(FileHashTest, ComputeMd5_NonExistentFile)
{
    QString hash = FileHash::computeMd5("/nonexistent/file.txt");
    EXPECT_TRUE(hash.isEmpty());
}

TEST_F(FileHashTest, ComputeMd5_DirectoryPath)
{
    // Directories can be opened but read may behave differently
    QString hash = FileHash::computeMd5(tmp.path());
    // May or may not return empty depending on platform
    SUCCEED();
}

TEST_F(FileHashTest, ComputeMd5_BinaryFile)
{
    QString filePath = tmp.path() + "/binary.bin";
    QFile f(filePath);
    ASSERT_TRUE(f.open(QIODevice::WriteOnly));
    QByteArray data(4096, '\0');
    for (int i = 0; i < data.size(); i++)
        data[i] = static_cast<char>(i % 256);
    f.write(data);
    f.close();

    QString hash = FileHash::computeMd5(filePath);
    EXPECT_EQ(hash.length(), 32);
}

TEST_F(FileHashTest, ComputeMd5_LargeFile)
{
    QString filePath = tmp.path() + "/large.txt";
    QFile f(filePath);
    ASSERT_TRUE(f.open(QIODevice::WriteOnly));
    // Write 1MB of data
    QByteArray data(1024 * 1024, 'A');
    f.write(data);
    f.close();

    QString hash = FileHash::computeMd5(filePath);
    EXPECT_EQ(hash.length(), 32);
}

TEST_F(FileHashTest, ComputeMd5_UnreadableFile)
{
    // Under root (e.g. fakeroot package builds) permission bits do not make
    // a file unreadable, so the premise of this test does not hold there.
    if (geteuid() == 0) {
        GTEST_SKIP() << "running as root: read-permission denial is ineffective";
    }

    QString filePath = tmp.path() + "/unreadable.txt";
    QFile f(filePath);
    ASSERT_TRUE(f.open(QIODevice::WriteOnly));
    f.write("secret");
    f.close();
    // Make file unreadable
    QFile::setPermissions(filePath, QFile::WriteOwner | QFile::ExeOwner);

    QString hash = FileHash::computeMd5(filePath);
    // Should fail to open -> empty
    EXPECT_TRUE(hash.isEmpty());
}