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

The version string in the app's UI used to lie as well: Ninja regenerated `git-version.cpp` but didn't
recompile it, so the APK could carry an older commit's string. That's fixed (the file is now declared
as a `BYPRODUCTS` of the generating step in `Core/CMakeLists.txt`), so on a build directory that has
been reconfigured since, the string matches the commit the APK was built from.

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

### LeakCanary and StrictMode

Debug builds set a StrictMode VM policy that logs leaked closables, unregistered receivers and leaked
activities under the tag `StrictMode`. Building with `-PleakCanary` also adds LeakCanary to the debug
build: it logs `Watching instance of ...` for each destroyed activity, and `Found N objects retained`
(then dumps and analyses the heap) only if one isn't collected. No such line after a run of recreates
means nothing was retained. It adds a second launcher activity to the package, so start the app with
`adb shell am start -n org.ppsspp.ppsspp/.PpssppActivity` rather than `monkey`, which can pick
LeakCanary's.

On a device where `persist.log.tag` is `S`, enable the tags first:
`adb shell setprop log.tag.LeakCanary V`, and the same for `StrictMode`.

## logcat on a noisy device

Filter to the process, since the tag alone is not enough:

    adb logcat -d --pid=$(adb shell pidof org.ppsspp.ppsspp)

On a device with a chatty system - a Quest especially - the ring buffer can rotate the app's lines
away within seconds, which reads as "logging stopped" rather than "logging was lost". Start a stream
*before* launching the app instead:

    adb logcat -c && adb logcat -s PPSSPP:V PpssppActivity:V   # in the background, then start the app

## Starting the app with options

    python Tools/android_run.py --build --start-screen=memstick   # build, install, open the memory stick screen
    python Tools/android_run.py --log --start-screen=firmware     # and follow the app's log

It force-stops the app first, since options are only read at startup, and passes them in the
`org.ppsspp.ppsspp.Args` intent extra. (`memstick` is the memory stick screen as opened from the
settings, `memsticksetup` the first-boot one.) It also sets `log.tag.PPSSPP` to `V`: some devices default every
tag to silent (`persist.log.tag` is `S`), and then logcat busily shows other apps but never a line of ours. The `--start-screen` names are in `NativeInit()` in `UI/NativeApp.cpp`.

## Running the C++ unit tests on a device

CI builds the Android unit test executable but doesn't run it. To run it on a connected device:

    python Tools/android_unittest.py --build        # build for arm64-v8a, push, run everything
    python Tools/android_unittest.py Path VFS       # just these, with the binary already built

It runs each test as its own process (a crash would otherwise swallow the buffered output of every test
before it), keeps each test's output in `android/unittest_logs/`, and prints the command to re-run only the
failures. Worth doing before a release, and after changing `Common/File` or anything with an
`#if PPSSPP_PLATFORM(ANDROID)` branch, which desktop unit tests compile out.

Two things to know when reading its results:

- The executable has no Java VM, so content URIs fail like missing files there. They're only exercised
  for real inside the app, by the storage tests below.
- From Git Bash, set `MSYS_NO_PATHCONV=1` before any `adb push`/`adb shell` of your own, or
  `/data/local/tmp` gets rewritten into a Windows path and the push silently lands somewhere else.

## Running the storage tests in the app

`Core/Util/StorageTest.cpp` exercises the file functions (create, overwrite, list, rename, move, copy,
delete, odd names, free space) and the memory stick move (rename, copy, undo, folder checks) in a scratch
folder, and times the calls the emulator makes most. The
`Storage` unit test runs them on a native folder; to run them on a picked folder, through the content
URI code:

    python Tools/android_storage_test.py --build    # build and install the normal debug APK, then run
    python Tools/android_storage_test.py            # with what's installed

That starts the app with `--start-screen=storagetest`. The first time, tap Choose folder on the device,
create a new empty folder in the picker, and choose it. It's remembered (in `storage_test_dir.txt` in
the app's internal data), so later runs need no hands and take seconds. Results go to logcat as
`StorageTest: PASS/FAIL ...` lines, ending with `StorageTest: DONE, N failed`. Run them in a folder on
an SD card too, if the device has one. Keep the screen on (Stay awake in the developer options) while
timing: a locked phone throttles the app, and the times double.

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
