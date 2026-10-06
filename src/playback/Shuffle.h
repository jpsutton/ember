// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <QRandomGenerator>
#include <QStringList>

namespace ember {

// A random order of |aired| (episode IDs in airing order) in which no
// episode is directly followed by the one that aired after it, so a shuffle
// never plays two in sequence. |previous| is the episode just played: the
// order doesn't start with it or with the one after it. When no order avoids
// every such pair (two or three episodes), the one with the fewest wins.
QStringList ShuffleOrder(const QStringList& aired, const QString& previous, QRandomGenerator& random);

}  // namespace ember
