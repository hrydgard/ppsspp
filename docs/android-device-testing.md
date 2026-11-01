# Testing an Android build on a device

Notes from driving real devices over adb. Most of what follows is not about Android being hard, but
about ways the tooling quietly answers a question you did not ask - you measure the wrong build, or
measure garbage that has not been collected yet, and get a confident wrong answer.

## Install the build you think you are testing

`versionCode` is derived from the git commit count (see `gitVersionCode` in
`android/build.gradle.kts`), so a feature branch has a *lower* code than a longer branch. Installing
one over the other fails:

    adb: failed to install ...: Failure [INSTALL_FAILED_VERSION_DOWNGRADE]

`-r` does not permit downgrades; `-d` does. Use `adb install -r -d <apk>`.

Do not pipe the install through `tail -1` or `grep Success` and move on. The failure is a single
line and easy to swallow, and the device then keeps running whatever was there before - every
measurement afterwards describes the wrong binary, including A/B comparisons where both arms end up
being the same build and therefore agree with each other.

Verify after every install, not just when something looks odd:

    adb shell dumpsys package org.ppsspp.ppsspp | grep versionName

A second, independent check is worth it when a result is surprising: grep the log for a symbol that
exists only on the branch under test (a renamed class or log tag, say).

The version string itself can also lie. The Android build regenerates `git-version.cpp` but does not
always recompile it, so the APK can carry the previous build's string while containing current code.
When the version matters:

    find android/.cxx -name git-version.cpp.o -delete

## Forcing an activity recreate without touching the device

- `adb shell settings put system font_scale 1.15`, then back. `fontScale` is not in the activity's
  `configChanges`, so each change destroys and recreates the activity. Independent of orientation.
- `adb shell wm density <n>` also recreates - but only while `density` is absent from
  `configChanges`.
- `adb shell settings put system user_rotation 1` does **not** rotate PPSSPP. The activity follows
  the sensor, so this needs a physical rotation (or a cooperative device).

Put back whatever you changed: `font_scale`, `accelerometer_rotation`, `user_rotation`.

`wm density reset` is actively wrong on a device with a resolution override - a Galaxy S8 running
1080x2220 on a 1440x2960 panel resets to the *physical* panel's density and leaves the UI oversized.
Read the original with `wm density` first and set it back explicitly.

## Measuring retained activities

`adb shell dumpsys meminfo <pkg>` prints an Objects block with `Activities` and `AppContexts`.

The counts include objects that are garbage but not yet collected. Read it straight after N
recreates and you will see roughly N whether or not anything leaks, which looks exactly like a leak.
Force collection instead:

    adb shell am send-trim-memory org.ppsspp.ppsspp COMPLETE

then wait around eight seconds and read. Repeat until two readings agree - three or four rounds is
normal, and a single trim is not enough.

The protocol that gives an answer you can trust: cold start and read the baseline, do N recreates,
trim until stable, compare. A sound build returns to the baseline; a leaking one sits at baseline+N
and stays there.

Worked example, Galaxy S8 on Android 9, same device and protocol, both builds confirmed by
`versionName`. Before the activity handover fix: 13 recreates left 13 Activities and 18 AppContexts,
stable across repeated trims. After: the same 13 recreates settled back to 1 and 6.

A JNI global reference is a GC root, so an activity pinned by one shows up here. This is a usable
check for JNI ownership bugs, not only Java ones.

## logcat on a noisy device

Filter to the process, since the tag alone is not enough:

    adb logcat -d --pid=$(adb shell pidof org.ppsspp.ppsspp)

On a device with a chatty system - a Quest especially - the ring buffer can rotate the app's lines
away within seconds, which reads as "logging stopped" rather than "logging was lost". Start a stream
*before* launching the app instead:

    adb logcat -c && adb logcat -s PPSSPP:V PpssppActivity:V   # in the background, then start the app

## VR (Quest)

- The VR flavor is a separate package: `./gradlew :android:assembleVrDebug`, installing as
  `org.ppsspp.ppssppvr`. It can sit alongside the normal build.
- `adb exec-out screencap` returns solid black for an immersive app - it captures the 2D panel
  layer, which such an app does not have. Use the runtime's own line instead: a logcat entry like
  `VrApi: FPS=73/72 ... App=2.67ms` proves frames are being submitted and how much GPU time the app
  spent on them. Zero app time means the render loop is not producing.
- `am start` makes the activity `topResumedActivity`, but the shell keeps focus, so the app never
  gets a surface and the render loop does not restart. Re-entering an immersive app has to be done
  from inside the headset. A fresh `am start -S` after a `force-stop` does work.
- `debuggerd -b <tid>` needs root, so no native stacks from a retail headset.

## Check which paths the device actually exercises

Before concluding that a test covered something:

    adb shell grep -E '^(MemStickDirectory|GraphicsBackend)' /storage/emulated/0/PSP/SYSTEM/ppsspp.ini

A plain filesystem path for the memstick means the SAF/content-URI code
(`Common/File/AndroidStorage.cpp`, `ContentUri.java`) is never entered, so a run on that device says
nothing about it - a recents list full of plain paths says the same. The VR flavor reads
`ppssppvr.ini`, not `ppsspp.ini`.
