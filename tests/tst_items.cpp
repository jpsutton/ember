// SPDX-License-Identifier: GPL-3.0-only

#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QTest>
#include <QUrlQuery>

#include "jellyfin/ApiClient.h"
#include "jellyfin/Items.h"

using ember::jellyfin::ApiClient;

// The fixtures are real BaseItemDto responses from a Jellyfin 12.2 server
// running scripts/dev-server.sh.
class TestItems : public QObject {
  Q_OBJECT

 private:
  static QJsonObject Fixture(const char* name) {
    QFile file(QStringLiteral(EMBER_TEST_DATA "/%1.json").arg(QLatin1String(name)));
    if (!file.open(QIODevice::ReadOnly)) qFatal("missing fixture %s", name);
    return QJsonDocument::fromJson(file.readAll()).object();
  }

  QNetworkAccessManager network_;
  ApiClient api_{&network_, QStringLiteral("device-1")};

 private slots:
  void initTestCase() {
    api_.setBaseUrl(QUrl(QStringLiteral("http://media.example:8096/jellyfin/")));
    api_.setToken(QStringLiteral("secret"));
  }

  void movie() {
    const QJsonObject json = Fixture("movie");
    const QVariantMap m = ember::jellyfin::ItemToVariant(json, &api_);
    QCOMPARE(m.value("name").toString(), QStringLiteral("Night of the Living Dead"));
    QCOMPARE(m.value("type").toString(), QStringLiteral("Movie"));
    QCOMPARE(m.value("year").toInt(), 1968);
    QVERIFY(m.value("playable").toBool());
    QCOMPARE(m.value("videoFlags").toString(), QStringLiteral("720p · H.264"));
    QCOMPARE(m.value("audioFlags").toString(), QStringLiteral("Dolby Digital · 5.1"));
    QVERIFY(m.value("hasSubtitles").toBool());
    QCOMPARE(m.value("runtimeText").toString(), QStringLiteral("4 min"));

    const QJsonObject user = json.value("UserData").toObject();
    const QString expected = user.value("Played").toBool() ? QStringLiteral("watched")
                             : user.value("PlaybackPositionTicks").toInteger() > 0 ? QStringLiteral("inProgress")
                                                                                    : QStringLiteral("unwatched");
    QCOMPARE(m.value("status").toString(), expected);

    // Images: JPEG for photos, PNG for logos, under the server's base path.
    const QUrl poster(m.value("poster").toString());
    QCOMPARE(poster.path(), QStringLiteral("/jellyfin/Items/%1/Images/Primary").arg(json.value("Id").toString()));
    QCOMPARE(QUrlQuery(poster).queryItemValue("format"), QStringLiteral("Jpg"));
    QCOMPARE(QUrlQuery(QUrl(m.value("logo").toString())).queryItemValue("format"), QStringLiteral("Png"));
    QVERIFY(!m.value("backdrop").toString().isEmpty());
  }

  void episode() {
    const QJsonObject json = Fixture("episode");
    const QVariantMap m = ember::jellyfin::ItemToVariant(json, &api_);
    QCOMPARE(m.value("episodeLabel").toString(), QStringLiteral("S01E03"));
    QCOMPARE(m.value("seriesName").toString(), QStringLiteral("The Twilight Zone"));
    QCOMPARE(m.value("status").toString(), QStringLiteral("inProgress"));
    QVERIFY(!m.value("resumeText").toString().isEmpty());
    // No season poster in the fixture: the pane falls back to the show's.
    QVERIFY(m.value("poster").toString().contains(json.value("SeriesId").toString()));
    // The episode still is kept as the thumbnail.
    QVERIFY(m.value("thumb").toString().contains(json.value("Id").toString()));
  }

  void folders() {
    const QVariantMap season = ember::jellyfin::ItemToVariant(Fixture("season"), &api_);
    QCOMPARE(season.value("type").toString(), QStringLiteral("Season"));
    QVERIFY(!season.value("playable").toBool());
    QCOMPARE(season.value("status").toString(), QString());

    const QVariantMap series = ember::jellyfin::ItemToVariant(Fixture("series"), &api_);
    QCOMPARE(series.value("type").toString(), QStringLiteral("Series"));
    QVERIFY(!series.value("playable").toBool());
    QVERIFY(series.value("recursiveCount").toInt() > 0);
  }

  void runtime_data() {
    QTest::addColumn<qint64>("ticks");
    QTest::addColumn<QString>("text");
    QTest::newRow("none") << qint64(0) << QString();
    QTest::newRow("under half a minute") << qint64(290'000'000) << QStringLiteral("< 1 min");
    QTest::newRow("1 min") << qint64(600'000'000) << QStringLiteral("1 min");
    QTest::newRow("hour") << qint64(36'000'000'000) << QStringLiteral("1 h 0 min");
    QTest::newRow("feature") << qint64(57'000'000'000) << QStringLiteral("1 h 35 min");
  }

  void runtime() {
    QFETCH(qint64, ticks);
    QFETCH(QString, text);
    QCOMPARE(ember::jellyfin::FormatRuntime(ticks), text);
  }

  void airedDate_data() {
    QTest::addColumn<QString>("premiere");
    QTest::addColumn<QString>("created");
    QTest::addColumn<QString>("aired");
    const QString may = QStringLiteral("2026-05-01T00:00:00.0000000Z");
    const QString june = QStringLiteral("2026-06-01T00:00:00.0000000Z");
    QTest::newRow("added after airing") << may << june << may;
    // Released early on streaming: the episode was there before its air date.
    QTest::newRow("added before airing") << june << may << may;
    QTest::newRow("no premiere date") << QString() << june << june;
    QTest::newRow("no added date") << may << QString() << may;
    QTest::newRow("neither") << QString() << QString() << QString();
  }

  void airedDate() {
    QFETCH(QString, premiere);
    QFETCH(QString, created);
    QFETCH(QString, aired);
    QJsonObject item;
    if (!premiere.isEmpty()) item.insert(QStringLiteral("PremiereDate"), premiere);
    if (!created.isEmpty()) item.insert(QStringLiteral("DateCreated"), created);
    QCOMPARE(ember::jellyfin::AiredDate(item), QDateTime::fromString(aired, Qt::ISODate));
  }

  void authorization() {
    const QString header = QString::fromUtf8(api_.authorization());
    QVERIFY(header.startsWith(QStringLiteral("MediaBrowser Client=\"Ember\"")));
    QVERIFY(header.contains(QStringLiteral("DeviceId=\"device-1\"")));
    QVERIFY(header.contains(QStringLiteral("Token=\"secret\"")));
  }

  void urls() {
    QCOMPARE(api_.url(QStringLiteral("/Items")).toString(), QStringLiteral("http://media.example:8096/jellyfin/Items"));
    // Jellyfin 12 accepts ApiKey, never api_key.
    const QUrl stream = api_.authenticatedUrl(QStringLiteral("/Videos/x/stream"));
    QCOMPARE(QUrlQuery(stream).queryItemValue("ApiKey"), QStringLiteral("secret"));
    QVERIFY(!QUrlQuery(stream).hasQueryItem("api_key"));
  }
};

QTEST_MAIN(TestItems)
#include "tst_items.moc"
