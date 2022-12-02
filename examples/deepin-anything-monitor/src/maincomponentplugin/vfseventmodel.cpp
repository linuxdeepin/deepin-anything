#include "vfseventmodel.h"

#include <signal.h>

#include <QAbstractItemModelTester>
#include <QDebug>
#include <QDesktopServices>
#include <QFile>
#include <QString>
#include <QUrl>

#include "blockdeviceitem.h"
#include "dagenlclient.h"

VfsEventModel::VfsEventModel(QObject *parent) : QAbstractTableModel(parent) {
  connect(&DAGenlClient::ref(), SIGNAL(onVfsEvent(VfsEvent)), this,
          SLOT(insertVfsEvent(VfsEvent)));
  // currently has 14 events
  filtered = QVector<bool>(14, true);
}

int VfsEventModel::rowCount(const QModelIndex &parent) const {
  return show_evts_.count();
}

int VfsEventModel::columnCount(const QModelIndex &parent) const { return 5; }

QVariant VfsEventModel::data(const QModelIndex &index, int role) const {
  if (!index.isValid())
    return QVariant();
  auto cols = index.column();
  auto rows = index.row();

  if (role > Qt::UserRole) {
    auto s = show_evts_[rows].toVariant(role - VfsRole::IdRole);
    return s;
  }
  if (role == Qt::DisplayRole) {
    auto s = show_evts_[rows].toVariant(cols);
    return s;
  }
  if (role == Qt::TextAlignmentRole) {
    return Qt::AlignLeft;
  }
  return QVariant();
}

void VfsEventModel::setRunning(bool running) {
  running_ = running;
  qDebug() << "set running state:" << running;
  emit runningChanged();
}

bool VfsEventModel::isRunning() { return running_; }

bool VfsEventModel::isFiltered(int index) { return filtered[index]; }

void VfsEventModel::filter(int index, bool checked) {
  filtered[index] = checked;
  emit filterChanged(index, checked);
  resetHitModel();
}

QString VfsEventModel::getReadableAction(int action) {
  switch (action) {
  case ACT_NEW_FILE:
    return tr("New file");
  case ACT_NEW_SYMLINK:
    return tr("New symlink");
  case ACT_NEW_LINK:
    return tr("New link");
  case ACT_NEW_FOLDER:
    return tr("New folder");
  case ACT_DEL_FILE:
    return tr("Delete file");
  case ACT_DEL_FOLDER:
    return tr("Delete folder");
  case ACT_RENAME_FROM_FILE:
    return tr("Rename from file");
  case ACT_RENAME_FROM_FOLDER:
    return tr("Rename from folder");
  case ACT_RENAME_TO_FILE:
    return tr("Rename to file");
  case ACT_RENAME_TO_FOLDER:
    return tr("Rename to folder");
  case ACT_MOUNT:
    return tr("Mount");
  case ACT_UNMOUNT:
    return tr("Unmount");
  case ACT_RENAME_FILE:
    return tr("Rename file");
  case ACT_RENAME_FOLDER:
    return tr("Rename folder");
  default:
    return tr("Unknown");
  }
}

void VfsEventModel::exportToFile(QUrl path) {
  auto url = path.toLocalFile();
  if (!url.endsWith(".csv")) {
    url += ".csv";
  }
  QFile file(url);
  bool res = file.open(QIODevice::WriteOnly);
  if (!res) {
    qDebug() << path << "not opened successfully";
  }
  file.write(VfsEvent::headerRow());
  int rows = rowCount();
  int columns = columnCount();
  for (int i = 0; i < rows; i++) {
    QString textData;
    textData += "\n";
    for (int j = 0; j < columns; j++) {
      if (j == 0) {
        textData +=
            block_device_model->getDeviceName(data(index(i, j)).toString());
      } else if (j == 1) {
        textData += getReadableAction(data(index(i, j)).toInt());
      } else {
        textData += data(index(i, j)).toString();
      }
      textData += ","; // for .csv file format
    }
    file.write(textData.toStdString().c_str(), textData.length());
  }
  file.close();
  if (file.exists()) {
    QDesktopServices::openUrl(QUrl(url));
  }
}

void VfsEventModel::search(QString searchText) {
  this->searchText = std::move(searchText);
  qDebug() << "search ";
  resetHitModel();
}

void VfsEventModel::setBlockDeviceModel(BlockDeviceModel *model) {
  this->block_device_model = model;
}

void VfsEventModel::clear() {
  beginResetModel();
  show_evts_.clear();
  evts_.clear();
  endResetModel();
}

void VfsEventModel::insertVfsEvent(const VfsEvent &evt) {
  if (isRunning()) {
    evts_.push_front(evt);
    if (isHitFilter(evt)) {
      beginInsertRows(QModelIndex(), 0, 0);
      show_evts_.push_front(std::move(evt));
      endInsertRows();
    }
  }
}

bool VfsEventModel::isHitFilter(const VfsEvent &evt) {
  auto hit = true;
  // searchText
  if (!searchText.isEmpty()) {
    if (!evt.dst_.contains(searchText) && !evt.src_.contains(searchText)) {
      return false;
    }
  }
  // dev
  if (block_device_model != nullptr) {
    hit = false;
    for (auto &devItem : block_device_model->checkedItems()) {
      if (devItem->getMajor() == evt.major_ &&
          devItem->getMinor() == evt.minor_) {
        hit = true;
      }
    }
  }
  // event type
  if (!filtered[evt.act_]) {
    return false;
  }
  return hit;
}

void VfsEventModel::resetHitModel() {
  beginResetModel();
  this->show_evts_.clear();
  for (auto &evt : evts_) {
    if (isHitFilter(evt)) {
      this->show_evts_.append(evt);
    }
  }
  qDebug() << "filtered events count: " << this->show_evts_.length();
  endResetModel();
}

QHash<int, QByteArray> VfsEventModel::roleNames() const {
  return {{VfsRole::ActionRole, "action"},
          {VfsRole::IdRole, "id"},
          {VfsRole::SrcRole, "source"},
          {VfsRole::DstRole, "dest"},
          {VfsRole::TimeRole, "time"}};
}
