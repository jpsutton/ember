// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QVariantMap>

namespace ember::jellyfin {

class ApiClient;

// Fields requested for list rows: everything the list's details pane shows.
QString ListFields();

// Builds an image URL. |max_width|/|max_height| of 0 leave that bound out.
// Photos are fetched as JPEG and logos as PNG, so no image plugin beyond
// Qt's own JPEG and PNG support is needed.
QString ImageUrl(const ApiClient* api, const QString& item_id, const QString& type, const QString& tag,
                 int max_width, int max_height);

// Converts a BaseItemDto into the flat map QML reads. Keys:
//   id, name, type, isFolder, collectionType, sortName, originalTitle,
//   overview, tagline, year, premiereDate, dateCreated, officialRating,
//   communityRating, criticRating, runtimeTicks, runtimeText, genres,
//   studios, seriesId, seriesName, seasonId, seasonName, indexNumber,
//   parentIndexNumber, episodeLabel (S01E02), childCount, recursiveCount,
//   played, playedPercentage, playbackPositionTicks, resumeText,
//   unplayedCount, isFavorite, poster, backdrop, logo, thumb, videoFlags,
//   audioFlags, hasSubtitles, status (playable leaf: "watched",
//   "inProgress", "unwatched"; folders: "")
QVariantMap ItemToVariant(const QJsonObject& item, const ApiClient* api);

// "1 h 32 min" style runtime from 100 ns ticks.
QString FormatRuntime(qint64 ticks);

// Whether OK plays this item rather than opening it.
bool IsPlayable(const QJsonObject& item);

}  // namespace ember::jellyfin
