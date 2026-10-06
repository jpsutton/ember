# Ember: implementation plan

Ember is a Jellyfin client for couchbox. App ID `org.couchbox.ember`, repo
`jpsutton/ember`, package and binary `ember`. The app ID is baked into the
desktop file, `visible-apps` and the user's config paths, so settle it before
the first tag.

## Status (2026-10-06)

M0 through M4 are done and tested on the BRIX test box (Bay Trail N2807,
couchbox-base 0.9.0) against a local Jellyfin 12.2 dev server
(`scripts/dev-server.sh`), driven by injected key presses and screenshots
(`tools/remote-keys.py`, `tools/ember-shot`). M5 and M6 are done too, and
the cast target from "Later". See "Findings" at the end for what the spikes
and testing showed,
and "Not yet verified" for what still needs a real remote, a real server or a
newer couchbox.

| Milestone | State |
|---|---|
| M0 spikes | Done. Plane under Qt works (see Findings); key survey done with injected codes; Jellyfin 12 auth rules confirmed. |
| M1 skeleton | Done: CMake, CI (GitHub Actions, Arch container), unit tests, single instance, Quick Connect / password sign-in, LAN discovery, library picker. |
| M2 home | Done: blade with pinned or static highlight, submenus, random fanart per library, clock, item count. No shelves (by decision). |
| M3 lists | Done: Amber List view, paging, drill-down show → season → episodes (one-season shows skip the season list), position kept on Back. |
| M4 playback | Done: direct play and HLS transcode, resume, progress reports, audio/subtitle menus, chapters, skip intro (chapter fallback), Up Next with auto-play, MPRIS, screen-saver inhibit. couchbox packaging prepared (couchbox branch `feat/ember`, not pushed). |
| M5 Amber parity | Done: side blade (sort, order, hide watched), A–Z strip, Channel ± paging, info page with cast, context menu, genres, years, collections, search, and the server's event stream (rows update when watched state changes elsewhere; lists reload after library changes). |
| M6 polish | Done: trickplay preview on the seek bar, on-screen keyboard and search, Amber's Low, Tall, Big and Simple list styles besides List (remembered per library), home menu order and visibility, threaded render loop tried (no gain; basic kept). The GLib-free plane item is moot: the Qt port never used GLib. |
| Later | Cast target done: other Jellyfin clients can play to Ember ("Play On", with a queue), control playback, navigate (arrows, OK, Back, Home, menu) and send messages. Shelves, music and refresh-rate switching not started. |

## Goal

Browse the library the way Kodi's Amber skin does with its vertical menu: a
text menu on the left, list views with a details pane and fanart, drill-down
with Back, driven entirely by the remote. Ember replaces Kodi + jellyfin-kodi
for day-to-day watching.

### In scope

- Jellyfin only (10.10, 10.11 and 12.x servers).
- Movies, TV shows, collections (BoxSets), plain folders. The user picks which
  libraries appear at first sign-in.
- Direct play and server transcode, resume, progress reporting, audio and
  subtitle selection, intro/credits skip, next-episode auto-play.
- couchbox integration: tile, single instance, minimize/pause, remote keys,
  idle inhibit, couchboxrc codec settings.

### Out of scope (for now)

- Home shelves (Continue Watching, Next Up, Latest). The home layout leaves
  room for them.
- Plex and Emby.
- Music libraries.
- Live TV (couchbox-iptv covers it).
- Downloads, SyncPlay, multiple servers at once.
- Being remote-controlled by other Jellyfin clients ("Play On"). (Done after
  all; see Status.)

## Decisions

| Area | Choice | Why |
|---|---|---|
| UI | Qt 6 Quick (QML) | GPU scene graph, `ListView` + `QAbstractListModel` fit drill-down lists, shares Qt with Plasma Bigscreen. |
| Logic | C++20 | Jellyfin client, models, player glue. QCoro needs C++20 coroutines. |
| Async | QCoro (`co_await` on `QNetworkReply`) | Network code reads top to bottom, all on the GUI thread. |
| Settings | KConfig + KConfigXT (`.kcfg`), `~/.config/emberrc` | Generated typed accessors; same format as couchboxrc. |
| Formatting | KCoreAddons `KFormat` | "1 hour, 30 minutes", sizes, relative dates. |
| Single instance | KDBusAddons `KDBusService(Unique)` | Second launch raises the first window. |
| Icons | Kirigami `Kirigami.Icon` only | Theme icons. No other Kirigami controls: their focus handling and Breeze look don't suit a remote-driven Amber look. (As built, Ember draws its few icons itself and doesn't use Kirigami at all.) |
| Video | Port couchbox-iptv's vendored Plezy mpv plane to Qt | mpv renders into a `wl_subsurface` below the transparent window; no copy through the toolkit. |
| Credentials | KConfig state file `~/.local/state/ember/session`, mode 0600 | **Not QtKeychain**: couchbox disables KWallet (`couchbox-base/kwalletrc`), so there is no secret service. |
| Licence | GPL-3.0-only | Carries Plezy's GPL-3.0 mpv plane code, same as couchbox-iptv. |

Toolchain on the workstation today: Qt 6.11.2 (the Wayland client plugin is
part of `qt6-base` since 6.10), CMake 4.4, GCC 16, qcoro 0.13, KF 6.30.
Missing and needed: `mpv` (for libmpv headers), `extra-cmake-modules`.

## The UI to reproduce

Stock Amber with the vertical menu, from Amber's Omega source
(`xbmc/repo-skins`, `skin.amber`). All sizes are in Amber's 1920x1080 space;
see "Scaling" below. Particulars get tuned after implementation.

### First sign-in

1. Find servers on the LAN, or type a URL.
2. Quick Connect code on screen; username/password as the fallback.
3. Library picker: every user view on the server with a check mark. OK
   toggles, "Done" saves. Settings > Libraries reopens it.

### Home

- Left blade, 440 px wide, dark and translucent. Uppercase text menu, 80 px
  rows, right-aligned labels. Focused item in the highlight colour, others
  grey.
- Highlight pinned at slot 4 with items scrolling under it (Amber default).
  "Static" mode (highlight moves, list stays) is a setting.
- Items: the libraries picked at sign-in, in server order, then Search and
  Settings.
- **OK** opens the library's default listing (all items, sorted by name).
- **Left** (or Back) slides a 400 px submenu in over the blade: Recently
  Added, In Progress, Next Up (TV), Genres, Years, Collections, A-Z. **Right**
  or Back returns.
- **Right** does nothing for now; it is where shelves go later.
- Background: random fanart from the focused library, crossfading.

### Library list (Amber "List", view 50)

- List on the right third: 9 rows of 72 px. Each row: title, grey second
  label that follows the sort (date added, year, rating, runtime), status icon
  (watched check, partial, empty circle). Shows and seasons show an unwatched
  count badge instead.
- Focused row: solid highlight bar, dark text. Up/Down wrap. Option "keep
  focus centred" (fixed highlight at row 5).
- Left two-thirds: poster, then title (year), "runtime . rating . MPAA",
  auto-scrolling plot, genres, codec flags (resolution, codec, channels).
  Shows add season and episode counts.
- Top band: fanart with the clearlogo.
- Later variants: Low List (6 rows, bigger fanart), Tall List (13 rows),
  Big List (two-line rows), Simple List (three text columns, no pane).

### Navigation

- **OK on a folder, show or season** drills down. **Back** goes up one level
  and restores the previous list's position. Back at the top of a library goes
  Home.
- **OK on a leaf item** (movie, episode, video) plays it, resuming from the
  saved position if there is one. No prompt; "Play from beginning" is in the
  context menu.
- **Info** opens the info screen: poster left, metadata, buttons (Play,
  Episodes, Mark watched), plot, codec flags, then rows for cast, seasons and
  more in the set.
- **Menu, or OK held for 0.6 s**, opens the context menu: Play from
  beginning (when in progress), Information, Mark watched/unwatched, Go to
  show, Go to season.
- **Left** from a list opens the side blade (510 px): view type, sort by,
  order, hide watched, filter (genre, year), search in this library.
- **Right** from a list (when sorted by name) opens an A-Z strip on the right
  edge; Up/Down jumps letters.
- **Channel Up/Down** page the list.

Long-press OK lives in Ember, not in fire-blaster. A global long-OK in
fire-blaster would delay OK in every app and would break Bigscreen's
hold-OK-to-close on its Tasks page. In Ember, OK acts on key release when held
less than 0.6 s; at 0.6 s the context menu opens and the release is swallowed.
(As built, OK acts on release everywhere, menus and dialogs included: one rule
is simpler, and the delay is the length of the press.)

### Scaling

On a 1080p TV, Linux reports a device pixel ratio of 1, so Qt's logical size
is 1920x1080 and desktop-sized controls look tiny. Plasma output scaling and
`QT_SCALE_FACTOR` would scale the Bigscreen shell or fight the video plane.
Instead, design in Amber's 1920x1080 grid: a `Theme` QML singleton exposes
`u = window.height / 1080`, and every size and font is `n * u`. Text renders
natively at the real size, so it stays sharp. A "UI scale" setting multiplies
`u` for other screen sizes.

## Architecture

```
src/
  main.cpp                 QGuiApplication, render loop choice, KDBusService, QML engine
  app/
    Settings.kcfg          KConfigXT: UI, libraries shown, playback, audio, subtitle prefs
    CouchboxConfig         reads ~/.config/couchboxrc [Video] (codec flags)
    RemoteKeys             application event filter: normalizes remote keys, long-press OK
    ScreenInhibitor        org.freedesktop.ScreenSaver.Inhibit while playing
    Mpris                  org.mpris.MediaPlayer2 + .Player adaptors
    ImageNam               QQmlNetworkAccessManagerFactory: QNetworkDiskCache, auth
  jellyfin/
    ApiClient              QNAM wrapper, Authorization header, error mapping, QCoro tasks
    Auth                   password login, Quick Connect, session persistence
    Discovery              UDP 7359 broadcast, URL guessing, /System/Info/Public probe
    Browse                 views, items (paged), seasons, episodes, resume, next up, latest, search
    Item                   value type mapped from BaseItemDto (+ MediaStreams, UserData)
    ImageUrls              Primary/Backdrop/Logo/Thumb URLs with parent/series fallbacks
    DeviceProfile          built from couchboxrc codec flags and audio settings
    PlaybackSession        PlaybackInfo, stream URL, Started/Progress/Stopped reports
    Segments, Trickplay    MediaSegments, trickplay tiles
    EventSocket            /socket: LibraryChanged, UserDataChanged
  models/
    LibraryModel           user views with a "shown" flag, for the picker and the menu
    MenuModel              home menu and submenus
    ItemListModel          QAbstractListModel, canFetchMore/fetchMore paging, sort/filter
    TrackModel             audio and subtitle tracks for the player dialog
  player/
    plane/                 ported mpv plane (mpv_player, wayland_video_surface, plane_*, hdr_*, shared/)
    VideoPlane             QQuickItem: reports its scene rect to the plane, shows/hides it
    PlayerController       QML-facing: open(item), state, position, tracks, commands
  qml/
    Main.qml, Theme.qml
    setup/  home/  library/  info/  player/  components/
protocols/                 color-management-v1 (vendored), viewporter
tests/
```

### Threading rules

These exist because Kodi's threading caused deadlocks.

- Everything Ember's own code does runs on the GUI thread: network (QNAM is
  asynchronous), models, QML, Jellyfin session state.
- Background threads only in the vendored plane (render worker, mpv's own
  threads, teardown queue). They hand results back with queued invokes and
  never block on the GUI thread.
- No `Qt::BlockingQueuedConnection`, no `QEventLoop::exec()` in app code, no
  locks in app code.
- Image decoding uses `Image { asynchronous: true }` (Qt's loader threads).

### Jellyfin client notes

Reference implementation: Plezy's Jellyfin backend (paths below are in
`plezy/lib/`). Port the behaviour, not the structure.

- **Auth header** on every request:
  `Authorization: MediaBrowser Client="Ember", Device="couchbox", DeviceId="<uuid>", Version="<ver>", Token="<token>"`,
  values percent-encoded (`services/jellyfin_auth_header.dart:21-45`). No
  `X-Emby-*` headers: Jellyfin 12 turns legacy auth off by default.
- **URLs given to mpv** (stream, subtitles, trickplay, websocket) carry
  `ApiKey=<token>`, never `api_key` (`media/media_browser_dialect.dart:79-84`).
  Image URLs go through `ImageNam`, which adds the header for the server's
  origin. (Item images may be anonymous on Jellyfin; check, and drop the auth
  if so to keep cache keys stable.)
- **DeviceId**: UUID v4, generated once, stored in the state file.
- **Login**: probe `/System/Info/Public`, then Quick Connect first (no typing
  on a TV): `/QuickConnect/Enabled`, `/QuickConnect/Initiate`, poll
  `/QuickConnect/Connect?secret=`, `/Users/AuthenticateWithQuickConnect`
  (`services/jellyfin_auth_service.dart:169-356`). Username/password as the
  fallback. LAN discovery: `who is JellyfinServer?` to UDP 7359
  (`services/jellyfin_lan_discovery_service.dart:20-85`).
- **Browse**: `/UserViews`, `/Items?ParentId=&Fields=&SortBy=&StartIndex=&Limit=`
  (field sets: `jellyfin_client/parts/browse.dart:79-265`),
  `/Shows/{id}/Seasons`, `/Shows/{id}/Episodes?SeasonId=&IsMissing=false`,
  `/UserItems/Resume`, `/Shows/NextUp`, `/Items/Latest`, `/Items/Filters`.
  A-Z via `NameStartsWith` (`NameLessThan=A` for `#`)
  (`services/library_query_translator.dart:187-308`).
- **Mapping** BaseItemDto to `Item`: `services/jellyfin_mappers.dart:189-303`,
  `services/file_info_parser.dart:35-56`. Ticks are 100 ns.
- **DeviceProfile** for `POST /Items/{id}/PlaybackInfo`
  (`jellyfin_client/parts/playback.dart:906-1021`):
  - DirectPlay containers `mp4,mkv,m4v,webm,mov,ts,mpegts`; video codecs
    `h264,mpeg4,mpeg2video,vp8` plus `hevc`, `vp9`, `av1` unless couchboxrc
    `[Video] TranscodeHEVC/TranscodeVP9/TranscodeAV1` says otherwise; any
    audio (mpv downmixes).
  - A CodecProfile limiting HEVC to `VideoBitDepth <= 8` when
    `TranscodeHEVC10` is set. Plezy has no 10-bit check; the NUC's Skylake
    needs one.
  - Transcoding profile: HLS, fMP4 segments, H.264, AAC/AC-3.
  - Subtitles: Embed for text and bitmap formats, External for
    `srt,ass,ssa,vtt`.
- **Play method**: no bitrate cap means static URL
  (`/Videos/{id}/stream?Static=true&MediaSourceId=&PlaySessionId=&ApiKey=`);
  a `TranscodingUrl` in the response means transcode. Keep the
  `PlaySessionId` on direct play too (Plezy drops it).
- **Resume**: start at `UserData.PlaybackPositionTicks`; on transcode, also
  send `StartTimeTicks` in PlaybackInfo.
- **Progress**: `/Sessions/Playing` after the first frame,
  `/Sessions/Playing/Progress` every 10 s and on pause, seek and track change,
  `/Sessions/Playing/Stopped` once at exit. Back off on failures. The server
  marks the item played at 90 % on Stopped
  (`services/playback_progress_tracker.dart`,
  `services/playback_report_session.dart`).
- **Mark watched/unwatched**: `POST`/`DELETE /UserPlayedItems/{id}`
  (`jellyfin_client/parts/watch_state.dart:19-45`).
- **Tracks**: audio = server default, then language preference, then first;
  subtitles = server default, then mode (None / OnlyForced / Always), then off
  (`services/track_selection_service.dart:944-1166`). Turn on the server's
  `RememberAudioSelections`/`RememberSubtitleSelections`. Changing audio while
  transcoding re-requests PlaybackInfo and resumes at the same position.
- **Segments**: `/MediaSegments/{id}` (Intro, Outro); chapter-title fallback
  (`media/media_source_info.dart:400-470`). Skip button, optional auto-skip.
- **Trickplay**: `Trickplay` field, smallest width >= 160,
  `/Videos/{id}/Trickplay/{w}/{n}.jpg`
  (`services/jellyfin_trickplay_service.dart:66-105`).

### Player

- mpv options: the couchbox-iptv base set (`shared/mpv/mpv_player_common.h:527-553`
  and `mpv_player.cc:352-371`), plus `hwdec=auto-safe`, `alang`/`slang`
  from settings.
- **Downmix** (default on, matching the Plezy and Kodi defaults couchbox
  ships): `audio-channels=stereo`, `audio-normalize-downmix`, and
  `audio-swresample-o=center_mix_level=<10^((-3+boost)/20)>` with boost
  8 dB (`plezy/lib/mpv/player/player_base.dart:1357-1391`). Passthrough off.
- OSD: title, seek bar with trickplay thumbnail, elapsed/remaining, end time,
  skip-intro button. Hidden by default; any key shows it.
- Up Next at the Outro segment or the last 30 s: 5 s countdown, OK plays now,
  Back cancels.
- Stop key (`Key_MediaStop`) and Back leave the player and report Stopped.

## couchbox integration

From couchbox's README and `packages/couchbox-base`:

- **App ID**: `QGuiApplication::setDesktopFileName("org.couchbox.ember")`
  and ship `org.couchbox.ember.desktop`. Bigscreen raises a running app only
  when the Wayland app ID equals the desktop file ID; otherwise a tile press
  starts a second copy.
- **Fullscreen, no decorations** at start; `EMBER_WINDOWED=1` for
  development (same pattern as couchbox-iptv).
- **Minimize**: a long Home press makes the couchbox-home KWin script minimize
  the app and call couchbox-focus, which pauses any other app's MPRIS player
  found by PID. Exposing MPRIS from Ember's own process is enough; no
  couchbox-focus change needed. MPRIS also gives Plasma's media controls.
- **Idle**: hold `org.freedesktop.ScreenSaver.Inhibit` while playing (couchbox
  turns the display off after 10 min; Bigscreen's own inhibit is disabled).
  Tie it to the bus connection like couchbox-iptv's `screen_inhibitor.dart`.
- **Remote keys** (after fire-blaster remaps):

  Measured on the BRIX (KWin 6.7.5, Qt 6.11.2) by injecting the evdev codes
  fire-blaster emits (M0 spike 2):

  | evdev code | Qt reports | Ember turns it into |
  |---|---|---|
  | KEY_ENTER | `Key_Return` (scan 36) | OK (acts on release; held 0.6 s is `Key_Menu`) |
  | KEY_SELECT | `Key_Select` | OK |
  | KEY_BACK | `Key_Back` (keysym XF86Back) | Back |
  | KEY_ESC | `Key_Escape` | Back |
  | KEY_INFO | `Key_unknown`, keysym `0x10081166` | `Key_Info` |
  | KEY_CHANNELUP / DOWN | `Key_unknown`, keysym `0x10081192` / `0x10081193` | `Key_ChannelUp` / `Key_ChannelDown` |
  | KEY_EPG | `Key_unknown`, keysym `0x1008116a` | `Key_Guide` |
  | KEY_CONTEXT_MENU | `Key_unknown`, keysym `0x100811b6` | `Key_Menu` |
  | KEY_COMPOSE (fire-blaster's Menu) | taken by Bigscreen on couchbox 0.9.0 ("Toggle Bigscreen Tasks Overview") | `Key_Menu` once couchbox frees it |
  | KEY_HOMEPAGE | taken by Bigscreen on couchbox 0.9.0 ("Toggle Bigscreen Home Screen") | Home |
  | KEY_PLAYPAUSE, PLAY, STOPCD, FASTFORWARD, REWIND, NEXTSONG, PREVIOUSSONG | never reach the app on couchbox 0.9.0 (Plasma's media keys) | handled through MPRIS meanwhile |
  | KEY_PAUSE | `Key_Pause` | `Key_MediaPause` |
  | KEY_NUMERIC_0..9 | `Key_0`..`Key_9` with text (keysym `0x10081200`+) | digits |
  | KEY_PAGEUP / DOWN | `Key_PageUp` / `Key_PageDown` | page the list |
  | KEY_RED / GREEN / YELLOW / BLUE | `Key_Red` .. `Key_Blue` | unused |
  | KEY_SUBTITLE | `Key_Subtitle` | unused |

  Holding a key auto-repeats after 600 ms at 25 Hz (`isAutoRepeat()` set).
  Newer couchbox releases free Home Page, Menu and the media keys for apps
  (couchbox-shortcuts); the BRIX's 0.9.0 predates that.

  `RemoteKeys` (an application event filter) rewrites these into plain Qt
  keys before QML sees them, and logs raw events when `EMBER_KEYS=1`.
  Long-press detection ignores auto-repeat events and times press to
  release. Reference: `couchbox-iptv/lib/app/keys.dart`.
- **Codec settings**: read `[Video] TranscodeHEVC, TranscodeHEVC10,
  TranscodeAV1, TranscodeVP9` from couchboxrc directly with KConfig.
  couchbox-video-profile already sets them from `vainfo`.
- **Packaging** (same as couchbox-iptv): `packages/ember/PKGBUILD` building
  from `git+https://github.com/jpsutton/ember.git#tag=v$pkgver`; a `depends`
  entry in couchbox-base's PKGBUILD; one line in `couchbox-base/visible-apps`;
  README lines. Runtime depends: `qt6-base`, `qt6-declarative`, `mpv`,
  `qcoro`, `kconfig`, `kcoreaddons`, `kdbusaddons`, `kirigami`, `wayland`,
  `libepoxy`. Make depends: `cmake`, `ninja`, `extra-cmake-modules`,
  `wayland-protocols`.

## Milestones

Each milestone ends with something the user can try on the NUC. The user runs
the TV tests; after a deploy, hand over with what to check.

### M0: spikes

Three throwaway programs that settle the risky parts before the app exists.

1. **Video plane under Qt.** Minimal transparent `QQuickWindow` with a QML
   label over a playing video. Port only what's needed:
   - `ParentSurface`: `QGuiApplication::platformNativeInterface()->nativeResourceForWindow("surface", window)`.
     Only the call goes through a QPA vtable; no private symbols are linked.
   - `wl_display`, `wl_compositor`: `QNativeInterface::QWaylandApplication`.
   - Parent commit: `QQuickWindow::update()`. Output refresh:
     `QScreen::refreshRate()`, `QNativeInterface::QWaylandScreen::output()`.
   - Keep the GLib idle/timeout sources for now: Arch's Qt uses the GLib event
     dispatcher, so they still fire.
   - Use the basic render loop (`QSG_RENDER_LOOP=basic`) so Qt commits the
     parent surface on the GUI thread, as GTK did.

   Done when: video plays under a QML overlay, `hwdec-current` is `vaapi`,
   the overlay hides cleanly, the window survives hide/show (surface
   recreation), and CPU use is similar to couchbox-iptv's player.
   Fallback if it fails: render through the Qt scene graph the way mpvqt does
   (one extra GPU pass; fine for 1080p SDR).
2. **Remote keys.** A window that logs `key()`, `text()`,
   `nativeScanCode()`, `nativeVirtualKey()`, `isAutoRepeat()` and timing for
   every key. The user presses every button on the Alexa and MCE remotes, and
   holds OK once; the table above gets corrected.
3. **Jellyfin API.** A CLI using `ApiClient` + QCoro: Quick Connect login,
   list views, page through a library, call PlaybackInfo for one movie. Run it
   against the user's server to confirm the version and the 12.x auth rules.

### M1: skeleton and first sign-in

- CMake project: `qt_add_qml_module`, C++20, `qmllint` in the build, Qt Test
  wiring, GitHub Actions (Arch container: configure, build, `ctest`, `qmllint`).
- `main.cpp`: app ID, fullscreen, `KDBusService(Unique)` with raise on
  activation, render loop setting, QML engine.
- `Theme` singleton (scaling unit, colours, fonts), `RemoteKeys` filter with
  long-press OK, `Settings.kcfg`, `CouchboxConfig`.
- First sign-in: discover servers or type a URL, Quick Connect code on
  screen, username/password fallback, then the library picker. Session saved;
  later starts skip it. Settings screen with Libraries and Sign out.
- Text entry: check whether Bigscreen's on-screen keyboard appears for a QML
  `TextField`. If not, build a small on-screen keyboard (also needed for
  search).

Done when: the tile starts Ember full screen, sign-in and library picking work
with the remote only, and a second tile press raises the running copy.

### M2: home screen

- `LibraryModel`, `MenuModel` from the picked libraries plus Search and
  Settings.
- Vertical blade with pinned-slot (default) and static modes; submenus
  sliding in from the left.
- Backgrounds: random backdrop per library, crossfade.

Done when: the home screen matches stock Amber's vertical-menu layout and key
behaviour (minus shelves).

### M3: library list and drill-down

- `ItemListModel`: paged (`canFetchMore`/`fetchMore`, 100 per page), roles
  for every field the pane needs, sort and filter state.
- List view (Amber List) with details pane, fanart band and clearlogo,
  status icons, unwatched badges, sort-dependent second label.
- Drill-down Library > Show > Season > Episodes with `StackView`; each page
  keeps its model and current index, so Back lands on the same row.
- Image pipeline: `ImageNam` with a 500 MB `QNetworkDiskCache`; request sized
  images (`maxHeight` for posters, `maxWidth=1920` for backdrops); preload the
  neighbours' fanart.

Done when: browsing a 1,000+ item library with the remote stays smooth and
Back always returns to the right row.

### M4: playback (first daily-usable version, tag v0.1.0)

- Port the rest of the plane into `player/plane/`; `VideoPlane` item;
  `PlayerController`.
- `PlaybackSession`: DeviceProfile, PlaybackInfo, static or transcode URL,
  resume position, progress reports, Stopped on exit.
- OK on a leaf item plays; context menu (Menu or long OK) with Play from
  beginning and Mark watched/unwatched.
- OSD, seek (Left/Right 10 s, Up/Down chapters), audio and subtitle dialog
  with `TrackModel`, downmix with centre boost.
- `ScreenInhibitor`, `Mpris` (pause on minimize).
- Up Next and auto-play next episode.
- Package into couchbox (PKGBUILD, visible-apps, couchbox-base depends) via a
  PR into `staging`.

Done when: the user can pick an episode, watch it, have it marked watched on
the server, and continue to the next one, with Kodi left alone.

### M5: Amber parity

- Side blade: view type, sort, order, hide watched, genre/year filters.
- A-Z strip; Channel +/- paging.
- Info screen (Info key, or Information in the context menu) with cast and
  set rows; remaining context menu actions (Go to show, Go to season).
- Submenu entries: Genres, Years, Collections, Recently Added, In Progress,
  Next Up.
- Websocket: refresh lists on `LibraryChanged` and `UserDataChanged`.

### M6: polish

- Intro/credits skip (MediaSegments) with optional auto-skip.
- Trickplay thumbnails on the seek bar.
- Search with an on-screen keyboard.
- More list variants (Low, Tall, Big, Simple) and per-library default view.
- Home menu editing (order, hide items).
- Replace the GLib sources in the plane with Qt timers and queued invokes;
  drop the GLib link. Try the threaded render loop and keep it only if parent
  commits stay in step with the plane.

### Later

- Home shelves (Continue Watching, Next Up, Latest), entered with Right from
  the menu as in Amber.
- Cast target ("Play On" from the Jellyfin phone apps): session capabilities
  and websocket Playstate/Play commands.
- Music libraries.
- Refresh-rate switching for 24p content (no couchbox component does this
  today; KWin output management would be needed).

## Testing

- **Unit (Qt Test, in CI)**: Item mapping against JSON fixtures recorded from
  the user's server (tokens scrubbed); DeviceProfile from each couchboxrc
  combination; URL builders; tick conversions; track selection rules;
  `RemoteKeys` with synthetic events including native keysyms, auto-repeat and
  long-press timing; progress reporter state machine with a fake clock.
- **Local server (optional)**: a `scripts/dev-server.sh` that runs the
  `jellyfin/jellyfin` container with a few freely licensed clips, for
  development without touching the user's server.
- **On the NUC**: the user tests. Build and deploy, then hand over a short
  list of what changed and what to try.

## Risks

| Risk | Mitigation |
|---|---|
| Plane under Qt: parent commits out of step with the subsurface, surface recreated on hide/show, Wayland event dispatch differs from GDK. | Spike M0.1 first; basic render loop; handle surface recreation. Fallback: scene-graph rendering like mpvqt. |
| QPA vtable call breaks on a Qt minor update. | Smoke-test video after each Qt minor bump; rebuild the package against the new Qt. |
| Remote keys arrive as `Key_unknown`. | Match on `nativeVirtualKey()`/`nativeScanCode()`; spike M0.2 confirms. |
| Long-press OK feels laggy, since OK on list items waits for release. | Release-time action is under 0.6 s by definition and usually ~100 ms; acting on press stays in menus and dialogs. Tune after M4. |
| Text entry on the TV. | Quick Connect and LAN discovery avoid typing at sign-in; own on-screen keyboard if Bigscreen's doesn't appear. |
| Jellyfin 12 auth and API changes. | Header auth only; `ApiKey` for media URLs; spike M0.3 against the real server. |
| Skylake can't decode HEVC 10-bit, VP9, AV1 in hardware. | DeviceProfile from couchboxrc flags; server transcodes. |
| Scope creep before anything is usable. | M4 is the first usable version; Amber parity and polish come after. |

## Repository setup

- New GitHub repo `jpsutton/ember` (the user creates it or approves creating
  it). Branch `main`, release tags `vX.Y.Z`, like couchbox-iptv.
- Per-repo git config: `user.email = jpsutton@protonmail.com`,
  `core.sshCommand` with the personal key.
- Commit subjects: plain imperative sentences.
- `LICENSE`: GPL-3.0-only. `README.md` describing the app as built.

## Findings

- **Video plane under Qt works as planned**, with the basic render loop.
  Wayland events for the plane's own proxies are dispatched on the GUI thread
  by Qt (the plane warns if that changes). The port dropped HDR and uses
  `wp_viewporter` instead of `wl_surface.set_buffer_scale`. No GLib was
  needed: GLib sources became queued calls and `QTimer`s from the start.
- **The plane is cheaper than stock mpv on the BRIX.** 1080p30 H.264 with
  VA-API: about 21 % of one core for Ember, 6 % for KWin, one dropped frame
  in 15 s after startup. Stock mpv 0.41 (`vo=gpu-next`) on the same clip
  drops about 12 frames per second. couchbox's fast-scaling profile
  (`PlezyScaling=fast`) applies to Ember too.
- **Minimizing works**: KWin stops acknowledging frames and the plane backs
  off to one present a second, then resumes.
- **Browsing cost on the BRIX**: about 43 % of one core while scrolling
  through a list at three rows a second (fanart and posters decoding);
  300–320 MB resident.
- **A 1,200-item library** (generated, no artwork) pages smoothly on the
  BRIX: 12 page-downs in 8 s cost about 8 % of one core, pages of 100 load
  as the list reaches them, and the A–Z strip jumps to rows not loaded yet
  (M3's "done when").
- **Jellyfin 12.2** rejects `X-Emby-Token` and `api_key` (401) and accepts
  the `Authorization: MediaBrowser … Token=` header and `ApiKey=`. Item
  images and static streams need no auth at all.
- **Jellyfin keeps no resume point for items under 5 minutes**
  (`MinResumeDurationSeconds`, default 300) and marks them played on stop.
  The dev server lowers it to 30 s because its clips are short.
- **Discovery** answers with the server's own idea of its address, which may
  be on a network the box can't reach; Ember also tries the address the reply
  came from. On the BRIX, discovery found both the dev server (over ZeroTier)
  and the user's own server on the LAN.
- **QML gotcha**: the QML compiler drops a bare read like `items.revision`
  in a binding, and the dependency with it; use the value in the expression.
- **QML gotcha**: a `QML_SINGLETON` with a default constructor gets
  constructed again by QML even when it has a static `create()`; Session's
  constructor takes a required argument so QML has to use `create()`.
- **The HEVC 10-bit limit works**: with HEVC allowed and the
  `VideoBitDepth <= 8` codec profile (the NUC's case), Jellyfin 12.2
  transcodes the 10-bit film and direct-plays 8-bit and H.264. On the BRIX
  (all HEVC transcoded) the 10-bit film plays through the server's H.264
  transcode with VA-API decode.

## Not yet verified

- A real remote (only injected key codes so far), and Home, Menu and the
  media keys reaching the app on a current couchbox (the BRIX runs 0.9.0,
  which gives them to Plasma).
- Pause on minimize through couchbox-focus (not in 0.9.0); its MPRIS Pause
  call itself works.
- A real library on a real server (only the dev server has been used).
- Media segments (needs the Intro Skipper plugin; the chapter-name
  fallback is tested), burned-in bitmap subtitles during a transcode,
  playback on the NUC itself.
- Hiding and re-showing the window (the plane is rebuilt on show; only
  minimize was exercised).

## Open questions

1. ~~Jellyfin server version~~: the dev server runs 12.2.0; the user's
   server version is still unknown.
