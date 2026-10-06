# Video plane

mpv renders into a Wayland subsurface stacked below Ember's transparent
window, so video reaches the compositor without passing through Qt's scene
graph. QML drawn over the video shows on top of it.

Adapted from Plezy's Linux runner (<https://github.com/edde746/plezy>,
GPL-3.0), tag 2.22.0, by way of couchbox-iptv's vendored copy:

| Here | Plezy | Changes |
|---|---|---|
| `mpv_player_common.h` | `shared/mpv/mpv_player_common.h` | None (copied as is). |
| `MpvCore` | `linux/runner/mpv/mpv_player.{h,cc}` | No HDR output path, no audio-only core, no restart positions. GLib sources became queued calls and a `QTimer`; Flutter values became `QVariant`. |
| `WaylandVideoPlane` | `linux/runner/mpv/wayland_video_surface.{h,cc}` | No HDR (color-management-v1). Binds its own `wl_compositor`; sizes the buffer in device pixels and scales it with `wp_viewporter` instead of `set_buffer_scale`, so fractional scaling works. Timers are `QTimer`s. |
| `PlaneRenderExecutor` | `linux/runner/mpv/plane_render_executor.{h,cc}` | Completions run through `QMetaObject::invokeMethod` on the application object. |
| `../MpvVideo` | `linux/runner/mpv/mpv_plugin.cc` | Only the render scheduling and lifecycle; the method channel is now a QML item. |

## Qt specifics

- The window's `wl_surface` comes from
  `QGuiApplication::platformNativeInterface()->nativeResourceForWindow("surface", window)`.
  That is a QPA header (hence `Qt6::GuiPrivate` at build time), but the call
  only goes through a virtual function, so no private symbols are linked.
  Check video still plays after each Qt minor update.
- All Wayland listeners rely on Qt dispatching the default event queue on the
  GUI thread. `WaylandVideoPlane::HandleFrameDone` warns once if it doesn't.
- The window needs an alpha channel (`QQuickWindow::setDefaultAlphaBuffer(true)`
  before the window exists) and a transparent colour where the video shows.
