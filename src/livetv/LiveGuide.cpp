// SPDX-License-Identifier: GPL-3.0-only

#include "LiveGuide.h"

#include <QDateTime>
#include <QJsonArray>
#include <QJsonObject>
#include <QLocale>
#include <QLoggingCategory>
#include <QPointer>
#include <QTimeZone>
#include <QUrlQuery>

#include <algorithm>

#include "../jellyfin/ApiClient.h"
#include "../jellyfin/Items.h"
#include "../jellyfin/Session.h"

Q_LOGGING_CATEGORY(lcGuide, "ember.livetv")

namespace ember::livetv {
namespace {

using jellyfin::Reply;
using jellyfin::Session;

// The guide holds programmes from an hour ago to a day ahead, and reloads
// when less than this much is left ahead of now.
constexpr qint64 kBehind = 3600;
constexpr qint64 kAhead = 24 * 3600;
constexpr qint64 kReloadWhenLeft = 18 * 3600;
// The focus goes at most this far ahead.
constexpr qint64 kFocusAhead = 12 * 3600;
constexpr int kTickMs = 30 * 1000;

qint64 Epoch(const QJsonValue& value) {
  const QDateTime time = QDateTime::fromString(value.toString(), Qt::ISODate);
  return time.isValid() ? time.toSecsSinceEpoch() : 0;
}

QString Iso(qint64 t) { return QDateTime::fromSecsSinceEpoch(t, QTimeZone::UTC).toString(Qt::ISODate); }

// "4.10" -> {4, 10}; false when it isn't a number.
bool ParseNumber(const QString& text, int* major, int* minor) {
  const QStringList parts = text.split(QLatin1Char('.'));
  if (parts.isEmpty() || parts.size() > 2) return false;
  bool ok = false;
  *major = parts.at(0).toInt(&ok);
  if (!ok) return false;
  *minor = 0;
  if (parts.size() == 2) *minor = parts.at(1).toInt(&ok);
  return ok;
}

}  // namespace

bool ChannelNumberLess(const QString& a, const QString& b) {
  int a_major, a_minor, b_major, b_minor;
  const bool a_ok = ParseNumber(a, &a_major, &a_minor);
  const bool b_ok = ParseNumber(b, &b_major, &b_minor);
  if (a_ok != b_ok) return a_ok;
  if (!a_ok) return a < b;
  return a_major != b_major ? a_major < b_major : a_minor < b_minor;
}

LiveGuide::LiveGuide(QObject* parent) : QObject(parent) {
  now_ = QDateTime::currentSecsSinceEpoch();
  tick_.setInterval(kTickMs);
  connect(&tick_, &QTimer::timeout, this, &LiveGuide::tick);
  tick_.start();
  reload();
}

void LiveGuide::tick() {
  now_ = QDateTime::currentSecsSinceEpoch();
  emit nowChanged();
  if (!loading_ && loaded_until_ > 0 && loaded_until_ - now_ < kReloadWhenLeft) loadProgrammesTask(generation_);
}

qint64 LiveGuide::latest() const {
  const qint64 limit = now_ + kFocusAhead;
  return guide_end_ > now_ ? qMin(limit, guide_end_) : limit;
}

QVariantList LiveGuide::channels() const {
  QVariantList result;
  for (const int index : shown_) {
    const Channel& channel = all_.at(index);
    result.append(QVariantMap{{QStringLiteral("id"), channel.id},
                              {QStringLiteral("number"), channel.number},
                              {QStringLiteral("name"), channel.name},
                              {QStringLiteral("logo"), channel.logo},
                              {QStringLiteral("isFavorite"), channel.favorite}});
  }
  return result;
}

void LiveGuide::setFilter(const QString& filter) {
  if (filter == filter_) return;
  filter_ = filter;
  shown_.clear();
  for (qsizetype i = 0; i < all_.size(); ++i) {
    if (filter_ != QLatin1String("favourites") || all_.at(i).favorite) shown_.append(int(i));
  }
  emit channelsChanged();
}

void LiveGuide::setLoading(bool loading) {
  if (loading == loading_) return;
  loading_ = loading;
  emit loadingChanged();
}

void LiveGuide::setError(const QString& error) {
  if (error == error_string_) return;
  error_string_ = error;
  emit errorStringChanged();
}

void LiveGuide::reload() {
  ++generation_;
  loadChannelsTask(generation_);
}

QCoro::Task<> LiveGuide::loadChannelsTask(quint64 generation) {
  QPointer<LiveGuide> self(this);
  Session* session = Session::instance();
  if (session == nullptr || session->state() != Session::State::SignedIn) co_return;
  setLoading(true);
  setError(QString());

  QUrlQuery query;
  query.addQueryItem(QStringLiteral("userId"), session->userId());
  query.addQueryItem(QStringLiteral("Type"), QStringLiteral("TV"));
  query.addQueryItem(QStringLiteral("EnableImages"), QStringLiteral("true"));
  query.addQueryItem(QStringLiteral("ImageTypeLimit"), QStringLiteral("1"));
  query.addQueryItem(QStringLiteral("EnableImageTypes"), QStringLiteral("Primary"));
  query.addQueryItem(QStringLiteral("EnableUserData"), QStringLiteral("true"));
  query.addQueryItem(QStringLiteral("AddCurrentProgram"), QStringLiteral("false"));
  const Reply reply = co_await session->api()->get(QStringLiteral("/LiveTv/Channels"), query);
  if (!self || generation != generation_) co_return;
  if (!reply.ok()) {
    setError(tr("Could not load the channels (%1).").arg(reply.error));
    setLoading(false);
    co_return;
  }
  QList<Channel> channels;
  for (const QJsonValue& value : reply.object().value(QStringLiteral("Items")).toArray()) {
    const QJsonObject item = value.toObject();
    Channel channel;
    channel.id = item.value(QStringLiteral("Id")).toString();
    channel.number = item.value(QStringLiteral("ChannelNumber")).toString();
    if (channel.number.isEmpty()) channel.number = item.value(QStringLiteral("Number")).toString();
    channel.name = item.value(QStringLiteral("Name")).toString();
    channel.logo = jellyfin::ImageUrl(session->api(), channel.id, QStringLiteral("Primary"),
                                      item.value(QStringLiteral("ImageTags")).toObject().value(QStringLiteral("Primary")).toString(),
                                      0, 128);
    channel.favorite = item.value(QStringLiteral("UserData")).toObject().value(QStringLiteral("IsFavorite")).toBool();
    channels.append(channel);
  }
  std::stable_sort(channels.begin(), channels.end(), [](const Channel& a, const Channel& b) {
    if (a.number != b.number) return ChannelNumberLess(a.number, b.number);
    return a.name < b.name;
  });
  all_ = channels;
  const QString filter = filter_;
  filter_.clear();
  setFilter(filter);
  qCInfo(lcGuide) << all_.size() << "channels";

  // How far the server's guide data goes.
  const Reply info = co_await session->api()->get(QStringLiteral("/LiveTv/GuideInfo"));
  if (!self || generation != generation_) co_return;
  if (info.ok()) guide_end_ = Epoch(info.object().value(QStringLiteral("EndDate")));
  co_await loadProgrammesTask(generation);
}

QCoro::Task<> LiveGuide::loadProgrammesTask(quint64 generation) {
  QPointer<LiveGuide> self(this);
  Session* session = Session::instance();
  if (session == nullptr || session->state() != Session::State::SignedIn) co_return;
  setLoading(true);
  const qint64 now = QDateTime::currentSecsSinceEpoch();
  const qint64 until = now + kAhead;
  QUrlQuery query;
  query.addQueryItem(QStringLiteral("userId"), session->userId());
  // By end and start, so a programme already on when the window opens is in.
  query.addQueryItem(QStringLiteral("MinEndDate"), Iso(now - kBehind));
  query.addQueryItem(QStringLiteral("MaxStartDate"), Iso(until));
  query.addQueryItem(QStringLiteral("SortBy"), QStringLiteral("StartDate"));
  query.addQueryItem(QStringLiteral("EnableTotalRecordCount"), QStringLiteral("false"));
  query.addQueryItem(QStringLiteral("EnableImages"), QStringLiteral("false"));
  query.addQueryItem(QStringLiteral("EnableUserData"), QStringLiteral("false"));
  query.addQueryItem(QStringLiteral("Fields"), QStringLiteral("Overview"));
  const Reply reply = co_await session->api()->get(QStringLiteral("/LiveTv/Programs"), query);
  if (!self || generation != generation_) co_return;
  setLoading(false);
  if (!reply.ok()) {
    setError(tr("Could not load the guide (%1).").arg(reply.error));
    co_return;
  }
  QHash<QString, QList<Entry>> programmes;
  int count = 0;
  for (const QJsonValue& value : reply.object().value(QStringLiteral("Items")).toArray()) {
    const QJsonObject item = value.toObject();
    Entry entry;
    entry.start = Epoch(item.value(QStringLiteral("StartDate")));
    entry.stop = Epoch(item.value(QStringLiteral("EndDate")));
    if (entry.stop <= entry.start) continue;
    entry.id = item.value(QStringLiteral("Id")).toString();
    entry.title = item.value(QStringLiteral("Name")).toString();
    entry.episode_title = item.value(QStringLiteral("EpisodeTitle")).toString();
    entry.overview = item.value(QStringLiteral("Overview")).toString();
    entry.live = item.value(QStringLiteral("IsLive")).toBool();
    entry.is_new = item.value(QStringLiteral("IsPremiere")).toBool();
    entry.repeat = item.value(QStringLiteral("IsRepeat")).toBool();
    programmes[item.value(QStringLiteral("ChannelId")).toString()].append(entry);
    ++count;
  }
  for (QList<Entry>& list : programmes) {
    std::stable_sort(list.begin(), list.end(), [](const Entry& a, const Entry& b) { return a.start < b.start; });
  }
  programmes_ = programmes;
  loaded_until_ = until;
  ++revision_;
  qCInfo(lcGuide) << count << "programmes on" << programmes_.size() << "channels";
  emit revisionChanged();
}

const QList<LiveGuide::Entry>* LiveGuide::entries(int row) const {
  if (row < 0 || row >= shown_.size()) return nullptr;
  const auto it = programmes_.constFind(all_.at(shown_.at(row)).id);
  return it == programmes_.constEnd() ? nullptr : &it.value();
}

QList<Programme> LiveGuide::spans(int row) const {
  QList<Programme> result;
  if (const QList<Entry>* list = entries(row)) {
    result.reserve(list->size());
    for (const Entry& entry : *list) result.append({entry.start, entry.stop});
  }
  return result;
}

QVariantMap LiveGuide::entryToVariant(const Entry& entry) const {
  return {{QStringLiteral("id"), entry.id},
          {QStringLiteral("title"), entry.title},
          {QStringLiteral("episodeTitle"), entry.episode_title},
          {QStringLiteral("overview"), entry.overview},
          {QStringLiteral("start"), entry.start},
          {QStringLiteral("stop"), entry.stop},
          {QStringLiteral("isLive"), entry.live},
          {QStringLiteral("isNew"), entry.is_new},
          {QStringLiteral("isRepeat"), entry.repeat}};
}

QVariantList LiveGuide::cells(int row, qint64 from, qint64 to) const {
  QVariantList result;
  const QList<Entry>* list = entries(row);
  if (list == nullptr) return result;
  for (const Entry& entry : *list) {
    if (entry.start >= to) break;
    if (entry.stop <= from) continue;
    result.append(QVariantMap{{QStringLiteral("id"), entry.id},
                              {QStringLiteral("title"), entry.title},
                              {QStringLiteral("start"), entry.start},
                              {QStringLiteral("stop"), entry.stop}});
  }
  return result;
}

QVariantMap LiveGuide::programmeAt(int row, qint64 t) const {
  const QList<Entry>* list = entries(row);
  if (list == nullptr) return {};
  const qsizetype index = ProgrammeAt(spans(row), t);
  return index >= 0 ? entryToVariant(list->at(index)) : QVariantMap();
}

QVariantMap LiveGuide::programmeAfter(int row, qint64 t) const {
  const QList<Entry>* list = entries(row);
  if (list == nullptr) return {};
  for (const Entry& entry : *list) {
    if (entry.start > t) return entryToVariant(entry);
  }
  return {};
}

qint64 LiveGuide::step(int row, qint64 focus, int direction) const {
  return StepFocus(spans(row), focus, direction, now_, latest());
}

qint64 LiveGuide::windowFor(qint64 focus, qint64 window_start) const {
  return WindowFor(focus, window_start, span(), now_);
}

int LiveGuide::rowForNumber(const QString& number) const {
  for (qsizetype row = 0; row < shown_.size(); ++row) {
    if (all_.at(shown_.at(row)).number == number) return int(row);
  }
  return -1;
}

int LiveGuide::rowForChannel(const QString& channel_id) const {
  for (qsizetype row = 0; row < shown_.size(); ++row) {
    if (all_.at(shown_.at(row)).id == channel_id) return int(row);
  }
  return -1;
}

bool LiveGuide::hasNumber(const QString& number) const {
  return std::any_of(all_.begin(), all_.end(), [&number](const Channel& c) { return c.number == number; });
}

void LiveGuide::setFavorite(const QString& channel_id, bool favorite) {
  for (Channel& channel : all_) {
    if (channel.id == channel_id) channel.favorite = favorite;
  }
  const QString filter = filter_;
  filter_.clear();
  setFilter(filter);
  setFavoriteTask(channel_id, favorite);
}

QCoro::Task<> LiveGuide::setFavoriteTask(QString channel_id, bool favorite) {
  Session* session = Session::instance();
  if (session == nullptr) co_return;
  QUrlQuery query;
  query.addQueryItem(QStringLiteral("userId"), session->userId());
  const QString path = QStringLiteral("/UserFavoriteItems/%1").arg(channel_id);
  const Reply reply = favorite ? co_await session->api()->post(path, {}, query) : co_await session->api()->del(path, query);
  if (!reply.ok()) qCWarning(lcGuide) << "favourite" << channel_id << "failed:" << reply.error;
}

QString LiveGuide::timeText(qint64 t) const {
  return QLocale().toString(QDateTime::fromSecsSinceEpoch(t).time(), QLocale::ShortFormat);
}

}  // namespace ember::livetv
