// Copyright (c) 2020 The Orbit Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "OrbitGgp/Instance.h"

#include <QByteArray>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <Qt>
#include <utility>

#include "OrbitBase/Result.h"
#include "OrbitGgp/Error.h"

namespace orbit_ggp {

namespace {

ErrorMessageOr<Instance> GetInstanceFromJson(const QJsonObject& obj) {
  const auto process = [](const QJsonValue& val) -> ErrorMessageOr<QString> {
    if (!val.isString()) {
      return ErrorMessage{"Unable to parse JSON: String expected."};
    }
    return val.toString();
  };

  OUTCOME_TRY(auto&& display_name, process(obj.value("displayName")));
  OUTCOME_TRY(auto&& id, process(obj.value("id")));
  OUTCOME_TRY(auto&& ip_address, process(obj.value("ipAddress")));
  OUTCOME_TRY(auto&& last_updated, process(obj.value("lastUpdated")));
  OUTCOME_TRY(auto&& owner, process(obj.value("owner")));
  OUTCOME_TRY(auto&& pool, process(obj.value("pool")));
  OUTCOME_TRY(auto&& state, process(obj.value("state")));

  auto last_updated_date_time = QDateTime::fromString(last_updated, Qt::ISODate);
  if (!last_updated_date_time.isValid()) {
    return ErrorMessage{"Unable to parse JSON: DateTime expected."};
  }

  Instance inst{};

  inst.display_name = display_name;
  inst.id = id;
  inst.ip_address = ip_address;
  inst.last_updated = last_updated_date_time;
  inst.owner = owner;
  inst.pool = pool;
  inst.state = state;

  return inst;
}

}  // namespace

ErrorMessageOr<QVector<Instance>> Instance::GetListFromJson(const QByteArray& json) {
  const QJsonDocument doc = QJsonDocument::fromJson(json);

  if (!doc.isArray()) return ErrorMessage{"Unable to parse JSON: Array expected."};

  const QJsonArray arr = doc.array();

  QVector<Instance> list;

  for (const auto& json_value : arr) {
    if (!json_value.isObject()) return ErrorMessage{"Unable to parse JSON: Object expected."};

    OUTCOME_TRY(auto&& instance, GetInstanceFromJson(json_value.toObject()));
    list.push_back(std::move(instance));
  }

  return list;
}

ErrorMessageOr<Instance> Instance::CreateFromJson(const QByteArray& json) {
  const QJsonDocument doc = QJsonDocument::fromJson(json);

  if (!doc.isObject()) return ErrorMessage{"Unable to parse JSON: Object expected."};

  return GetInstanceFromJson(doc.object());
}

bool Instance::CmpById(const Instance& lhs, const Instance& rhs) { return lhs.id < rhs.id; }

}  // namespace orbit_ggp
