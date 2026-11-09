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

## Smaller ones

- **Denied permissions get no answer:** a denied location or microphone permission sends nothing to
  native, so the game's request just stays silent. Storage and local network do report denial.
- **Picker result after an activity recreate:** if the activity was destroyed and recreated while a
  picker proxy was open, check that the result still arrives through `onNewIntent`. (It should be
  queued for the recreated activity, but nobody has tested this with "Don't keep activities".)
