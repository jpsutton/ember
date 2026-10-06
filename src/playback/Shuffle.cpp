// SPDX-License-Identifier: GPL-3.0-only

#include "Shuffle.h"

#include <QHash>

namespace ember {
namespace {

// Random orders tried before settling for the best one. Most orders of four
// or more episodes have no sequential pair within a few tries.
constexpr int kAttempts = 64;

}  // namespace

QStringList ShuffleOrder(const QStringList& aired, const QString& previous, QRandomGenerator& random) {
  QHash<QString, QString> next_aired;
  for (qsizetype i = 0; i + 1 < aired.size(); ++i) next_aired.insert(aired.at(i), aired.at(i + 1));

  auto clashes = [&](const QStringList& order) {
    int count = 0;
    if (!order.isEmpty() && !previous.isEmpty() &&
        (order.first() == previous || order.first() == next_aired.value(previous))) {
      ++count;
    }
    for (qsizetype i = 0; i + 1 < order.size(); ++i) {
      if (next_aired.value(order.at(i)) == order.at(i + 1)) ++count;
    }
    return count;
  };

  QStringList best;
  int best_clashes = -1;
  for (int attempt = 0; attempt < kAttempts; ++attempt) {
    QStringList order = aired;
    for (qsizetype i = order.size() - 1; i > 0; --i) order.swapItemsAt(i, random.bounded(i + 1));
    const int count = clashes(order);
    if (best_clashes < 0 || count < best_clashes) {
      best = order;
      best_clashes = count;
    }
    if (count == 0) break;
  }
  return best;
}

}  // namespace ember
