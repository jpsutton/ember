// SPDX-License-Identifier: GPL-3.0-only
// Field mapping follows Plezy's lib/services/jellyfin_mappers.dart (GPL-3.0).

#include "Items.h"

#include <KFormat>

#include <QDateTime>
#include <QJsonArray>
#include <QLocale>
#include <QUrlQuery>

#include <cmath>

#include "ApiClient.h"
#include "EmberSettings.h"
#include "playback/TrackLanguage.h"

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

QString Str(const QJsonObject& o, const char* key) { return o.value(QLatin1String(key)).toString(); }

// A badge for the details pane: |tag| (or |icon|) on a solid block, then
// |value| in an outlined one. Either half may be empty.
QVariantMap Badge(const QString& tag, const QString& icon, const QString& value) {
  return QVariantMap{{QStringLiteral("tag"), tag}, {QStringLiteral("icon"), icon}, {QStringLiteral("value"), value}};
}

QString VideoCodecName(const QString& codec) {
  const QString c = codec.toLower();
  if (c == QLatin1String("h264") || c == QLatin1String("avc")) return QStringLiteral("H.264");
  if (c == QLatin1String("hevc") || c == QLatin1String("h265")) return QStringLiteral("HEVC");
  if (c == QLatin1String("av1")) return QStringLiteral("AV1");
  if (c == QLatin1String("vp9")) return QStringLiteral("VP9");
  if (c == QLatin1String("vp8")) return QStringLiteral("VP8");
  if (c == QLatin1String("mpeg2video")) return QStringLiteral("MPEG-2");
  if (c == QLatin1String("mpeg4")) return QStringLiteral("MPEG-4");
  if (c == QLatin1String("vc1")) return QStringLiteral("VC-1");
  return codec.toUpper();
}

// Short names, to fit a badge: DD for Dolby Digital and so on.
QString AudioCodecName(const QString& codec, const QString& profile) {
  const QString c = codec.toLower();
  if (c == QLatin1String("ac3")) return QStringLiteral("DD");
  if (c == QLatin1String("eac3")) return QStringLiteral("DD+");
  if (c == QLatin1String("truehd")) return QStringLiteral("TrueHD");
  if (c == QLatin1String("dts") || c == QLatin1String("dca")) {
    if (profile.contains(QLatin1String("DTS:X"))) return QStringLiteral("DTS:X");
    if (profile.contains(QLatin1String("MA"))) return QStringLiteral("DTS-HD MA");
    if (profile.contains(QLatin1String("HRA"))) return QStringLiteral("DTS-HD HRA");
    return QStringLiteral("DTS");
  }
  if (c == QLatin1String("aac")) return QStringLiteral("AAC");
  if (c == QLatin1String("flac")) return QStringLiteral("FLAC");
  if (c == QLatin1String("alac")) return QStringLiteral("ALAC");
  if (c == QLatin1String("opus")) return QStringLiteral("Opus");
  if (c == QLatin1String("vorbis")) return QStringLiteral("Vorbis");
  if (c == QLatin1String("mp3")) return QStringLiteral("MP3");
  if (c == QLatin1String("mp2")) return QStringLiteral("MP2");
  if (c.startsWith(QLatin1String("pcm"))) return QStringLiteral("PCM");
  return codec.toUpper();
}

QString ChannelLayout(int channels, const QString& layout) {
  switch (channels) {
    case 0: return {};
    case 1: return QStringLiteral("1.0");
    case 2: return QStringLiteral("2.0");
    case 3: return QStringLiteral("2.1");
    case 6: return QStringLiteral("5.1");
    case 8: return QStringLiteral("7.1");
    default: return layout.isEmpty() ? QString::number(channels) : layout;
  }
}

// The picture's shape as Amber shows it (1.78, 2.35), from the stream's
// display aspect ("16:9", "2.40:1") or else its size, snapped to the usual
// film and TV ratios.
QString AspectRatio(const QJsonObject& video) {
  double ratio = 0;
  const QStringList parts = Str(video, "AspectRatio").split(QLatin1Char(':'));
  if (parts.size() == 2 && parts[1].toDouble() > 0) ratio = parts[0].toDouble() / parts[1].toDouble();
  const int width = video.value(QStringLiteral("Width")).toInt();
  const int height = video.value(QStringLiteral("Height")).toInt();
  if (ratio <= 0 && height > 0) ratio = double(width) / height;
  if (ratio <= 0) return {};
  for (const double usual : {1.33, 1.37, 1.66, 1.78, 1.85, 2.0, 2.2, 2.35, 2.39, 2.4, 2.55, 2.76}) {
    if (std::abs(ratio - usual) < 0.02) {
      ratio = usual;
      break;
    }
  }
  return QString::number(ratio, 'f', 2);
}

}  // namespace

QVariantList MediaBadges(const QJsonObject& item, const QString& subtitle_language) {
  QJsonObject video;
  QJsonObject audio;
  bool subtitles = false;
  for (const QJsonValue& value : item.value(QStringLiteral("MediaStreams")).toArray()) {
    const QJsonObject stream = value.toObject();
    const QString kind = Str(stream, "Type");
    if (kind == QLatin1String("Video") && video.isEmpty()) video = stream;
    if (kind == QLatin1String("Audio")) {
      if (audio.isEmpty() || stream.value(QStringLiteral("IsDefault")).toBool()) audio = stream;
    }
    if (kind == QLatin1String("Subtitle") &&
        (subtitle_language.isEmpty() || LanguageScore(stream, subtitle_language) >= 0)) {
      subtitles = true;
    }
  }

  QVariantList badges;
  if (!video.isEmpty()) {
    const QString resolution =
        VideoResolution(video.value(QStringLiteral("Width")).toInt(), video.value(QStringLiteral("Height")).toInt());
    if (!resolution.isEmpty()) {
      const bool interlaced = video.value(QStringLiteral("IsInterlaced")).toBool();
      const int height = video.value(QStringLiteral("Height")).toInt();
      QString value = resolution == QLatin1String("4K")      ? QStringLiteral("4K")
                      : resolution == QLatin1String("1080p") ? QStringLiteral("1080")
                      : resolution == QLatin1String("720p")  ? QStringLiteral("720")
                      : height >= 560                        ? QStringLiteral("576")
                                                             : QStringLiteral("480");
      if (interlaced && value != QLatin1String("4K")) value += QLatin1Char('i');
      const QString tag = resolution == QLatin1String("4K") ? QStringLiteral("UHD")
                          : resolution == QLatin1String("SD") ? QStringLiteral("SD")
                                                              : QStringLiteral("HD");
      badges.append(Badge(tag, {}, value));
    }
    badges.append(Badge({}, QStringLiteral("film"), VideoCodecName(Str(video, "Codec"))));
    const QString range = Str(video, "VideoRangeType");
    if (range.startsWith(QLatin1String("DOVI"))) badges.append(Badge(QStringLiteral("DV"), {}, {}));
    if (range == QLatin1String("HDR10") || range == QLatin1String("DOVIWithHDR10")) {
      badges.append(Badge(QStringLiteral("HDR"), {}, QStringLiteral("10")));
    } else if (range == QLatin1String("HDR10Plus") || range == QLatin1String("DOVIWithHDR10Plus")) {
      badges.append(Badge(QStringLiteral("HDR"), {}, QStringLiteral("10+")));
    } else if (range == QLatin1String("HLG") || range == QLatin1String("DOVIWithHLG")) {
      badges.append(Badge(QStringLiteral("HDR"), {}, QStringLiteral("HLG")));
    }
    if (!Str(item, "Video3DFormat").isEmpty()) badges.append(Badge(QStringLiteral("3D"), {}, {}));
    const QString aspect = AspectRatio(video);
    if (!aspect.isEmpty()) badges.append(Badge({}, QStringLiteral("aspect"), aspect));
  }
  if (!audio.isEmpty()) {
    const QString profile = Str(audio, "Profile");
    badges.append(Badge({}, QStringLiteral("speaker"), AudioCodecName(Str(audio, "Codec"), profile)));
    if (profile.contains(QLatin1String("Atmos"))) badges.append(Badge(QStringLiteral("ATMOS"), {}, {}));
    const QString layout =
        ChannelLayout(audio.value(QStringLiteral("Channels")).toInt(), Str(audio, "ChannelLayout"));
    if (!layout.isEmpty()) badges.append(Badge({}, QStringLiteral("channels"), layout));
  }
  if (subtitles) badges.append(Badge(QStringLiteral("CC"), {}, {}));
  return badges;
}

QVariantList InfoBadges(const QJsonObject& item) {
  const QString type = Str(item, "Type");
  const QJsonObject user = item.value(QStringLiteral("UserData")).toObject();
  QVariantList badges;

  const QString rating = Str(item, "OfficialRating");
  if (!rating.isEmpty()) badges.append(Badge(rating, {}, {}));

  // When: an episode's air date, a show's run, a film's year.
  const QDateTime premiere = QDateTime::fromString(Str(item, "PremiereDate"), Qt::ISODate);
  const int year = item.value(QStringLiteral("ProductionYear")).toInt();
  if (type == QLatin1String("Episode") && premiere.isValid()) {
    badges.append(Badge({}, QStringLiteral("calendar"), QLocale().toString(premiere.date(), QLocale::ShortFormat)));
  } else if (type == QLatin1String("Series") && year > 0) {
    const QDateTime end = QDateTime::fromString(Str(item, "EndDate"), Qt::ISODate);
    QString run = QString::number(year) + QChar(0x2013);
    if (Str(item, "Status") == QLatin1String("Ended") && end.isValid() && end.date().year() != year) {
      run += QString::number(end.date().year());
    } else if (Str(item, "Status") == QLatin1String("Ended")) {
      run = QString::number(year);
    }
    badges.append(Badge({}, QStringLiteral("calendar"), run));
  } else if (year > 0 && type != QLatin1String("Season")) {
    badges.append(Badge({}, QStringLiteral("calendar"), QString::number(year)));
  }

  const QString runtime = FormatRuntime(item.value(QStringLiteral("RunTimeTicks")).toInteger());
  if (!runtime.isEmpty() && type != QLatin1String("Season")) badges.append(Badge({}, QStringLiteral("clock"), runtime));
  const double community = item.value(QStringLiteral("CommunityRating")).toDouble();
  if (community > 0) badges.append(Badge({}, QStringLiteral("star"), QString::number(community, 'f', 1)));
  const double critic = item.value(QStringLiteral("CriticRating")).toDouble();
  if (critic > 0) badges.append(Badge({}, QStringLiteral("critic"), QStringLiteral("%1%").arg(int(critic))));

  const int unplayed = user.value(QStringLiteral("UnplayedItemCount")).toInt();
  if (type == QLatin1String("Series")) {
    const int seasons = item.value(QStringLiteral("ChildCount")).toInt();
    const int episodes = item.value(QStringLiteral("RecursiveItemCount")).toInt();
    if (seasons > 0) badges.append(Badge(QStringLiteral("SEASONS"), {}, QString::number(seasons)));
    if (episodes > 0) badges.append(Badge(QStringLiteral("EPISODES"), {}, QString::number(episodes)));
  } else if (type == QLatin1String("Season")) {
    const int episodes = item.value(QStringLiteral("ChildCount")).toInt();
    if (episodes > 0) badges.append(Badge(QStringLiteral("EPISODES"), {}, QString::number(episodes)));
  }
  if (user.value(QStringLiteral("Played")).toBool()) {
    badges.append(Badge(QStringLiteral("WATCHED"), QStringLiteral("check"), {}));
  } else if (unplayed > 0 && (type == QLatin1String("Series") || type == QLatin1String("Season"))) {
    badges.append(Badge(QStringLiteral("UNWATCHED"), {}, QString::number(unplayed)));
  }
  if (user.value(QStringLiteral("IsFavorite")).toBool()) badges.append(Badge({}, QStringLiteral("heart"), {}));
  return badges;
}

namespace {

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
  if (minutes < 1) return QStringLiteral("< 1 min");
  if (minutes < 60) return QStringLiteral("%1 min").arg(minutes);
  return QStringLiteral("%1 h %2 min").arg(minutes / 60).arg(minutes % 60);
}

bool IsPlayable(const QJsonObject& item) {
  if (item.value(QStringLiteral("IsFolder")).toBool()) return false;
  const QString type = item.value(QStringLiteral("Type")).toString();
  return type == QLatin1String("Movie") || type == QLatin1String("Episode") || type == QLatin1String("Video") ||
         type == QLatin1String("MusicVideo") || type == QLatin1String("Trailer") || type == QLatin1String("TvChannel") ||
         item.value(QStringLiteral("MediaType")).toString() == QLatin1String("Video");
}

QDateTime AiredDate(const QJsonObject& item) {
  const QDateTime premiere = QDateTime::fromString(item.value(QStringLiteral("PremiereDate")).toString(), Qt::ISODate);
  const QDateTime created = QDateTime::fromString(item.value(QStringLiteral("DateCreated")).toString(), Qt::ISODate);
  if (!premiere.isValid()) return created;
  return created.isValid() && created < premiere ? created : premiere;
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
  const QDateTime aired = AiredDate(item);
  m[QStringLiteral("airedDate")] = aired.isValid() ? QLocale().toString(aired.date(), QLocale::ShortFormat) : QString();
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
  // Live TV: a channel's number; a programme's channel, times and episode.
  m[QStringLiteral("channelNumber")] = item.value(QStringLiteral("ChannelNumber")).toString();
  m[QStringLiteral("channelId")] = item.value(QStringLiteral("ChannelId")).toString();
  const QDateTime starts = QDateTime::fromString(item.value(QStringLiteral("StartDate")).toString(), Qt::ISODate);
  const QDateTime ends = QDateTime::fromString(item.value(QStringLiteral("EndDate")).toString(), Qt::ISODate);
  m[QStringLiteral("startEpoch")] = starts.isValid() ? QVariant(starts.toSecsSinceEpoch()) : QVariant();
  m[QStringLiteral("endEpoch")] = ends.isValid() ? QVariant(ends.toSecsSinceEpoch()) : QVariant();
  m[QStringLiteral("episodeTitle")] = item.value(QStringLiteral("EpisodeTitle")).toString();
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
  // A channel is never "watched".
  if (IsPlayable(item) && type != QLatin1String("TvChannel")) {
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

  m[QStringLiteral("infoBadges")] = InfoBadges(item);
  m[QStringLiteral("mediaBadges")] = MediaBadges(item, EmberSettings::self()->subtitleLanguage());
  return m;
}

}  // namespace ember::jellyfin
