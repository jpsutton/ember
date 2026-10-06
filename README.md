# Ember

Ember is a Jellyfin app for the TV, made for
[couchbox](https://github.com/jpsutton/couchbox). It looks and works like
Kodi's Amber skin: a plain menu down the left side, lists with artwork and
details, and everything done with a TV remote.

![The home menu](docs/screenshots/home.jpg)

## What it does

- Finds your Jellyfin server on the network and signs you in with a code
  from your phone or with your password.
- Puts the libraries you choose on the home menu.
- Remembers every server you sign in to. Switch between them from Settings
  on the home menu.
- Shows what's new in each library: recently added, and recently aired. An
  episode that a streaming service releases early shows up when it arrives,
  not weeks later when it officially airs.
- Remembers where you stopped, skips intros, and offers the next episode
  when one ends.
- Works with the Jellyfin app on your phone: send a video to the TV, or use
  the phone as a remote.

## Screenshots

<table>
  <tr>
    <td><img src="docs/screenshots/submenu.jpg" alt="A library's menu"></td>
    <td><img src="docs/screenshots/list.jpg" alt="A film library"></td>
  </tr>
  <tr>
    <td>More ways to browse a library.</td>
    <td>A library, with details for the highlighted film.</td>
  </tr>
  <tr>
    <td><img src="docs/screenshots/episodes.jpg" alt="A season's episodes"></td>
    <td><img src="docs/screenshots/recently-aired.jpg" alt="Recently aired episodes"></td>
  </tr>
  <tr>
    <td>A season, showing what you've watched.</td>
    <td>Recently aired episodes from every show.</td>
  </tr>
  <tr>
    <td><img src="docs/screenshots/info.jpg" alt="A film's details"></td>
    <td><img src="docs/screenshots/player.jpg" alt="Playback"></td>
  </tr>
  <tr>
    <td>Details, cast and actions for one film.</td>
    <td>Playback. Video: the Sintel trailer, Blender Foundation, CC BY 3.0.</td>
  </tr>
</table>

## Using the remote

**Home menu.** Up and Down pick a library. OK opens it. Left shows more ways
to browse it: recently added, recently aired, in progress, genres and so on.
Left on Settings offers Switch server.

**Lists.**

- OK plays a film or episode, or opens a show.
- Info shows the details page.
- Menu, or holding OK, shows options such as "mark watched".
- Left changes the sort order and the look of the list. On a show or a
  season, it also offers Shuffle.
- Right moves to the scroll bar. There, Up and Down move a page at a time.
- Channel up and down also move a page at a time.

**Watching.**

- OK pauses.
- Left and Right skip back and forward.
- Up and Down jump between chapters.
- Menu picks audio and subtitles.
- Back stops.

When an episode ends, the next one starts after a short countdown.

**Typing.** Text fields take typing from a keyboard, or from your phone
through KDE Connect. With the remote, press Right in a field to reach
Paste, which types whatever your phone shared to the clipboard, and the
on-screen keyboard.

## Settings

Settings is at the bottom of the home menu. It covers text size, the stereo
mix and dialogue boost, streaming quality, subtitles, playing the next
episode, skipping intros, and how far Left and Right skip.

## Getting it

Ember comes with couchbox. To build it yourself, see
[CONTRIBUTING.md](CONTRIBUTING.md).

## Licence

GPL-3.0. Ember's video player is adapted from
[Plezy](https://github.com/edde746/plezy). The fonts come from Kodi's Amber
skin; their licences are in [fonts/](fonts/).
