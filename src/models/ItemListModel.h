// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <QAbstractListModel>
#include <QJsonObject>
#include <QTimer>
#include <QtQml/qqmlregistration.h>

#include <QCoroTask>

namespace ember {

// One list of Jellyfin items, fetched a page at a time as the view scrolls.
//
// The query is set through properties; any change reloads the list (once,
// after the current event). `mode` selects the endpoint:
//   "items"     /Items under parentId (folders, libraries, collections, a
//               genre or year when genreId/year is set)
//   "seasons"   /Shows/{seriesId}/Seasons
//   "episodes"  /Shows/{seriesId}/Episodes, of seasonId when set
//   "resume"    /UserItems/Resume (in progress)
//   "nextup"    /Shows/NextUp
//   "genres"    /Genres under parentId
//   "years"     the production years present under parentId
//   "search"    /Items matching searchTerm across the shown libraries
class ItemListModel : public QAbstractListModel {
  Q_OBJECT
  QML_ELEMENT

  Q_PROPERTY(QString mode READ mode WRITE setMode NOTIFY queryChanged)
  Q_PROPERTY(QString parentId READ parentId WRITE setParentId NOTIFY queryChanged)
  Q_PROPERTY(QString seriesId READ seriesId WRITE setSeriesId NOTIFY queryChanged)
  Q_PROPERTY(QString seasonId READ seasonId WRITE setSeasonId NOTIFY queryChanged)
  Q_PROPERTY(QString includeTypes READ includeTypes WRITE setIncludeTypes NOTIFY queryChanged)
  Q_PROPERTY(bool recursive READ recursive WRITE setRecursive NOTIFY queryChanged)
  Q_PROPERTY(QString sortBy READ sortBy WRITE setSortBy NOTIFY queryChanged)
  Q_PROPERTY(bool descending READ descending WRITE setDescending NOTIFY queryChanged)
  Q_PROPERTY(bool hideWatched READ hideWatched WRITE setHideWatched NOTIFY queryChanged)
  Q_PROPERTY(QString genreId READ genreId WRITE setGenreId NOTIFY queryChanged)
  Q_PROPERTY(int year READ year WRITE setYear NOTIFY queryChanged)
  Q_PROPERTY(QString searchTerm READ searchTerm WRITE setSearchTerm NOTIFY queryChanged)
  Q_PROPERTY(int count READ count NOTIFY countChanged)
  Q_PROPERTY(int totalCount READ totalCount NOTIFY countChanged)
  Q_PROPERTY(bool loading READ loading NOTIFY loadingChanged)
  Q_PROPERTY(QString errorString READ errorString NOTIFY errorStringChanged)
  // Changes whenever any row does, so bindings that call get() re-evaluate.
  Q_PROPERTY(int revision READ revision NOTIFY revisionChanged)

 public:
  explicit ItemListModel(QObject* parent = nullptr);

  enum Role { ItemRole = Qt::UserRole + 1, FirstFieldRole };

  int rowCount(const QModelIndex& parent = {}) const override;
  QVariant data(const QModelIndex& index, int role) const override;
  QHash<int, QByteArray> roleNames() const override;
  bool canFetchMore(const QModelIndex& parent) const override;
  void fetchMore(const QModelIndex& parent) override;

  QString mode() const { return mode_; }
  void setMode(const QString& v) { setQueryField(mode_, v); }
  QString parentId() const { return parent_id_; }
  void setParentId(const QString& v) { setQueryField(parent_id_, v); }
  QString seriesId() const { return series_id_; }
  void setSeriesId(const QString& v) { setQueryField(series_id_, v); }
  QString seasonId() const { return season_id_; }
  void setSeasonId(const QString& v) { setQueryField(season_id_, v); }
  QString includeTypes() const { return include_types_; }
  void setIncludeTypes(const QString& v) { setQueryField(include_types_, v); }
  bool recursive() const { return recursive_; }
  void setRecursive(bool v) { setQueryField(recursive_, v); }
  QString sortBy() const { return sort_by_; }
  void setSortBy(const QString& v) { setQueryField(sort_by_, v); }
  bool descending() const { return descending_; }
  void setDescending(bool v) { setQueryField(descending_, v); }
  bool hideWatched() const { return hide_watched_; }
  void setHideWatched(bool v) { setQueryField(hide_watched_, v); }
  QString genreId() const { return genre_id_; }
  void setGenreId(const QString& v) { setQueryField(genre_id_, v); }
  int year() const { return year_; }
  void setYear(int v) { setQueryField(year_, v); }
  QString searchTerm() const { return search_term_; }
  void setSearchTerm(const QString& v) { setQueryField(search_term_, v); }

  int count() const { return int(rows_.size()); }
  int totalCount() const { return total_; }
  bool loading() const { return loading_; }
  QString errorString() const { return error_string_; }
  int revision() const { return revision_; }

  Q_INVOKABLE QVariantMap get(int index) const;
  Q_INVOKABLE int indexOfId(const QString& id) const;
  Q_INVOKABLE void reload();
  // Re-reads one item (after playback changed its watched state).
  Q_INVOKABLE void refreshItem(const QString& id);
  Q_INVOKABLE void setPlayed(const QString& id, bool played);
  // Finds the first row whose sort name starts at or after |letter| ("#" for
  // digits and symbols), loads pages up to it, then emits letterFound.
  Q_INVOKABLE void findLetter(const QString& letter);
  // The letters present in the list, for the A-Z strip.
  Q_INVOKABLE QStringList letters() const;

 signals:
  void queryChanged();
  void countChanged();
  void loadingChanged();
  void errorStringChanged();
  void letterFound(int index);
  void revisionChanged();
  // The first page has arrived after a reload.
  void loaded();

 private:
  template <typename T>
  void setQueryField(T& field, const T& value) {
    if (field == value) return;
    field = value;
    emit queryChanged();
    scheduleReload();
  }

  void scheduleReload();
  QCoro::Task<> fetchPage(quint64 generation, int start);
  QCoro::Task<> refreshItemTask(QString id);
  QCoro::Task<> setPlayedTask(QString id, bool played);
  QCoro::Task<> findLetterTask(QString letter, quint64 generation);
  bool pagedMode() const;
  void setLoading(bool loading);
  void setError(const QString& error);
  void appendRows(const QJsonArray& items);

  QString mode_ = QStringLiteral("items");
  QString parent_id_;
  QString series_id_;
  QString season_id_;
  QString include_types_;
  bool recursive_ = false;
  QString sort_by_ = QStringLiteral("SortName");
  bool descending_ = false;
  bool hide_watched_ = false;
  QString genre_id_;
  int year_ = 0;
  QString search_term_;

  QList<QVariantMap> rows_;
  int total_ = 0;
  bool loading_ = false;
  bool exhausted_ = false;
  QString error_string_;
  quint64 generation_ = 0;
  int revision_ = 0;
  QTimer reload_timer_;
  QList<QByteArray> field_names_;
};

}  // namespace ember
