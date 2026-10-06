// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <QObject>
#include <QVariant>
#include <QtQml/qqmlregistration.h>

#include <QCoroTask>

namespace ember {

// One item with everything the info screen shows, cast included.
class ItemDetails : public QObject {
  Q_OBJECT
  QML_ELEMENT
  Q_PROPERTY(QString itemId READ itemId WRITE setItemId NOTIFY itemIdChanged)
  Q_PROPERTY(QVariantMap item READ item NOTIFY loaded)
  // {name, role, type}
  Q_PROPERTY(QVariantList people READ people NOTIFY loaded)
  Q_PROPERTY(bool loading READ loading NOTIFY loadingChanged)

 public:
  using QObject::QObject;

  QString itemId() const { return item_id_; }
  void setItemId(const QString& id);
  QVariantMap item() const { return item_; }
  QVariantList people() const { return people_; }
  bool loading() const { return loading_; }

  Q_INVOKABLE void reload();
  Q_INVOKABLE void setPlayed(bool played);

 signals:
  void itemIdChanged();
  void loaded();
  void loadingChanged();

 private:
  QCoro::Task<> load(quint64 generation);
  QCoro::Task<> setPlayedTask(bool played);

  QString item_id_;
  QVariantMap item_;
  QVariantList people_;
  bool loading_ = false;
  quint64 generation_ = 0;
};

}  // namespace ember
