// SPDX-License-Identifier: GPL-3.0-only

#include "Playback.h"

#include <mpv/client.h>

#include <QJsonDocument>
#include <QLoggingCategory>
#include <QRegularExpression>
#include <QUrlQuery>

#include <algorithm>
#include <cmath>

#include "../app/SystemIntegration.h"
#include "../jellyfin/ApiClient.h"
#include "../jellyfin/Items.h"
#include "../jellyfin/Session.h"
#include "../player/MpvVideo.h"
#include "EmberSettings.h"
#include "Shuffle.h"

Q_LOGGING_CATEGORY(lcPlayback, "ember.playback")

namespace ember {
namespace {

using jellyfin::ApiClient;
using jellyfin::Reply;
using jellyfin::Session;

constexpr double kTicksPerSecond = 1e7;
constexpr int kProgressIntervalMs = 10000;
constexpr qint64 kOriginalQualityBitrate = 200'000'000;

qint64 Ticks(double seconds) { return qint64(std::llround(seconds * kTicksPerSecond)); }
double Seconds(qint64 ticks) { return double(ticks) / kTicksPerSecond; }

QString Str(const QJsonObject& o, const char* key) { return o.value(QLatin1String(key)).toString(); }

// Absolute URL for a server-relative path (TranscodingUrl, DeliveryUrl),
// with the token as ApiKey: Jellyfin 12 rejects api_key.
QString AbsoluteUrl(const ApiClient* api, QString relative) {
  relative.replace(QStringLiteral("api_key="), QStringLiteral("ApiKey="));
  QString url = api->baseUrl().toString() + relative;
  if (!url.contains(QStringLiteral("ApiKey="))) {
    url += (url.contains(QLatin1Char('?')) ? QStringLiteral("&") : QStringLiteral("?")) + QStringLiteral("ApiKey=") +
           api->token();
  }
  return url;
}

}  // namespace

Playback::Playback(QObject* parent) : QObject(parent) {
  progress_timer_.setInterval(kProgressIntervalMs);
  connect(&progress_timer_, &QTimer::timeout, this, [this]() {
    if (session_open_ && started_reported_) report(QStringLiteral("Progress"));
  });
}

Playback::~Playback() {
  if (session_open_) report(QStringLiteral("Stopped"));
}

void Playback::setVideo(MpvVideo* video) {
  if (video == video_) return;
  if (video_) disconnect(video_, nullptr, this, nullptr);
  video_ = video;
  if (video_) {
    connect(video_, &MpvVideo::mpvEvent, this, &Playback::onMpvEvent);
    connect(video_, &MpvVideo::mpvPropertyChanged, this, &Playback::onMpvProperty);
    for (const char* name : {"time-pos", "duration"}) video_->observe(QString::fromLatin1(name), QStringLiteral("double"));
    for (const char* name : {"pause", "paused-for-cache", "eof-reached"}) {
      video_->observe(QString::fromLatin1(name), QStringLiteral("flag"));
    }
    video_->observe(QStringLiteral("track-list"), QStringLiteral("node"));
    applyAudioSettings();
    if (CouchboxConfig().fastScaling()) {
      // couchbox's profile for weak GPUs: cheap scalers, no dithering.
      for (const auto& [name, value] :
           std::initializer_list<std::pair<const char*, const char*>>{{"scale", "bilinear"},
                                                                     {"dscale", "bilinear"},
                                                                     {"cscale", "bilinear"},
                                                                     {"dither", "no"},
                                                                     {"correct-downscaling", "no"},
                                                                     {"linear-downscaling", "no"},
                                                                     {"sigmoid-upscaling", "no"},
                                                                     {"hdr-compute-peak", "no"}}) {
        video_->setOption(QString::fromLatin1(name), QString::fromLatin1(value));
      }
    }
  }
  emit videoChanged();
}

void Playback::applyAudioSettings() {
  if (!video_) return;
  EmberSettings* settings = EmberSettings::self();
  if (settings->downmix()) {
    // Kodi's centre level: 10^((-3 + boost) / 20). Swresample options are
    // read when the filter chain is built, so they go before audio-channels.
    const int boost = std::clamp(settings->centerBoostDb(), 0, 12);
    const double level = std::pow(10.0, (-3.0 + boost) / 20.0);
    video_->setOption(QStringLiteral("audio-swresample-o"), QStringLiteral("center_mix_level=%1").arg(level, 0, 'f', 4));
    video_->setOption(QStringLiteral("audio-normalize-downmix"), QStringLiteral("no"));
    video_->setOption(QStringLiteral("audio-channels"), QStringLiteral("stereo"));
  } else {
    video_->setOption(QStringLiteral("audio-swresample-o"), QString());
    video_->setOption(QStringLiteral("audio-channels"), QStringLiteral("auto-safe"));
  }
}

QString Playback::title() const {
  if (item_.value(QStringLiteral("type")).toString() == QLatin1String("Episode")) {
    return item_.value(QStringLiteral("seriesName")).toString();
  }
  return item_.value(QStringLiteral("name")).toString();
}

QString Playback::subtitle() const {
  if (item_.value(QStringLiteral("type")).toString() == QLatin1String("Episode")) {
    return item_.value(QStringLiteral("episodeLabel")).toString() + QStringLiteral(" · ") +
           item_.value(QStringLiteral("name")).toString();
  }
  const QVariant year = item_.value(QStringLiteral("year"));
  return year.isValid() ? year.toString() : QString();
}

void Playback::setState(State state) {
  if (state == state_) return;
  state_ = state;
  emit stateChanged();
}

void Playback::setError(const QString& error) {
  if (error == error_string_) return;
  error_string_ = error;
  emit errorStringChanged();
}

void Playback::play(const QString& item_id, bool from_start) {
  stopShuffle();
  playTask(item_id, from_start, -1, ++generation_);
}

void Playback::playAt(const QString& item_id, double seconds) {
  stopShuffle();
  playTask(item_id, false, std::max(0.0, seconds), ++generation_);
}

void Playback::shuffle(const QString& series_id, const QString& season_id) {
  stopShuffle();
  shuffleTask(series_id, season_id, ++generation_);
}

void Playback::stopShuffle() {
  if (shuffle_aired_.isEmpty()) return;
  shuffle_aired_.clear();
  shuffle_queue_.clear();
  emit shufflingChanged();
}

QCoro::Task<> Playback::shuffleTask(QString series_id, QString season_id, quint64 generation) {
  QPointer<Playback> self(this);
  setError(QString());
  setState(State::Loading);
  Session* session = Session::instance();
  QUrlQuery query;
  query.addQueryItem(QStringLiteral("userId"), session->userId());
  query.addQueryItem(QStringLiteral("IsMissing"), QStringLiteral("false"));
  if (!season_id.isEmpty()) query.addQueryItem(QStringLiteral("SeasonId"), season_id);
  const Reply reply = co_await session->api()->get(QStringLiteral("/Shows/%1/Episodes").arg(series_id), query);
  if (!self || generation != generation_) co_return;
  if (!reply.ok()) {
    setError(tr("Could not load the episodes (%1).").arg(reply.error));
    setState(State::Failed);
    co_return;
  }
  QStringList aired;
  QStringList specials;
  for (const QJsonValue& value : reply.object().value(QStringLiteral("Items")).toArray()) {
    const QJsonObject episode = value.toObject();
    const bool special = episode.value(QStringLiteral("ParentIndexNumber")).toInt(-1) == 0;
    (special && season_id.isEmpty() ? specials : aired).append(Str(episode, "Id"));
  }
  // A show with nothing but specials shuffles those.
  if (aired.isEmpty()) aired = specials;
  if (aired.isEmpty()) {
    setError(tr("There are no episodes to shuffle."));
    setState(State::Failed);
    co_return;
  }
  shuffle_aired_ = aired;
  shuffle_queue_ = ShuffleOrder(aired, QString(), *QRandomGenerator::global());
  emit shufflingChanged();
  playTask(shuffle_queue_.takeFirst(), true, -1, generation);
}

QCoro::Task<> Playback::playTask(QString item_id, bool from_start, double start_seconds, quint64 generation) {
  QPointer<Playback> self(this);
  if (session_open_) {
    // Switching items (next episode): close the old session first.
    session_open_ = false;
    progress_timer_.stop();
    const QString previous = item_.value(QStringLiteral("id")).toString();
    const bool finished_previous = mark_previous_played_;
    mark_previous_played_ = false;
    co_await report(QStringLiteral("Stopped"));
    if (!self) co_return;
    if (finished_previous && !previous.isEmpty()) {
      // Moving on from Up Next means the episode was watched, even when the
      // credits started before the server's 90 % mark.
      Session* session = Session::instance();
      QUrlQuery query;
      query.addQueryItem(QStringLiteral("userId"), session->userId());
      co_await session->api()->post(QStringLiteral("/UserPlayedItems/%1").arg(previous), {}, query);
      if (!self) co_return;
      co_await session->api()->post(QStringLiteral("/UserItems/%1/UserData").arg(previous),
                                    QJsonDocument(QJsonObject{{QStringLiteral("PlaybackPositionTicks"), 0}}), query);
      if (!self) co_return;
    }
  }
  if (generation != generation_) co_return;
  setError(QString());
  setState(State::Loading);
  stream_ = Stream();
  started_reported_ = false;
  file_loaded_ = false;
  if (video_shown_) {
    video_shown_ = false;
    emit videoShownChanged();
  }
  position_ = 0;
  duration_ = 0;
  emit positionChanged();
  emit durationChanged();
  intro_start_ = intro_end_ = credits_start_ = -1;
  emit segmentsChanged();
  next_item_.clear();
  emit nextItemChanged();

  Session* session = Session::instance();
  ApiClient* api = session->api();
  QUrlQuery query;
  query.addQueryItem(QStringLiteral("userId"), session->userId());
  query.addQueryItem(QStringLiteral("Fields"), jellyfin::ListFields() + QStringLiteral(",MediaSources,Chapters,Trickplay"));
  const Reply reply = co_await api->get(QStringLiteral("/Items/%1").arg(item_id), query);
  if (!self || generation != generation_) co_return;
  if (!reply.ok()) {
    setError(tr("Could not load the item (%1).").arg(reply.error));
    setState(State::Failed);
    co_return;
  }
  item_json_ = reply.object();
  item_ = jellyfin::ItemToVariant(item_json_, api);

  chapters_.clear();
  for (const QJsonValue& value : item_json_.value(QStringLiteral("Chapters")).toArray()) {
    const QJsonObject chapter = value.toObject();
    chapters_.append(QVariantMap{{QStringLiteral("title"), Str(chapter, "Name")},
                                 {QStringLiteral("start"), Seconds(chapter.value(QStringLiteral("StartPositionTicks")).toInteger())}});
  }

  trickplay_.clear();
  const QJsonObject trickplay_sources = item_json_.value(QStringLiteral("Trickplay")).toObject();
  if (!trickplay_sources.isEmpty()) {
    const QString source_id = trickplay_sources.keys().first();
    const QJsonObject widths = trickplay_sources.value(source_id).toObject();
    QJsonObject best;
    int best_width = 0;
    for (const QString& key : widths.keys()) {
      const int width = key.toInt();
      if (best_width == 0 || (width >= 240 && (best_width < 240 || width < best_width)) || (best_width < 240 && width > best_width)) {
        best_width = width;
        best = widths.value(key).toObject();
      }
    }
    if (best_width > 0) {
      QUrlQuery tq;
      tq.addQueryItem(QStringLiteral("MediaSourceId"), source_id);
      tq.addQueryItem(QStringLiteral("ApiKey"), api->token());
      trickplay_ = QVariantMap{
          {QStringLiteral("url"),
           api->url(QStringLiteral("/Videos/%1/Trickplay/%2/").arg(item_id).arg(best_width)).toString() +
               QStringLiteral("%1.jpg?") + tq.toString(QUrl::FullyEncoded)},
          {QStringLiteral("width"), best.value(QStringLiteral("Width")).toInt()},
          {QStringLiteral("height"), best.value(QStringLiteral("Height")).toInt()},
          {QStringLiteral("tileWidth"), best.value(QStringLiteral("TileWidth")).toInt()},
          {QStringLiteral("tileHeight"), best.value(QStringLiteral("TileHeight")).toInt()},
          {QStringLiteral("count"), best.value(QStringLiteral("ThumbnailCount")).toInt()},
          {QStringLiteral("interval"), best.value(QStringLiteral("Interval")).toInt() / 1000.0}};
    }
  }
  emit itemChanged();

  const qint64 saved = item_json_.value(QStringLiteral("UserData")).toObject().value(QStringLiteral("PlaybackPositionTicks")).toInteger();
  start_seconds_ = start_seconds >= 0 ? start_seconds : from_start ? 0 : Seconds(saved);
  audio_index_ = -1;
  subtitle_index_ = -2;  // not chosen yet
  if (!co_await negotiate(start_seconds_, generation)) co_return;
  if (!self || generation != generation_) co_return;
  chooseDefaultTracks();
  loadStream(start_seconds_);
  loadSegments(item_id, generation);
  loadNextItem(item_json_, generation);
}

QJsonObject Playback::deviceProfile() const {
  const CouchboxConfig couchbox;
  QStringList video_codecs{QStringLiteral("h264"), QStringLiteral("mpeg4"), QStringLiteral("mpeg2video"), QStringLiteral("vp8")};
  if (!couchbox.transcodeHevc()) video_codecs << QStringLiteral("hevc");
  if (!couchbox.transcodeVp9()) video_codecs << QStringLiteral("vp9");
  if (!couchbox.transcodeAv1()) video_codecs << QStringLiteral("av1");

  QJsonArray codec_profiles;
  if (!couchbox.transcodeHevc() && couchbox.transcodeHevc10()) {
    codec_profiles.append(QJsonObject{
        {QStringLiteral("Type"), QStringLiteral("Video")},
        {QStringLiteral("Codec"), QStringLiteral("hevc")},
        {QStringLiteral("Conditions"),
         QJsonArray{QJsonObject{{QStringLiteral("Condition"), QStringLiteral("LessThanEqual")},
                                {QStringLiteral("Property"), QStringLiteral("VideoBitDepth")},
                                {QStringLiteral("Value"), QStringLiteral("8")},
                                {QStringLiteral("IsRequired"), true}}}}});
  }

  QJsonArray subtitle_profiles;
  for (const char* format : {"srt", "subrip", "ass", "ssa", "vtt", "webvtt"}) {
    subtitle_profiles.append(QJsonObject{{QStringLiteral("Format"), QString::fromLatin1(format)}, {QStringLiteral("Method"), QStringLiteral("External")}});
  }
  for (const char* format : {"srt", "subrip", "ass", "ssa", "vtt", "webvtt", "pgssub", "pgs", "dvdsub", "dvbsub", "sub", "mov_text"}) {
    subtitle_profiles.append(QJsonObject{{QStringLiteral("Format"), QString::fromLatin1(format)}, {QStringLiteral("Method"), QStringLiteral("Embed")}});
  }

  const int max_mbps = EmberSettings::self()->maxBitrateMbps();
  const qint64 bitrate = max_mbps > 0 ? qint64(max_mbps) * 1'000'000 : kOriginalQualityBitrate;
  return QJsonObject{
      {QStringLiteral("Name"), QStringLiteral("Ember")},
      {QStringLiteral("MaxStreamingBitrate"), bitrate},
      {QStringLiteral("MaxStaticBitrate"), bitrate},
      {QStringLiteral("DirectPlayProfiles"),
       QJsonArray{QJsonObject{{QStringLiteral("Type"), QStringLiteral("Video")},
                              {QStringLiteral("Container"), QStringLiteral("mp4,m4v,mkv,webm,mov,ts,mpegts,avi,ogg,wmv,asf")},
                              {QStringLiteral("VideoCodec"), video_codecs.join(QLatin1Char(','))}}}},
      {QStringLiteral("TranscodingProfiles"),
       QJsonArray{QJsonObject{{QStringLiteral("Type"), QStringLiteral("Video")},
                              {QStringLiteral("Container"), QStringLiteral("ts")},
                              {QStringLiteral("Protocol"), QStringLiteral("hls")},
                              {QStringLiteral("Context"), QStringLiteral("Streaming")},
                              {QStringLiteral("VideoCodec"), QStringLiteral("h264")},
                              {QStringLiteral("AudioCodec"), QStringLiteral("aac,ac3,mp3")},
                              {QStringLiteral("MaxAudioChannels"), QStringLiteral("6")},
                              {QStringLiteral("MinSegments"), 1},
                              {QStringLiteral("BreakOnNonKeyFrames"), true}}}},
      {QStringLiteral("ContainerProfiles"), QJsonArray()},
      {QStringLiteral("CodecProfiles"), codec_profiles},
      {QStringLiteral("SubtitleProfiles"), subtitle_profiles}};
}

QCoro::Task<bool> Playback::negotiate(double start_seconds, quint64 generation) {
  QPointer<Playback> self(this);
  Session* session = Session::instance();
  ApiClient* api = session->api();
  const QString item_id = item_.value(QStringLiteral("id")).toString();
  const QJsonObject profile = deviceProfile();
  QJsonObject body{{QStringLiteral("UserId"), session->userId()},
                   {QStringLiteral("MaxStreamingBitrate"), profile.value(QStringLiteral("MaxStreamingBitrate"))},
                   {QStringLiteral("StartTimeTicks"), Ticks(start_seconds)},
                   {QStringLiteral("DeviceProfile"), profile},
                   {QStringLiteral("EnableDirectPlay"), true},
                   {QStringLiteral("EnableDirectStream"), true},
                   {QStringLiteral("EnableTranscoding"), true},
                   {QStringLiteral("AllowVideoStreamCopy"), true},
                   {QStringLiteral("AllowAudioStreamCopy"), true},
                   {QStringLiteral("AutoOpenLiveStream"), true}};
  // Set when renegotiating the same item (a track change during a transcode).
  if (!stream_.media_source_id.isEmpty()) body.insert(QStringLiteral("MediaSourceId"), stream_.media_source_id);
  if (audio_index_ >= 0) body.insert(QStringLiteral("AudioStreamIndex"), audio_index_);
  if (subtitle_index_ >= -1) body.insert(QStringLiteral("SubtitleStreamIndex"), subtitle_index_);
  QUrlQuery query;
  query.addQueryItem(QStringLiteral("userId"), session->userId());
  const Reply reply = co_await api->post(QStringLiteral("/Items/%1/PlaybackInfo").arg(item_id), QJsonDocument(body), query);
  if (!self || generation != generation_) co_return false;
  const QJsonArray sources = reply.object().value(QStringLiteral("MediaSources")).toArray();
  if (!reply.ok() || sources.isEmpty()) {
    const QString code = Str(reply.object(), "ErrorCode");
    setError(code.isEmpty() ? tr("The server can't play this (%1).").arg(reply.error) : tr("The server can't play this (%1).").arg(code));
    setState(State::Failed);
    co_return false;
  }
  const QJsonObject source = sources.first().toObject();
  Stream stream;
  stream.source = source;
  stream.media_source_id = Str(source, "Id");
  stream.play_session_id = Str(reply.object(), "PlaySessionId");
  const QString transcoding_url = Str(source, "TranscodingUrl");
  if (source.value(QStringLiteral("SupportsDirectPlay")).toBool() || transcoding_url.isEmpty()) {
    QUrlQuery stream_query;
    stream_query.addQueryItem(QStringLiteral("Static"), QStringLiteral("true"));
    stream_query.addQueryItem(QStringLiteral("MediaSourceId"), stream.media_source_id);
    if (!stream.play_session_id.isEmpty()) stream_query.addQueryItem(QStringLiteral("PlaySessionId"), stream.play_session_id);
    stream_query.addQueryItem(QStringLiteral("DeviceId"), api->deviceId());
    stream.url = api->authenticatedUrl(QStringLiteral("/Videos/%1/stream").arg(item_id), stream_query).toString();
    stream.method = QStringLiteral("DirectPlay");
  } else {
    stream.url = AbsoluteUrl(api, transcoding_url);
    stream.method = source.value(QStringLiteral("SupportsDirectStream")).toBool() &&
                            Str(source, "TranscodingSubProtocol") != QLatin1String("hls")
                        ? QStringLiteral("DirectStream")
                        : QStringLiteral("Transcode");
    if (QUrlQuery(QUrl(stream.url)).hasQueryItem(QStringLiteral("PlaySessionId"))) {
      stream.play_session_id = QUrlQuery(QUrl(stream.url)).queryItemValue(QStringLiteral("PlaySessionId"));
    }
  }
  qCInfo(lcPlayback).noquote() << "playing" << item_.value(QStringLiteral("name")).toString() << "by" << stream.method;
  stream_ = stream;
  play_method_ = stream.method;
  emit streamChanged();
  emit tracksChanged();
  co_return true;
}

void Playback::chooseDefaultTracks() {
  EmberSettings* settings = EmberSettings::self();
  const QJsonObject& source = stream_.source;
  const QJsonArray streams = source.value(QStringLiteral("MediaStreams")).toArray();

  if (audio_index_ < 0) {
    const QString language = settings->audioLanguage();
    int chosen = -1;
    if (!language.isEmpty()) {
      for (const QJsonValue& value : streams) {
        const QJsonObject s = value.toObject();
        if (Str(s, "Type") == QLatin1String("Audio") && Str(s, "Language") == language) {
          chosen = s.value(QStringLiteral("Index")).toInt();
          if (s.value(QStringLiteral("IsDefault")).toBool()) break;
        }
      }
    }
    if (chosen < 0 && source.contains(QStringLiteral("DefaultAudioStreamIndex"))) {
      chosen = source.value(QStringLiteral("DefaultAudioStreamIndex")).toInt(-1);
    }
    if (chosen < 0) {
      for (const QJsonValue& value : streams) {
        if (Str(value.toObject(), "Type") == QLatin1String("Audio")) {
          chosen = value.toObject().value(QStringLiteral("Index")).toInt();
          break;
        }
      }
    }
    audio_index_ = chosen;
  }

  if (subtitle_index_ == -2) {
    const QString mode = settings->subtitleMode();
    const QString language = settings->subtitleLanguage();
    int chosen = -1;
    auto find = [&](auto predicate) {
      for (const QJsonValue& value : streams) {
        const QJsonObject s = value.toObject();
        if (Str(s, "Type") == QLatin1String("Subtitle") && predicate(s)) return s.value(QStringLiteral("Index")).toInt();
      }
      return -1;
    };
    if (mode == QLatin1String("off")) {
      chosen = -1;
    } else if (mode == QLatin1String("forced")) {
      chosen = find([&](const QJsonObject& s) {
        return s.value(QStringLiteral("IsForced")).toBool() && (language.isEmpty() || Str(s, "Language") == language);
      });
    } else if (mode == QLatin1String("always")) {
      chosen = language.isEmpty() ? -1 : find([&](const QJsonObject& s) { return Str(s, "Language") == language; });
      if (chosen < 0) chosen = source.value(QStringLiteral("DefaultSubtitleStreamIndex")).toInt(-1);
      if (chosen < 0) chosen = find([](const QJsonObject&) { return true; });
    } else {
      chosen = source.value(QStringLiteral("DefaultSubtitleStreamIndex")).toInt(-1);
    }
    subtitle_index_ = chosen;
  }
  emit tracksChanged();
}

void Playback::loadStream(double start_seconds) {
  if (!video_) return;
  file_loaded_ = false;
  mpv_tracks_.clear();
  QStringList command{QStringLiteral("loadfile"), stream_.url, QStringLiteral("replace"), QStringLiteral("-1")};
  QStringList options;
  if (start_seconds > 0) options << QStringLiteral("start=%1").arg(start_seconds, 0, 'f', 3);
  options << QStringLiteral("force-media-title=%1").arg(QString(title()).remove(QLatin1Char(',')));
  command << options.join(QLatin1Char(','));
  video_->setOption(QStringLiteral("pause"), false);
  video_->command(command);
  session_open_ = true;
}

QJsonObject Playback::streamByIndex(int index) const {
  for (const QJsonValue& value : stream_.source.value(QStringLiteral("MediaStreams")).toArray()) {
    if (value.toObject().value(QStringLiteral("Index")).toInt(-100) == index) return value.toObject();
  }
  return {};
}

int Playback::mpvTrackId(const QString& type, int jellyfin_index) const {
  const QJsonObject stream = streamByIndex(jellyfin_index);
  const bool external = stream.value(QStringLiteral("IsExternal")).toBool() ||
                        Str(stream, "DeliveryMethod") == QLatin1String("External");
  const QString marker = QStringLiteral("/Subtitles/%1/").arg(jellyfin_index);
  for (const QVariant& value : mpv_tracks_) {
    const QVariantMap track = value.toMap();
    if (track.value(QStringLiteral("type")).toString() != type) continue;
    if (track.value(QStringLiteral("external")).toBool()) {
      if (external && track.value(QStringLiteral("external-filename")).toString().contains(marker)) {
        return track.value(QStringLiteral("id")).toInt();
      }
    } else if (!external && stream_.method == QLatin1String("DirectPlay") &&
               track.value(QStringLiteral("ff-index")).toInt() == jellyfin_index) {
      return track.value(QStringLiteral("id")).toInt();
    }
  }
  return -1;
}

void Playback::applyTrackSelection() {
  if (!video_ || !file_loaded_) return;
  ApiClient* api = Session::instance()->api();
  if (stream_.method == QLatin1String("DirectPlay")) {
    const int aid = mpvTrackId(QStringLiteral("audio"), audio_index_);
    video_->setOption(QStringLiteral("aid"), aid > 0 ? QString::number(aid) : QStringLiteral("auto"));
    qCInfo(lcPlayback, "audio stream %d -> mpv aid %d", audio_index_, aid);
  }
  qCInfo(lcPlayback, "subtitle stream %d -> mpv sid %d", subtitle_index_,
         subtitle_index_ >= 0 ? mpvTrackId(QStringLiteral("sub"), subtitle_index_) : -1);
  if (subtitle_index_ < 0) {
    video_->setOption(QStringLiteral("sid"), QStringLiteral("no"));
    return;
  }
  const QJsonObject stream = streamByIndex(subtitle_index_);
  if (Str(stream, "DeliveryMethod") == QLatin1String("Encode")) {
    // Burned into the video by the server.
    video_->setOption(QStringLiteral("sid"), QStringLiteral("no"));
    return;
  }
  const int sid = mpvTrackId(QStringLiteral("sub"), subtitle_index_);
  if (sid > 0) {
    video_->setOption(QStringLiteral("sid"), QString::number(sid));
    return;
  }
  const QString delivery = Str(stream, "DeliveryUrl");
  if (!delivery.isEmpty()) {
    video_->command({QStringLiteral("sub-add"), AbsoluteUrl(api, delivery), QStringLiteral("select"),
                     Str(stream, "DisplayTitle"), Str(stream, "Language")});
  }
}

QVariantList Playback::Tracks(const QString& type, int selected) const {
  QVariantList result;
  for (const QJsonValue& value : stream_.source.value(QStringLiteral("MediaStreams")).toArray()) {
    const QJsonObject s = value.toObject();
    if (Str(s, "Type") != type) continue;
    const int index = s.value(QStringLiteral("Index")).toInt();
    QString label = Str(s, "DisplayTitle");
    if (label.isEmpty()) label = Str(s, "Title");
    if (label.isEmpty()) label = Str(s, "Language");
    if (label.isEmpty()) label = tr("Track %1").arg(index);
    result.append(QVariantMap{{QStringLiteral("index"), index},
                              {QStringLiteral("title"), label},
                              {QStringLiteral("language"), Str(s, "Language")},
                              {QStringLiteral("codec"), Str(s, "Codec")},
                              {QStringLiteral("isDefault"), s.value(QStringLiteral("IsDefault")).toBool()},
                              {QStringLiteral("isForced"), s.value(QStringLiteral("IsForced")).toBool()},
                              {QStringLiteral("isExternal"), s.value(QStringLiteral("IsExternal")).toBool()},
                              {QStringLiteral("selected"), index == selected}});
  }
  return result;
}

void Playback::selectAudio(int index) {
  if (index == audio_index_) return;
  audio_index_ = index;
  emit tracksChanged();
  if (stream_.method == QLatin1String("DirectPlay")) {
    applyTrackSelection();
    if (started_reported_) report(QStringLiteral("Progress"));
    return;
  }
  // The server encodes one audio track; ask again with the new one.
  const quint64 generation = ++generation_;
  const double resume_at = position_;
  [](Playback* self, double at, quint64 gen) -> QCoro::Task<> {
    QPointer<Playback> guard(self);
    if (!co_await self->negotiate(at, gen) || !guard || gen != self->generation_) co_return;
    self->loadStream(at);
  }(this, resume_at, generation);
}

void Playback::selectSubtitle(int index) {
  if (index == subtitle_index_) return;
  const bool was_burned = Str(streamByIndex(subtitle_index_), "DeliveryMethod") == QLatin1String("Encode");
  const QJsonObject next = streamByIndex(index);
  const bool burn = !next.isEmpty() && Str(next, "DeliveryMethod") == QLatin1String("Encode");
  const bool needs_server = stream_.method != QLatin1String("DirectPlay") && (was_burned || burn ||
                            (!next.isEmpty() && Str(next, "DeliveryUrl").isEmpty() && mpvTrackId(QStringLiteral("sub"), index) < 0));
  subtitle_index_ = index;
  emit tracksChanged();
  if (!needs_server) {
    applyTrackSelection();
    if (started_reported_) report(QStringLiteral("Progress"));
    return;
  }
  const quint64 generation = ++generation_;
  const double resume_at = position_;
  [](Playback* self, double at, quint64 gen) -> QCoro::Task<> {
    QPointer<Playback> guard(self);
    if (!co_await self->negotiate(at, gen) || !guard || gen != self->generation_) co_return;
    self->loadStream(at);
  }(this, resume_at, generation);
}

void Playback::togglePause() { setPaused(!paused_); }

void Playback::setPaused(bool paused) {
  if (video_ && state_ == State::Playing) video_->setOption(QStringLiteral("pause"), paused);
}

void Playback::seekRelative(double seconds) {
  if (video_ && state_ == State::Playing) video_->command({QStringLiteral("seek"), QString::number(seconds), QStringLiteral("relative")});
}

void Playback::seekTo(double seconds) {
  if (video_ && state_ == State::Playing) {
    video_->command({QStringLiteral("seek"), QString::number(std::max(0.0, seconds), 'f', 3), QStringLiteral("absolute")});
  }
}

void Playback::stop() {
  ++generation_;
  if (video_) video_->command({QStringLiteral("stop")});
  endSession(false);
}

void Playback::endSession(bool completed) {
  progress_timer_.stop();
  if (session_open_) {
    session_open_ = false;
    report(QStringLiteral("Stopped"));
  }
  setState(completed ? State::Ended : State::Idle);
  emit finished(completed);
}

void Playback::playNext() {
  const QString id = next_item_.value(QStringLiteral("id")).toString();
  if (id.isEmpty()) return;
  mark_previous_played_ = true;
  if (shuffling()) {
    if (!shuffle_queue_.isEmpty() && shuffle_queue_.first() == id) shuffle_queue_.removeFirst();
    playTask(id, true, -1, ++generation_);
    return;
  }
  play(id, next_item_.value(QStringLiteral("played")).toBool());
}

QJsonObject Playback::progressBody() const {
  QJsonObject body{{QStringLiteral("ItemId"), item_.value(QStringLiteral("id")).toString()},
                   {QStringLiteral("MediaSourceId"), stream_.media_source_id},
                   {QStringLiteral("PositionTicks"), Ticks(position_)},
                   {QStringLiteral("IsPaused"), paused_},
                   {QStringLiteral("IsMuted"), false},
                   {QStringLiteral("CanSeek"), true},
                   {QStringLiteral("PlayMethod"), stream_.method},
                   {QStringLiteral("AudioStreamIndex"), audio_index_},
                   {QStringLiteral("SubtitleStreamIndex"), subtitle_index_},
                   {QStringLiteral("RepeatMode"), QStringLiteral("RepeatNone")}};
  if (!stream_.play_session_id.isEmpty()) body.insert(QStringLiteral("PlaySessionId"), stream_.play_session_id);
  return body;
}

QCoro::Task<> Playback::report(QString what) {
  Session* session = Session::instance();
  if (session == nullptr || item_.isEmpty()) co_return;
  QString path = QStringLiteral("/Sessions/Playing");
  if (what != QLatin1String("Started")) path += QLatin1Char('/') + what;
  const QJsonObject body = progressBody();
  qCDebug(lcPlayback).noquote() << "report" << what << "at" << position_;
  const Reply reply = co_await session->api()->post(path, QJsonDocument(body));
  if (!reply.ok()) qCInfo(lcPlayback).noquote() << "progress report" << what << "failed:" << reply.error;
}

QCoro::Task<> Playback::loadSegments(QString item_id, quint64 generation) {
  QPointer<Playback> self(this);
  Session* session = Session::instance();
  QUrlQuery query;
  query.addQueryItem(QStringLiteral("includeSegmentTypes"), QStringLiteral("Intro"));
  query.addQueryItem(QStringLiteral("includeSegmentTypes"), QStringLiteral("Outro"));
  const Reply reply = co_await session->api()->get(QStringLiteral("/MediaSegments/%1").arg(item_id), query);
  if (!self || generation != generation_) co_return;
  for (const QJsonValue& value : reply.object().value(QStringLiteral("Items")).toArray()) {
    const QJsonObject segment = value.toObject();
    const double start = Seconds(segment.value(QStringLiteral("StartTicks")).toInteger());
    const double end = Seconds(segment.value(QStringLiteral("EndTicks")).toInteger());
    if (Str(segment, "Type") == QLatin1String("Intro") && intro_start_ < 0) {
      intro_start_ = start;
      intro_end_ = end;
    } else if (Str(segment, "Type") == QLatin1String("Outro") && credits_start_ < 0) {
      credits_start_ = start;
    }
  }
  // Without segments, chapter names are the next best hint.
  static const QRegularExpression intro_name(QStringLiteral("^(intro|opening|opening credits|cold open)$"),
                                             QRegularExpression::CaseInsensitiveOption);
  static const QRegularExpression credits_name(QStringLiteral("^(credits|end credits|closing credits|outro|ending)$"),
                                               QRegularExpression::CaseInsensitiveOption);
  for (int i = 0; i < chapters_.size(); ++i) {
    const QVariantMap chapter = chapters_.at(i).toMap();
    const QString name = chapter.value(QStringLiteral("title")).toString().trimmed();
    const double start = chapter.value(QStringLiteral("start")).toDouble();
    if (intro_start_ < 0 && intro_name.match(name).hasMatch() && i + 1 < chapters_.size()) {
      const double end = chapters_.at(i + 1).toMap().value(QStringLiteral("start")).toDouble();
      if (end - start <= 180) {
        intro_start_ = start;
        intro_end_ = end;
      }
    }
    if (credits_start_ < 0 && credits_name.match(name).hasMatch()) credits_start_ = start;
  }
  emit segmentsChanged();
}

QCoro::Task<> Playback::loadNextItem(QJsonObject item, quint64 generation) {
  QPointer<Playback> self(this);
  Session* session = Session::instance();
  if (shuffling()) {
    // Once every episode has played, shuffle them all again.
    if (shuffle_queue_.isEmpty() && shuffle_aired_.size() > 1) {
      shuffle_queue_ = ShuffleOrder(shuffle_aired_, Str(item, "Id"), *QRandomGenerator::global());
    }
    if (shuffle_queue_.isEmpty()) co_return;
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("userId"), session->userId());
    query.addQueryItem(QStringLiteral("Ids"), shuffle_queue_.first());
    query.addQueryItem(QStringLiteral("Fields"), jellyfin::ListFields());
    const Reply reply = co_await session->api()->get(QStringLiteral("/Items"), query);
    if (!self || generation != generation_ || !reply.ok()) co_return;
    const QJsonArray items = reply.object().value(QStringLiteral("Items")).toArray();
    if (items.isEmpty()) co_return;
    next_item_ = jellyfin::ItemToVariant(items.first().toObject(), session->api());
    emit nextItemChanged();
    co_return;
  }
  if (Str(item, "Type") != QLatin1String("Episode") || Str(item, "SeriesId").isEmpty()) co_return;
  QUrlQuery query;
  query.addQueryItem(QStringLiteral("userId"), session->userId());
  query.addQueryItem(QStringLiteral("StartItemId"), Str(item, "Id"));
  query.addQueryItem(QStringLiteral("Limit"), QStringLiteral("2"));
  query.addQueryItem(QStringLiteral("IsMissing"), QStringLiteral("false"));
  query.addQueryItem(QStringLiteral("Fields"), jellyfin::ListFields());
  const Reply reply = co_await session->api()->get(QStringLiteral("/Shows/%1/Episodes").arg(Str(item, "SeriesId")), query);
  if (!self || generation != generation_ || !reply.ok()) co_return;
  const QJsonArray items = reply.object().value(QStringLiteral("Items")).toArray();
  if (items.size() < 2) co_return;
  next_item_ = jellyfin::ItemToVariant(items.at(1).toObject(), session->api());
  emit nextItemChanged();
}

void Playback::onMpvEvent(const QString& name, const QVariantMap& data) {
  if (name == QLatin1String("file-loaded")) {
    file_loaded_ = true;
    applyTrackSelection();
  } else if (name == QLatin1String("playback-restart")) {
    if (state_ == State::Loading) {
      setState(State::Playing);
      if (!video_shown_) {
        video_shown_ = true;
        emit videoShownChanged();
      }
    }
    if (!started_reported_ && session_open_) {
      started_reported_ = true;
      report(QStringLiteral("Started"));
      progress_timer_.start();
    } else if (started_reported_) {
      report(QStringLiteral("Progress"));
      emit seeked();
    }
  } else if (name == QLatin1String("end-file")) {
    const int reason = data.value(QStringLiteral("reason")).toInt();
    if (reason == MPV_END_FILE_REASON_ERROR && state_ == State::Loading) {
      setError(tr("Playback failed: %1").arg(data.value(QStringLiteral("message")).toString()));
      session_open_ = false;
      progress_timer_.stop();
      setState(State::Failed);
    }
  }
}

void Playback::onMpvProperty(const QString& name, const QVariant& value) {
  if (name == QLatin1String("time-pos")) {
    if (!value.isValid()) return;
    position_ = value.toDouble();
    emit positionChanged();
  } else if (name == QLatin1String("duration")) {
    duration_ = value.isValid() ? value.toDouble() : 0;
    emit durationChanged();
  } else if (name == QLatin1String("pause")) {
    const bool paused = value.toBool();
    if (paused == paused_) return;
    paused_ = paused;
    emit pausedChanged();
    if (started_reported_ && session_open_) report(QStringLiteral("Progress"));
  } else if (name == QLatin1String("paused-for-cache")) {
    buffering_ = value.toBool();
    emit bufferingChanged();
  } else if (name == QLatin1String("track-list")) {
    mpv_tracks_ = value.toList();
  } else if (name == QLatin1String("eof-reached")) {
    // keep-open holds the last frame instead of ending the file.
    if (value.toBool() && state_ == State::Playing && session_open_) {
      position_ = std::max(position_, duration_);
      endSession(true);
    }
  }
}

}  // namespace ember
