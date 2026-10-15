// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QString>

namespace ember {

// A language preference is an ISO 639-2 code with an optional region:
// "eng-US", "eng-GB", "spa". Empty means no preference.
//
// Jellyfin gives a stream's language as an ISO 639-2 code, bibliographic or
// terminology ("fre" or "fra"), and never a region; a region shows up, if
// anywhere, in the track's title ("English (US)", "American English",
// "en-GB"). So a region only ranks tracks of the right language.
//
// How well |stream| matches |preference|: -1 for another language (or no
// preference), 0 when its title names a different region, 1 when it names
// none, 2 when it names the preferred one.
int LanguageScore(const QJsonObject& stream, const QString& preference);

// The audio stream to start on: the best LanguageScore match for |language|,
// non-commentary first, then the file's default; else |server_default| if it
// is >= 0; else the first audio stream. -1 when there is none.
int PickAudioStream(const QJsonArray& streams, const QString& language, int server_default);

// The subtitle stream to start on, or -1 for none, by |mode|:
//   server  |server_default| (the server applies the user's own settings),
//           swapped for the best |language| stream that is just as forced
//           (or not), when there is one
//   off     none
//   forced  the best forced stream for |language| (any forced one when
//           |language| is empty)
//   always  the best stream for |language|, unforced and not for the hearing
//           impaired first; else |server_default|; else the first one
int PickSubtitleStream(const QJsonArray& streams, const QString& language, const QString& mode,
                       int server_default);

}  // namespace ember
