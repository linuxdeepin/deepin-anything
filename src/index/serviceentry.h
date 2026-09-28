// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef SERVICEENTRY_H
#define SERVICEENTRY_H

namespace anything_index {

// Registers the three D-Bus service names (org.deepin.Filemanager.TextIndex /
// OcrIndex / FileNameIndex), creates the three D-Bus objects and lowers the
// process priority. Returns 0 on success, matching the old DSMRegister contract.
int registerIndexServices();

// Stops monitoring and running tasks, marks unfinished indexes dirty and
// unregisters the bus names. Returns 0, matching the old DSMUnRegister contract.
int unregisterIndexServices();

}   // namespace anything_index

#endif   // SERVICEENTRY_H
