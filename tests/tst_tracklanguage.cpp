// SPDX-License-Identifier: GPL-3.0-only

#include <QJsonArray>
#include <QJsonObject>
#include <QTest>

#include "playback/TrackLanguage.h"

using ember::LanguageScore;
using ember::PickAudioStream;
using ember::PickSubtitleStream;

namespace {

// A MediaStream as Jellyfin reports it, with only the fields the picker reads.
QJsonObject Stream(int index, const char* type, const char* language, const char* title = "",
                   bool is_default = false, bool forced = false, bool hearing_impaired = false) {
  return QJsonObject{{QStringLiteral("Index"), index},
                     {QStringLiteral("Type"), QLatin1String(type)},
                     {QStringLiteral("Language"), QLatin1String(language)},
                     {QStringLiteral("Title"), QLatin1String(title)},
                     {QStringLiteral("IsDefault"), is_default},
                     {QStringLiteral("IsForced"), forced},
                     {QStringLiteral("IsHearingImpaired"), hearing_impaired}};
}

}  // namespace

class TestTrackLanguage : public QObject {
  Q_OBJECT

 private slots:
  void score() {
    const QString us = QStringLiteral("eng-US");
    QCOMPARE(LanguageScore(Stream(1, "Audio", "eng", "English (US)"), us), 2);
    QCOMPARE(LanguageScore(Stream(1, "Audio", "eng", "American English"), us), 2);
    QCOMPARE(LanguageScore(Stream(1, "Audio", "eng", "en-US"), us), 2);
    QCOMPARE(LanguageScore(Stream(1, "Audio", "eng", "English"), us), 1);
    QCOMPARE(LanguageScore(Stream(1, "Audio", "eng", "Stereo - Default"), us), 1);
    QCOMPARE(LanguageScore(Stream(1, "Audio", "eng", "English (UK)"), us), 0);
    QCOMPARE(LanguageScore(Stream(1, "Audio", "eng", "Commentary with us"), us), 1);  // not "US"
    QCOMPARE(LanguageScore(Stream(1, "Audio", "spa", "Spanish"), us), -1);
    QCOMPARE(LanguageScore(Stream(1, "Audio", "eng", "English"), QString()), -1);
    QCOMPARE(LanguageScore(Stream(1, "Audio", "fra", ""), QStringLiteral("fre")), 1);
    QCOMPARE(LanguageScore(Stream(1, "Audio", "en", ""), QStringLiteral("eng")), 1);
  }

  void audio() {
    const QString us = QStringLiteral("eng-US");
    // An English dub beats the Japanese default.
    QCOMPARE(PickAudioStream({Stream(1, "Audio", "jpn", "", true), Stream(2, "Audio", "eng")}, us, 1), 2);
    // US over UK, wherever the default is.
    QCOMPARE(PickAudioStream({Stream(1, "Audio", "eng", "English (UK)", true), Stream(2, "Audio", "eng", "English (US)")},
                             us, 1),
             2);
    // Among equals, the file's default.
    QCOMPARE(PickAudioStream({Stream(1, "Audio", "eng"), Stream(2, "Audio", "eng", "", true)}, us, -1), 2);
    // Commentary only when nothing else matches.
    QCOMPARE(PickAudioStream({Stream(1, "Audio", "eng", "Director's Commentary (US)"), Stream(2, "Audio", "eng")}, us, -1), 2);
    // No English: the server's choice, else the first audio stream.
    QCOMPARE(PickAudioStream({Stream(0, "Video", ""), Stream(1, "Audio", "fre"), Stream(2, "Audio", "ger")}, us, 2), 2);
    QCOMPARE(PickAudioStream({Stream(0, "Video", ""), Stream(1, "Audio", "fre"), Stream(2, "Audio", "ger")}, us, -1), 1);
    // No preference follows the server.
    QCOMPARE(PickAudioStream({Stream(1, "Audio", "eng"), Stream(2, "Audio", "jpn")}, QString(), 2), 2);
    QCOMPARE(PickAudioStream({Stream(0, "Video", "")}, us, -1), -1);
  }

  void subtitles() {
    const QString us = QStringLiteral("eng-US");
    const QJsonArray streams{Stream(1, "Audio", "eng"),
                             Stream(2, "Subtitle", "spa"),
                             Stream(3, "Subtitle", "eng", "English (Forced)", false, true),
                             Stream(4, "Subtitle", "eng", "English SDH", false, false, true),
                             Stream(5, "Subtitle", "eng", "English"),
                             Stream(6, "Subtitle", "spa", "Spanish (Forced)", false, true)};
    QCOMPARE(PickSubtitleStream(streams, us, QStringLiteral("off"), 2), -1);
    QCOMPARE(PickSubtitleStream(streams, us, QStringLiteral("forced"), -1), 3);
    QCOMPARE(PickSubtitleStream(streams, QString(), QStringLiteral("forced"), -1), 3);
    QCOMPARE(PickSubtitleStream(streams, QStringLiteral("jpn"), QStringLiteral("forced"), -1), -1);
    // Always: plain English over SDH and forced.
    QCOMPARE(PickSubtitleStream(streams, us, QStringLiteral("always"), 2), 5);
    QCOMPARE(PickSubtitleStream(streams, QStringLiteral("jpn"), QStringLiteral("always"), 2), 2);
    QCOMPARE(PickSubtitleStream(streams, QStringLiteral("jpn"), QStringLiteral("always"), -1), 2);
    // Server: it says whether, we say which language.
    QCOMPARE(PickSubtitleStream(streams, us, QStringLiteral("server"), -1), -1);
    QCOMPARE(PickSubtitleStream(streams, us, QStringLiteral("server"), 2), 5);
    QCOMPARE(PickSubtitleStream(streams, us, QStringLiteral("server"), 6), 3);
    QCOMPARE(PickSubtitleStream(streams, us, QStringLiteral("server"), 4), 5);
    QCOMPARE(PickSubtitleStream(streams, QStringLiteral("jpn"), QStringLiteral("server"), 6), 6);
    QCOMPARE(PickSubtitleStream(streams, QString(), QStringLiteral("server"), 2), 2);
  }
};

QTEST_GUILESS_MAIN(TestTrackLanguage)
#include "tst_tracklanguage.moc"
