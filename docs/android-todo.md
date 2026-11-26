# Android: known issues to fix later

Found in reviews of the Java code, not yet fixed. Line numbers drift; search for the names.

## Image picker decodes on the main thread

`ImageResultProxyActivity.copyAndDownscaleToCache` runs in the activity result callback, on the main
thread. It opens the picked image's stream three times (bounds, orientation, decode) and decodes it,
then writes the downscaled copy to the cache. With a cloud-backed provider (Google Photos fetching the
original) or a very large image, that can take long enough to ANR.

Fix: do the copy on a background thread, then call `returnWithResult` back on the UI thread.

## VR: `System.exit(0)` in `onDestroy`

`PpssppActivity.onDestroy` ends with `if (isVRDevice() && !restarting) System.exit(0);`, a workaround for
OpenXR not surviving a restart. Only `graphics_restart` sets `restarting`, so:

- The `recreate` command, and any config change that recreates the activity, kill the process
  mid-recreate.
- After a finish and quick relaunch, the old activity's late `onDestroy` (which `checkCurrent` guards
  against for the native shutdown just above it) kills the process the new activity runs in.

Probably the exit should also be skipped when `isChangingConfigurations()` is true or the activity isn't
current. Needs testing on a Quest, which is why it wasn't changed blind.

## Achievements host override receiver is open to any app

`AchievementsHostOverrideReceiver` (PR #21760) is exported without a permission, so that a companion
app can point RetroAchievements at a local server. Any installed app can send it, though, and since
loopback is shared between apps, the "loopback only" check doesn't help: an app listening on
`127.0.0.1` can set itself as the host and receive the user's RA login and token. The override persists
across launches.

`android:permission="android.permission.DUMP"` would restrict it to adb, but locks out the companion
app it was made for. Options: a signature permission (only if the companion is ours), or keep it open
but only apply an override after the user confirms it in PPSSPP. Also, `onReceive` calls
`NativeApp.setAchievementsHostOverride` on the main thread, which writes `g_Config` fields other
threads read.

## A failed surface init leaves a black screen

When `InitSurface` fails, `RenderLoopThread` gives up, but `runRenderLoop` had already returned true, so
Java's `renderLoopRunning` stays set. Every later `surfaceChanged` takes the "already running, surface
changed in place" path, and the reaping branch in `runRenderLoop` is never reached until the surface is
destroyed or the activity stops. The `System_Toast` it queues is never delivered either, since only a
running frame loop or `NativeApp.init` processes the command queue.

## Commands to a destroyed activity are dropped

`postCommand` drops commands that arrive after `isDestroyed()`, failing only those with a request ID.
`registerCallbacks` re-sends the keep-screen-on state, but the soft keyboard `persist` state is lost, and
so is a `startRecording` that lands between activities (the game then waits on a silent mic) or a
`stopVideo`.

## The render loop can start while the activity is stopped

`startRenderLoopThread` checks for a surface and that the activity is current, not that it's started.
On devices that keep a stopped activity's surface, a `surfaceChanged` after `onStop` (resize, rotation,
the `setFixedSize` from `viewSizeChanged`) restarts emulation invisibly with audio paused and sensors
off. A `started` flag checked there would close it.

## Smaller ones

- **Achievements host override applied before the config loads:** `onCreate` calls
  `setAchievementsHostOverride` before the first `Initialize()`, so `ApplyAchievementsHostOverride` saves
  the default host and hardcore flag as the values to restore. Clearing the override then restores the
  defaults for that session.
- **Hardware keyboard characters above U+FFFF:** `dispatchKeyEvent` casts the unicode char to `char`,
  leaving a lone surrogate. The IME path in `NativeSurfaceView` handles code points.
- **Joins on the UI thread:** `NativeShutdown` (from `onDestroy`, also on a graphics restart) joins the
  web server thread with no timeout, and the UPnP thread, which with UPnP on can be several seconds into
  a discovery or a request to an unresponsive router. Leaving the UPnP thread to finish was tried and
  dropped: a thread still running at process exit uses globals during static destruction, and one left
  behind on Android races the next session's thread.
- **Second display removal timeout:** `removeOutput` returns after `Native_RemoveOutputAndWait`'s 3 s
  timeout even if the swapchain is still live, so a long frame can present to the Presentation's surface
  after `surfaceDestroyed` returned. The VkSurface holds its own window reference, so no use-after-free.
- **Moving the activity onto the TV display isn't re-checked:** with desktop windowing, nothing calls
  `updateExternalDisplay` when the activity moves to the display showing the Presentation.
- **`AndroidJNIThreadContext` doesn't nest:** the destructor detaches even when the thread was already
  attached. `Core/WebServer.cpp` nests it (`HandleFallback` inside `ForwardDebuggerRequest`); harmless only
  because nothing calls Java after the inner scope.
- **Unsynchronized reads:** `getSelectedCamera` reads `g_Config.sCameraDevice` on the UI thread while the
  settings screen can assign it; `display_scale_x/y` change on the UI thread a frame before
  `g_display.dpi_scale_*`, so touches in between use a mixed scale.
- **Unchecked OOM:** `pushCameraImageAndroid` doesn't null-check `GetByteArrayElements`, and
  `GetJavaString` leaves the exception pending when `GetStringChars` fails.
- **`inputbox` params:** split on `:@:`, so a title containing it corrupts the default text.

- **Denied permissions get no answer:** a denied location or microphone permission sends nothing to
  native, so the game's request just stays silent. Storage and local network do report denial.
- **Picker result after an activity recreate:** if the activity was destroyed and recreated while a
  picker proxy was open, check that the result still arrives through `onNewIntent`. (It should be
  queued for the recreated activity, but nobody has tested this with "Don't keep activities".)
