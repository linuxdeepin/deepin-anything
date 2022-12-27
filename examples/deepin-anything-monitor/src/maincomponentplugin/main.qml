// SPDX-FileCopyrightText: 2022 Kingtous <me@kingtous.cn>
//
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick 2.12
import QtQuick.Controls 1.4 as C
import QtQuick.Controls 2.4
import QtQuick.Layouts 1.15
import Qt.labs.platform 1.1
import org.deepin.dtk 1.0

import com.kingtous.block_device_model 1.0
import com.kingtous.vfs_event_model 1.0

/**
        AppLoader 用于动态创建组件，它在 Preaload 创建显示
    出来后再进行创建操作，并将创建出来的组件放置在 Preload 的
    窗口内部。开发过程过不需要处理 AppLoader 的处理过程，对于
    需要使用到 Window 属性的情况，请使用其属性 AppLoader.window。

        AppLoader 中请将相互独立的组件放置在不同 Component
    中，将相互引用的组件放在在一个 Component 内，将全局属性或
    者创建逻辑不需要太复杂的控件放置在 AppLoader 的全局区域。

        AppLoader 能够配合 Loader 等动态创建的控件一起使用，
    在使用时，Loader 的 active 属性可以配合 AppLoader 的
    loaded 属性达到控件加载先后顺序的效果。
 */
AppLoader {


    BlockDeviceModel {
        id: blockDeviceModel
    }

    VfsEventModel {
        id: vfsEventModel
    }

    Component {


        ColumnLayout {
            anchors.fill: parent
            Component.onCompleted: {
                vfsEventModel.setBlockDeviceModel(blockDeviceModel);
            }
            // main frame
            RowLayout {
                Layout.fillHeight: true
                Layout.fillWidth: true

                Rectangle {
                    Layout.fillHeight: true
                    Layout.fillWidth: true

                    ColumnLayout {
                        anchors.fill: parent

                        C.TreeView {
                            id: blockDeviceListView
                            model: blockDeviceModel

                            Layout.fillWidth: true
                            Layout.fillHeight: true

                            C.TableViewColumn {
                                title: qsTr("Filter")
                                role: "filter"
                                width: 75
                                delegate: CheckBox {
                                    checked: blockDeviceModel.getCheckState(styleData.index)
                                    onCheckedChanged: {
                                        blockDeviceModel.check(styleData.index, checked);
                                        vfsEventModel.resetHitModel();
                                    }
                                }
                            }

                            C.TableViewColumn {
                                title: qsTr("Block device")
                                role: "name"
                                width: 100
                                delegate:  Text {
                                    elide: styleData.elideMode
                                    text: "/dev/"+ styleData.value //name + "(" + major + ":"+ minor + ")" + " " + size
                                    ToolTip.visible: block_device_ma.containsMouse
                                    ToolTip.text: "/dev/"+ styleData.value
                                    MouseArea {
                                        id: block_device_ma
                                        hoverEnabled: true
                                        anchors.fill: parent
                                        acceptedButtons: Qt.NoButton
                                    }
                                }
                            }

                            C.TableViewColumn {
                                width: 50
                                title: qsTr("Size")
                                role: "size"
                            }

                            C.TableViewColumn {
                                width: 150
                                title: qsTr("Mount point")
                                role: "mount"
                                delegate: Text {
                                    text: styleData.value
                                    elide: styleData.elideMode
                                    ToolTip.text: styleData.value
                                    ToolTip.visible: ma.containsMouse
                                    ToolTip.delay: 100
                                    MouseArea {
                                        id: ma
                                        anchors.fill: parent
                                        hoverEnabled: true
                                        acceptedButtons: Qt.NoButton
                                    }
                                }
                            }

                        }

                        Column {
                            Layout.minimumHeight: 200
                            Layout.fillWidth: true


                            Column {
                                anchors.centerIn: parent
                                spacing: 8

                                Button {
                                    id: btn_start_stop_control
                                    text: vfsEventModel.isRunning() ? qsTr("Stop") : qsTr("Start")
                                    highlighted: vfsEventModel.isRunning()
                                    onClicked: {
                                        vfsEventModel.setRunning(!vfsEventModel.isRunning());
                                    }

                                    Connections {
                                        target: vfsEventModel

                                        function onRunningChanged() {
                                            let isRunning = vfsEventModel.isRunning();
                                            btn_start_stop_control.text = isRunning ? qsTr("Stop") : qsTr("Start");
                                            btn_start_stop_control.highlighted = isRunning ? true: false;
                                        }
                                    }
                                }

                                Button {
                                    id: btn_clear
                                    text: qsTr("Clear")
                                    onClicked: vfsEventModel.clear();
                                }

                                Button {
                                    id: btn_export
                                    text: qsTr("Export")
                                    onClicked: {
                                        exportFileDialog.open();
                                    }

                                    FileDialog {
                                        id: exportFileDialog
                                        title: qsTr("Please choose a location")
                                        folder: shortcuts.home
                                        fileMode: FileDialog.SaveFile
                                        nameFilters: [
                                            "CSV table files (*.csv)"
                                        ]
                                        onAccepted: {
                                            vfsEventModel.exportToFile(exportFileDialog.file)
                                        }
                                        onRejected: {
                                            console.log("Canceled")
                                        }
                                    }
                                }


                                Button {
                                    id: btn_filter
                                    text: qsTr("Event Filter")
                                    onClicked: vfs_event_filter_page.open()
                                }
                            }
                        }
                    }
                }

                ColumnLayout {
                    Layout.fillHeight: true
                    Layout.fillWidth: true
                    Layout.preferredWidth: 4

                    C.TableView {
                        Layout.fillHeight: true
                        Layout.fillWidth: true

                        Component {
                            id: cell
                            Text {
                                 anchors.centerIn: parent
                                 text: styleData.value
                                 elide: styleData.elideMode
                                 ToolTip.text: styleData.value
                                 ToolTip.visible: ma_source.containsMouse
                                 ToolTip.delay: 100
                                 MouseArea {
                                     id: ma_source
                                     anchors.fill: parent
                                     hoverEnabled: true
                                     acceptedButtons: Qt.NoButton
                                 }
                             }
                        }

                        C.TableViewColumn {
                                 role: "id"
                                 title: qsTr("Id")
                                 width: 50
                                 delegate: Text {
                                     anchors.centerIn: parent
                                     elide: styleData.elideMode
                                     text: blockDeviceModel.getDeviceName(styleData.value)
                                }
                        }

                        C.TableViewColumn {
                            role: "action"
                            title: qsTr("Action")
                            width: 100
                            delegate:  Text {
                                anchors.centerIn: parent
                                elide: styleData.elideMode
                                text: vfsEventModel.getReadableAction(styleData.value)
                            }
                        }

                         C.TableViewColumn {
                             role: "source"
                             title: qsTr("Source")
                             width: 250
                             delegate: cell
                         }
                         C.TableViewColumn {
                             role: "dest"
                             title: qsTr("Destination")
                             width: 250
                             delegate: cell
                         }
                         C.TableViewColumn {
                             role: "time"
                             title: qsTr("Time")
                             width: 200
                         }
                        model: vfsEventModel
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        Layout.margins: 8

                        TextField {
                            id: searchText
                            Layout.fillWidth: true
                            onAccepted: {
                                vfsEventModel.search(searchText.text);
                            }
                        }

                        Button {
                            Layout.minimumWidth: 100
                            text: qsTr("Search")
                            focus: true
                            onClicked: {
                                vfsEventModel.search(searchText.text);
                            }
                        }

                    }
                }

            }
        }
    }

    Popup {
        id: vfs_event_filter_page
        width: 200
        height: 500
        x: 100
        y: 100
        modal: true

        contentItem: Rectangle {
            Column {
                CheckBox {
                    id: action_0
                    checked: vfsEventModel.isFiltered(0)
                    onCheckedChanged: vfsEventModel.filter(0, checked)
                    text: qsTr("New file")
                }
                CheckBox {
                    id: action_1
                    checked: vfsEventModel.isFiltered(1)
                    onCheckedChanged: vfsEventModel.filter(1, checked)
                    text: qsTr("New symlink")
                }
                CheckBox {
                    id: action_2
                    checked: vfsEventModel.isFiltered(2)
                    onCheckedChanged: vfsEventModel.filter(2, checked)
                    text: qsTr("New link")
                }

                CheckBox {
                    id: action_3
                    checked: vfsEventModel.isFiltered(3)
                    onCheckedChanged: vfsEventModel.filter(3, checked)
                    text: qsTr("New folder")
                }
                CheckBox {
                    id: action_4
                    checked: vfsEventModel.isFiltered(4)
                    onCheckedChanged: vfsEventModel.filter(4, checked)
                    text: qsTr("Delete file")
                }
                CheckBox {
                    id: action_5
                    checked: vfsEventModel.isFiltered(5)
                    onCheckedChanged: vfsEventModel.filter(5, checked)
                    text: qsTr("Delete folder")
                }
                CheckBox {
                    id: action_6
                    checked: vfsEventModel.isFiltered(6)
                    onCheckedChanged: vfsEventModel.filter(6, checked)
                    text: qsTr("Rename from file")
                }
                CheckBox {
                    id: action_7
                    checked: vfsEventModel.isFiltered(7)
                    onCheckedChanged: vfsEventModel.filter(7, checked)
                    text: qsTr("Rename from folder")
                }
                CheckBox {
                    id: action_8
                    checked: vfsEventModel.isFiltered(8)
                    onCheckedChanged: vfsEventModel.filter(8, checked)
                    text: qsTr("Rename to file")
                }
                CheckBox {
                    id: action_9
                    checked: vfsEventModel.isFiltered(9)
                    onCheckedChanged: vfsEventModel.filter(9, checked)
                    text: qsTr("Rename to folder")
                }
                CheckBox {
                    id: action_10
                    checked: vfsEventModel.isFiltered(10)
                    onCheckedChanged: vfsEventModel.filter(10, checked)
                    text: qsTr("Mount")
                }
                CheckBox {
                    id: action_11
                    checked: vfsEventModel.isFiltered(11)
                    onCheckedChanged: vfsEventModel.filter(11, checked)
                    text: qsTr("Unmount")
                }
                CheckBox {
                    id: action_12
                    checked: vfsEventModel.isFiltered(12)
                    onCheckedChanged: vfsEventModel.filter(12, checked)
                    text: qsTr("Rename file")
                }
                CheckBox {
                    id: action_13
                    checked: vfsEventModel.isFiltered(13)
                    onCheckedChanged: vfsEventModel.filter(13, checked)
                    text: qsTr("Rename folder")
                }
            }
        }

    }


}
