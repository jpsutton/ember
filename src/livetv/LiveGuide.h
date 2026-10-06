// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <QHash>
#include <QObject>
#include <QTimer>
#include <QVariant>
#include <QtQml/qqmlregistration.h>

#include <QCoroTask>

#include "GuideLogic.h"

namespace ember::livetv {

// The Live TV guide's data: the server's TV channels and their programmes
// from an hour ago to a day ahead, kept in memory and handed to QML a
// window at a time (a guide holds thousands of programmes). The rules for
// moving the focus are GuideLogic's.
class LiveGuide : public QObject {
  Q_OBJECT
  QML_ELEMENT

  // The channels the filter lets through, in channel-number order:
  // {id, number, name, logo, isFavorite}.
  Q_PROPERTY(QVariantList channels READ channels NOTIFY channelsChanged)
  // "all" or "favourites".
  Q_PROPERTY(QString filter READ filter WRITE setFilter NOTIFY channelsChanged)
  Q_PROPERTY(bool loading READ loading NOTIFY loadingChanged)
  Q_PROPERTY(QString errorString READ errorString NOTIFY errorStringChanged)
  // Seconds since the epoch, updated every 30 s.
  Q_PROPERTY(qint64 now READ now NOTIFY nowChanged)
  // How far ahead the focus may go: 12 hours, or the end of the guide data.
  Q_PROPERTY(qint64 latest READ latest NOTIFY nowChanged)
  // Changes whenever the programmes are reloaded, for bindings that call
  // cells() or programmeAt().
  Q_PROPERTY(int revision READ revision NOTIFY revisionChanged)

 public:
  explicit LiveGuide(QObject* parent = nullptr);

  QVariantList channels() const;
  QString filter() const { return filter_; }
  void setFilter(const QString& filter);
  bool loading() const { return loading_; }
  QString errorString() const { return error_string_; }
  qint64 now() const { return now_; }
  qint64 latest() const;
  int revision() const { return revision_; }

  Q_INVOKABLE void reload();

  // The programmes of channel |row| overlapping [from, to), start-ordered:
  // {id, title, start, stop}.
  Q_INVOKABLE QVariantList cells(int row, qint64 from, qint64 to) const;
  // The programme on at |t| (or the next one after it) on channel |row|:
  // {id, title, episodeTitle, overview, start, stop, isLive, isNew, isRepeat},
  // or an empty map.
  Q_INVOKABLE QVariantMap programmeAt(int row, qint64 t) const;
  Q_INVOKABLE QVariantMap programmeAfter(int row, qint64 t) const;

  Q_INVOKABLE qint64 step(int row, qint64 focus, int direction) const;
  Q_INVOKABLE qint64 windowFor(qint64 focus, qint64 window_start) const;
  Q_INVOKABLE qint64 floorToSlot(qint64 t) const { return FloorToSlot(t); }
  // The span of the grid: three hours.
  Q_INVOKABLE qint64 span() const { return 3 * 3600; }

  // The row of a channel number or id among the shown channels, or -1.
  Q_INVOKABLE int rowForNumber(const QString& number) const;
  Q_INVOKABLE int rowForChannel(const QString& channel_id) const;
  // Whether some channel (shown or not) has this number.
  Q_INVOKABLE bool hasNumber(const QString& number) const;

  Q_INVOKABLE void setFavorite(const QString& channel_id, bool favorite);

  // "21:30" in the locale's short format.
  Q_INVOKABLE QString timeText(qint64 t) const;

 signals:
  void channelsChanged();
  void loadingChanged();
  void errorStringChanged();
  void nowChanged();
  void revisionChanged();

 private:
  struct Channel {
    QString id;
    QString number;
    QString name;
    QString logo;
    bool favorite = false;
  };
  struct Entry {
    qint64 start = 0;
    qint64 stop = 0;
    QString id;
    QString title;
    QString episode_title;
    QString overview;
    bool live = false;
    bool is_new = false;
    bool repeat = false;
  };

  QCoro::Task<> loadChannelsTask(quint64 generation);
  QCoro::Task<> loadProgrammesTask(quint64 generation);
  QCoro::Task<> setFavoriteTask(QString channel_id, bool favorite);
  void setLoading(bool loading);
  void setError(const QString& error);
  void tick();
  const QList<Entry>* entries(int row) const;
  QList<Programme> spans(int row) const;
  QVariantMap entryToVariant(const Entry& entry) const;

  QList<Channel> all_;      // every TV channel, number order
  QList<int> shown_;        // indices into all_ the filter lets through
  QHash<QString, QList<Entry>> programmes_;  // by channel id, start order
  QString filter_ = QStringLiteral("all");
  bool loading_ = false;
  QString error_string_;
  qint64 now_ = 0;
  qint64 loaded_until_ = 0;
  qint64 guide_end_ = 0;
  int revision_ = 0;
  quint64 generation_ = 0;
  QTimer tick_;
};

// Orders ATSC-style channel numbers: "4.2" before "4.10", numbers before
// anything else.
bool ChannelNumberLess(const QString& a, const QString& b);

}  // namespace ember::livetv
