// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <QList>
#include <QtGlobal>

// The time and focus rules of the Live TV guide, ported from couchbox-iptv's
// guide_logic.dart. Times are seconds since the epoch; the grid's columns
// are half hours of local time.
namespace ember::livetv {

constexpr qint64 kSlot = 30 * 60;

struct Programme {
  qint64 start = 0;
  qint64 stop = 0;
};

// |t| rounded down to its half hour (local time).
qint64 FloorToSlot(qint64 t);

// The index of the programme on at |t| in a start-ordered list, or -1.
qsizetype ProgrammeAt(const QList<Programme>& programmes, qint64 t);

// Where the focus goes on Left (|direction| -1) or Right (+1) from |focus|:
// the start of the neighbouring programme, or half an hour over where there
// is none. Never before |earliest| (now: past programmes can't be watched)
// nor after |latest|.
qint64 StepFocus(const QList<Programme>& programmes, qint64 focus, int direction, qint64 earliest, qint64 latest);

// The first slot of the window that keeps |focus| visible, given the current
// |window_start| and |span|. Moves by whole slots; scrolling forward leaves
// one more slot visible after the focus. Never before |earliest|'s slot.
qint64 WindowFor(qint64 focus, qint64 window_start, qint64 span, qint64 earliest);

}  // namespace ember::livetv
