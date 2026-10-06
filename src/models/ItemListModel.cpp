// SPDX-License-Identifier: GPL-3.0-only

#include "ItemListModel.h"

#include <QJsonArray>
#include <QLoggingCategory>
#include <QPointer>
#include <QUrlQuery>

#include <QCoroTimer>

#include <algorithm>

#include "../jellyfin/ApiClient.h"
#include "../jellyfin/Items.h"
#include "../jellyfin/Session.h"

namespace ember {
namespace {

constexpr int kPageSize = 100;

using jellyfin::ApiClient;
using jellyfin::Reply;
using jellyfin::Session;

// Keys every row map has, exposed as roles of the same name.
const QList<QByteArray>& FieldNames() {
  static const QList<QByteArray> names = {
      "id",          "name",          "type",          "isFolder",       "playable",       "collectionType",
      "sortName",    "originalTitle", "overview",      "tagline",        "year",           "premiereDate",
      "dateCreated", "officialRating", "communityRating", "criticRating", "runtimeTicks",   "runtimeText",
      "genres",      "studios",       "seriesId",      "seriesName",     "seasonId",       "seasonName",
      "indexNumber", "parentIndexNumber", "episodeLabel", "childCount",  "recursiveCount", "played",
      "playedPercentage", "playbackPositionTicks", "resumeText", "unplayedCount", "isFavorite", "status",
      "poster",      "backdrop",      "logo",          "thumb",          "videoFlags",     "audioFlags",
      "hasSubtitles"};
  return names;
}

QString LetterOf(const QString& sort_name) {
  if (sort_name.isEmpty()) return QStringLiteral("#");
  const QChar first = sort_name.at(0).toUpper();
  return first.isLetter() && first.unicode() < 128 ? QString(first) : QStringLiteral("#");
}

}  // namespace

ItemListModel::ItemListModel(QObject* parent) : QAbstractListModel(parent) {
  reload_timer_.setSingleShot(true);
  reload_timer_.setInterval(0);
  connect(&reload_timer_, &QTimer::timeout, this, &ItemListModel::reload);
}

int ItemListModel::rowCount(const QModelIndex& parent) const { return parent.isValid() ? 0 : int(rows_.size()); }

QVariant ItemListModel::data(const QModelIndex& index, int role) const {
  if (!index.isValid() || index.row() >= rows_.size()) return {};
  const QVariantMap& row = rows_.at(index.row());
  if (role == ItemRole) return row;
  if (role == Qt::DisplayRole) return row.value(QStringLiteral("name"));
  const int field = role - FirstFieldRole;
  if (field < 0 || field >= FieldNames().size()) return {};
  return row.value(QString::fromLatin1(FieldNames().at(field)));
}

QHash<int, QByteArray> ItemListModel::roleNames() const {
  QHash<int, QByteArray> roles{{ItemRole, "item"}, {Qt::DisplayRole, "display"}};
  for (int i = 0; i < FieldNames().size(); ++i) roles.insert(FirstFieldRole + i, FieldNames().at(i));
  return roles;
}

bool ItemListModel::pagedMode() const {
  return mode_ == QLatin1String("items") || mode_ == QLatin1String("resume") || mode_ == QLatin1String("nextup") ||
         mode_ == QLatin1String("search");
}

bool ItemListModel::canFetchMore(const QModelIndex& parent) const {
  return !parent.isValid() && pagedMode() && !loading_ && !exhausted_ && rows_.size() < total_;
}

void ItemListModel::fetchMore(const QModelIndex& parent) {
  if (!canFetchMore(parent)) return;
  fetchPage(generation_, int(rows_.size()));
}

void ItemListModel::scheduleReload() { reload_timer_.start(); }

void ItemListModel::reload() {
  reload_timer_.stop();
  ++generation_;
  beginResetModel();
  rows_.clear();
  total_ = 0;
  exhausted_ = false;
  endResetModel();
  emit countChanged();
  setError(QString());
  setLoading(false);
  if (Session::instance() == nullptr || Session::instance()->state() != Session::State::SignedIn) return;
  if (mode_ == QLatin1String("search") && search_term_.isEmpty()) return;
  fetchPage(generation_, 0);
}

void ItemListModel::setLoading(bool loading) {
  if (loading == loading_) return;
  loading_ = loading;
  emit loadingChanged();
}

void ItemListModel::setError(const QString& error) {
  if (error == error_string_) return;
  error_string_ = error;
  emit errorStringChanged();
}

void ItemListModel::appendRows(const QJsonArray& items) {
  if (items.isEmpty()) return;
  ApiClient* api = Session::instance()->api();
  beginInsertRows({}, int(rows_.size()), int(rows_.size() + items.size() - 1));
  for (const QJsonValue& value : items) rows_.append(jellyfin::ItemToVariant(value.toObject(), api));
  endInsertRows();
  emit countChanged();
}

QCoro::Task<> ItemListModel::fetchPage(quint64 generation, int start) {
  QPointer<ItemListModel> self(this);
  Session* session = Session::instance();
  ApiClient* api = session->api();
  const QString user_id = session->userId();
  setLoading(true);

  QUrlQuery query;
  query.addQueryItem(QStringLiteral("userId"), user_id);
  query.addQueryItem(QStringLiteral("Fields"), jellyfin::ListFields());
  query.addQueryItem(QStringLiteral("EnableImageTypes"), QStringLiteral("Primary,Backdrop,Logo,Thumb"));
  query.addQueryItem(QStringLiteral("ImageTypeLimit"), QStringLiteral("1"));
  QString path;
  const bool paged = pagedMode();
  if (paged) {
    query.addQueryItem(QStringLiteral("StartIndex"), QString::number(start));
    query.addQueryItem(QStringLiteral("Limit"), QString::number(kPageSize));
    query.addQueryItem(QStringLiteral("EnableTotalRecordCount"), QStringLiteral("true"));
  }
  auto add_sort = [&]() {
    // Ties broken by name, so equal dates or ratings keep a stable order.
    QString sort = sort_by_;
    if (sort != QLatin1String("SortName") && sort != QLatin1String("Random")) sort += QStringLiteral(",SortName");
    query.addQueryItem(QStringLiteral("SortBy"), sort);
    query.addQueryItem(QStringLiteral("SortOrder"), descending_ ? QStringLiteral("Descending") : QStringLiteral("Ascending"));
  };

  if (mode_ == QLatin1String("items") || mode_ == QLatin1String("search")) {
    path = QStringLiteral("/Items");
    if (!parent_id_.isEmpty()) query.addQueryItem(QStringLiteral("ParentId"), parent_id_);
    if (!include_types_.isEmpty()) query.addQueryItem(QStringLiteral("IncludeItemTypes"), include_types_);
    const bool recursive = recursive_ || mode_ == QLatin1String("search") || !genre_id_.isEmpty() || year_ > 0;
    if (recursive) query.addQueryItem(QStringLiteral("Recursive"), QStringLiteral("true"));
    if (!genre_id_.isEmpty()) query.addQueryItem(QStringLiteral("GenreIds"), genre_id_);
    if (year_ > 0) query.addQueryItem(QStringLiteral("Years"), QString::number(year_));
    if (hide_watched_) query.addQueryItem(QStringLiteral("Filters"), QStringLiteral("IsUnplayed"));
    if (mode_ == QLatin1String("search")) query.addQueryItem(QStringLiteral("SearchTerm"), search_term_);
    if (mode_ != QLatin1String("search")) add_sort();
  } else if (mode_ == QLatin1String("seasons")) {
    path = QStringLiteral("/Shows/%1/Seasons").arg(series_id_);
  } else if (mode_ == QLatin1String("episodes")) {
    path = QStringLiteral("/Shows/%1/Episodes").arg(series_id_);
    if (!season_id_.isEmpty()) query.addQueryItem(QStringLiteral("SeasonId"), season_id_);
    query.addQueryItem(QStringLiteral("IsMissing"), QStringLiteral("false"));
  } else if (mode_ == QLatin1String("resume")) {
    path = QStringLiteral("/UserItems/Resume");
    query.addQueryItem(QStringLiteral("MediaTypes"), QStringLiteral("Video"));
    if (!parent_id_.isEmpty()) query.addQueryItem(QStringLiteral("ParentId"), parent_id_);
    if (!include_types_.isEmpty()) query.addQueryItem(QStringLiteral("IncludeItemTypes"), include_types_);
  } else if (mode_ == QLatin1String("nextup")) {
    path = QStringLiteral("/Shows/NextUp");
    if (!parent_id_.isEmpty()) query.addQueryItem(QStringLiteral("ParentId"), parent_id_);
    if (!series_id_.isEmpty()) query.addQueryItem(QStringLiteral("SeriesId"), series_id_);
    query.addQueryItem(QStringLiteral("EnableResumable"), QStringLiteral("false"));
  } else if (mode_ == QLatin1String("genres")) {
    path = QStringLiteral("/Genres");
    if (!parent_id_.isEmpty()) query.addQueryItem(QStringLiteral("ParentId"), parent_id_);
    if (!include_types_.isEmpty()) query.addQueryItem(QStringLiteral("IncludeItemTypes"), include_types_);
    query.addQueryItem(QStringLiteral("SortBy"), QStringLiteral("SortName"));
  } else if (mode_ == QLatin1String("years")) {
    path = QStringLiteral("/Items/Filters");
    if (!parent_id_.isEmpty()) query.addQueryItem(QStringLiteral("ParentId"), parent_id_);
    if (!include_types_.isEmpty()) query.addQueryItem(QStringLiteral("IncludeItemTypes"), include_types_);
  } else {
    setError(tr("Unknown list mode %1").arg(mode_));
    setLoading(false);
    co_return;
  }

  const Reply reply = co_await api->get(path, query);
  if (!self || generation != generation_) co_return;
  setLoading(false);
  if (!reply.ok()) {
    setError(tr("Could not load the list (%1).").arg(reply.error));
    exhausted_ = true;
    co_return;
  }

  if (mode_ == QLatin1String("years")) {
    // Synthesize one row per year, newest first.
    QList<int> years;
    for (const QJsonValue& value : reply.object().value(QStringLiteral("Years")).toArray()) years.append(value.toInt());
    std::sort(years.begin(), years.end(), std::greater<int>());
    QJsonArray items;
    for (int y : years) {
      items.append(QJsonObject{{QStringLiteral("Id"), QStringLiteral("year:%1").arg(y)},
                               {QStringLiteral("Name"), QString::number(y)},
                               {QStringLiteral("Type"), QStringLiteral("Year")},
                               {QStringLiteral("IsFolder"), true},
                               {QStringLiteral("ProductionYear"), y}});
    }
    total_ = int(items.size());
    exhausted_ = true;
    appendRows(items);
  } else {
    const QJsonArray items = reply.json.isArray() ? reply.array() : reply.object().value(QStringLiteral("Items")).toArray();
    total_ = paged ? reply.object().value(QStringLiteral("TotalRecordCount")).toInt(int(rows_.size() + items.size()))
                   : int(items.size());
    if (!paged || items.isEmpty()) exhausted_ = true;
    appendRows(items);
  }
  emit countChanged();
  if (start == 0) emit loaded();
}

QVariantMap ItemListModel::get(int index) const {
  if (index < 0 || index >= rows_.size()) return {};
  return rows_.at(index);
}

int ItemListModel::indexOfId(const QString& id) const {
  for (int i = 0; i < rows_.size(); ++i) {
    if (rows_.at(i).value(QStringLiteral("id")).toString() == id) return i;
  }
  return -1;
}

void ItemListModel::refreshItem(const QString& id) { refreshItemTask(id); }

QCoro::Task<> ItemListModel::refreshItemTask(QString id) {
  QPointer<ItemListModel> self(this);
  Session* session = Session::instance();
  QUrlQuery query;
  query.addQueryItem(QStringLiteral("userId"), session->userId());
  query.addQueryItem(QStringLiteral("Fields"), jellyfin::ListFields());
  const quint64 generation = generation_;
  const Reply reply = co_await session->api()->get(QStringLiteral("/Items/%1").arg(id), query);
  if (!self || generation != generation_ || !reply.ok()) co_return;
  const int row = indexOfId(id);
  if (row < 0) co_return;
  rows_[row] = jellyfin::ItemToVariant(reply.object(), session->api());
  emit dataChanged(index(row), index(row));
}

void ItemListModel::setPlayed(const QString& id, bool played) { setPlayedTask(id, played); }

QCoro::Task<> ItemListModel::setPlayedTask(QString id, bool played) {
  QPointer<ItemListModel> self(this);
  Session* session = Session::instance();
  QUrlQuery query;
  query.addQueryItem(QStringLiteral("userId"), session->userId());
  const QString path = QStringLiteral("/UserPlayedItems/%1").arg(id);
  const Reply reply = played ? co_await session->api()->post(path, {}, query) : co_await session->api()->del(path, query);
  if (!self || !reply.ok()) co_return;
  if (played) {
    // Marking played leaves a resume point behind; clear it.
    co_await session->api()->post(QStringLiteral("/UserItems/%1/UserData").arg(id),
                                  QJsonDocument(QJsonObject{{QStringLiteral("PlaybackPositionTicks"), 0}}), query);
    if (!self) co_return;
  }
  refreshItem(id);
}

QStringList ItemListModel::letters() const {
  QStringList result;
  for (const QVariantMap& row : rows_) {
    const QString letter = LetterOf(row.value(QStringLiteral("sortName")).toString().isEmpty()
                                        ? row.value(QStringLiteral("name")).toString()
                                        : row.value(QStringLiteral("sortName")).toString());
    if (!result.contains(letter)) result.append(letter);
  }
  return result;
}

void ItemListModel::findLetter(const QString& letter) { findLetterTask(letter, generation_); }

QCoro::Task<> ItemListModel::findLetterTask(QString letter, quint64 generation) {
  QPointer<ItemListModel> self(this);
  int target = 0;
  if (mode_ == QLatin1String("items") && sort_by_ == QLatin1String("SortName") && !descending_ && letter != QLatin1String("#")) {
    // Count the rows that sort before the letter; that's its first index.
    Session* session = Session::instance();
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("userId"), session->userId());
    if (!parent_id_.isEmpty()) query.addQueryItem(QStringLiteral("ParentId"), parent_id_);
    if (!include_types_.isEmpty()) query.addQueryItem(QStringLiteral("IncludeItemTypes"), include_types_);
    if (recursive_ || !genre_id_.isEmpty() || year_ > 0) query.addQueryItem(QStringLiteral("Recursive"), QStringLiteral("true"));
    if (!genre_id_.isEmpty()) query.addQueryItem(QStringLiteral("GenreIds"), genre_id_);
    if (year_ > 0) query.addQueryItem(QStringLiteral("Years"), QString::number(year_));
    if (hide_watched_) query.addQueryItem(QStringLiteral("Filters"), QStringLiteral("IsUnplayed"));
    query.addQueryItem(QStringLiteral("NameLessThan"), letter);
    query.addQueryItem(QStringLiteral("Limit"), QStringLiteral("0"));
    query.addQueryItem(QStringLiteral("EnableTotalRecordCount"), QStringLiteral("true"));
    const Reply reply = co_await session->api()->get(QStringLiteral("/Items"), query);
    if (!self || generation != generation_) co_return;
    if (!reply.ok()) co_return;
    target = reply.object().value(QStringLiteral("TotalRecordCount")).toInt();
  } else {
    // Unsorted by name, or not paged: search what is loaded.
    for (int i = 0; i < rows_.size(); ++i) {
      const QVariantMap& row = rows_.at(i);
      const QString name = row.value(QStringLiteral("sortName")).toString().isEmpty()
                               ? row.value(QStringLiteral("name")).toString()
                               : row.value(QStringLiteral("sortName")).toString();
      if (LetterOf(name) == letter || (letter != QLatin1String("#") && LetterOf(name) > letter)) {
        target = i;
        break;
      }
    }
  }
  target = std::min(target, std::max(0, total_ - 1));
  while (target >= rows_.size() && !exhausted_ && pagedMode()) {
    if (loading_) {
      co_await QCoro::sleepFor(std::chrono::milliseconds(50));
    } else {
      co_await fetchPage(generation, int(rows_.size()));
    }
    if (!self || generation != generation_) co_return;
  }
  emit letterFound(std::min(target, int(rows_.size()) - 1));
}

}  // namespace ember
