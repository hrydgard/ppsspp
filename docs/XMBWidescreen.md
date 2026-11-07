# Experimental XMB widescreen layout

The PSP firmware screen has an opt-in widescreen setting for firmware 6.61. On
displays wider than the PSP's 480:272 image, it fills the primary output while
preserving the horizontal proportions of the shell's icons and text. Narrower
displays, VR and secondary game outputs use the existing display layout.

This is experimental. It has been checked against the stock 6.61 firmware's
menus, not every firmware resource, media player or custom theme. In particular,
some modal backdrops still cover only their original logical width. Android
device testing is needed before treating the option as generally supported.

## Firmware findings

These are emulator observations and disassembly findings, not PSP hardware test
results. The inspected `scePaf_Module` has CRC `70c3dbcf`, from the official PSP
6.61 updater unpacked for `02g`.

`PPSSPPHeadless --re-module flash0:/vsh/module/paf.prx --re-out re-paf`
identifies the scene setup and rendering functions. Offsets below are relative
to the module's allocation base, not host addresses or file offsets.

| Offset | Function |
| --- | --- |
| `+0x36d88` | `scePaf_82b37153`, which creates a page and its scene |
| `+0xef670` | Scene constructor |
| `+0xefa20` | `scePaf_7b77b7e7`, sets the scene's projection dimensions |
| `+0xefa54` | Scene rendering setup; analyzed size 904, hash `4d94d9f6c7f9cabd` |
| `+0xeff74` | Recalculates the scene's projection |

The scene stores logical width and height at `+0x340` and `+0x344`, followed by
the camera distance at `+0x348`. The projection matrix starts at `+0x3f0`.
The parent widget is at `+0x1d4`; its name string pointer is at parent `+4`.
These fields were also checked through the WebSocket debugger in a running XMB.

Increasing the logical width directly is insufficient: it also changes other
scene calculations and caused menu content to disappear in an experiment.
Changing only the projection keeps the existing viewport and widget geometry.

## Projection and presentation

The function replacement hook recognizes the scene rendering function by hash
and size. It validates the scene's dimensions and perspective matrix before
writing the two horizontal projection components. During games, the hook returns
before accessing scene data.

The correction is `min(1, (480 / 272) / outputAspect)`. The frontend presents the
XMB across the output and the projection correction cancels its horizontal
stretch for menu content. The camera/notch centering setting determines the
available output frame.

Menu pages use a left anchor. `page_indicator` and `page_btnnavi` use a right
anchor, while the opening logo, busy page and names containing `dialog` use a
center anchor.
The opening background (`page_bg`) and default and custom theme pages retain
their original projection to fill the background. This prevents a gap at the
right edge during the Sony Computer Entertainment opening; its `page_logo`
keeps the corrected width with a center anchor. The page classifications are a
limitation of this experimental approach; a complete responsive implementation
needs more resource coverage.

The hook keeps each scene's original and last-applied projection. Repeated
draws therefore do not accumulate scaling, and a projection the firmware
rebuilds becomes the new original. Replacement shutdown discards the cache.
No serialized state format changes; XMB savestates are already unavailable.

## Validation

Build through `Windows/PPSSPP.sln` and run `UnitTest.exe all` and
`python test.py -g --graphics=software` as described in `building.md`.

For visual checks, launch the real 6.61 XMB with the option enabled and compare
it with both the normal layout and Stretch. Keep the display height constant:
icons and letters should keep their original width/height proportions, the
background should reach both edges, and the clock/battery should stay at the
right edge. Check the saved-data browser and other dynamically created pages.
Repeat in 18:9, 20:9 and 21:9 on Vulkan and OpenGL, then resize/rotate and launch
a game from the shell. The game must use its own existing display settings.

Current local results and screenshots are described in the PR validation notes;
successful desktop captures do not substitute for testing an Android device.

## Android test APK

From a checkout with the Android SDK, NDK and Gradle JDK configured, build an
ARM64 debug APK with the test-only flag explicitly disabled:

```powershell
./gradlew.bat :android:assembleNormalDebug -Pandroid.injected.build.abi=arm64-v8a -Pandroid.injected.testOnly=false
```

On Linux or macOS, use `./gradlew` with the same arguments. Selecting an ABI
through `android.injected.build.abi` can otherwise mark the APK `testOnly=true`.
Such an APK requires `adb install -t` and cannot be installed by tapping the file
in Android's file manager. This is a packaging setting, independent of the XMB
runtime option.

The normal debug APK uses the normal PPSSPP application ID. To test alongside
an existing installation, save this as an untracked `xmb-test.gradle` file and
add `--init-script xmb-test.gradle` to the build command:

```groovy
allprojects { project ->
    project.plugins.withId('com.android.application') {
        project.android.buildTypes.getByName('debug').applicationIdSuffix = '.xmbwide'
    }
}
```

Find `android-normal-debug.apk` under `android/build/intermediates/apk/normal/debug/`.
Before sharing it, use the SDK build tools to check `apksigner verify --verbose`
and `aapt dump badging`: signatures must verify, the ABI must include
`arm64-v8a`, and `testOnly` must not be true. The corrected local APK was built
with these settings and passed these checks; installation on a device remains
part of validation.

Transfer the complete APK to the phone and open it with the file manager.
Allow installation from that app if Android requests it. In PPSSPP, configure
PSP firmware 6.61 in the firmware screen. With the XMB stopped, enable
**XMB widescreen (experimental)**, then start the XMB and compare the option on
and off in landscape orientation. A separate `.xmbwide` installation has its
own settings and needs its firmware configured separately.
