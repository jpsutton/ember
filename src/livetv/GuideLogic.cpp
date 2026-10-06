// SPDX-License-Identifier: GPL-3.0-only

#include "GuideLogic.h"

#include <QDateTime>

namespace ember::livetv {

qint64 FloorToSlot(qint64 t) {
  // Local time, so the columns line up with the clock even where the zone
  // is not a whole number of hours from UTC.
  const QDateTime local = QDateTime::fromSecsSinceEpoch(t);
  const QTime time = local.time();
  return QDateTime(local.date(), QTime(time.hour(), time.minute() >= 30 ? 30 : 0)).toSecsSinceEpoch();
}

qsizetype ProgrammeAt(const QList<Programme>& programmes, qint64 t) {
  for (qsizetype i = 0; i < programmes.size(); ++i) {
    const Programme& p = programmes.at(i);
    if (p.start <= t && p.stop > t) return i;
    if (p.start > t) break;
  }
  return -1;
}

qint64 StepFocus(const QList<Programme>& programmes, qint64 focus, int direction, qint64 earliest, qint64 latest) {
  const qsizetype current = ProgrammeAt(programmes, focus);
  qint64 target;
  if (direction > 0) {
    target = current >= 0 ? programmes.at(current).stop : focus + kSlot;
    if (current < 0) {
      // In a gap: stop at the next programme if it comes first.
      for (const Programme& p : programmes) {
        if (p.start > focus) {
          if (p.start < target) target = p.start;
          break;
        }
      }
    }
  } else {
    target = (current >= 0 ? programmes.at(current).start : focus) - 60;
    const qsizetype landed = ProgrammeAt(programmes, target);
    if (landed >= 0) {
      target = programmes.at(landed).start;
    } else {
      // A gap: half-hour steps, but not into the programme before it.
      target = FloorToSlot(target);
      const qsizetype before = ProgrammeAt(programmes, target);
      if (before >= 0) target = programmes.at(before).stop;
    }
  }
  return qBound(earliest, target, latest);
}

qint64 WindowFor(qint64 focus, qint64 window_start, qint64 span, qint64 earliest) {
  qint64 start = window_start;
  if (focus < start) start = FloorToSlot(focus);
  if (focus >= start + span) start = FloorToSlot(focus) - span + 2 * kSlot;
  return qMax(start, FloorToSlot(earliest));
}

}  // namespace ember::livetv
