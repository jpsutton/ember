// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <QDateTime>
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
//   overview, tagline, year, premiereDate, dateCreated, airedDate (see
//   AiredDate), officialRating,
//   communityRating, criticRating, runtimeTicks, runtimeText, genres,
//   studios, seriesId, seriesName, seasonId, seasonName, indexNumber,
//   parentIndexNumber, episodeLabel (S01E02), childCount, recursiveCount,
//   played, playedPercentage, playbackPositionTicks, resumeText,
//   unplayedCount, isFavorite, poster, backdrop, logo, thumb, infoBadges,
//   mediaBadges (see InfoBadges, MediaBadges), status (playable leaf: "watched",
//   "inProgress", "unwatched"; folders and channels: ""), and for Live TV
//   channelNumber, channelId, startEpoch, endEpoch (seconds), episodeTitle
QVariantMap ItemToVariant(const QJsonObject& item, const ApiClient* api);

// The details pane shows what it can as badges, as Amber does. Each is a map
// of tag (text on a solid block), icon (drawn there instead: film, speaker,
// channels, aspect, calendar, clock, star, critic, check, heart) and value
// (in an outlined block); any of them may be empty.
//
// About the item: rating, air date or year (a show's run), runtime, scores,
// seasons and episodes, watched or unwatched count, favourite.
QVariantList InfoBadges(const QJsonObject& item);
// About the file, from its first video and default (or first) audio stream:
// resolution, codec, HDR, 3D, aspect ratio, audio codec, Atmos, channels,
// and CC when there are subtitles in |subtitle_language| (a preference as in
// playback/TrackLanguage.h; any language when empty). ItemToVariant passes
// the Subtitle language setting.
QVariantList MediaBadges(const QJsonObject& item, const QString& subtitle_language);

// When the item became watchable, for "Recently aired": its premiere date,
// or the date it was added when that is earlier (a streaming service that
// releases episodes ahead of their air date). Invalid when neither is set.
QDateTime AiredDate(const QJsonObject& item);

// "1 h 32 min" style runtime from 100 ns ticks.
QString FormatRuntime(qint64 ticks);

// Whether OK plays this item rather than opening it.
bool IsPlayable(const QJsonObject& item);

}  // namespace ember::jellyfin
