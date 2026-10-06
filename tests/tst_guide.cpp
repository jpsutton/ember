// SPDX-License-Identifier: GPL-3.0-only

#include <QDateTime>
#include <QTest>

#include "livetv/GuideLogic.h"
#include "livetv/LiveGuide.h"

using namespace ember::livetv;

// The cases of couchbox-iptv's test/guide_test.dart.
class TestGuide : public QObject {
  Q_OBJECT

 private:
  static qint64 t(int h, int m = 0) { return QDateTime(QDate(2026, 10, 4), QTime(h, m)).toSecsSinceEpoch(); }

  // 20:00-21:00 a, 21:00-21:30 b, gap 21:30-22:30, 22:30-23:00 c
  const QList<Programme> list_ = {{t(20), t(21)}, {t(21), t(21, 30)}, {t(22, 30), t(23)}};
  const qint64 earliest_ = t(20, 10);
  const qint64 latest_ = t(23, 59);

  qint64 right(qint64 focus) const { return StepFocus(list_, focus, 1, earliest_, latest_); }
  qint64 left(qint64 focus) const { return StepFocus(list_, focus, -1, earliest_, latest_); }

 private slots:
  void floorToSlot() {
    QCOMPARE(FloorToSlot(t(20, 29)), t(20));
    QCOMPARE(FloorToSlot(t(20, 30)), t(20, 30));
  }

  void programmeAt() {
    QCOMPARE(ProgrammeAt(list_, t(20, 59)), 0);
    QCOMPARE(ProgrammeAt(list_, t(21)), 1);
    QCOMPARE(ProgrammeAt(list_, t(22)), -1);
    QCOMPARE(ProgrammeAt({}, t(22)), -1);
  }

  void rightWalksProgrammesThenHalfHours() {
    QCOMPARE(right(t(20, 10)), t(21));
    QCOMPARE(right(t(21)), t(21, 30));
    QCOMPARE(right(t(21, 30)), t(22));
    QCOMPARE(right(t(22)), t(22, 30));  // the next programme comes first
    QCOMPARE(right(t(22, 30)), t(23));
  }

  void leftWalksBackNeverBeforeNow() {
    QCOMPARE(left(t(22, 30)), t(22));
    QCOMPARE(left(t(22)), t(21, 30));
    QCOMPARE(left(t(21, 30)), t(21));
    QCOMPARE(left(t(21)), earliest_);  // a started before now
  }

  void noGuideHalfHourSteps() {
    QCOMPARE(StepFocus({}, t(21), 1, earliest_, latest_), t(21, 30));
    QCOMPARE(StepFocus({}, t(21), -1, earliest_, latest_), t(20, 30));
  }

  void channelNumbers() {
    QStringList numbers = {"4.10", "13.1", "4.2", "News", "4.1", "7"};
    std::sort(numbers.begin(), numbers.end(), ChannelNumberLess);
    QCOMPARE(numbers, (QStringList{"4.1", "4.2", "4.10", "7", "13.1", "News"}));
  }

  void windowKeepsFocusVisible() {
    const qint64 span = 3 * 3600;
    QCOMPARE(WindowFor(t(21), t(20), span, t(20, 10)), t(20));
    QCOMPARE(WindowFor(t(23, 15), t(20), span, t(20, 10)), t(21));  // one more slot after the focus
    QCOMPARE(WindowFor(t(20, 10), t(21, 30), span, t(20, 10)), t(20));
  }
};

QTEST_MAIN(TestGuide)
#include "tst_guide.moc"
