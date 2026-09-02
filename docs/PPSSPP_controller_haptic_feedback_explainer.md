# Bringing Controller Haptic Feedback to PSP Emulation

## A feature the original PSP did not provide

The original PlayStation Portable was designed with a directional pad, an analog pad, face buttons, shoulder buttons, START, SELECT, HOME, and other system controls. Sony’s official PSP product specification lists those controls and the system’s other hardware features, but it does not list a vibration motor, force-feedback system, or controller-rumble feature.[1] In practical terms, the PSP experience was built around visual and audio feedback rather than the controller vibration that many modern players expect from a console gamepad.

PPSSPP already has a separate **Haptic Feedback** setting for vibrating a phone or tablet when the player uses on-screen touch controls.[2] That is different from making a connected Bluetooth or USB game controller vibrate. The new work described here adds that missing bridge at the emulator level.

## What I was able to do

I was able to add a software-side controller-feedback feature to a PPSSPP source build and test it across different PSP games. The important idea is that the PSP games themselves do not need to know anything about modern rumble hardware. PPSSPP watches the controller input that it is already receiving and uses that moment to request vibration from the connected controller.

The result is a modern “click and vibrate” experience: when the feature is enabled and a supported controller button is pressed, the controller can vibrate; when the button is released, the vibration stops. This gives PSP games a type of tactile feedback that was not part of the original handheld hardware.

The feature is credited to **Xelamayain**, with the requested attribution:

> Haptic mod created by Iny@m@Førtune(Xelamayain)

## How it works, in everyday language

Think of PPSSPP as a translator between two worlds. A PSP game sends normal PSP button and control activity to the emulator. PPSSPP converts that activity into the picture, sound, and gameplay that appear on the modern device. The new feature adds one more translation: when PPSSPP notices a qualifying button press from a connected gamepad, it sends a vibration request back to that gamepad.

The feature has four simple parts:

| Part | What it does |
|---|---|
| A setting | Adds a switch called **“Vibrate controller on button press”** under the Controls settings. It is off by default so existing users are not surprised. |
| Input awareness | Notices when a connected controller button goes down and when it comes back up. |
| Platform support | Uses the normal vibration capability provided by the host device: SDL-based builds on desktop systems, XInput on Windows, and the Android controller-vibrator system on Android. |
| Safe stopping | Stops the vibration when the input is released, and ignores controllers that do not expose usable vibration hardware. |

In the recorded implementation, the input layer is the central point. That means the feature can work across different PSP games without adding separate rumble code to every game. The game does not have to be rewritten. PPSSPP supplies the extra behavior around the game.

The recorded patch focuses on ordinary controller buttons such as face buttons, shoulder buttons, START/SELECT-style buttons, triggers used as buttons, and stick-click buttons. D-pad presses and analog movement were deliberately treated differently in the patch, so the exact behavior can depend on the particular source revision and controller mapping. This is one reason testers should report which controller, platform, PPSSPP build, and game they used.

## Why this is interesting

This is not a claim that the PSP secretly had a disabled rumble feature. It did not have the same built-in haptic hardware as systems and controllers designed around vibration. Instead, the feature uses capabilities that exist in the modern device running PPSSPP and connects them to the emulator’s existing understanding of controller input.

That distinction matters. The original PSP game remains the original game. The new tactile response is an emulator enhancement, similar in spirit to playing PSP games at a higher resolution or using modern controller mapping. It changes the surrounding play experience without pretending that the original handheld contained hardware that it did not contain.

## How people can try it

A tester needs a PPSSPP build containing the modified source, a Bluetooth or USB controller with vibration support, and a PSP game that can be used for ordinary gameplay testing. After launching the modified build, open **Settings → Controls**, enable **“Vibrate controller on button press,”** connect the controller, and try several ordinary buttons in more than one game.

For useful testing, try a mixture of face buttons, shoulder buttons, triggers, and stick-click buttons. Check that vibration begins when expected and stops promptly when the button is released. Also try a controller without vibration support, disconnect and reconnect the controller, pause and resume the emulator, and test more than one game. These checks help separate a real implementation issue from a controller driver or platform limitation.

Please report the following when sharing results:

| Test detail | Example information to include |
|---|---|
| Platform | Windows, Linux, Steam Deck, Android, or another supported host |
| Controller | Exact model and whether it is connected by Bluetooth or USB |
| PPSSPP build | Build date, commit, or package version |
| Game | Game title and region/version if relevant |
| Result | Which buttons vibrated, whether release stopped vibration, and whether any delay or unwanted vibration occurred |

## A request to the community

Please try it and tell me what happens. Test it with the controllers and PSP games you already use. If it works well, share the platform and controller combination so other players can reproduce the result. If it does not work, that report is equally valuable: controller rumble support varies between operating systems, drivers, connection methods, and hardware.

The goal is straightforward: make PSP emulation feel more natural on modern gamepads while keeping the original games playable and leaving the new behavior optional. The PSP never shipped with native controller haptic feedback, but an emulator can add a carefully controlled version of that experience by connecting modern controller hardware to the input events PPSSPP already processes.

## References

[1]: https://sonyinteractive.com/en/press-releases/2004/sony-computer-entertainment-inc-announces-product-specifications-of-handheld-video-game-system-playstationportable-psp/ "Sony Interactive Entertainment: official PSP product specifications"

[2]: https://www.ppsspp.org/docs/settings/controls/ "PPSSPP official control settings documentation"

[3]: https://github.com/hrydgard/ppsspp "Official PPSSPP source repository"
