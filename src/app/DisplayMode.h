// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QObject>
#include <QtQml/qqmlregistration.h>

#include <QCoroTask>

namespace ember {

// Switches the TV to a refresh rate that fits the video's frame rate (24p
// at 24 Hz rather than 60, which judders), through kscreen-doctor, and back
// afterwards. The mode to return to is written down first, so a crash
// mid-film is put right the next time Ember starts.
class DisplayMode : public QObject {
  Q_OBJECT
  QML_ELEMENT
  QML_SINGLETON
  Q_PROPERTY(bool switched READ switched NOTIFY switchedChanged)

 public:
  explicit DisplayMode(QObject* parent = nullptr);

  bool switched() const { return !original_mode_.isEmpty(); }

  // Picks the mode of the same size whose refresh rate is a whole multiple
  // of |fps| (closest to the current rate when there are several).
  Q_INVOKABLE void matchFrameRate(double fps);
  Q_INVOKABLE void restore();

  // The refresh rate to use for |fps| among |modes| (kscreen-doctor's mode
  // objects of one size), or -1 when none fits. Public for the tests.
  static QString ChooseMode(const QJsonArray& modes, double fps, double current_rate);

 signals:
  void switchedChanged();

 private:
  QCoro::Task<> matchTask(double fps);
  QCoro::Task<> restoreTask();
  QCoro::Task<QJsonArray> outputs();
  QCoro::Task<bool> setMode(QString output, QString mode_id);
  void remember(const QString& output, const QString& mode_id);

  QString output_;
  QString original_mode_;
  bool busy_ = false;
};

}  // namespace ember
