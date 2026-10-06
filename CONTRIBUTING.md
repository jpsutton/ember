# Working on Ember

Ember is Qt 6 (QML and C++) with mpv for playback. [PLAN.md](PLAN.md) has the
design, the milestones and their status, and what was learned along the way.

## Building

Ember needs Qt 6.8 or later (Base, Declarative, WebSockets), KDE Frameworks 6
(Config, CoreAddons, DBusAddons, WindowSystem), QCoro 6, mpv (libmpv), the
Wayland client libraries and wayland-protocols. On Arch:

```sh
pacman -S --needed cmake ninja extra-cmake-modules qt6-base qt6-declarative qt6-websockets \
  kconfig kcoreaddons kdbusaddons kwindowsystem qcoro mpv wayland wayland-protocols
cmake -S . -B build -G Ninja
cmake --build build
ctest --test-dir build
```

Video needs a Wayland session: mpv renders into a subsurface below Ember's
transparent window (see `src/player/plane/README.md`).

Environment variables:

- `EMBER_WINDOWED=1` opens a window instead of going full screen.
- `EMBER_KEYS=1` logs every key event.

## Files Ember keeps

- `~/.config/emberrc`: settings, and the list style chosen for each library.
- `~/.local/state/ember/session`: every server signed in to, each with its
  access token and library choices, and which one is in use. Readable only
  by its owner. An older single-server file is converted on first start.
- `~/.cache/ember`: cached artwork.

On couchbox, Ember also reads `~/.config/couchboxrc`. Its `[Video]` keys
(`TranscodeHEVC`, `TranscodeHEVC10`, `TranscodeAV1`, `TranscodeVP9`) decide
which codecs the server is asked to convert, and `PlezyScaling=fast` selects
mpv's cheap scalers.

## Development

- `scripts/dev-server.sh` runs a throwaway Jellyfin server in Docker. Its
  library is short generated test clips named after open and public-domain
  films, so Jellyfin fetches real metadata and artwork. The admin user and
  password are both `ember`.
- `-DEMBER_BUILD_DEV_TOOLS=ON` also builds `ember-plane-spike`, which plays a
  file through the video plane, and `ember-shot`, which takes screenshots
  through KWin with the video included.
- `tools/remote-keys.py` presses keys through a virtual input device, the way
  a remote does on couchbox after fire-blaster.

## Releases

Tag `vX.Y.Z` after bumping the version in `CMakeLists.txt`. couchbox builds
Ember from the tag: bump `pkgver` in couchbox's `packages/ember/PKGBUILD` and
open a pull request into its `staging` branch.

## Credits

The video plane is adapted from [Plezy](https://github.com/edde746/plezy)
(GPL-3.0) by way of [couchbox-iptv](https://github.com/jpsutton/couchbox-iptv),
and the Jellyfin behaviour follows Plezy's Jellyfin backend.

The fonts in `fonts/` are the ones Kodi's Amber skin uses: Ubuntu Condensed
(Ubuntu Font Licence 1.0, taken from Amber) and Bebas Neue (SIL Open Font
License 1.1, from Google Fonts). Their licences are next to them, and the
couchbox package installs them with Ember's.
