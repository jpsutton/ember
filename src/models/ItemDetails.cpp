// SPDX-License-Identifier: GPL-3.0-only

#include "ItemDetails.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QPointer>
#include <QUrlQuery>

#include "../jellyfin/ApiClient.h"
#include "../jellyfin/Items.h"
#include "../jellyfin/Session.h"

namespace ember {

using jellyfin::Reply;
using jellyfin::Session;

void ItemDetails::setItemId(const QString& id) {
  if (id == item_id_) return;
  item_id_ = id;
  emit itemIdChanged();
  reload();
}

void ItemDetails::reload() {
  if (item_id_.isEmpty() || Session::instance() == nullptr) return;
  load(++generation_);
}

QCoro::Task<> ItemDetails::load(quint64 generation) {
  QPointer<ItemDetails> self(this);
  Session* session = Session::instance();
  loading_ = true;
  emit loadingChanged();
  QUrlQuery query;
  query.addQueryItem(QStringLiteral("userId"), session->userId());
  query.addQueryItem(QStringLiteral("Fields"), jellyfin::ListFields() + QStringLiteral(",People"));
  const Reply reply = co_await session->api()->get(QStringLiteral("/Items/%1").arg(item_id_), query);
  if (!self || generation != generation_) co_return;
  loading_ = false;
  emit loadingChanged();
  if (!reply.ok()) co_return;
  item_ = jellyfin::ItemToVariant(reply.object(), session->api());
  people_.clear();
  for (const QJsonValue& value : reply.object().value(QStringLiteral("People")).toArray()) {
    const QJsonObject person = value.toObject();
    people_.append(QVariantMap{{QStringLiteral("name"), person.value(QStringLiteral("Name")).toString()},
                               {QStringLiteral("role"), person.value(QStringLiteral("Role")).toString()},
                               {QStringLiteral("type"), person.value(QStringLiteral("Type")).toString()}});
  }
  emit loaded();
}

void ItemDetails::setPlayed(bool played) { setPlayedTask(played); }

QCoro::Task<> ItemDetails::setPlayedTask(bool played) {
  QPointer<ItemDetails> self(this);
  Session* session = Session::instance();
  QUrlQuery query;
  query.addQueryItem(QStringLiteral("userId"), session->userId());
  const QString path = QStringLiteral("/UserPlayedItems/%1").arg(item_id_);
  const Reply reply = played ? co_await session->api()->post(path, {}, query) : co_await session->api()->del(path, query);
  if (!self || !reply.ok()) co_return;
  if (played) {
    co_await session->api()->post(QStringLiteral("/UserItems/%1/UserData").arg(item_id_),
                                  QJsonDocument(QJsonObject{{QStringLiteral("PlaybackPositionTicks"), 0}}), query);
    if (!self) co_return;
  }
  reload();
}

}  // namespace ember
