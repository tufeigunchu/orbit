// Copyright (c) 2020 The Orbit Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "OrbitGgp/InstanceItemModel.h"

#include <QDateTime>
#include <QLatin1String>
#include <QString>
#include <algorithm>
#include <iterator>
#include <utility>

#include "OrbitBase/Logging.h"
#include "OrbitGgp/Instance.h"

namespace orbit_ggp {

InstanceItemModel::InstanceItemModel(QVector<Instance> instances, QObject* parent)
    : QAbstractItemModel(parent), instances_(std::move(instances)) {
  std::sort(instances_.begin(), instances_.end(), &Instance::CmpById);
}

int InstanceItemModel::columnCount(const QModelIndex& parent) const {
  return parent.isValid() ? 0 : static_cast<int>(Columns::kEnd);
}

QVariant InstanceItemModel::data(const QModelIndex& index, int role) const {
  CHECK(index.isValid());
  CHECK(index.model() == this);
  CHECK(index.row() < instances_.size());  // instances_.size());

  const Instance& current_instance = instances_[index.row()];

  if (role == Qt::UserRole) return QVariant::fromValue(current_instance);

  if (role != Qt::DisplayRole) return {};

  switch (static_cast<Columns>(index.column())) {
    case Columns::kDisplayName:
      return current_instance.display_name;
    case Columns::kId:
      return current_instance.id;
    case Columns::kIpAddress:
      return current_instance.ip_address;
    case Columns::kLastUpdated:
      return current_instance.last_updated.toString(Qt::TextDate);
    case Columns::kOwner:
      return current_instance.owner;
    case Columns::kPool:
      return current_instance.pool;
    case Columns::kState:
      return current_instance.state;
    case Columns::kEnd:
      CHECK(false);
      return {};
  }

  CHECK(false);  // That means, someone (me?) forgot a column.
  return {};
}

QModelIndex InstanceItemModel::index(int row, int col, const QModelIndex& parent) const {
  if (parent.isValid()) return {};
  if (row < 0 || row >= instances_.size()) return {};
  if (col < 0 || col >= static_cast<int>(Columns::kEnd)) return {};

  return createIndex(row, col, nullptr);
}

QVariant InstanceItemModel::headerData(int section, Qt::Orientation orientation, int role) const {
  if (role != Qt::DisplayRole) return {};
  if (orientation != Qt::Horizontal) return {};
  if (section < 0 || section >= static_cast<int>(Columns::kEnd)) {
    return {};
  }

  switch (static_cast<Columns>(section)) {
    case Columns::kDisplayName:
      return QLatin1String("Display Name");
    case Columns::kId:
      return QLatin1String("ID");
    case Columns::kIpAddress:
      return QLatin1String("IP Address");
    case Columns::kLastUpdated:
      return QLatin1String("Last Updated");
    case Columns::kOwner:
      return QLatin1String("Owner");
    case Columns::kPool:
      return QLatin1String("Pool");
    case Columns::kState:
      return QLatin1String("State");
    case Columns::kEnd:
      UNREACHABLE();
      return {};
  }

  CHECK(false);
  return {};
}

QModelIndex InstanceItemModel::parent(const QModelIndex& /*child*/) const { return {}; }

int InstanceItemModel::rowCount(const QModelIndex& parent) const {
  return parent.isValid() ? 0 : instances_.size();
}

void InstanceItemModel::SetInstances(QVector<Instance> new_instances) {
  std::sort(new_instances.begin(), new_instances.end(), &Instance::CmpById);

  QVector<Instance>& old_instances(instances_);

  QVector<Instance>::iterator old_iter = old_instances.begin();
  QVector<Instance>::iterator new_iter = new_instances.begin();

  while (old_iter != old_instances.end() && new_iter != new_instances.end()) {
    const int current_row = static_cast<int>(std::distance(old_instances.begin(), old_iter));

    if (old_iter->id == new_iter->id) {
      if (*old_iter != *new_iter) {
        *old_iter = *new_iter;
        emit dataChanged(index(current_row, 0, {}),
                         index(current_row, static_cast<int>(Columns::kEnd) - 1, {}));
      }
      ++old_iter;
      ++new_iter;
    } else if (old_iter->id < new_iter->id) {
      beginRemoveRows({}, current_row, current_row);
      old_iter = old_instances.erase(old_iter);
      endRemoveRows();
    } else {
      beginInsertRows({}, current_row, current_row);
      old_iter = old_instances.insert(old_iter, *new_iter);
      ++old_iter;
      ++new_iter;
      endInsertRows();
    }
  }

  if (old_iter == old_instances.end() && new_iter != new_instances.end()) {
    beginInsertRows({}, old_instances.size(), new_instances.size() - 1);
    std::copy(new_iter, new_instances.end(), std::back_inserter(old_instances));
    CHECK(old_instances.size() == new_instances.size());
    endInsertRows();
  } else if (old_iter != old_instances.end() && new_iter == new_instances.end()) {
    beginRemoveRows({}, new_instances.size(), old_instances.size() - 1);
    old_instances.erase(old_iter, old_instances.end());
    CHECK(old_instances.size() == new_instances.size());
    endRemoveRows();
  }
}

}  // namespace orbit_ggp
