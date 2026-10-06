// SPDX-License-Identifier: GPL-3.0-only
// Field mapping follows Plezy's lib/services/jellyfin_mappers.dart (GPL-3.0).

#include "Items.h"

#include <KFormat>

#include <QDateTime>
#include <QJsonArray>
#include <QLocale>
#include <QUrlQuery>

#include "ApiClient.h"

namespace ember::jellyfin {
namespace {

constexpr qint64 kTicksPerMillisecond = 10000;

QString VideoResolution(int width, int height) {
  // By width as well as height, so 2.39:1 films land in the right class.
  if (width >= 3800 || height >= 2100) return QStringLiteral("4K");
  if (width >= 1900 || height >= 1060) return QStringLiteral("1080p");
  if (width >= 1260 || height >= 700) return QStringLiteral("720p");
  if (width > 0 || height > 0) return QStringLiteral("SD");
  return {};
}

QString CodecName(const QString& codec) {
  const QString c = codec.toLower();
  if (c == QLatin1String("h264") || c == QLatin1String("avc")) return QStringLiteral("H.264");
  if (c == QLatin1String("hevc") || c == QLatin1String("h265")) return QStringLiteral("HEVC");
  if (c == QLatin1String("av1")) return QStringLiteral("AV1");
  if (c == QLatin1String("vp9")) return QStringLiteral("VP9");
  if (c == QLatin1String("mpeg2video")) return QStringLiteral("MPEG-2");
  if (c == QLatin1String("ac3")) return QStringLiteral("Dolby Digital");
  if (c == QLatin1String("eac3")) return QStringLiteral("Dolby Digital+");
  if (c == QLatin1String("truehd")) return QStringLiteral("TrueHD");
  if (c == QLatin1String("dts")) return QStringLiteral("DTS");
  if (c == QLatin1String("aac")) return QStringLiteral("AAC");
  if (c == QLatin1String("flac")) return QStringLiteral("FLAC");
  if (c == QLatin1String("opus")) return QStringLiteral("Opus");
  if (c == QLatin1String("mp3")) return QStringLiteral("MP3");
  return codec.toUpper();
}

QString ChannelLayout(int channels) {
  switch (channels) {
    case 0: return {};
    case 1: return QStringLiteral("Mono");
    case 2: return QStringLiteral("Stereo");
    case 6: return QStringLiteral("5.1");
    case 8: return QStringLiteral("7.1");
    default: return QStringLiteral("%1 ch").arg(channels);
  }
}

QStringList Names(const QJsonArray& array) {
  QStringList names;
  for (const QJsonValue& value : array) {
    names.append(value.isString() ? value.toString() : value.toObject().value(QStringLiteral("Name")).toString());
  }
  return names;
}

}  // namespace

QString ListFields() {
  return QStringLiteral(
      "Overview,Genres,DateCreated,PremiereDate,MediaStreams,ChildCount,RecursiveItemCount,Taglines,Studios,"
      "SortName,OriginalTitle");
}

QString ImageUrl(const ApiClient* api, const QString& item_id, const QString& type, const QString& tag,
                 int max_width, int max_height) {
  if (api == nullptr || item_id.isEmpty() || tag.isEmpty()) return {};
  QUrlQuery query;
  query.addQueryItem(QStringLiteral("tag"), tag);
  if (max_width > 0) query.addQueryItem(QStringLiteral("maxWidth"), QString::number(max_width));
  if (max_height > 0) query.addQueryItem(QStringLiteral("maxHeight"), QString::number(max_height));
  const bool alpha = type == QLatin1String("Logo");
  query.addQueryItem(QStringLiteral("format"), alpha ? QStringLiteral("Png") : QStringLiteral("Jpg"));
  if (!alpha) query.addQueryItem(QStringLiteral("quality"), QStringLiteral("90"));
  return api->url(QStringLiteral("/Items/%1/Images/%2").arg(item_id, type), query).toString();
}

QString FormatRuntime(qint64 ticks) {
  if (ticks <= 0) return {};
  const qint64 minutes = (ticks / kTicksPerMillisecond + 30000) / 60000;
  if (minutes < 60) return QStringLiteral("%1 min").arg(minutes);
  return QStringLiteral("%1 h %2 min").arg(minutes / 60).arg(minutes % 60);
}

bool IsPlayable(const QJsonObject& item) {
  if (item.value(QStringLiteral("IsFolder")).toBool()) return false;
  const QString type = item.value(QStringLiteral("Type")).toString();
  return type == QLatin1String("Movie") || type == QLatin1String("Episode") || type == QLatin1String("Video") ||
         type == QLatin1String("MusicVideo") || type == QLatin1String("Trailer") ||
         item.value(QStringLiteral("MediaType")).toString() == QLatin1String("Video");
}

QVariantMap ItemToVariant(const QJsonObject& item, const ApiClient* api) {
  QVariantMap m;
  const QString id = item.value(QStringLiteral("Id")).toString();
  const QString type = item.value(QStringLiteral("Type")).toString();
  m[QStringLiteral("id")] = id;
  m[QStringLiteral("name")] = item.value(QStringLiteral("Name")).toString();
  m[QStringLiteral("type")] = type;
  m[QStringLiteral("isFolder")] = item.value(QStringLiteral("IsFolder")).toBool();
  m[QStringLiteral("playable")] = IsPlayable(item);
  m[QStringLiteral("collectionType")] = item.value(QStringLiteral("CollectionType")).toString();
  m[QStringLiteral("sortName")] = item.value(QStringLiteral("SortName")).toString();
  m[QStringLiteral("originalTitle")] = item.value(QStringLiteral("OriginalTitle")).toString();
  m[QStringLiteral("overview")] = item.value(QStringLiteral("Overview")).toString();
  const QJsonArray taglines = item.value(QStringLiteral("Taglines")).toArray();
  m[QStringLiteral("tagline")] = taglines.isEmpty() ? QString() : taglines.first().toString();
  const int year = item.value(QStringLiteral("ProductionYear")).toInt();
  m[QStringLiteral("year")] = year > 0 ? QVariant(year) : QVariant();
  const QDateTime premiere = QDateTime::fromString(item.value(QStringLiteral("PremiereDate")).toString(), Qt::ISODate);
  m[QStringLiteral("premiereDate")] = premiere.isValid() ? QLocale().toString(premiere.date(), QLocale::ShortFormat) : QString();
  const QDateTime created = QDateTime::fromString(item.value(QStringLiteral("DateCreated")).toString(), Qt::ISODate);
  m[QStringLiteral("dateCreated")] = created.isValid() ? QLocale().toString(created.date(), QLocale::ShortFormat) : QString();
  m[QStringLiteral("officialRating")] = item.value(QStringLiteral("OfficialRating")).toString();
  const double community = item.value(QStringLiteral("CommunityRating")).toDouble();
  m[QStringLiteral("communityRating")] = community > 0 ? QString::number(community, 'f', 1) : QString();
  const double critic = item.value(QStringLiteral("CriticRating")).toDouble();
  m[QStringLiteral("criticRating")] = critic > 0 ? QVariant(int(critic)) : QVariant();
  const qint64 runtime = item.value(QStringLiteral("RunTimeTicks")).toInteger();
  m[QStringLiteral("runtimeTicks")] = runtime;
  m[QStringLiteral("runtimeText")] = FormatRuntime(runtime);
  m[QStringLiteral("genres")] = Names(item.value(QStringLiteral("Genres")).toArray()).join(QStringLiteral(" / "));
  m[QStringLiteral("studios")] = Names(item.value(QStringLiteral("Studios")).toArray()).join(QStringLiteral(", "));
  m[QStringLiteral("seriesId")] = item.value(QStringLiteral("SeriesId")).toString();
  m[QStringLiteral("seriesName")] = item.value(QStringLiteral("SeriesName")).toString();
  m[QStringLiteral("seasonId")] = item.value(QStringLiteral("SeasonId")).toString();
  m[QStringLiteral("seasonName")] = item.value(QStringLiteral("SeasonName")).toString();
  const QJsonValue index = item.value(QStringLiteral("IndexNumber"));
  const QJsonValue parent_index = item.value(QStringLiteral("ParentIndexNumber"));
  m[QStringLiteral("indexNumber")] = index.isDouble() ? QVariant(index.toInt()) : QVariant();
  m[QStringLiteral("parentIndexNumber")] = parent_index.isDouble() ? QVariant(parent_index.toInt()) : QVariant();
  if (type == QLatin1String("Episode") && index.isDouble()) {
    m[QStringLiteral("episodeLabel")] = parent_index.isDouble()
                                            ? QStringLiteral("S%1E%2")
                                                  .arg(parent_index.toInt(), 2, 10, QLatin1Char('0'))
                                                  .arg(index.toInt(), 2, 10, QLatin1Char('0'))
                                            : QStringLiteral("E%1").arg(index.toInt(), 2, 10, QLatin1Char('0'));
  } else {
    m[QStringLiteral("episodeLabel")] = QString();
  }
  m[QStringLiteral("childCount")] = item.value(QStringLiteral("ChildCount")).toInt();
  m[QStringLiteral("recursiveCount")] = item.value(QStringLiteral("RecursiveItemCount")).toInt();

  const QJsonObject user = item.value(QStringLiteral("UserData")).toObject();
  const bool played = user.value(QStringLiteral("Played")).toBool();
  const qint64 position = user.value(QStringLiteral("PlaybackPositionTicks")).toInteger();
  m[QStringLiteral("played")] = played;
  m[QStringLiteral("playedPercentage")] = user.value(QStringLiteral("PlayedPercentage")).toDouble();
  m[QStringLiteral("playbackPositionTicks")] = position;
  m[QStringLiteral("resumeText")] =
      position > 0 ? KFormat().formatDuration(quint64(position / kTicksPerMillisecond)) : QString();
  m[QStringLiteral("unplayedCount")] = user.value(QStringLiteral("UnplayedItemCount")).toInt();
  m[QStringLiteral("isFavorite")] = user.value(QStringLiteral("IsFavorite")).toBool();
  QString status;
  if (IsPlayable(item)) {
    status = played ? QStringLiteral("watched") : position > 0 ? QStringLiteral("inProgress") : QStringLiteral("unwatched");
  }
  m[QStringLiteral("status")] = status;

  // Images, with the parent's or series' artwork when the item has none.
  const QJsonObject tags = item.value(QStringLiteral("ImageTags")).toObject();
  QString poster = ImageUrl(api, id, QStringLiteral("Primary"), tags.value(QStringLiteral("Primary")).toString(), 0, 600);
  if (type == QLatin1String("Episode")) {
    // Episode stills are landscape; the pane wants the season or show poster.
    const QString season_tag = item.value(QStringLiteral("SeasonPrimaryImageTag")).toString();
    const QString series_tag = item.value(QStringLiteral("SeriesPrimaryImageTag")).toString();
    m[QStringLiteral("thumb")] = poster;
    poster = !season_tag.isEmpty()
                 ? ImageUrl(api, item.value(QStringLiteral("SeasonId")).toString(), QStringLiteral("Primary"), season_tag, 0, 600)
                 : ImageUrl(api, item.value(QStringLiteral("SeriesId")).toString(), QStringLiteral("Primary"), series_tag, 0, 600);
    if (poster.isEmpty()) poster = m[QStringLiteral("thumb")].toString();
  } else {
    m[QStringLiteral("thumb")] = ImageUrl(api, id, QStringLiteral("Thumb"), tags.value(QStringLiteral("Thumb")).toString(), 800, 0);
    if (poster.isEmpty() && type == QLatin1String("Season")) {
      poster = ImageUrl(api, item.value(QStringLiteral("SeriesId")).toString(), QStringLiteral("Primary"),
                        item.value(QStringLiteral("SeriesPrimaryImageTag")).toString(), 0, 600);
    }
  }
  m[QStringLiteral("poster")] = poster;
  const QJsonArray backdrops = item.value(QStringLiteral("BackdropImageTags")).toArray();
  QString backdrop;
  if (!backdrops.isEmpty()) {
    backdrop = ImageUrl(api, id, QStringLiteral("Backdrop"), backdrops.first().toString(), 1920, 0);
  } else {
    const QJsonArray parent_backdrops = item.value(QStringLiteral("ParentBackdropImageTags")).toArray();
    if (!parent_backdrops.isEmpty()) {
      backdrop = ImageUrl(api, item.value(QStringLiteral("ParentBackdropItemId")).toString(), QStringLiteral("Backdrop"),
                          parent_backdrops.first().toString(), 1920, 0);
    }
  }
  m[QStringLiteral("backdrop")] = backdrop;
  QString logo = ImageUrl(api, id, QStringLiteral("Logo"), tags.value(QStringLiteral("Logo")).toString(), 800, 0);
  if (logo.isEmpty()) {
    logo = ImageUrl(api, item.value(QStringLiteral("ParentLogoItemId")).toString(), QStringLiteral("Logo"),
                    item.value(QStringLiteral("ParentLogoImageTag")).toString(), 800, 0);
  }
  m[QStringLiteral("logo")] = logo;

  // Codec flags from the first video and default (or first) audio stream.
  QStringList video_flags;
  QStringList audio_flags;
  bool subtitles = false;
  QJsonObject video;
  QJsonObject audio;
  for (const QJsonValue& value : item.value(QStringLiteral("MediaStreams")).toArray()) {
    const QJsonObject stream = value.toObject();
    const QString kind = stream.value(QStringLiteral("Type")).toString();
    if (kind == QLatin1String("Video") && video.isEmpty()) video = stream;
    if (kind == QLatin1String("Audio") && (audio.isEmpty() || stream.value(QStringLiteral("IsDefault")).toBool())) {
      audio = stream;
    }
    if (kind == QLatin1String("Subtitle")) subtitles = true;
  }
  if (!video.isEmpty()) {
    const QString resolution =
        VideoResolution(video.value(QStringLiteral("Width")).toInt(), video.value(QStringLiteral("Height")).toInt());
    if (!resolution.isEmpty()) video_flags.append(resolution);
    video_flags.append(CodecName(video.value(QStringLiteral("Codec")).toString()));
    const QString range = video.value(QStringLiteral("VideoRangeType")).toString();
    if (!range.isEmpty() && range != QLatin1String("SDR") && range != QLatin1String("Unknown")) video_flags.append(range);
  }
  if (!audio.isEmpty()) {
    audio_flags.append(CodecName(audio.value(QStringLiteral("Codec")).toString()));
    const QString layout = ChannelLayout(audio.value(QStringLiteral("Channels")).toInt());
    if (!layout.isEmpty()) audio_flags.append(layout);
  }
  m[QStringLiteral("videoFlags")] = video_flags.join(QStringLiteral(" · "));
  m[QStringLiteral("audioFlags")] = audio_flags.join(QStringLiteral(" · "));
  m[QStringLiteral("hasSubtitles")] = subtitles;
  return m;
}

}  // namespace ember::jellyfin
