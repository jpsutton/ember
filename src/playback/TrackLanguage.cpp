// SPDX-License-Identifier: GPL-3.0-only

#include "TrackLanguage.h"

#include <QRegularExpression>

#include <functional>

namespace ember {
namespace {

QString Str(const QJsonObject& o, const char* key) { return o.value(QLatin1String(key)).toString(); }
bool Flag(const QJsonObject& o, const char* key) { return o.value(QLatin1String(key)).toBool(); }

// ISO 639-1 and terminology codes, to their ISO 639-2 bibliographic code.
QString CanonicalLanguage(const QString& code) {
  static const QHash<QString, QString> kAliases = {
      {QStringLiteral("en"), QStringLiteral("eng")},  {QStringLiteral("es"), QStringLiteral("spa")},
      {QStringLiteral("fr"), QStringLiteral("fre")},  {QStringLiteral("fra"), QStringLiteral("fre")},
      {QStringLiteral("de"), QStringLiteral("ger")},  {QStringLiteral("deu"), QStringLiteral("ger")},
      {QStringLiteral("it"), QStringLiteral("ita")},  {QStringLiteral("pt"), QStringLiteral("por")},
      {QStringLiteral("ja"), QStringLiteral("jpn")},  {QStringLiteral("ko"), QStringLiteral("kor")},
      {QStringLiteral("zh"), QStringLiteral("chi")},  {QStringLiteral("zho"), QStringLiteral("chi")},
      {QStringLiteral("nl"), QStringLiteral("dut")},  {QStringLiteral("nld"), QStringLiteral("dut")},
      {QStringLiteral("ces"), QStringLiteral("cze")}, {QStringLiteral("ell"), QStringLiteral("gre")},
      {QStringLiteral("fas"), QStringLiteral("per")}, {QStringLiteral("ron"), QStringLiteral("rum")},
      {QStringLiteral("slk"), QStringLiteral("slo")}, {QStringLiteral("isl"), QStringLiteral("ice")},
      {QStringLiteral("msa"), QStringLiteral("may")}, {QStringLiteral("cym"), QStringLiteral("wel")},
  };
  const QString lower = code.trimmed().toLower();
  return kAliases.value(lower, lower);
}

// How a title names each region. Case-sensitive, whole words.
const QHash<QString, QRegularExpression>& RegionPatterns() {
  static const QHash<QString, QRegularExpression> kPatterns = {
      {QStringLiteral("US"), QRegularExpression(QStringLiteral(R"(\b(US|USA|American|[a-z]{2}-US)\b)"))},
      {QStringLiteral("GB"), QRegularExpression(QStringLiteral(R"(\b(UK|GB|British|[a-z]{2}-GB)\b)"))},
      {QStringLiteral("CA"), QRegularExpression(QStringLiteral(R"(\b(Canadian|[a-z]{2}-CA)\b)"))},
      {QStringLiteral("AU"), QRegularExpression(QStringLiteral(R"(\b(Australian|[a-z]{2}-AU)\b)"))},
  };
  return kPatterns;
}

// The best-ranked stream of |type|, ignoring rank < 0; -1 when none.
int Best(const QJsonArray& streams, const QString& type, const std::function<int(const QJsonObject&)>& rank) {
  int best = -1;
  int best_rank = -1;
  for (const QJsonValue& value : streams) {
    const QJsonObject s = value.toObject();
    if (Str(s, "Type") != type) continue;
    const int r = rank(s);
    if (r > best_rank) {
      best_rank = r;
      best = s.value(QStringLiteral("Index")).toInt();
    }
  }
  return best;
}

bool IsCommentary(const QJsonObject& s) {
  static const QRegularExpression kCommentary(QStringLiteral("commentary"), QRegularExpression::CaseInsensitiveOption);
  return Str(s, "Title").contains(kCommentary) || Str(s, "DisplayTitle").contains(kCommentary);
}

}  // namespace

int LanguageScore(const QJsonObject& stream, const QString& preference) {
  const int dash = preference.indexOf(QLatin1Char('-'));
  const QString language = CanonicalLanguage(dash < 0 ? preference : preference.left(dash));
  const QString region = dash < 0 ? QString() : preference.mid(dash + 1).toUpper();
  if (language.isEmpty() || CanonicalLanguage(Str(stream, "Language")) != language) return -1;
  if (region.isEmpty()) return 1;

  const QString title = Str(stream, "Title") + QLatin1Char(' ') + Str(stream, "DisplayTitle");
  const auto& patterns = RegionPatterns();
  if (patterns.contains(region) && title.contains(patterns.value(region))) return 2;
  for (auto it = patterns.cbegin(); it != patterns.cend(); ++it) {
    if (it.key() != region && title.contains(it.value())) return 0;
  }
  return 1;
}

int PickAudioStream(const QJsonArray& streams, const QString& language, int server_default) {
  const QString audio = QStringLiteral("Audio");
  if (!language.isEmpty()) {
    const int chosen = Best(streams, audio, [&](const QJsonObject& s) {
      const int score = LanguageScore(s, language);
      if (score < 0) return -1;
      return (IsCommentary(s) ? 0 : 100) + score * 10 + (Flag(s, "IsDefault") ? 1 : 0);
    });
    if (chosen >= 0) return chosen;
  }
  if (server_default >= 0) return server_default;
  return Best(streams, audio, [](const QJsonObject&) { return 0; });
}

int PickSubtitleStream(const QJsonArray& streams, const QString& language, const QString& mode,
                       int server_default) {
  const QString subtitle = QStringLiteral("Subtitle");
  if (mode == QLatin1String("off")) return -1;
  if (mode == QLatin1String("forced")) {
    return Best(streams, subtitle, [&](const QJsonObject& s) {
      if (!Flag(s, "IsForced")) return -1;
      if (language.isEmpty()) return 0;
      const int score = LanguageScore(s, language);
      return score < 0 ? -1 : score * 10 + (Flag(s, "IsDefault") ? 1 : 0);
    });
  }
  if (mode == QLatin1String("always")) {
    if (!language.isEmpty()) {
      const int chosen = Best(streams, subtitle, [&](const QJsonObject& s) {
        const int score = LanguageScore(s, language);
        if (score < 0) return -1;
        return (Flag(s, "IsForced") ? 0 : 1000) + score * 100 + (Flag(s, "IsHearingImpaired") ? 0 : 10) +
               (Flag(s, "IsDefault") ? 1 : 0);
      });
      if (chosen >= 0) return chosen;
    }
    if (server_default >= 0) return server_default;
    return Best(streams, subtitle, [](const QJsonObject&) { return 0; });
  }
  // server: it decides whether subtitles show; the language is still ours.
  if (server_default < 0 || language.isEmpty()) return server_default;
  bool forced = false;
  for (const QJsonValue& value : streams) {
    const QJsonObject s = value.toObject();
    if (Str(s, "Type") == subtitle && s.value(QStringLiteral("Index")).toInt() == server_default) {
      forced = Flag(s, "IsForced");
    }
  }
  const int chosen = Best(streams, subtitle, [&](const QJsonObject& s) {
    const int score = LanguageScore(s, language);
    if (score < 0 || Flag(s, "IsForced") != forced) return -1;
    return score * 100 + (Flag(s, "IsHearingImpaired") ? 0 : 10) +
           (s.value(QStringLiteral("Index")).toInt() == server_default ? 1 : 0);
  });
  return chosen >= 0 ? chosen : server_default;
}

}  // namespace ember
