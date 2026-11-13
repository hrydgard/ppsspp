# PPSSPP Frontend UI — Translation Grounding Document

Purpose: grounding reference for human translators and AI translation of PPSSPP's UI strings
(source: `ppsspp/assets/lang/en_US.ini`). Each entry explains the real context and behavior of
a string as verified in the PPSSPP source code.

Status: DRAFT v0.4 — partial. Covered so far: [Controls], [Graphics], [PostShaders], [TextureShaders],
[Achievements], [System]. Remaining sections (Audio, Dialog, Networking, Error, Developer, ...) will be
appended in later revisions. Each covered section ends with its own "Needs Clarification" list.

Conventions:
- **Key** — the en_US.ini key; "English text" — the displayed English string.
- **Where** — screen/widget/trigger (with code file citations).
- **Meaning** — what the option/message actually does per the code (not literal wording).
- **Note** — translator notes / false-friend warnings.
- "Self-evident" = trivially understandable; "Skipped" = universal terms not needing guidance.
- "Needs Clarification" = keys that could not be grounded in code — answers welcome.

---
# Translation Grounding — [Controls] (en_US.ini lines 143–259)

Scope: PPSSPP frontend "Controls" strings only. Every claim verified against source in `ppsspp/`.
Screens involved: Settings → Controls (`UI/Settings/ControlsPage.cpp`), Calibrate analog stick (`UI/ControlMappingScreen.cpp` `AnalogCalibrationScreen`), Tilt control setup (`UI/TiltAnalogSettingsScreen.cpp`), Gesture mapping (`UI/GameSettingsScreen.cpp` `GestureMappingScreen`), Edit touch control layout (`UI/TouchControlLayoutScreen.cpp`), Touch Control Visibility (`UI/TouchControlVisibilityScreen.cpp`), custom touch button setup (`UI/CustomButtonMappingScreen.cpp`). Core input math: `Core/ControlMapper.cpp`, `Core/TiltEventProcessor.cpp`, `UI/GamepadEmu.cpp`, `Core/HLE/sceCtrl.cpp`.

---

## [Controls]

### 1. Analog stick processing — "Calibrate analog stick" screen

Opened from Settings → Controls. These affect real analog sticks (controllers) globally. Math in `Core/ControlMapper.cpp` (`MapAxisValue`, `ConvertAnalogStick`).

- **`Deadzone radius`** — "Deadzone radius"
  - Where: Calibrate analog stick screen → "Analog Settings" group (`UI/ControlMappingScreen.cpp`); also Tilt control setup (same label, `g_Config.fTiltAnalogDeadzoneRadius`, `UI/TiltAnalogSettingsScreen.cpp`).
  - Meaning: inner dead zone around stick center. `MapAxisValue` stage 1 (`Core/ControlMapper.cpp`): deflection below this value gives **zero** output; above it, output is rescaled to fill 0–1: `(absV − deadzone)/(1 − deadzone)`. Slider 0–0.5 of full deflection, default 0.15, unit "/ 1.0".
  - Note: "Radius" = stick-deflection threshold, not pixels. On the tilt screen the same English string governs tilt input instead.

- **`Low end radius`** — "Low end radius"
  - Where: same screens (`fAnalogInverseDeadzone` / `fTiltInverseDeadzone`).
  - Meaning: minimum output produced by any non-zero (post-deadzone) input — lifts tiny movements off zero. `MapAxisValue` stage 2: `absV = invDeadzone + absV × (sensitivity − invDeadzone)`, clamped 0–1. Default 0 (off). E.g. at 0.2 the smallest possible non-zero output is 0.2.
  - Note: It *removes* the sluggish near-zero output band (compensates for games that ignore small values) — the opposite of `Deadzone radius`. Don't translate as "inner deadzone".

- **`Sensitivity (scale)`** — "Sensitivity (scale)" (code fallback: `co->T("Sensitivity (scale)", "Sensitivity")`)
  - Where: Calibrate analog stick screen (`fAnalogSensitivity`, slider 0–2, default 1.1, unit "x").
  - Meaning: multiplicative gain on stick output magnitude after the deadzone (stage 2 of `MapAxisValue`, see formula above). >1 reaches full output earlier; values <1 make full deflection never reach 1.0.
  - Note: `(scale)` = multiplicative factor. Distinct from the tilt/gesture settings simply named "Sensitivity".

- **`Circular stick input`** — "Circular stick input"
  - Where: Calibrate analog stick screen (`bAnalogIsCircular`, `UI/ControlMappingScreen.cpp`).
  - Meaning: legacy checkbox converting the stick's norm from square/max-norm to Euclidean before processing (`ConvertAnalogStick`, `Core/ControlMapper.cpp`: `max(|x|,|y|)` → `sqrt(x²+y²)`), so diagonal deflection reads as "further out", matching a circular gate. The checkbox is **greyed out** when `Deadzone shape` = "Circle" (redundant then; code comment says so).
  - Note: NOT the same as `Deadzone shape = Circle` — this changes the input norm, that changes the deadzone geometry. Keep the two "circle" concepts distinct.

- **`Deadzone shape`** — "Deadzone shape"
  - Where: Calibrate analog stick screen → "Advanced Deadzone Settings" (`iAnalogDeadzoneShape`; options `deadzoneShapes[] = {"Circle","Square","Cross"}` in `UI/ControlMappingScreen.cpp`).
  - Meaning: geometry of the stick's normalization/deadzone (Steam-Input-style). `ConvertAnalogStick`: **Circle** uses Euclidean norm; **Square** (default) uses max-norm (classic PPSSPP behavior); **Cross** uses max-norm and additionally enables `Axial anti-deadzone`.
  - Note: Here Circle/Square/Cross describe gate geometry — NOT the PSP face buttons ○/□/✕ (same words appear as button names elsewhere; context disambiguates).

- **`Axial anti-deadzone`** — "Axial anti-deadzone"
  - Where: Advanced Deadzone Settings (`fAnalogAxialDeadzone`, slider 0–0.5). **Only enabled when `Deadzone shape` = "Cross"** (`axialDZ->SetEnabledFunc`).
  - Meaning: per-axis output-stage remap (`ApplyAxialAntiDeadzone`, `Core/ControlMapper.cpp`): any non-zero axis value is remapped (0,1] → [antiDZ, 1], i.e. it *jumps past* the threshold instantly. Effect: the stick "skips" the sluggish zone near each cardinal axis, easing diagonals; exact cardinals (0.0) remain reachable.
  - Note: "Anti-deadzone" = shrinks the effective near-zero output zone; opposite of `Deadzone radius`. Unlike `Low end radius` (radial, uniform), this is per-axis and only active for Cross shape.

- **`Response curve`** — "Response curve"
  - Where: Advanced Deadzone Settings (`iAnalogResponseCurve`), options `responseCurves[] = {"Linear","Aggressive","Relaxed","Wide"}` (`UI/ControlMappingScreen.cpp`).
  - Meaning: output-vs-deflection curve, applied after deadzone+scale (`ApplyResponseCurve`, `Core/ControlMapper.cpp`):
    - **Linear**: output = input (1:1).
    - **Aggressive**: √input — high output from small deflection (fast response).
    - **Relaxed**: input² — more range for fine/slow movement.
    - **Wide**: input³ — even more low-end precision.
  - Note: "Relaxed"/"Wide" = slow/sensitive low end (precision near center); "Aggressive" = fast response — not "violent".

- **`Linear`** / **`Aggressive`** / **`Relaxed`** / **`Wide`** — the four `Response curve` choices (`responseCurves[]`, `UI/ControlMappingScreen.cpp`; translated in the CONTROLS category via `PopupMultiChoice`). See above for behavior.

- **`Circle`** / **`Square`** / **`Cross`** — the three `Deadzone shape` choices (`deadzoneShapes[]`, `UI/ControlMappingScreen.cpp`). Gate/deadzone geometry, not PSP buttons.

- **`Auto-rotation speed`** — "Auto-rotation speed"
  - Where: Calibrate analog stick screen (`fAnalogAutoRotSpeed`, slider 0.1–20, default 8).
  - Meaning: rotation speed of the virtual keys **"Rotate Analog (CW)"/"Rotate Analog (CCW)"** (`VIRTKEY_ANALOG_ROTATE_CW/CCW` in `Core/KeyMap.cpp`), which spin the left analog stick in a circle while held. `ControlMapper::UpdateAutoMovements` computes `cos/sin(now × fAnalogAutoRotSpeed)` — radians/second. For games requiring continuous stick rotation.
  - Note: Not screen auto-rotation!

- **`Analog Settings`** — "Analog Settings"
  - Where: group header on the Calibrate analog stick screen (`UI/ControlMappingScreen.cpp`).

- **`Advanced Deadzone Settings`** — "Advanced Deadzone Settings"
  - Where: group header on the Calibrate analog stick screen (Deadzone shape / Axial anti-deadzone / Response curve).

- **`Reset to defaults`** — "Reset to defaults"
  - Where: button on the Calibrate analog stick screen (`OnResetToDefaults`, `UI/ControlMappingScreen.cpp`) — restores the analog defaults (deadzone 0.15, low end 0, sensitivity 1.1, shape Square, etc.).

- **`Calibrate analog stick`** — "Calibrate analog stick"
  - Where: entry under Settings → Controls (`UI/Settings/ControlsPage.cpp`) and title of `AnalogCalibrationScreen` (`UI/ControlMappingScreen.cpp` `GetTitle`).

- **`Calibrated`** — "Calibrated"
  - Where: label of the left stick visualization on the Calibrate analog stick screen (`JoystickHistoryView`, `StickHistoryViewType::OUTPUT`, `CreateContentViews`) — stick position *after* deadzone/sensitivity processing (what the game receives).

- **`Raw input`** — "Raw input"
  - Where: label of the right stick visualization (`StickHistoryViewType::INPUT`) — unprocessed stick position as the device reports it. Compare with "Calibrated" to see your settings' effect.
  - Note: "raw" = unprocessed, not Windows Raw Input.

- **`Calibration`** — "Calibration"
  - Where: group header on the Tilt control setup screen (`UI/TiltAnalogSettingsScreen.cpp`), heading the Calibrate button.

- **`Calibrate`** — "Calibrate"
  - Where: button in the tilt Calibration group (`UI/TiltAnalogSettingsScreen.cpp` `OnCalibrate`) — stores the current device tilt angle as the neutral angle (`fTiltBaseAngleY`).

- **`To Calibrate`** — "Hold device at your preferred angle and press \"Calibrate\"."
  - Where: hint under the Calibrate button (`calibrate->SetHintText(co->T("To Calibrate", ...))`, `UI/TiltAnalogSettingsScreen.cpp`).
  - Meaning: hold the device at the resting angle you want as "neutral", then tap Calibrate; that angle becomes the center.
  - Note: Key is short but the text is a full instruction sentence. Keep the quoted button name consistent with the `Calibrate` button label.

### 2. Analog limiter, rapid fire, analog-to-digital (Settings → Controls → "Keyboard control settings")

- **`Analog Limiter`** — "Analog limiter"
  - Where: Settings → Controls → Keyboard section (`fAnalogLimiterDeadzone`, range 0–1, default 0.6, unit "/ 1.0", `Core/Config.cpp`).
  - Meaning: scale factor applied to the analog stick output **while the "Analog limiter" virtual key is held** (`VIRTKEY_ANALOG_LIGHTLY`, `ControlMapper::UpdateAnalogOutput` multiplies x,y by this value). Limits stick deflection — e.g. for careful walking/aiming. Also labeled in the mapping screen as "Analog limiter" (virtual key name, `Core/KeyMap.cpp`).

- **`AnalogLimiter Tip`** — "When the analog limiter button is pressed"
  - Where: hint text under the Analog limiter setting (`ControlsPage.cpp` `.Hint({I18NCat::CONTROLS, "AnalogLimiter Tip", ...})`).
  - Note: sentence fragment explaining when the scale applies. "Analog limiter button" = the key you mapped the "Analog limiter" virtual key to.

- **`Analog to digital mapping`** — "Analog to digital mapping"
  - Where: section header in Settings → Controls (`ControlsPage.cpp`), above the two threshold settings below. It covers converting analog axis motion into digital button presses (when you map an axis to a PSP button in control mapping).

- **`Analog trigger threshold`** — "Analog trigger threshold"
  - Where: `fAnalogTriggerThreshold`, range 0.02–0.98, default 0.75 (`Core/Config.cpp`). Used in `ControlMapper::GetDeviceAxisThreshold`.
  - Meaning: how far an analog **trigger** axis must be pushed to count as "pressed" when it's mapped to a digital button (fraction of full travel).

- **`Analog stick threshold`** — "Analog stick threshold"
  - Where: `fAnalogStickThreshold`, range 0.2–0.98, default 0.75. Same function.
  - Meaning: how far an analog **stick** axis must be pushed to count as "pressed" when mapped to a digital button. `GetDeviceAxisThreshold` lowers this threshold by up to 35% when the perpendicular axis is already active ("co-axis" bias, `Core/ControlMapper.cpp`), to make diagonals easier.

- **`Rapid fire interval`** — "Rapid fire interval"
  - Where: Keyboard section (`iRapidFireInterval`, range 1–10, default 5, unit "frames", `Core/Config.cpp`).
  - Meaning: half-period of the "RapidFire" virtual key (`VIRTKEY_RAPID_FIRE`, `Core/KeyMap.cpp`). While that key is held, the emulator blinks held buttons: every N vblanks (`__CtrlVblank`, `Core/HLE/sceCtrl.cpp`) it toggles between passing buttons through and masking them off (D-pad and analog are exempt, `CTRL_EMU_RAPIDFIRE_MASK`). In games this produces automatic rapid presses.

- **`frames`** — "frames"
  - Where: unit suffix of **Rapid fire interval** (`Config.cpp` `.Units("frames")`) — video frames (vblanks), not input frames.

- **`Enable standard shortcut keys`** — "Enable standard shortcut keys"
  - Where: `bSystemControls`, desktop Windows/Linux only (`ControlsPage.cpp`).
  - Meaning: enables PPSSPP's built-in keyboard shortcuts. Windows: the standard accelerator table (`Windows/main.cpp`, `hAccelTable`); SDL: a convenience subset such as Ctrl+W to stop the game (`SDL/SDLMain.cpp`).

- **`Ignore Windows Key`** — "Disable Windows key"
  - Where: `bIgnoreWindowsKey`, Windows non-UWP only (`Core/Config.cpp` label; toggle also in Windows menu `Windows/MainWindowMenu.cpp`).
  - Meaning: when on, registers raw input with `RIDEV_NOHOTKEYS` (`Windows/RawInput.cpp`) so pressing the Windows key doesn't pop the Start menu/switch out while playing.
  - Note: the key says "Ignore" but the display text says "Disable Windows key" — the setting disables Win-key *hotkey behavior* while PPSSPP has input focus, not the key itself.

- **`Ignore gamepads when not focused`** — "Ignore gamepads when not focused"
  - Where: `bGamepadOnlyFocused`, Windows only (`Windows/InputDevice.cpp`: gamepads are polled only when the window is focused unless this is off).

- **`Control input sources`** — "Control input sources"
  - Where: Windows-only section header in Settings → Controls (`ControlsPage.cpp`), above the HID/XInput/DirectInput enable toggles.

- **`HID input`** — "HID input"
  - Where: `bAllowHIDInput`, Windows only. Toggles the Windows HID gamepad backend (`Windows/Hid/HidInputDevice.cpp` drops input when disabled). Siblings (not in this en_US section): "XInput" (`bAllowXInput`), "DirectInput" (`bAllowDInput`) — the other two Windows gamepad APIs; toggles only affect whether each backend sends input.

- **`HapticFeedback`** — "Haptic feedback (vibration)"
  - Where: `bHapticFeedback`, mobile/VR (`ControlsPage.cpp`, `Core/Config.cpp`). Vibration on touch-control presses (`UI/GamepadEmu.cpp` `System_Vibrate` on button/D-pad press).
  - Note: key is camel-case English; display text is what users see.

- **`Game controller connected`** — "Game controller connected"
  - Where: on-screen toast "Game controller connected: <name>" when a gamepad is plugged in (`Core/KeyMap.cpp`, `g_OSD.Show(... ApplySafeSubstitutions("%1: %2", ...))`).

- **`Game controller disconnected`** — "Game controller disconnected"
  - Where: same toast on unplug (`Core/KeyMap.cpp`).

- **`Control mapping`** — "Control mapping"
  - Where: first entry of Settings → Controls (`ControlsPage.cpp`), opens `ControlMappingScreen` where keys/buttons are bound to PSP controls.

### 3. Tilt control (Settings → Controls → "Tilt control setup")

Tilt = driving controls by tilting the phone/tablet. Screen: `UI/TiltAnalogSettingsScreen.cpp`; math: `Core/TiltEventProcessor.cpp` (`ProcessTilt`).

- **`Tilt control`** — "Tilt control"
  - Where: on-screen toast "Tilt control: Enabled" when a game starts while tilt input is active (`UI/EmuScreen.cpp`).
- **`Tilt control setup`** — "Tilt control setup"
  - Where: entry in Settings → Controls (shows current tilt type or "Disabled"; only on devices with an accelerometer, `ControlsPage.cpp`) and title of the tilt settings screen.
- **`Tilt Input Type`** — "Tilt input type"
  - Where: `iTiltInputType` popup (`UI/TiltAnalogSettingsScreen.cpp`; choices `g_tiltTypes`). Selects what the tilt drives (see the five type entries below). Changing it resets tilt state (`TiltEventProcessor::ResetTiltEvents`).
- **`None (Disabled)`** — "None (disabled)"
  - Where: choice 0 of **Tilt Input Type** (`g_tiltTypes` in `UI/TiltAnalogSettingsScreen.cpp`). Legacy "off" value; hidden once the separate Enabled checkbox exists.
- **`Analog Stick`** — "Analog stick" (en_US display; source string is "Analog Stick")
  - Where: choice 1 of Tilt Input Type — tilt drives the **left** PSP analog stick (`TILT_ANALOG`, `GenerateAnalogStickEvent`). Also a group header in the Gesture mapping screen ("Analog Stick" group = analog-stick gesture settings).
- **`D-PAD`** — "D-Pad"
  - Where: choice 2 of Tilt Input Type — tilt presses D-pad directions digitally (threshold 0.5 on the scaled tilt, `ProcessTilt` → `GenerateDPadEvent`).
- **`PSP Action Buttons`** — "PSP action buttons"
  - Where: choice 3 of Tilt Input Type — tilt presses face buttons: left/right → Square/Circle, down/up → Cross/Triangle (`GenerateActionButtonEvent`).
- **`L/R Trigger Buttons`** — "L/R trigger buttons"
  - Where: choice 4 of Tilt Input Type — tilt presses shoulder buttons: tilt up → both L+R, left → L only, right → R only (`GenerateTriggerButtonEvent`).
- **`Right Analog Stick`** — "Right Analog Stick"
  - Where: choice 5 of Tilt Input Type — tilt drives the **right** PSP analog stick (`TILT_ANALOG_RIGHT`). (Same English text is the title of the right-analog mapping screen, but that lookup is in the MAPPABLECONTROLS category.)
- **`Use gyroscope`** — "Use gyroscope"
  - Where: checkbox in the tilt setup group (only if the device has a gyro, `bTiltUseGyro`).
  - Meaning: fuse gyroscope with the accelerometer to estimate gravity (`TiltEventProcessor::ProcessGyro`) instead of using the accelerometer alone — much steadier when the device shakes or moves linearly.
- **`Sensitivity`** — "Sensitivity"
  - Where: (1) group header above the tilt sensitivity sliders (`UI/TiltAnalogSettingsScreen.cpp`); (2) slider label for the analog-stick-gesture sensitivity, 0.01–5.0 (`UI/GameSettingsScreen.cpp` `GestureMappingScreen`); (3) fallback text of `Sensitivity (scale)`.
- **`Tilt Sensitivity along X axis`** / **`Tilt Sensitivity along Y axis`** — "Tilt sensitivity along X axis" / "Tilt sensitivity along Y axis"
  - Where: sliders 0–100% (default 60), `UI/TiltAnalogSettingsScreen.cpp`.
  - Meaning: per-axis gain on the tilt angle (`tiltX *= xSensitivity × 3` in `ProcessTilt`) applied before the deadzone. Higher = less physical tilt needed to reach full input. "X axis" = tilting the device left/right, "Y axis" = forward/back (pitch), roughly.
- **`Invert Axes`** — "Invert axes"
  - Where: group header above the two invert checkboxes (`UI/TiltAnalogSettingsScreen.cpp`).
- **`Invert Tilt along X axis`** / **`Invert Tilt along Y axis`** — "Invert tilt along X axis" / "Invert tilt along Y axis"
  - Where: checkboxes (`bInvertTiltX/Y`) — negate the tilt value on that axis (`ProcessTilt`: `tiltX = -tiltX`), flipping the direction.
- **`Deadzone radius`** (tilt) — see §1; here `fTiltAnalogDeadzoneRadius` (0–0.8) applied to tilt-derived analog output only (analog tilt types; `ApplyDeadzoneXY`).
- **`Low end radius`** (tilt) — see §1; here `fTiltInverseDeadzone`, applied via `ApplyInverseDeadzone` (per code comment: deliberately applied to both axes together to compensate for in-game deadzones).
- **`Circular deadzone`** — "Circular deadzone"
  - Where: tilt screen only (`bTiltCircularDeadzone`, default on). Applies the tilt deadzone/low-end-radius as a **circular (Euclidean) region** instead of per-axis (`ApplyDeadzoneXY`/`ApplyInverseDeadzone` in `Core/TiltEventProcessor.cpp`).
  - Note: tilt-only counterpart of the stick screen's "Circular stick input"; same concept, different screen.

### 4. Gesture control (Settings → Controls → "Gesture mapping")

Screen `GestureMappingScreen` (`UI/GameSettingsScreen.cpp`) with two tabs — left/right half of the screen ("Gesture: Left side / Right side"). Per-zone config `GestureControlConfig`. Runtime: `GestureGamepad` in `UI/GamepadEmu.cpp`.

- **`Gesture mapping`** — "Gesture mapping"
  - Where: entry in Settings → Controls (`ControlsPage.cpp`), opens `GestureMappingScreen`.
- **`Gesture`** — "Gesture"
  - Where: tab/top-bar title prefix "Gesture: Left side" / "Gesture: Right side" (`UI/GameSettingsScreen.cpp`).
- **`Enable gesture control`** — "Enable gesture control"
  - Where: per-zone master checkbox (`bGestureControlEnabled`). Without it, swipes do nothing (see `GestureGamepad::Update` early return).
- **`Swipe`** — "Swipe"
  - Where: group header over the swipe mappings + sensitivity/smoothing (`UI/GameSettingsScreen.cpp`).
  - Meaning: dragging a finger across the screen ("swiping") in a direction presses the mapped PSP button. Runtime: accumulated finger deltas with threshold, `GestureGamepad::Update` (swipe left/right/up/down each press their mapped key while the delta exceeds the threshold).
- **`Swipe sensitivity`** — "Swipe sensitivity"
  - Where: slider 0.01–2.0, default 1.0 (`fSwipeSensitivity`) — multiplies swipe distance before it's compared against the threshold (`GestureGamepad::Update`). Higher = smaller movements register as swipes.
- **`Swipe smoothing`** — "Swipe smoothing"
  - Where: slider 0–0.95, default 0.3 (`fSwipeSmoothing`) — per-frame decay factor of the accumulated swipe delta (`deltaX_ *= smoothing`). Higher = smoother/laggier (movement lingers); 0 = no smoothing (instant).
- **`Double tap`** — "Double tap"
  - Where: group header over the double-tap mapping (`UI/GameSettingsScreen.cpp`); also a trigger type in gesture config. Two taps within 0.3 s (`GestureGamepad::Touch`) press the mapped PSP button (held while the second tap is held).
- **`Enable analog stick gesture`** — "Enable analog stick gesture"
  - Where: checkbox in the "Analog Stick" group of the gesture screen (`bAnalogGesture`).
  - Meaning: dragging a finger (in that screen zone) drives the PSP **left** analog stick relative to where the finger first touched (`__CtrlSetAnalogXY(0, ...)` in `GestureGamepad::Touch`; re-centers to 0 on release). Sensitivity slider "Sensitivity" (0.01–5) scales it (`k = sensitivity × 0.02`).
- **`Keep first touched button pressed when dragging`** — "Keep first touched button pressed when dragging"
  - Where: `bTouchGliding` (`UI/Settings/ControlsPage.cpp`, label in `Core/Config.cpp`).
  - Meaning: when a finger that started on an on-screen button drags away, that button **stays pressed** (it becomes the pointer's "primary button", `MultiTouchButton::Touch`/`CanGlide` in `UI/GamepadEmu.cpp`), and buttons the finger sweeps over also get pressed. Off: leaving a button's area releases it immediately.

### 5. On-screen touch controls (Settings → Controls → "On-screen touch controls")

Touch gamepad overlay; behavior in `UI/GamepadEmu.cpp`.

- **`On-screen touch controls`** — "On-screen touch controls"
  - Where: master toggle `bShowTouchControls` (`Core/Config.cpp` label) — show/hide the whole touch gamepad.
- **`Edit touch control layout`** — "Edit touch control layout"
  - Where: Settings → Controls entry and Pause menu (`UI/PauseScreen.cpp`); opens `TouchControlLayoutScreen` (drag/resize the touch buttons; Move/Resize modes, snap-to-grid).
- **`Are you sure you want to reset the touch control layout?`** — "Are you sure you want to reset the touch control layout?"
  - Where: confirmation dialog when pressing Reset in the layout editor (`UI/TouchControlLayoutScreen.cpp`, `MessagePopupScreen` with "Reset"/"Cancel").
- **`Customize`** — "Customize"
  - Where: button in the layout editor's left column (`UI/TouchControlLayoutScreen.cpp`) — opens the **Touch Control Visibility** screen.
- **`Touch Control Visibility`** — "Touch control visibility"
  - Where: title of `TouchControlVisibilityScreen` (`UI/TouchControlVisibilityScreen.cpp`) — per-button show/hide checkboxes for the touch gamepad (Circle, Cross, … Custom 1–8, etc.; toggle names come from the MAPPABLECONTROLS category).
- **`Visible`** — "Visible"
  - Where: checkbox in the custom touch button setup (`UI/CustomButtonMappingScreen.cpp`) and in the right-analog style group (`UI/TouchControlVisibilityScreen.cpp` `RightAnalogMappingScreen`) — show/hide that single on-screen element.
- **`Button style`** — "Button style"
  - Where: `iTouchButtonStyle` (`Core/Config.cpp`), choices `touchButtonStyleNames[] = {"Classic","Thin borders","Glowing borders"}` — overall look of touch buttons.
- **`Classic`** — "Classic"
  - Where: button style 0 — filled/tan classic look (`GetButtonColor` returns 0xc0b080, filled images in `UI/GamepadEmu.cpp`).
- **`Thin borders`** — "Thin borders"
  - Where: button style 1 — white outline-only images (`I_ROUND_LINE` etc.).
- **`Glowing borders`** — "Glowing borders"
  - Where: button style 2 — outline style where a pressed button **glows brighter** (opacity ×1.35 instead of scaling up, `MultiTouchButton::Draw`).
- **`Button Opacity`** — "Buttons opacity"
  - Where: `iTouchButtonOpacity`, 0–100% (`Core/Config.cpp`) — opacity of all touch buttons (`opacity = iTouchButtonOpacity/100` in `UI/GamepadEmu.cpp`).
- **`Auto-hide buttons after delay`** — "Auto-hide buttons after delay"
  - Where: `iTouchButtonHideSeconds`, 0–300, default 8, unit "seconds", value 0 shows as "Off" (`ZeroLabel("Off")`).
  - Meaning: after this many seconds without any screen touch, the touch buttons fade out (`GamepadUpdateOpacity` in `UI/GamepadEmu.cpp`; fade transition ≤0.5 s) until the next touch.
- **`Sticky D-Pad`** — "Sticky D-Pad"
  - Where: `bStickyTouchDPad` (`UI/Settings/ControlsPage.cpp`), hint "Easier sweeping movements".
  - Meaning: normally the on-screen D-pad releases if the finger slides into the center dead zone or more than half the pad's size from center (`PSPDpad::ProcessTouch`, `UI/GamepadEmu.cpp`). With sticky on, the direction stays active while the finger keeps dragging anywhere — lets you sweep across the pad without releasing. "Sticky" = the direction sticks to your finger; not "sticky keys".
- **`Easier sweeping movements`** — "Easier sweeping movements"
  - Where: hint text under `Sticky D-Pad` (`ControlsPage.cpp` `.Hint("Easier sweeping movements")`).
- **`Auto-centering analog stick`** — "Auto-centering analog stick"
  - Where: `bAutoCenterTouchAnalog` (`UI/Settings/ControlsPage.cpp`).
  - Meaning: the touch analog stick's center jumps to wherever your finger first touches the stick area (`PSPAnalogStick::Touch`, `centerX_ = input.x`), instead of the drawn stick's fixed center. Lets you grab the stick anywhere; it recenters on release.
- **`Hide touch analog stick background circle`** — "Hide touch analog stick background circle"
  - Where: `bHideStickBackground` (per-orientation touch config, `Core/Config.cpp` label; drawing check in `UI/GamepadEmu.cpp`).
  - Meaning: hides the big circle graphic behind the touch analog stick (useful when enlarging the stick).
- **`Disable D-Pad diagonals (4-way touch)`** — "Disable D-Pad diagonals (4-way touch)"
  - Where: `bDisableDpadDiagonals` (`Core/Config.cpp`).
  - Meaning: forces the on-screen D-pad to register only the four cardinal directions (4-way); diagonals are never reported (`fourWay` in `PSPDpad::ProcessTouch` — without this, near-center touches are 4-way and further out are 8-way).

### 6. Custom touch buttons

Configured per custom button (Custom 1–8) via the visibility screen; `UI/CustomButtonMappingScreen.cpp`, runtime `CustomButton` in `UI/GamepadEmu.cpp`.

- **`Custom touch button setup`** — "Custom touch button setup"
  - Where: title of `CustomButtonMappingScreen`.
- **`Icon`** — "Icon"
  - Where: choice opening `ButtonIconScreen` — pick the icon drawn on the custom button (`UI/CustomButtonMappingScreen.cpp`).
- **`Shape`** — "Shape"
  - Where: choice opening `ButtonShapeScreen` — pick the button's outline shape (round/rect/shoulder etc.).
- **`Button Binding`** — "Button Binding"
  - Where: group header over the custom button's action settings (toggle/repeat + the list of PSP buttons it presses).
- **`Toggle mode`** — "Toggle mode"
  - Where: checkbox (`cfg->toggle`, `UI/CustomButtonMappingScreen.cpp`).
  - Meaning: the button **latches** — each tap toggles its buttons on/off instead of press-and-hold (`CustomButton::Touch`: `on_ = toggle_ ? !on_ : true`). Like Caps Lock vs. Shift.
- **`Repeat mode`** — "Repeat mode"
  - Where: checkbox (`cfg->repeat`).
  - Meaning: while the custom button is on, it automatically re-presses its buttons in a 10-frame cycle (5 frames down, 5 up — `CustomButton::Update`, `DOWN_FRAME = 5`; frames so it speeds up with fast-forward). Auto-fire turbo.
- **`Keep this button pressed when right analog is pressed`** — "Keep this button pressed when right analog is pressed"
  - Where: last popup of the right-analog binding list (`iRightAnalogPress`, `UI/TouchControlVisibilityScreen.cpp` `RightAnalogMappingScreen`, `PSPCustomStick::ProcessTouch` in `UI/GamepadEmu.cpp`).
  - Meaning: choose one PSP button that is held **the whole time the on-screen right analog stick is being touched/pressed** (`toggle(iRightAnalogPress, true)` while down). Only active with "Use custom right analog".
  - Note: "right analog is pressed" = the right analog stick is being touched/deflected, not "clicked".
- **`Analog Style`** — "Analog Style"
  - Where: group header in the right-analog mapping screen (Visible / Use custom right analog / Disable diagonal input).
- **`Analog Binding`** — "Analog Binding"
  - Where: group header in the right-analog mapping screen, over the four direction bindings + the pressed-stick binding.
- **`Use custom right analog`** — "Use custom right analog"
  - Where: `bRightAnalogCustom` (`RightAnalogMappingScreen`).
  - Meaning: turns the on-screen right stick into a **configurable button/axis stick**: each direction (pushed >50%) presses the PSP button you bound to it, or drives a bound analog axis proportionally (`PSPCustomStick::ProcessTouch`). Off: the right stick behaves as a normal analog stick (camera stick).
- **`Disable diagonal input`** — "Disable diagonal input"
  - Where: `bRightAnalogDisableDiagonal`, only with "Use custom right analog" (`RightAnalogMappingScreen`).
  - Meaning: diagonal pushes only activate the dominant axis's binding; the other axis reads 0 (both for button bindings via `diagCheck` and axis outputs via zeroing the minor axis, `PSPCustomStick::ProcessTouch`).
- **`Right Analog Stick`** — "Right Analog Stick"
  - Where: as CONTROLS key: choice 5 of Tilt Input Type (see tilt section). The identical-looking screen title "Right Analog Stick" of `RightAnalogMappingScreen` is looked up in the **MAPPABLECONTROLS** category instead.

### 7. Mouse (Settings → Controls → "Mouse settings")

Desktop only (Windows/SDL; Android 12+). Behavior: `MouseEventProcessor` in `Core/TiltEventProcessor.cpp`, wheel handling in `UI/NativeApp.cpp`.

- **`Mouse`** — "Mouse settings"
  - Where: section header (`ControlsPage.cpp` `Header({I18NCat::CONTROLS, "Mouse", "Mouse settings"})`).
- **`Use Mouse Control`** — "Use mouse control"
  - Where: `bMouseControl` (`Core/Config.cpp` label).
  - Meaning: enables using the mouse as a PSP control — mouse movement is converted to analog input (`MouseDeltaToAxes`) and mouse buttons/wheel can be bound in control mapping.
- **`MouseControl Tip`** — "You can map mouse inputs in the control mapping screen by pressing the 'M' icon."
  - Where: hint under Use Mouse Control (`ControlsPage.cpp`).
- **`Confine Mouse`** — "Trap mouse within window/display area"
  - Where: `bMouseConfine` (label key "Confine Mouse", en_US display text differs). Windows/SDL (not Android).
  - Meaning: captures/traps the mouse cursor inside the window while in game (or while mapping mouse input) — `SDL/SDLMain.cpp` `captureMouseCondition`; so the cursor can't leave the window during mouse-controlled play.
  - Note: key and display text differ; translate the display text ("Trap mouse within window/display area").
- **`Mouse sensitivity`** — "Mouse sensitivity"
  - Where: `fMouseSensitivity`, 0.01–1.0, default 0.1, unit "x" (`Core/Config.cpp`).
  - Meaning: scales mouse movement into analog deflection (`scaleFactor = dpi_scale × 0.1 × fMouseSensitivity` in `MouseDeltaToAxes`).
- **`Mouse smoothing`** — "Mouse smoothing"
  - Where: `fMouseSmoothing`, 0–0.95, default 0.9, unit "x" (`Core/Config.cpp`).
  - Meaning: exponential smoothing of mouse motion: accumulated deltas decay by this factor every ~4 ms simulated poll (`DecayMouse`, `Core/TiltEventProcessor.cpp`). Higher = smoother but laggier cursor-like motion; 0 = raw, no lingering.
- **`Mouse wheel button-release delay`** — "Mouse wheel button-release delay"
  - Where: `iMouseWheelUpDelayMs`, 10–300 ms, default 80 (`Core/Config.cpp`).
  - Meaning: mouse wheel ticks have no "release" event, so PPSSPP synthesizes one: the wheel "button" press is auto-released after this many milliseconds (`ProcessWheelRelease`, `UI/NativeApp.cpp`). Too low may make games miss the scroll; too high may register as a hold.
  - Note: applies to both wheel directions despite the config key saying "Up".

### 8. Screen rotation

- **`Screen Rotation`** — "Screen rotation"
  - Where: rotation picker `AddRotationPicker` (`UI/MiscViews.cpp`, label `co->T("Screen Rotation")`), used in Settings → System/General (`UI/Settings/GeneralPage.cpp`) and the display layout screen. Device screen orientation lock.
- **`Auto`** — "Auto"
  - Where: choice 0 of Screen Rotation (`screenRotation[]`, `UI/MiscViews.cpp`) — follow the device's orientation sensor (`ROTATION_AUTO`).
- **`Landscape`** — "Landscape"
  - Where: choice 1 of Screen Rotation (locked horizontal). Also a choice of the **PSP display rotation** popup in the display layout screen (`displayRotation[]`, `UI/DisplayLayoutScreen.cpp`, rotates the rendered game image) and the orientation caption in the touch layout editor.
- **`Portrait`** — "Portrait"
  - Where: choice 2 of Screen Rotation (locked vertical); same extra contexts as Landscape.
- **`Landscape Reversed`** — "Landscape reversed"
  - Where: choice 3 of Screen Rotation (landscape upside-down, `ROTATION_LOCKED_HORIZONTAL180`); also PSP display rotation choice.
- **`Portrait Reversed`** — "Portrait reversed"
  - Where: PSP display rotation choice 3 (`displayRotation[]`, `UI/DisplayLayoutScreen.cpp`) — portrait upside-down. In the Screen Rotation picker this slot is the string "N/A" and is hidden (`rot->HideChoice(ROTATION_LOCKED_VERTICAL180)`; deprecated — migrated to plain Portrait in `Core/Config.cpp`).
- **`Landscape Auto`** — "Landscape auto"
  - Where: choice 5 of Screen Rotation (`ROTATION_AUTO_HORIZONTAL`, `UI/MiscViews.cpp`) — automatic rotation restricted to the two landscape orientations (auto flip between normal and reversed landscape).
- **`N/A`** — "N/A" (source entry in `screenRotation[]`) — the hidden/deprecated Portrait Reversed slot of the Screen Rotation picker; users never see it (`HideChoice`).

### 9. Misc headers / dialog strings

- **`Keyboard`** — "Keyboard control settings"
  - Where: section header in Settings → Controls (`ControlsPage.cpp` `Header({I18NCat::CONTROLS, "Keyboard", ...})`) — above shortcut-keys/limiter/rapid-fire/mouse settings. Not a keyboard-mapping screen (that's "Control mapping").
- **`Tilt control setup`** entry shows current mode — see tilt section.

---

### Self-evident (no notes needed)

- `Off` — "Off" (also the zero-value label of "Auto-hide buttons after delay")
- `seconds` — "seconds" (unit of "Auto-hide buttons after delay")
- `Wide` — "Wide" (see Response curve choices — trivial once curve is understood)

### Skipped

- `X` — axis/direction label, universal
- `Y` — axis/direction label, universal

---

## Needs Clarification

Both keys below were checked again (2026-11-09): no code references found anywhere in the tree. The project owner is also unsure — treated as **likely legacy/unused, low priority**. Suggestion: translate them as generic nouns (`Binds` → "bindings", `Visibility` → "visibility") and move on; if they ever appear in some platform-specific UI, wording will still be reasonable.

- **`Binds`** — "Binds" — no reference to this key exists in the C++ sources (searched `T("Binds")` and literal `"Binds"` across UI/, Core/, Common/, Windows/, SDL/). Possibly legacy/unused. Low priority — generic noun "bindings" is safe.
- **`Visibility`** — "Visibility" — no `T("Visibility")` lookup found in the sources either. The related screen ("Touch Control Visibility") uses other keys. Possibly legacy/unused. Low priority — generic noun "visibility" is safe.
# Section 3: [Graphics], [PostShaders], [TextureShaders] — Translation Grounding

Verified against `ppsspp/` source. Key facts about how these strings surface:
- Setting labels/choices are declared in the metadata table `graphicsSettings[]` / `displayLayoutSettings[]` in `Core/Config.cpp` (`.Label(I18NCat::GRAPHICS, …)`, `.Choices(...)`, `.Units(...)`, `.ZeroLabel(...)`, `.Hint(...)`).
- They are rendered on the **Graphics settings page** (`UI/Settings/GraphicsPage.cpp`), the **Display layout & effects** editor (`UI/DisplayLayoutScreen.cpp`), the **Developer settings** (`UI/DeveloperSettingsPages.cpp`), the **Custom driver manager** (`UI/DriverManagerScreen.cpp`), etc.
- `SettingText` strings without an explicit category resolve in the page's category (`UI/SettingsPage.h`: "Texts without a category use the page's"); setting choices are translated in the same category as the setting label (`UI/SettingsPage.h`, `SettingItem`).
- Choice mapping is `name index i ↔ setting value = choiceMin + i` (`Core/ConfigSettings.h`, `Choices(names, minValue)`).

## [Graphics]

### Rendering & frame presentation

- **`Skip Buffer Effects`** — "Skip buffer effects"
  - Where: setting `g_Config.bSkipBufferEffects` (`Core/Config.cpp`, key `SkipBufferEffects`), shown on the **Speed hacks** subpage (`UI/Settings/GraphicsPage.cpp` `BuildSpeedHacksPage`), disabled during software rendering. Also a Windows menu toggle with OSD readout (`Windows/MainWindowMenu.cpp`).
  - Meaning: disables PSP-style buffered rendering. Without it (default), the game renders into offscreen framebuffers that are copied to the screen at each vblank. With it, drawing goes more directly to the output surface, which is faster but breaks anything that reads back or reuses the framebuffer (bloom/blur effects, some transitions; some games draw nothing). This is the old "Rendering Mode: Buffered rendering / Skip buffer effects" setting reduced to a checkbox — "buffered rendering" is now simply the default.
  - Note: while enabled, Rendering resolution, MSAA, post-processing shaders and PSP display rotation are disabled/hidden. Games flagged `RequireBufferedRendering` show the `BufferedRenderingRequired` warning.

- **`RenderingMode NonBuffered Tip`** — "Faster, but nothing may draw in some games"
  - Where: hint under `Skip Buffer Effects` (`UI/Settings/GraphicsPage.cpp`).
  - Meaning: warning that some games produce a blank screen with buffer effects skipped.
  - Note: the code's fallback English ("Faster, but graphics may be missing in some games") differs from the en_US.ini text; translate the ini text.

- **`Rendering Resolution`** — "Rendering resolution"
  - Where: setting `g_Config.iInternalResolution` (`Core/Config.cpp`, ini key `InternalResolution`), Graphics page under "Rendering"; disabled in software rendering and with "Skip buffer effects" on.
  - Meaning: how many times larger than the PSP's native 480×272 the emulated GPU renders. "Auto (1:1)" matches the output size; "1x PSP"–"10x PSP" are fixed multiples. Higher = sharper 3D, more GPU load.

- **`Internal Resolution`** — "Internal resolution"
  - Where: legacy label. The current label for `iInternalResolution` is `Rendering Resolution` (`Core/Config.cpp:958`); nothing in the UI looks this key up.
  - Note: same setting under an old name — do not treat as a second option.

- **`Auto (1:1)`** — first choice of Rendering Resolution (`internalResolutionNames`, `Core/Config.cpp`): pick the internal resolution closest to the output/window size.

- **`1x PSP` … `10x PSP`** — fixed multiples of 480×272 (`internalResolutionNames`; all ten values `1x PSP`, `2x PSP`, `3x PSP`, `4x PSP`, `5x PSP`, `6x PSP`, `7x PSP`, `8x PSP`, `9x PSP`, `10x PSP`).
  - Note: en_US display texts are inconsistent: most use "×" ("1× PSP", "2× PSP"…) but "4x PSP = 4x PSP (1080p)" and "8x PSP = 8x PSP (4K)" use "x" and append an approximate output size.

- **`Display Resolution (HW scaler)`** — "Display resolution (HW scaler)"
  - Where: setting `g_Config.iAndroidHwScale` (`Core/Config.cpp`, key `AndroidHwScale`), Android phones/tablets only (hidden on TV/VR; choices above the display's pixel count hidden — `UI/Settings/GraphicsPage.cpp`).
  - Meaning: size of the final output surface before the phone's hardware scaler enlarges it to the screen. Choices: `Native device resolution` (full screen pixels), `Same as Rendering resolution`, `1x PSP`…`5x PSP`. Smaller = fewer pixels rendered by PPSSPP, hardware scaler stretches (faster, blurrier).
  - Note: distinct from `Rendering Resolution` — that scales the emulated 3D render target; this scales the final composed image (Android only).

- **`Native device resolution` / `Same as Rendering resolution`** — choices of `Display Resolution (HW scaler)` (`androidHwScaleNames`).

- **`VSync`** — "VSync"
  - Where: setting `g_Config.bVSync` (`Core/Config.cpp`); shown only when the backend supports more than one present mode and an unsynchronized one (`UI/Settings/GraphicsPage.cpp`).
  - Meaning: locks presentation to the display refresh. Per the authoritative comment in `Core/Config.h`, the two checkboxes map to present modes: vsync off → **Immediate** (tearing, lowest latency); vsync on + "Low latency display" on → **Mailbox**; vsync on + "Low latency display" off → **FIFO**.

- **`Low latency display`** — "Low latency display"
  - Where: setting `g_Config.bLowLatencyPresent` (`Core/Config.cpp`); shown only when the backend supports Mailbox mode, and enabled only while `VSync` is on.
  - Meaning: with VSync on, present via mailbox (drop queued frames, show latest) instead of FIFO queue. Lower input lag at the cost of possible judder.

- **`Frame presentation mode`**, **`'Mailbox' (lower latency, recommended)`**, **`FIFO (higher latency, framerate stability)`**, **`Immediate (lower latency, tearing)`**, **`FIFO: latest ready`**, **`FIFO: relaxed`**
  - Where: **not referenced anywhere in the current code** (exhaustive literal search of cpp/h/mm/java/kt/cs/xaml/rc; only present in `assets/lang/*.ini`).
  - Meaning: legacy dropdown from older builds exposing the swapchain present mode directly. Today the VSync + "Low latency display" checkboxes select the mode (see `Core/Config.h` comment). Vulkan internally also supports `FIFO_RELAXED`/`FIFO_LATEST_READY` (`Common/GPU/Vulkan/thin3d_vulkan.cpp`) but the UI never names them; D3D11/GL/Metal report only `FIFO|IMMEDIATE` (`thin3d_d3d11.cpp`, `thin3d_gl.cpp`, `thin3d_metal.mm`).
  - Note: translate as descriptions of present modes, but they do not appear in current builds.

- **`Faster, input lag`** — "Faster, input lag"
  - Where: hint of `Buffer graphics commands` (`UI/Settings/GraphicsPage.cpp`, under the "Latency" header). Source comment: "TODO: This hint could use improvement."
  - Meaning: shorthand for the trade-off of `Buffer graphics commands`: less command buffering ("No buffer") reduces input lag; more buffering ("Up to 1/2") is needed for max speed/throughput.

- **`Buffer graphics commands`** — "Buffer graphics commands"
  - Where: setting `g_Config.iInflightFrames` (`Core/Config.cpp`, key `InflightFrames`), under the "Latency" header, only when the backend supports setting max frame latency; changing it offers a restart (`RestartOrDo`).
  - Meaning: how many frames of GPU commands the CPU may queue ahead (frames in flight). Values 1/2/3 = choices "No buffer"/"Up to 1"/"Up to 2" (`inflightFramesNames`, min value 1). "No buffer" also disables the multithreaded render thread on Vulkan (`Common/GPU/Vulkan/VulkanGraphicsContext.cpp`). History note: "reduce input lag when 'Buffer graphics commands' is set to off (#17831)".
  - Note: "No buffer" here is about queued GPU work, NOT rendering mode — don't confuse with "Skip buffer effects".

- **`No buffer` / `Up to 1` / `Up to 2`** — choices of `Buffer graphics commands` (`inflightFramesNames`): queue nothing / at most 1 / at most 2 extra frames of commands.

- **`Render duplicate frames to 60hz`** — "Render duplicate frames to 60 Hz"
  - Where: setting `g_Config.bRenderDuplicateFrames` (`Core/Config.cpp`, key `RenderDuplicateFrames`), Graphics page under "Display"; enabled only when "Skip buffer effects" is off and `Frame Skipping` = Off.
  - Meaning: when the game itself draws at less than 60 fps (e.g. 30 fps games), the last frame is re-presented on the skipped vblanks so the output updates at 60 Hz (`Core/HLE/sceDisplay.cpp` `__DisplayFlip`: `duplicateFrames` forces the flip even when the framebuffer isn't dirty). Smooths motion/judder on high-refresh displays.
  - Note: key spells "60hz", display text "60 Hz".

- **`RenderDuplicateFrames Tip`** — "Can make framerate smoother in games that run at lower framerates"
  - Where: hint of the setting above (`UI/Settings/GraphicsPage.cpp`).

### Framerate control & speed

- **`Frame Rate Control`** — "Framerate control"
  - Where: section header on the Graphics page (`page->Header("Frame Rate Control")`, `UI/Settings/GraphicsPage.cpp`).

- **`Alternative Speed`** — "Alternative speed (in %, 0 = unlimited)"
  - Where: setting `g_Config.iAltSpeedPercent1` (`Core/Config.cpp`, key `AltSpeedPercent`, slider 0–1000 step 5, unit "%").
  - Meaning: emulation speed (percent of the game's normal rate, converted to fps as percent×60/100 — `Core/Config.h` `AltSpeedPercentToFps`) used while speed preset 1 is active (`FPSLimit::CUSTOM1`). 0 = unlimited (fast-forward). A negative value (only from old configs) shows as `Disabled` and means the preset is skipped. A temporary "NN%" OSD with a fast-forward icon appears when active (`UI/EmuScreen.cpp` `ShowFpsLimitNotice`).
  - Note: the "Speed toggle" key (`VIRTKEY_SPEED_TOGGLE`) cycles Normal → speed 1 → speed 2 → Normal; there are also separate hold keys for each preset (`VIRTKEY_SPEED_CUSTOM1/2`, `UI/EmuScreen.cpp`).

- **`Alternative Speed 2`** — "Alternative speed 2 (in %, 0 = unlimited)"
  - Where: setting `g_Config.iAltSpeedPercent2` (`Core/Config.cpp`, key `AltSpeedPercent2`), same semantics, second speed preset (`FPSLimit::CUSTOM2`), reached by continuing to press the speed-toggle key or via its own dedicated key.

- **`Analog alternative speed`** — "Analog alternative speed"
  - Where: setting `g_Config.iAnalogAltSpeedPercent` (`Core/Config.cpp`, slider 1–1000), only shown when a control is mapped to "Analog speed" (`VIRTKEY_SPEED_ANALOG`, right trigger by default — `UI/Settings/GraphicsPage.cpp` `KeyMap::InputMappingsFromPspButton` check).
  - Meaning: target speed for analog-trigger speed control. The current fps limit ramps proportionally between 60 fps and this setting's rate as the trigger is pulled (`UI/EmuScreen.cpp:1165`). No 0/unlimited option (minimum 1).

- **`Unlimited`** — shown instead of "0" on the Alternative Speed sliders (`.ZeroLabel("Unlimited")`, `Core/Config.cpp`).
- **`Disabled`** — shown for negative values on the same sliders (`.NegativeLabel("Disabled")`), i.e. the feature is switched off (legacy values).

- **`Frame Skipping`** — "Frame skipping"
  - Where: setting `g_Config.iFrameSkip` (`Core/Config.cpp`, choices `Off`, `1`, `2`, `3`, `4`); also shown in the Windows frame-skip OSD (`Windows/MainWindowMenu.cpp` `setFrameSkipping`).
  - Meaning: fixed frameskip — render one frame, skip N frames (`Core/HLE/sceDisplay.cpp` `DoFrameTiming`). More skipping = higher emulation speed, choppier video. Disabled (greyed) while Auto frameskip is on.

- **`Auto FrameSkip`** — "Auto frameskip"
  - Where: setting `g_Config.bAutoFrameSkip` (`Core/Config.cpp`), Speed hacks subpage; also a macOS menu toggle (`SDL/CocoaBarItems.mm`).
  - Meaning: skip frames only when emulation falls behind schedule, to hold full speed (`Core/HLE/sceDisplay.cpp`: `UseAutoFrameSkip()` = auto && !skip-buffer-effects — silently inactive with "Skip buffer effects" on). The "Frame Skipping" number becomes the max skip count.

- **`Number of Frames`** — "Number of frames"
  - Where: **not referenced in current code** (repo-wide literal search). Legacy label from an older frame-skipping popup/slider. The current frame-skip control is the `Off/1/2/3/4` list whose numeric entries are deliberately untranslated (`PopupMultiChoice::SetChoicesUntranslated(1, …)` in `UI/Settings/GraphicsPage.cpp`).

- **`Framerate mode`** — "Framerate mode"
  - Where: setting `g_Config.iDisplayFramerateMode` (`Core/Config.cpp`, key `AndroidFramerateMode`) — **Android only, API ≥ 30**, shown on the **Developer** settings page (`UI/DeveloperSettingsPages.cpp:213`) although its label category is GRAPHICS. Changing it recreates the Activity.
  - Meaning: how PPSSPP asks the Android display to run at 60 Hz via `Surface.setFrameRate` (`android/src/org/ppsspp/ppsspp/PpssppActivity.java` `applyFrameRate`): `Default` = don't request anything; `Request 60 Hz` = compatibility-default request (OS may decline); `Force 60Hz` = `FRAME_RATE_COMPATIBILITY_FIXED_SOURCE`, plus `CHANGE_FRAME_RATE_ALWAYS` on Android 12+ (aggressive, forces the panel mode switch).
  - Note: `Default` / `Request 60 Hz` / `Force 60Hz` are its three choices (`androidFramerateModeNames`).

- **`Force 60 Hz`** (with space) — "Force 60 Hz"
  - Where: **not referenced in current code**. The live setting choice is `Force 60Hz` (no space) under `Framerate mode`. Do not confuse the two.

- **`Render all frames`** — "Render all frames"
  - Where: **not referenced in current code** (repo-wide search). Legacy setting/choice from an older build.

### Speed hacks

- **`Speed Hacks`** — "Speed hacks"
  - Where: link on the Graphics page opening the speed-hacks subpage (`UI/Settings/GraphicsPage.cpp`).
- **`SpeedHacksDescription`** — "These settings can improve performance on some devices, but they can also cause graphical issues or make rendering stop working entirely."
  - Where: description at the top of the speed-hacks subpage. The subpage contains: `Frame Skipping`, `Auto FrameSkip`, `Skip Buffer Effects`, `Skip GPU Readbacks`, `Lens flare occlusion` (`BuildSpeedHacksPage`).
- **`SpeedHacksHint`** — "These settings can cause rendering errors"
  - Where: hint shown on the "Speed Hacks" link itself.

### Texture upscaling (all under the "Texture upscaling" header)

- **`Texture upscaling`** — "Texture upscaling"
  - Where: section header on the Graphics page (`page->Header("Texture upscaling")`).

- **`CPU texture upscaler (slow)`** — en_US display: **"Upscale type"** (!)
  - Where: setting `g_Config.iTexScalingType` (`Core/Config.cpp`, key `TexScalingType`), disabled while hardware (GPU) texture scaling is active.
  - Meaning: which CPU-side algorithm enlarges game textures to reduce blockiness: `xBRZ` (edge-smoothing pixel-art scaler), `Hybrid` (mix of scaling and edge detection), `Bicubic` (smooth Mitchell bicubic), `Hybrid + Bicubic` (`GPU/Common/TextureScalerCommon.cpp` `ScaleTextures` switch).
  - Note: **misleading English source** — the key says "CPU texture upscaler (slow)" but the visible label is "Upscale type". The "slow" warning lives in the key, not in what users see.

- **`Upscale Level`** — "Upscale level"
  - Where: setting `g_Config.iTexScalingLevel` (`Core/Config.cpp`, key `TexScalingLevel`), disabled while hardware scaling is active.
  - Meaning: CPU texture upscale factor. Choices `Off`, `2x`, `3x` (mobile) / `Off`…`5x` (desktop); the value equals the scale factor, so "Off" = 1× (`TextureCacheCommon::NotifyConfigChanged`). Only textures (not the 3D output) are enlarged.
  - Note: `Off`/`2x`… are shared keys used by several settings.

- **`UpscaleLevel Tip`** — "CPU heavy - some scaling may be delayed to avoid stutter"
  - Where: hint under `Upscale Level` (`UI/Settings/GraphicsPage.cpp`).
  - Meaning: texture upscaling runs on the CPU, so it may be deferred to avoid frame hitches.

- **`GPU texture upscaler (fast)`** — "GPU texture upscaler (fast)"
  - Where: button on the Graphics page, **Vulkan and Metal only** (`UI/Settings/GraphicsPage.cpp` `UsingHardwareTextureScaling`), disabled in software rendering. Opens the `TextureShaderScreen` picker (`UI/MiscScreens.cpp`) listing the [TextureShaders] entries; picking a shader sets `bTexHardwareScaling` (`TextureShaderName != "Off"`).
  - Meaning: upscales textures on the GPU with compute shaders when they're loaded (fast, no CPU cost; options in [TextureShaders]).

- **`Deposterize`** — "Deposterize"
  - Where: setting `g_Config.bTexDeposterize` (`Core/Config.cpp`, key `TexDeposterize`), disabled while hardware scaling is active.
  - Meaning: pre-pass before CPU upscaling that smooths the hard color bands in gradient textures so upscaling doesn't turn them into visible steps (`TextureScalerCommon::DePosterize`).
- **`Deposterize Tip`** — "Fixes visual banding glitches in upscaled textures" (hint of the setting).

### Texture filtering (header: "Texture Filtering")

- **`Texture Filtering`** — "Texture filtering"
  - Where: section header on the Graphics page (above Anisotropic filtering / Texture filtering / Smart 2D texture filtering). (Also used by desktop menus via the `DesktopUI` category.)
  - Note: its display text is identical to the setting `Texture Filter` below — two different keys, same English.

- **`Texture Filter`** — "Texture filtering"
  - Where: setting `g_Config.iTexFiltering` (`Core/Config.cpp`, key `TextureFiltering`), disabled in software rendering.
  - Meaning: how textures are sampled. Choices (values 1–4, `texFilteringNames`, `Core/ConfigValues.h` `TEX_FILTER_*`): `Auto` = filter per texture as the game intends (mipmaps/linear where used), `Nearest` = always nearest (crisp pixels), `Linear` = always linear (smooth), `Auto Max Quality` = auto, but always picks the highest-quality filtering for each texture (forced high-quality filter mode in `GPU/Common/TextureCacheCommon.cpp`).
  - Note: distinct from `Screen Scaling Filter` (below), which filters the final scaled image, not texture sampling.

- **`Auto` / `Nearest` / `Linear` / `Auto Max Quality`** — choices of `Texture Filter` (`texFilteringNames`). `Nearest`/`Linear` are also the two choices of `Screen Scaling Filter`.

- **`Smart 2D texture filtering`** — "Smart 2D texture filtering"
  - Where: setting `g_Config.bSmart2DTexFiltering` (`Core/Config.cpp`, key `Smart2DTexFiltering`), disabled in software rendering.
  - Meaning: tries to detect pixel-perfect 2D draws (flat sprites mapped 1:1 to pixels, `GPU/Common/SoftwareTransformCommon.cpp` `lookForPixelMapping`/`pixelMapped`) so 2D elements aren't blurred or made to shimmer by texture filtering; skipped for video textures.

- **`Anisotropic Filtering`** — "Anisotropic filtering"
  - Where: setting `g_Config.iAnisotropyLevel` (`Core/Config.cpp`, choices `Off`, `2x`, `4x`, `8x`, `16x`; default is **16x**), disabled in software rendering.
  - Meaning: sharpens textures seen at steep angles (floors, roads). Harmless where unsupported (comment in `Core/Config.cpp` notes most mobile GPUs don't support it).

- **`Antialiasing (MSAA)`** — "Antialiasing (MSAA)"
  - Where: setting `g_Config.iMultiSampleLevel` (`Core/Config.cpp`, choices `Off`, `2x`, `4x`, `8x`, `16x`; sample count = 1 << value), only shown when the device supports MSAA; disabled in software rendering and with "Skip buffer effects"; unsupported levels hidden, and a warning icon is shown on tiling (mobile) GPUs where it is expensive (`UI/Settings/GraphicsPage.cpp`).
  - Meaning: multisample antialiasing of geometry edges in the 3D render.

- **`Lower resolution for effects`** — "Lower resolution for effects"
  - Where: setting `g_Config.iBloomHack` (`Core/Config.cpp`, key `BloomHack`), disabled in software rendering; choices `Off`, `Safe`, `Balanced`, `Aggressive`.
  - Meaning: forces small auxiliary framebuffers (used by in-game bloom/blur effects) to render at 1× PSP resolution instead of the Rendering Resolution (`GPU/Common/FramebufferManagerCommon.cpp` `bloomHack_`): `Safe` only for buffers ≤128×64, `Balanced` ≤256×128, `Aggressive` for anything that isn't roughly a full-screen buffer. Keeps bloom effects from shimmering/artifacting at high resolutions.
- **`ReducesBloomArtifactsHint`** — "Reduces artifacts in visual effects like blooms at higher resolutions." (hint of the setting, `UI/Settings/GraphicsPage.cpp`).

### GPU readbacks

- **`Skip GPU Readbacks`** — "Skip GPU Readbacks"
  - Where: setting `g_Config.iSkipGPUReadbackMode` (`Core/Config.cpp`, key `SkipGPUReadbackMode`), Speed hacks subpage, disabled in software rendering.
  - Meaning: controls what happens when the game reads the GPU framebuffer back into PSP RAM (block transfers / memcpy "downloads", `GPU/Common/FramebufferManagerCommon.cpp`): choice `No` (value 0, `NO_SKIP`, default) = do the slow GPU→CPU readback so the game sees the data; `Skip` (1, `SKIP`) = don't transfer at all (fastest; breaks effects that need the data); `Copy to texture` (2, `COPY_TO_TEXTURE`) = keep the data on the GPU and satisfy copies texture→texture where possible (compromise).
  - Note: choice order is `No`, `Skip`, `Copy to texture` (`skipGPUReadbackModeNames`) — "Skip" here is a readback choice, not frameskip.

- **`BlockTransferRequired`** — "Warning: This game requires Skip GPU Readbacks to be set to No."
  - Where: OSD warning at game boot (`Core/System.cpp` `ShowCompatWarnings`) when the game's compat flag `RequireBlockTransfer` is set and the mode isn't `No`. Auto-cancels when resolved.

- **`GPUReadbackRequired`** — "Warning: This game requires \"Skip GPU Readbacks\" to be set to Off."
  - Where: **not referenced in current code** — legacy warning text from an earlier version of the same check (today `BlockTransferRequired` is the only such warning, and it says "set to No" matching the current choice name).

### Rendering pipeline / platform

- **`Software Rendering`** — "Software rendering (slow, accurate)"
  - Where: setting `g_Config.bSoftwareRendering` (`Core/Config.cpp`, key `SoftwareRenderer`; code default label "Software Rendering (slow)", overridden by ini), Graphics page — only togglable while no game is running (`!PSP_IsInited`); also a macOS menu toggle.
  - Meaning: renders the PSP GPU on the CPU (`GPU/Software`) — accurate but slow. While on, most hardware settings are disabled and "Skip buffer effects" is inert.

- **`Hardware Transform`** — "Hardware transform"
  - Where: setting `g_Config.bHardwareTransform` (`Core/Config.cpp`, key `HardwareTransformDebug`), **Developer settings** page (`UI/DeveloperSettingsPages.cpp`).
  - Meaning: do vertex transformations on the GPU (fast path) instead of the CPU. On OpenGL, if the vertex shader fails to compile, PPSSPP falls back to software transform automatically.

- **`hardware transform error - falling back to software`** — "Hardware transform error, falling back to software"
  - Where: OSD error shown for 2.5s in the OpenGL backend when vertex shader compilation fails (`GPU/GLES/ShaderManagerGLES.cpp`); suppressed if slow warnings are hidden. Informative only; emulation continues with software transform.
  - Note: key is lowercase-with-dash, display text is capitalized — key and display differ.

- **`Stereo rendering`** — "Stereo rendering"
  - Where: checkbox on the **Developer** settings page under a "Stereo rendering" header (`UI/DeveloperSettingsPages.cpp`, requires multiview support).
  - Meaning: experimental — renders the game twice (two eyes) using GPU multiview for headset-style stereo. When active, a boot OSD says "Stereo rendering: Enabled" (`UI/EmuScreen.cpp`), and post-processing shaders/rotation are disabled.

- **`Stereo display shader`** — "Stereo display shader"
  - Where: button on the Developer settings page under "Stereo rendering" (`UI/DeveloperSettingsPages.cpp`), enabled only while stereo rendering is on.
  - Meaning: picks the [PostShaders] entry of type `StereoToMono` used to combine the two eye images for a normal display — e.g. `RedBlue` (anaglyph) or `SideBySide`. Its per-shader sliders appear below as [PostShaders] setting names.

- **`Backend`** — "Backend"
  - Where: label of the GPU backend picker (setting `iGPUBackend`, `Core/Config.cpp`; also the touch-test screen `UI/DevScreens.cpp`).
- **`OpenGL` / `Direct3D 11` / `Vulkan`** — choices of `Backend` (`gpuBackendNames`) and translated API names shown in System info/credits (`UI/SystemInfoScreen.cpp`, `UI/MiscScreens.cpp` via `InfoField::APINAME`).
  - Note: `Direct3D 9` is a hidden legacy choice; `Metal` is the macOS/iOS choice. Keep vendor API names untranslated.

- **`Device`** — "Device"
  - Where: label of the GPU device picker, shown when the backend exposes several devices: Vulkan device (`sVulkanDevice`) or Windows D3D11 device (`sD3D11Device`) (`Core/Config.cpp`, `UI/Settings/GraphicsPage.cpp` `GPUDeviceNameSetting`). Changing it triggers a restart prompt.

- **`Full screen`** — "Fullscreen"
  - Where: setting `g_Config.bFullScreen` (`Core/Config.cpp`, key `FullScreen`), desktop only (Graphics page "Display" section). Also a macOS menu toggle and the main-screen corner button (`UI/MainScreen.cpp`).
  - Note: key "Full screen" vs display "Fullscreen".

- **`Allow exclusive fullscreen`** — "Allow exclusive fullscreen"
  - Where: setting `g_Config.bAllowFullScreenExclusive` (`Core/Config.cpp`, key `FullScreenExclusive`), desktop, shown only when the backend supports exclusive fullscreen; disabled while "Use all displays" is on; changing it offers a restart.
  - Meaning: lets the backend use true exclusive fullscreen (e.g. `VK_EXT_full_screen_exclusive` on Vulkan, `Common/GPU/Vulkan/VulkanGraphicsContext.cpp`) which can enable better frame pacing/variable refresh at the cost of alt-tab quirks.

- **`Use all displays`** — "Use all displays"
  - Where: setting `g_Config.bFullScreenMulti` (`Core/Config.cpp`, key `FullScreenMulti`), desktop, shown only with more than one display.
  - Meaning: stretch fullscreen across all monitors (multi-monitor fullscreen span). Not related to "External display".

- **`External display`** — "External display"
  - Where: setting `g_Config.iExternalDisplay` (`Core/Config.cpp`, key `ExternalDisplay`), iOS/Android only when the device supports a second swapchain (TV/external screen) (`UI/Settings/GraphicsPage.cpp`).
  - Meaning: what shows on an attached TV/monitor. Choices: `Show the game` (render the game to the external display via its own swapchain; the device keeps the UI) vs `Mirror the screen` (let the system mirror whatever the device shows).

- **`Show the game` / `Mirror the screen`** — choices of `External display` (`externalDisplayNames`).

- **`Framerate mode`** — see Framerate control above.

### Display layout & effects editor (`UI/DisplayLayoutScreen.cpp`, opened from the Graphics page)

- **`Display layout & effects`** — "Display layout & effects"
  - Where: button on the Graphics page "Display" section that opens the display layout editor (`page->Choice("Display layout & effects").OpensScreen()`).
  - Note: desktop menus use a separate `DesktopUI` key "Display Layout && Effects".

- **`Stretch`** — "Stretch"
  - Where: checkbox in the layout editor (`config.bDisplayStretch`) — stretch the game image to fill the screen ignoring aspect ratio. Only shown when screen orientation matches the PSP display orientation; disabled when "Integer scale factor" is on.
- **`Partial Stretch`** — "Partial stretch"
  - Where: **not referenced in current code**. Legacy option from the old "Stretching: Auto / Partial stretch / Stretch" selector; the current editor has just the `Stretch` checkbox.
- **`Auto Scaling` / `Manual Scaling` / `Auto (default)`** — "Auto scaling" / "Manual scaling" / "Auto (default)"
  - Where: **not referenced in current code** (repo-wide search). Legacy labels of the old display-layout scaling mode selector.

- **`Aspect Ratio`** — "Aspect Ratio"
  - Where: slider in the layout editor (`config.fDisplayAspectRatio`, 0.1–2.0, default 1.0); disabled when Stretch or Integer scale factor is active. Multiplies the displayed width relative to the natural PSP aspect.

- **`Integer scale factor`** — "Integer scale factor"
  - Where: checkbox in the layout editor (`config.bDisplayIntegerScale`): scale the image only by whole numbers (no partial pixels), eliminating shimmer/uneven pixels at the cost of unused screen space. Disables Stretch and Aspect Ratio.

- **`Screen Scaling Filter`** — "Screen scaling filter"
  - Where: choice in the layout editor (`config.iDisplayFilter`, ini key `BufferFiltering`), choices `Linear` / `Nearest` (`UI/DisplayLayoutScreen.cpp` `bufFilters`).
  - Meaning: filtering used when the final game image is scaled to the display: `Linear` = smooth, `Nearest` = crisp pixels.
  - Note: filters the *output scaling*, not texture sampling — distinct from `Texture Filter` ("Texture filtering").

- **`Overlay`** — "Overlay"
  - Where: choice in the layout editor (`config.sOverlay`) listing bezel/overlay images found in `PSP/OVERLAYS` (plus an "Off" item, `UI/DisplayLayoutScreen.cpp`). Overlays are decorative frames drawn around/over the game image (`GPU/Common/Overlay.cpp`; disabled while Cardboard VR is on).

- **`Load overlay image...`** — "Load overlay image..."
  - Where: button in the layout editor opening an image file picker; the chosen image is copied into the overlays folder as `imported.png` (`UI/DisplayLayoutScreen.cpp`).

- **`Overlay images must be PNG files`** — "Overlay images must be PNG files"
  - Where: OSD error shown when overlay import fails (`UI/DisplayLayoutScreen.cpp`) — despite the button accepting any image, only PNGs can be imported.

- **`Rotation`** — "Rotation"
  - Where: popup label inside the "PSP display rotation" group in the layout editor (`config.iInternalScreenRotation`).
  - Meaning: rotates the emulated PSP screen output. Choices `Landscape`, `Portrait`, `Landscape Reversed`, `Portrait Reversed` are translated in the **Controls** category, not Graphics (`UI/DisplayLayoutScreen.cpp`, `I18NCat::CONTROLS`). Disabled while "Skip buffer effects" is on (unless software rendering).

- **`PSP display rotation`** — "PSP display rotation"
  - Where: group header around the `Rotation` popup and the `Rotate controls` checkbox (`UI/DisplayLayoutScreen.cpp`).

- **`Rotate controls`** — "Rotate controls"
  - Where: checkbox in the "PSP display rotation" group (`config.bRotateControlsWithScreen`): rotate the touch controls along with the rotated PSP screen. Disabled in portrait (the default) and with "Skip buffer effects" on.

- **`Ignore camera notch when centering`** — "Ignore camera notch when centering"
  - Where: checkbox in the layout editor (`config.bIgnoreScreenInsets`), shown only on Android ≥ 9 / iOS when the display reports asymmetric safe insets (a notch) (`UI/DisplayLayoutScreen.cpp`).
  - Meaning: when centering the game image, don't compensate for the camera cutout / rounded corners, letting the image center on the physical screen instead of the visible area.

- **`Postprocessing shaders`** — "Postprocessing shaders"
  - Where: header in the layout editor and title of the shader-picker popup (`UI/DisplayLayoutScreen.cpp` `PostProcScreen`). Post shaders are image effects applied to the final picture; the editor shows the shader chain (add/move/remove), each shader with optional sliders ([PostShaders] names).

- **`Add postprocessing shader`** — "Add postprocessing shader"
  - Where: display name of an empty ("Off") shader slot in the chain (`PostShaderTranslateName`, `UI/DisplayLayoutScreen.cpp` — "Off is a legacy fake item"); effectively the label of the "+" row that adds a shader.

### Cardboard VR (Google Cardboard)

- **`Cardboard VR Settings`** — "Google Cardboard VR settings"
  - Where: link on the Graphics page (visible on Android/iOS unless the device type is VR; elsewhere only if enabled via ini) opening the Cardboard page (`UI/Settings/GraphicsPage.cpp` `BuildCardboardVRPage`).
- **`Enable Cardboard VR`** — "Enable Cardboard VR"
  - Where: checkbox (`config.bEnableCardboardVR`). Splits the output into two side-by-side lens views for use in a Google Cardboard-style phone headset (`GPU/Common/PresentationCommon.cpp` `UseCardboard`). Disables overlays.
- **`Cardboard Screen Size`** — "Screen size (in % of the viewport)"
  - Where: slider 30–150 (`config.iCardboardScreenSize`), unit `"% of viewport"`.
  - Meaning: size of each eye's game image as a percentage of the per-eye half of the screen (`PresentationCommon::GetCardboardSettings`: image = half-width × value%).
- **`Cardboard Screen X Shift`** — "X shift (in % of the blank space)"
  - Where: slider −150…150 (`config.iCardboardXShift`), unit `"% of the void"`.
  - Meaning: horizontal offset of the two eye images, as a percentage of the empty margin between the scaled image and the half-screen edge — used to line the lenses up with your eyes (IPD). Positive shifts the eyes apart; the right eye mirrors the left.
  - Note: terminology inconsistency — the label says "blank space", the unit string is "% of the void"; same meaning (the empty margin).
- **`Cardboard Screen Y Shift`** — "Y shift (in % of the blank space)"
  - Where: slider −100…100 (`config.iCardboardYShift`), unit `"% of the void"`; vertical offset, percentage of the vertical margin.

- **`% of viewport` / `% of the void`** — unit suffixes shown next to the Cardboard slider values (`.Units(...)`, `Core/Config.cpp`).

### Warnings / compat messages (all shown as OSD warnings at game boot, `Core/System.cpp` `ShowCompatWarnings`)

- **`BufferedRenderingRequired`** — "Warning: This game is not compatible with \"Skip buffer effects\"" — shown when the game's compat flag `RequireBufferedRendering` is set and "Skip buffer effects" is enabled.
- **`BlockTransferRequired`** — see GPU readbacks above.
- **`DefaultCPUClockRequired`** — "Warning: This game requires the CPU clock to be set to default." — shown when the game's compat flag `RequireDefaultCPUClock` is set and the CPU clock override (`iLockedCPUSpeed`, en_US label "Change CPU Clock", System settings) isn't 0 ("Auto").

- **`Texture replacement pack activated`** — "Texture replacement pack activated"
  - Where: OSD success message when a texture replacement pack for the game is found and loaded at boot (`GPU/Common/TextureReplacer.cpp`).

- **`Your display is set to a low refresh rate: %1 Hz. 60 Hz or higher is recommended.`**
  - Where: OSD warning on desktop at startup when the display refresh rate is below ~60 Hz (`UI/NativeApp.cpp`), and a notice in System information (`UI/SystemInfoScreen.cpp`). `%1` = current refresh rate in Hz.

- **`Must Restart`** — "You must restart PPSSPP for this change to take effect."
  - Where: toast shown by `RecreateActivity()` fallback on Android < 4.1 (`UI/DevScreens.cpp`), and by generic restart-required flows (`UI/DevScreens.cpp:895` `System_Toast`).
  - Note: code fallback text omits the trailing period; the ini text has it.

- **`Recreate Activity`** — "Recreate activity"
  - Where: button on the touch-test developer screen (`UI/DevScreens.cpp` `TouchTestScreen`, Android only) — forces the Android Activity to be recreated (`SystemNotification::FORCE_RECREATE_ACTIVITY`), e.g. after graphics/display setting changes.

### Camera settings (on the **General** settings page, `UI/Settings/GeneralPage.cpp`)

- **`Camera`** — "Camera" — section header, shown only if the device lists at least one camera (`page->Header({I18NCat::GRAPHICS, "Camera"})`).
- **`Camera Device`** — "Camera device" — setting `g_Config.sCameraDevice` (`Core/Config.cpp`), the camera device used for games that use the PSP camera.
- **`Mirror camera image`** — "Mirror camera image" — checkbox `g_Config.bCameraMirrorHorizontal` (`Core/Config.cpp`), Windows-only in the UI: flips the camera image horizontally.

### Desktop menus / misc

- **`Cheats`** — "Cheats"
  - Where: OSD text when toggling cheats from a desktop menu (`Windows/MainWindowMenu.cpp` `gr->T("Cheats")`, `SDL/CocoaBarItems.mm` `T(I18NCat::GRAPHICS, "Cheats")`) — shows "Cheats: On/Off".
- **`Frame Skipping`** — see above (also Windows OSD "Frame skipping: N").
- **`Show FPS Counter` / `Show Speed` / `Show Battery %`** — "Show FPS counter" / "Show Speed" / "Show Battery %"
  - Where: checkboxes under the "Overlay Information" header on the Graphics page (`UI/Settings/GraphicsPage.cpp`, `ShowStatusFlags` bits), Battery only on devices that report battery percentage; macOS menu toggles too (`SDL/CocoaBarItems.mm`).
  - Meaning: small always-on status text in the top-right corner (`UI/DebugOverlay.cpp` `DrawFPS`): FPS value, emulation speed %, battery %. Note the drawn labels ("FPS:", "Speed:", "Battery:") are hardcoded English, not translated.
- **`Overlay Information`** — "Overlay information" — header above those checkboxes.
- **`Display rotation`** — "Display rotation"
  - Where: System information → Vulkan features tab, info row showing the current display rotation in degrees (`UI/SystemInfoScreen.cpp`: `gr->T("Display rotation")` + `°` value).
  - Note: distinct from `Rotation` and `PSP display rotation` in the layout editor.
- **`Low latency display`** — see presentation above.
- **`Latency`** — "Latency" — section header on the Graphics page above `Buffer graphics commands` (`page->Header("Latency")`).
- **`Rendering`** / **`Display`** — section headers on the Graphics page.
- **`Show Debug Statistics`** / **`Window Size`** — the [Graphics] copies are **not referenced**; the live keys are in the `DesktopUI` category (Windows menu translation `Windows/MainWindowMenu.cpp` `TranslateMenuItem` uses `I18NCat::DESKTOPUI`; macOS `SDL/CocoaBarItems.mm` likewise). "Show Debug Statistics" toggles the debug-statistics overlay; "Window Size" is the Windows "Window Size" menu popup (`Windows/ppsspp.rc`).

### Custom GPU drivers (Android AdrenoTools; `UI/DriverManagerScreen.cpp`, opened from Developer settings → "Custom driver manager")

- **`Custom driver manager`** — "Custom driver manager"
  - Where: entry in Developer settings (`UI/DeveloperSettingsPages.cpp`, Vulkan + `SupportsCustomDriver()` only) opening `DriverManagerScreen`; also the group header inside that screen.
- **`CustomDriversDescription`** — "Custom drivers loaded through AdrenoTools replace the GPU driver that came with your device. They can be faster and have fewer bugs, but they can also be slower or less stable. Only install drivers made for your GPU. If rendering breaks, switch back to the default driver."
  - Where: explanatory text at the top of the driver manager. AdrenoTools loads a user-installed Adreno GPU driver in place of the system one (Android ARM64).
- **`Drivers`** — "Drivers" — tab title in the driver manager screen.
- **`Current GPU driver`** — "Current GPU driver" — green "success" notice on the driver entry that is currently in use.
- **`Default GPU driver`** — "Default GPU driver" — title/description of the built-in system driver entry.
- **`Install custom driver...`** — "Install custom driver..." — button that opens a ZIP file picker for a driver package (`System_BrowseForFile(..., BrowseFileType::ZIP)`).
- **`The chosen ZIP file doesn't contain a valid driver`** — OSD error when the selected ZIP is unusable (unopenable zip, missing `meta.json`, invalid name, etc.).
- **`Driver requires Android API version %1, current is %2`** — warning notice on a driver entry whose `minApi` exceeds the device's Android version; that driver can't be selected. `%1` = required API level, `%2` = current.

### Legacy / not referenced by the current code (kept in ini for translation memory)

These keys exist in `[Graphics]` but no code in the repo looks them up (verified by exhaustive literal search): `Frame presentation mode`, `'Mailbox' (lower latency, recommended)`, `FIFO (higher latency, framerate stability)`, `Immediate (lower latency, tearing)`, `FIFO: latest ready`, `FIFO: relaxed`, `Force 60 Hz`, `Render all frames`, `Number of Frames`, `%, 0:unlimited`, `Auto (default)`, `Auto Scaling`, `Manual Scaling`, `Partial Stretch`, `Disable culling`, `GPUReadbackRequired`, `(supersampling)`, `(upscaling)`, `Internal Resolution`, `Window Size` (Graphics copy), `Show Debug Statistics` (Graphics copy). Their modern replacements are noted under each live setting above. See also "Needs Clarification" for fragment keys.

### Self-evident (no notes needed)

- `Off` — Off (choice of many settings: frame skipping, MSAA, anisotropy, upscale level, bloom hack, texture filter lists…)
- `Auto` — Auto (choice of Texture Filter / Lens flare occlusion)
- `Default` — Default (choice of Framerate mode)
- `No` — No (choice of Skip GPU Readbacks)
- `Skip` — Skip (choice of Skip GPU Readbacks)
- `Copy to texture` — Copy to texture (choice of Skip GPU Readbacks)
- `Safe` / `Balanced` / `Aggressive` — choices of Lower resolution for effects
- `Low` / `Always on` — choices of Lens flare occlusion
- `2x` / `3x` / `4x` / `5x` / `8x` / `16x` — multiplier choices (MSAA / anisotropy / upscale level); en_US renders them with "×"
- `Request 60 Hz` / `Force 60Hz` — choices of Framerate mode (Android)
- `Nearest` / `Linear` / `Auto Max Quality` — choices of Texture Filter (Linear/Nearest also Screen Scaling Filter)
- `xBRZ` / `Hybrid` / `Bicubic` / `Hybrid + Bicubic` — choices of Upscale type (CPU texture upscaler)
- `Unlimited` — zero-value label on Alternative speed sliders
- `Disabled` — negative-value label on Alternative speed sliders
- `Always on` — choice of Lens flare occlusion
- `Aspect Ratio` — slider label in the layout editor

### Skipped (universal / no guidance needed)

- `OpenGL`, `Direct3D 11`, `Vulkan` — vendor API names, keep as-is
- `FPS` — unit abbreviation (see Needs Clarification)
- `2x PSP`… — covered above

## [PostShaders]

Displayed by the post-processing shader picker (`UI/DisplayLayoutScreen.cpp` `PostProcScreen`) and the stereo shader picker (`UI/DeveloperSettingsPages.cpp`); shader metadata is loaded from `assets/shaders/defaultshaders.ini` (`GPU/Common/PostShader.cpp`), where `Name=` provides the English fallback and the section name is the lookup key. Entries with sliders get [PostShaders] sliders beneath the chain in the layout editor.

- **`Off`** — "Off"
  - Where: first entry of the shader picker (synthetic entry inserted by `GPU/Common/PostShader.cpp`), meaning "no shader in this slot".
- **`(duplicated setting, previous slider will be used)`** — "(duplicated setting, previous slider will be used)"
  - Where: appended to a shader slider's name when the **same shader appears more than once in the post-processing chain** (`UI/DisplayLayoutScreen.cpp` `duplicated` handling): the later copy's slider is greyed out and its value ignored — the first occurrence's value is used for both.
- **`Add postprocessing shader`** — see [Graphics] (label of an empty shader slot).

Shader names (what they do; section name = key):

- **`4xHqGLSL`** — "4xHQ pixel art upscaler": HQx-style scaler that smooths blocky pixel art while keeping edges (`4xhqglsl.fsh`).
- **`5xBR`** — "5xBR pixel art upscaler": xBR-family edge-directed pixel-art scaler (Hyllian).
- **`5xBR-lv2`** — "5xBR-lv2 pixel art upscaler": tuned variant of 5xBR (guest.r).
- **`AAColor`** — "AA-Color": color-based edge antialiasing/smoothing (guest.r's AA-Color shader).
- **`Bloom`** — "Bloom": adds a glow around bright areas (blur-based; `bloom.fsh`).
- **`BloomNoBlur`** — "Bloom (no blur)": glow without the blur pass (`bloomnoblur.fsh`).
- **`Cartoon`** — "Cartoon": posterizes colors and draws dark outlines, comic style (`cartoon.fsh`).
- **`ColorCorrection`** — "Color correction": brightness/saturation/contrast/gamma adjustment.
- **`CRT`** — "CRT scanlines": retro CRT look with an animated scanline/roll effect (`crt.fsh`).
- **`FXAA`** — "FXAA antialiasing": NVIDIA FXAA edge antialiasing (`fxaa.fsh`).
- **`FakeReflections`** — "Fake reflections": adds a cheap fake reflection overlay (`fakereflections.fsh`).
- **`LCDPersistence`** — "LCD persistence": ghosting/persistence trails like early LCDs; uses the previous frame (`persistence.fsh`, `UsePreviousFrame=true`).
- **`Natural`** — "Natural colors": "Natural Vision" color correction (ShadX/SimoneT).
- **`NaturalA`** — "Natural colors (no blur)": same without the blur component.
- **`PSPColor`** — "PSP color": color curve matched to a real PSP's output (hunterk, Pokefan531).
- **`RedBlue`** — "Red/Blue glasses": anaglyph stereo combining (type `StereoToMono`; used by `Stereo display shader`).
- **`Scanlines`** — "Scanlines (CRT)": simple scanline overlay (unlike `CRT`, static and lighter).
- **`Sharpen`** — "Sharpen": unsharp-style sharpening.
- **`SideBySide`** — "Side by side (SBS)": places the two stereo eyes side by side (type `StereoToMono`).
- **`SSAA(Gauss)`** — "Supersampling AA (Gauss)": 2× supersampling antialiasing with Gaussian downscale (`GaussianDownscale.fsh`, `SSAA=2`).
- **`UpscaleSharpBilinear`** — "Sharp bilinear upscaler": bilinear scaling with kept-crisp edges.
- **`UpscaleBicubic`** — "Bicubic upscaler": smooth bicubic upscaling (`upscale_bicubic.fsh`).
- **`UpscaleSpline36`** — "Spline36 upscaler": Spline36-interpolation upscaling.
- **`VideoSmoothingAA`** — "Video smoothing AA": smoothing antialiasing tuned for video-like content (ini name "Video aware guest.r AA 4.o").
- **`Vignette`** — "Vignette": darkens the frame corners.
- **`Tex4xBRZ`** — "4xBRZ" and **`TexMMPX`** — "MMPX": texture-upscaling shader names duplicated into this category (their sections are `Type=Texture` and normally shown via [TextureShaders]); likely legacy leftovers here.

Slider labels (all rendered as `PopupSliderChoiceFloat` under the shader in the chain, `UI/DisplayLayoutScreen.cpp`; values/ranges from `defaultshaders.ini`):

- **`Amount`** — effect strength slider of `Bloom`, `BloomNoBlur`, `FakeReflections`, `Sharpen`, `Scanlines`.
- **`Power`** — curve/strength shaping slider of `Vignette`, `FakeReflections`, `Bloom`, `BloomNoBlur`.
- **`Aspect`** — shape aspect of the `Vignette` darkening (0.5–2.0).
- **`Black border`** — `Cartoon`: thickness of the drawn dark outlines.
- **`Brightness` / `Saturation` / `Contrast` / `Gamma`** — `ColorCorrection` color-adjustment sliders.
- **`ColorPreservation`** — `RedBlue`: how much original color is preserved vs pure anaglyph tinting.
- **`GreenLevel`** — `RedBlue`: green channel level to balance the anaglyph image.
- **`Intensity`** — `Scanlines`: darkness/intensity of the scanlines.
- **`Strength`** — `LCDPersistence`: amount of ghosting/persistence (0.3–0.8).
- **`Animation speed (0 -> disable)`** — `CRT`: speed of the scrolling/rolling CRT animation; 0 = static image (0–2.0, default 1.0).
  - Note: the literal "(0 -> disable)" explains the zero behavior; keep it terse.

## [TextureShaders]

GPU compute-shader texture upscalers, listed by the picker opened from Graphics → "GPU texture upscaler (fast)" (`UI/MiscScreens.cpp` `TextureShaderScreen`, Vulkan/Metal only). Names are looked up in `I18NCat::TEXTURESHADERS` with the ini section as key; scale factors come from `assets/shaders/defaultshaders.ini` (`Type=Texture`, `Scale=`).

- **`Off`** — "Off": no GPU texture upscaling (synthetic first entry, `GPU/Common/PostShader.cpp`).
- **`TexMMPX`** — "MMPX (2x)": MMPX pixel-art magnification (McGuire/Gagiu), 2×.
- **`Tex2xBRZ`** — "2xBRZ (2x)": 2xBRZ edge-directed pixel-art upscale, 2×.
- **`Tex4xBRZ`** — "4xBRZ (4x)": 4xBRZ, 4×.
- **`TexSpline36`** — "Spline36 (2x)": Spline36 interpolation, 2×.
- **`TexSpline36_4x`** — "Spline36 (4x)": Spline36, 4×.
- **`TexMMPXAdvanced`** — "MMPX Advanced (2x)": MMPX variant with extra quality passes, 2× (blacklisted on several mobile GPU vendors, `VendorBlacklist` in the ini).
- **`TexNNEDI3NNS16`** — "NNEDI3 Multi (2x)": NNEDI3 neural edge-directed interpolation, multi-pass, 2×.
- **`TexNNEDI3NNS16_4x`** — "NNEDI3 Multi (4x)": same, 4×.
- **`TexNNEDI3NNS16Single`** — "NNEDI3 Single (2x)": single-pass NNEDI3, 2×.
- **`TexNNEDI3NNS16Single4x`** — "NNEDI3 Single (4x)": single-pass NNEDI3, 4×.

The parenthetical "(2x)"/"(4x)" states the fixed upscale factor of that shader.

## Needs Clarification

Fragment keys in `[Graphics]` that no code references as `I18NCat::GRAPHICS` lookups (searched all cpp/h/mm/java/kt/cs/xaml/rc). They may be legacy; please confirm whether to keep translating:

- **`Both`** — no graphics-context usage found. What setting/choice was "Both" (e.g. an old "Texture filter applies to: …" or dump mode)? Is it still needed?
- **`Mode`** — no graphics-context usage found. Which control used the bare label "Mode"?
- **`Speed`** — not used by the status overlay (which hardcodes "Speed:"). Legacy?
- **`FPS`** — likewise unused (overlay hardcodes "FPS:"). Legacy?
- **`High`** — no GRAPHICS-labeled choice array contains "High" (`Low` is the Lens flare occlusion choice; `Medium`/`High` are not). Which setting offered Low/Medium/High?
- **`Medium`** — same as `High` (the live "Replacement texture load speed" choice `Medium` is looked up in the **Developer** category, not Graphics).
- **`None`** — no graphics-context usage found. What used "None" (vs "Off")?
- **`Debugging`** — the live "Debugging" menu text is the `DesktopUI` key ("&Debug"); no [Graphics] usage found. Legacy?
- **`Auto (default)`** — no code usage found (legacy resolution/quality default entry?).
- **`GPUReadbackRequired`** — legacy warning text ("set to Off") superseded by `BlockTransferRequired` ("set to No", matching the current choice names). Should the old text be retired?

For the "legacy" cluster (`Frame presentation mode`, present-mode names, `Force 60 Hz`, `Render all frames`, `Number of Frames`, `%, 0:unlimited`, `Auto Scaling`, `Manual Scaling`, `Partial Stretch`, `Disable culling`, `(supersampling)`, `(upscaling)`): these are not shown by any current build; recommend keeping translations only for translation memory / downgrade compatibility.
# PPSSPP Frontend UI Strings — Grounding

## [Achievements]

> Scope: `ppsspp/assets/lang/en_US.ini` `[Achievements]` section (lines 25–108). All claims verified against this clone's C++ sources (the rcheevos submodule is NOT checked out). External sources used and marked inline: rcheevos `rc_client.h` / `rc_client.c` (RetroAchievements/rcheevos @ master, fetched via jsdelivr) and docs.retroachievements.org "Progression and Win Condition Guidelines".
>
> Screens involved: **RetroAchievementsSettingsScreen** (UI/RetroAchievementScreens.cpp; opened from Settings → Tools, UI/Settings/ToolsPage.cpp:40) with tabs Account / Customize / Developer Tools; **RetroAchievementsListScreen** (achievements + Leaderboards tabs; opened from the in-game pause screen via the "Achievements" button, UI/PauseScreen.cpp:778); **RetroAchievementsLeaderboardScreen** (single-leaderboard ranking). Game summary widgets also appear on the pause screen. OSD notifications are rendered by Common/System/OSD.cpp + UI/OnScreenDisplay.cpp.

### Hard domain terms

- **`Hardcore Mode (no savestates)`** — "Hardcore Mode (no savestates)"
  - Where: Achievements settings → **Account** tab → "Settings" group; checkbox bound to `g_Config.bAchievementsHardcoreMode` (UI/RetroAchievementScreens.cpp, `CreateAccountTab`). Config key is `AchievementsChallengeMode`, **default ON** (Core/Config.cpp:536).
  - Meaning: The RetroAchievements **hardcore** mode toggle. Hardcore is RA's "prestige" mode where unlocks are recorded as *hardcore* unlocks. It only takes effect when logged in and a supported game is loaded: `Achievements::HardcoreModeActive()` = `IsLoggedIn() && rc_client_get_hardcore_enabled() && rc_client_is_processing_required()` (Core/RetroAchievements.cpp). Turning it on while a game runs raises `RC_CLIENT_EVENT_RESET` and the game is reset (event handler, Core/RetroAchievements.cpp). The parenthetical "(no savestates)" is a UI hint: in hardcore, save-state **loading** is always blocked and saving too unless the separate dev option below is enabled; cheats (Core/CwCheat.cpp:296, 1460), rewind/state-undo (UI/PauseScreen.cpp:499, 516), auto-load of savestates (UI/EmuScreen.cpp:399), freeze-frame (UI/EmuScreen.cpp:1913, UI/DevScreens.cpp:196), debugger memory editing (UI/ImDebugger/ImMemView.cpp, Windows/Debugger/*) and slow-motion (Core/HLE/sceDisplay.cpp `FrameTimingLimit` — minimum 60fps in hardcore) are also disabled. Fast-forward is allowed.
  - Note: "Hardcore" is a RetroAchievements proper term; prefer the wording your local RA community uses. It is a verification mode, not a game difficulty.

- **`Hardcore Mode`** — "Hardcore Mode"
  - Where: status line appended (after "\n") to the game achievement summary in `Achievements::GetGameAchievementSummary` (Core/RetroAchievements.cpp) when `HardcoreModeActive()`. Displayed in the achievements list screen header (UI/RetroAchievementScreens.cpp `CreateAchievementsTab`), the pause screen (UI/PauseScreen.cpp `CreateViews`), and the OSD placard when a game is identified.
  - Meaning: "this session is running in Hardcore mode" — a status flag, not the setting. Distinct from `Hardcore Mode (no savestates)`; keep translations consistent but note this one labels a state.

- **`Save states not available in Hardcore Mode`** — "Save states not available in Hardcore Mode"
  - Where: game pause screen (UI/PauseScreen.cpp `GamePauseScreen::CreateViews`, ~line 691): `NoticeView` shown in place of the save-state controls when hardcore is active and `bAchievementsSaveStateInHardcoreMode` is off (`achievementsAllowSavestates == false`).
  - Meaning: explains why the save/load UI is missing there. Informational, not an error. (If the "Allow Save State in Hardcore Mode" dev option is on, this notice is not shown.)

- **`This feature is not available in Hardcore Mode`** — "This feature is not available in Hardcore Mode"
  - Where: generic OSD warning from `Achievements::WarnUserIfHardcoreModeActive` (Core/RetroAchievements.cpp), used when a blocked action is attempted and no specific message is given. Callers: Core/SaveState.cpp (load-state ops, e.g. 766/822), UI/EmuScreen.cpp (quick load/rewind hotkeys, 942–1080), UI/DevScreens.cpp:196 ("Toggle Freeze"), Windows/MainWindowMenu.cpp (Windows menus).
  - Meaning: catch-all "blocked because Hardcore mode" toast. Save-state *saving* passes `isSaveStateAction = true`, which is exempted when the dev option below is on.

- **`Allow Save State in Hardcore Mode (but not Load State)`** — "Allow Save State in Hardcore Mode (but not Load State)"
  - Where: Achievements settings → **Developer Tools** tab (UI/RetroAchievementScreens.cpp `CreateDeveloperToolsTab`); checkbox bound to `g_Config.bAchievementsSaveStateInHardcoreMode`.
  - Meaning: developer/testing escape hatch. When on, *writing* save states is allowed in hardcore (`WarnUserIfHardcoreModeActive(isSaveStateAction=true)` returns false → no block), but *loading* stays forbidden. The pause screen then shows save-state slots with "Load State" buttons omitted (UI/PauseScreen.cpp:137, 315).
  - Note: "save state" vs "load state" must be distinguished in translation (some languages use one word for both).

- **`Encore Mode`** — "Encore Mode"
  - Where: (a) checkbox in **Developer Tools** (UI/RetroAchievementScreens.cpp:522, `g_Config.bAchievementsEncoreMode`); (b) status line appended to the game summary when active (`GetGameAchievementSummary`).
  - Meaning: forwards to `rc_client_set_encore_mode_enabled` (applied when a game loads; rcheevos docs: "Evaluated when loading a game. Has no effect while a game is loaded." — external, `rc_client.h`). Encore mode is a replay mode in which achievements you have **already unlocked** are treated as still pending so they can trigger again while you replay; since the server still has your real unlocks, the unlock state shown in the UI may not match (hence the warning strings).
  - Note: keep the established RA term ("Encore") if your language's RA community uses it.

- **`In Encore mode - unlock state may not be accurate`** — "In Encore mode - unlock state may not be accurate"
  - Where: `NoticeView(NoticeLevel::WARN, …)` at the top of each achievements tab when `Achievements::EncoreModeActive()` (UI/RetroAchievementScreens.cpp `CreateAchievementsTab`, line 121).
  - Meaning: the Locked/Unlocked marks below may be wrong in Encore mode because already-earned achievements are being re-evaluated. "unlock state" = whether an achievement is displayed as earned.

- **`Test Mode`** — "Test Mode"
  - Where: **no reference in any source file of this clone** (see Needs Clarification).
  - Meaning: cannot be grounded; appears unused/leftover.

- **`Missable`** — "Missable"
  - Where: small type badge drawn top-right of each achievement row (UI/RetroAchievementScreens.cpp `RenderAchievement`, `achievement->type == RC_CLIENT_ACHIEVEMENT_TYPE_MISSABLE`). Only these three types get badges; `RC_CLIENT_ACHIEVEMENT_TYPE_STANDARD` gets none.
  - Meaning: author-assigned achievement type flag: this achievement **can be permanently missed during a playthrough** (one-time event, point of no return, mutually exclusive choice) — pass it up and you must replay to earn it. Contrast with Progression-type, which "cannot be missed assuming no glitches/exploits have been used" (docs.retroachievements.org — external).
  - Note: translate as "can be missed", never as "faulty/unreliable".

- **`Progression`** — "Progression"
  - Where: type badge in `RenderAchievement` (`RC_CLIENT_ACHIEVEMENT_TYPE_PROGRESSION`).
  - Meaning: author-assigned type: marks steady progress through the game's main path/story. RA rule (external, docs.retroachievements.org "Progression and Win Condition Guidelines"): a game is *beaten* when all Progression-typed achievements plus any Win-typed achievement are earned. Not the same as "achievement progress" (see `Achievement progress`).

- **`Win`** — "Win"
  - Where: type badge in `RenderAchievement` (`RC_CLIENT_ACHIEVEMENT_TYPE_WIN`). This one-word string is used **only** for this badge.
  - Meaning: short for RetroAchievements' **"Win Condition"** type: the achievement that marks finishing/beating the game (reaching the ending etc.). Sets may have several; any one (plus all Progression) marks the game beaten (external, same docs page).
  - Note: a category label — not a verb ("You win!"), not a button. Use your language's established RA term for "Win Condition".

- **`Unofficial`** — "Unofficial"
  - Where: collapsible section header on the achievements list — bucket title for `RC_CLIENT_ACHIEVEMENT_BUCKET_UNPROMOTED` (`AchievementBucketTitle` in UI/RetroAchievementScreens.cpp; note the raw English string doubles as the translation key via `ac->T_cstr`).
  - Meaning: achievement category name. "Unofficial" (rcheevos term "unpromoted") = achievements **not in the official core set** (in development or demoted); they don't count toward completion and aren't loaded at all unless the `Unofficial achievements` setting is on. (rcheevos master renamed this to `RC_CLIENT_ACHIEVEMENT_BUCKET_UNOFFICIAL` / `RC_CLIENT_ACHIEVEMENT_CATEGORY_UNOFFICIAL`, with core = "promoted" — external `rc_client.h`.)

- **`Unofficial achievements`** — "Unofficial achievements"
  - Where: (a) checkbox in **Developer Tools** (UI/RetroAchievementScreens.cpp:523, `g_Config.bAchievementsUnofficial`); (b) status line appended to the game summary when on (`GetGameAchievementSummary`).
  - Meaning: (a) also load/process unofficial (unpromoted) achievements (`rc_client_set_unpromoted_enabled`, applied at game load — Core/RetroAchievements.cpp); the list filter switches from `RC_CLIENT_ACHIEVEMENT_CATEGORY_PROMOTED` to `..._PROMOTED_AND_UNPROMOTED` (`GetListFilter`). (b) summary suffix meaning "counts below include unofficial achievements".
  - Note: one key serving as both setting label and status line.

- **`Rich Presence`** — "Rich Presence"
  - Where: **key not referenced in code** (see Needs Clarification). The feature: `Achievements::GetRichPresenceMessage` → `rc_client_get_rich_presence_message` (Core/RetroAchievements.cpp); shown as an unlabeled plain text line under the game summary on the pause screen (UI/PauseScreen.cpp:598).
  - Meaning: RetroAchievements' live status string describing what you're currently doing in-game (e.g. "World 3 — 12 stars"), shown on your retroachievements.org profile while playing. Produced by a script in the achievement set.
  - Note: RA feature name; do not translate literally as "wealth/abundance".

- **`Leaderboard`** — "Leaderboard"
  - Where: `ItemHeader` on the leaderboard detail screen (UI/RetroAchievementScreens.cpp `RetroAchievementsLeaderboardScreen::CreateLeaderboardTab`), above the Top players / Around me switcher.
  - Meaning: header of the ranked score table of a single leaderboard.

- **`Leaderboards`** — "Leaderboards"
  - Where: tab title (`RetroAchievementsListScreen::CreateTabs`) and `ItemHeader` in `CreateLeaderboardsTab`, listing the game's leaderboards (each row opens the detail screen).
  - Meaning: the game's leaderboard definitions (in-game high-score tables tracked by RA), not the PPSSPP UI itself.

- **`Leaderboard tracker`** — "Leaderboard tracker"
  - Where: (a) label of the screen-position picker for `g_Config.iAchievementsLeaderboardTrackerPos` (**Customize** tab, `CreateCustomizeTab`); (b) setting label in Core/Config.cpp:555 (`.Label(I18NCat::ACHIEVEMENTS, "Leaderboard tracker")`), rendered by the generic SettingsPage on the Developer tools "Tests" page (UI/DeveloperSettingsPages.cpp:461).
  - Meaning: persistent on-screen text showing a live value while leaderboard attempt(s) run (`OSDType::LEADERBOARD_TRACKER`; `RC_CLIENT_EVENT_LEADERBOARD_TRACKER_SHOW/UPDATE/HIDE` → `g_OSD.ShowLeaderboardTracker`, Core/RetroAchievements.cpp). Several active leaderboards may share one tracker. The setting picks its screen position; "None" hides it.
  - Note: "tracker" = live value overlay, not a device and not the attempt-start notification.

- **`Leaderboard attempt started or failed`** — "Leaderboard attempt started or failed"
  - Where: label of the position picker for `g_Config.iAchievementsLeaderboardStartedOrFailedPos` (**Customize** tab).
  - Meaning: placement control for the `%1: Attempt started` / `%1: Attempt failed` OSD notifications (`OSDType::LEADERBOARD_STARTED_FAILED`). An "attempt" = one run of an active leaderboard (one race, one credit…), starting at its start condition and ending in submission or failure.

- **`Leaderboard result submitted`** — "Leaderboard result submitted"
  - Where: label of the position picker for `g_Config.iAchievementsLeaderboardSubmittedPos` (**Customize** tab).
  - Meaning: placement control for the OSD notification (`OSDType::LEADERBOARD_SUBMITTED`) built from `Submitted %1 for %2` + `Rank:`/`Best:`. Distinct from the sound label `Leaderboard score submission` — this one is the notification position.

- **`Leaderboard score submission`** — "Leaderboard score submission"
  - Where: **Customize** tab → "Sound Effects" group: label of the audio-file chooser for the sound played on leaderboard submission (`g_Config.sAchievementsLeaderboardSubmitAudioFile`, `UISound::LEADERBOARD_SUBMITTED`; played in the `RC_CLIENT_EVENT_LEADERBOARD_SCOREBOARD` handler).
  - Meaning: names the event whose sound you are customizing. Not a toggle for whether submission is enabled.

- **`Leaderboard submission is enabled`** — "Leaderboard submission is enabled"
  - Where: **no reference in code** (see Needs Clarification).

- **`%1: Attempt started`** — "%1: Attempt started"
  - Where: OSD (`g_OSD.ShowLeaderboardStartEnd`) on `RC_CLIENT_EVENT_LEADERBOARD_STARTED` (Core/RetroAchievements.cpp `event_handler_callback`). `%1` = leaderboard title (falls back to its description if empty — Burnout Legends workaround).
  - Meaning: a leaderboard attempt has begun; the leaderboard description is shown as second line.

- **`%1: Attempt failed`** — "%1: Attempt failed"
  - Where: same handler, `RC_CLIENT_EVENT_LEADERBOARD_FAILED` — attempt ended without a valid submission.
  - Note: keep consistent with `%1: Attempt started` (%1 = leaderboard title).

- **`Submitted %1 for %2`** — "Submitted %1 for %2"
  - Where: OSD on `RC_CLIENT_EVENT_LEADERBOARD_SCOREBOARD` (Core/RetroAchievements.cpp). `%1` = submitted score (`scoreboard->submitted_score`, or the tracker value if no scoreboard data), `%2` = leaderboard title. `: Rank: %1` or `: Best: %1` is appended.
  - Meaning: your result was registered on the server for that leaderboard.

- **`Rank: %1`** — "Rank: %1"
  - Where: appended to `Submitted %1 for %2` **when the submitted score is your best** (`scoreboard->best_score == scoreboard->submitted_score`); `%1` = your new rank (`scoreboard->new_rank`).
  - Meaning: "your new rank: N" — only on (new) personal-best submissions.

- **`Best: %1`** — "Best: %1"
  - Where: appended instead when the submitted score was **not** your best; `%1` = your existing best score (`scoreboard->best_score`).
  - Meaning: "your best remains: X". Same message slot as `Rank: %1`, different condition — don't merge.

- **`Top players`** — "Top players"
  - Where: first option of the `ChoiceStrip` on the leaderboard detail screen (`CreateLeaderboardTab`).
  - Meaning: show the overall best entries (`rc_client_begin_fetch_leaderboard_entries(..., 0, 25)` — top 25). Default view.

- **`Around me`** — "Around me"
  - Where: second option of the same switcher.
  - Meaning: show entries surrounding your own rank (`rc_client_begin_fetch_leaderboard_entries_around_user(..., 10)`), i.e. the leaderboard centered on you.

- **`Achievements with active challenges`** — "Achievements with active challenges"
  - Where: collapsible section header on the achievements list — bucket title for `RC_CLIENT_ACHIEVEMENT_BUCKET_ACTIVE_CHALLENGE` (`AchievementBucketTitle`).
  - Meaning: challenge achievements whose unlock conditions are **live right now** — rcheevos puts an achievement here when its trigger is in the "primed" state (external, rcheevos `rc_client.c` `rc_client_update_achievement_display_information`), i.e. you are actively in the situation the achievement requires (mid-boss-fight etc.). These show the challenge indicator overlay.

- **`Active Challenges`** — "Active Challenges"
  - Where: `ItemHeader` on the pause screen (UI/PauseScreen.cpp ~line 680), only in the branch where save-state controls are hidden (typically hardcore); lists achievements returned by `Achievements::GetActiveChallengeIDs()`.
  - Meaning: the challenge achievements currently in progress in this session. Distinct from `Achievements with active challenges` (list-screen bucket header).

- **`Challenge indicator`** — "Challenge indicator"
  - Where: label of the position picker for `g_Config.iAchievementsChallengePos` (**Customize** tab).
  - Meaning: the small overlay showing just the achievement's icon while its challenge is active (`OSDType::ACHIEVEMENT_CHALLENGE_INDICATOR`, "Achievement icon ONLY, no auto-hide" — Common/System/OSD.h; `RC_CLIENT_EVENT_ACHIEVEMENT_CHALLENGE_INDICATOR_SHOW/HIDE`).

- **`Almost completed`** — "Almost completed"
  - Where: section header — bucket title for `RC_CLIENT_ACHIEVEMENT_BUCKET_ALMOST_THERE` (`AchievementBucketTitle`); list created with `RC_CLIENT_ACHIEVEMENT_LIST_GROUPING_PROGRESS`.
  - Meaning: locked achievements where you already have **≥ 80% measured progress** (external, rcheevos `rc_client.c`: `measured_percent >= 80.0 → ALMOST_THERE`). Means "achievements you nearly have", not "game almost 100%-ed".

- **`Recently unlocked`** — "Recently unlocked"
  - Where: section header — bucket title for `RC_CLIENT_ACHIEVEMENT_BUCKET_RECENTLY_UNLOCKED`.
  - Meaning: achievements you unlocked in the **last 10 minutes** (external, rcheevos `rc_client.c`: `RC_CLIENT_RECENT_UNLOCK_DELAY_SECONDS (10 * 60)`, `unlock_time >= now - delay`); older unlocks fall into `Unlocked`. Passive state label.

- **`Locked`** — "Locked"
  - Where: section header — bucket title for `RC_CLIENT_ACHIEVEMENT_BUCKET_LOCKED`: all not-yet-earned achievements not in another bucket.
  - Meaning: "not yet earned".

- **`Unlocked`** — "Unlocked"
  - Where: section header — bucket title for `RC_CLIENT_ACHIEVEMENT_BUCKET_UNLOCKED`.
  - Meaning: "earned" (rcheevos `rc_client.h`: `RC_CLIENT_ACHIEVEMENT_STATE_UNLOCKED = 2, /* earned by user */` — external). RA tracks hardcore vs softcore unlocks internally; this bucket just means earned.

- **`Unsupported`** — "Unsupported"
  - Where: section header — bucket title for `RC_CLIENT_ACHIEVEMENT_BUCKET_UNSUPPORTED`.
  - Meaning: achievements the runtime could not process — rcheevos marks them `STATE_DISABLED /* not supported by this version of the runtime */` when their definition fails to parse (external, `rc_client.h` + `rc_client.c`); they are listed but never unlock. Distinct from `RetroAchievements are not available for this game` (whole-game state).

- **`Earned`** — "You have unlocked %1 of %2 achievements, and earned %3 of %4 points"
  - Where: the long game summary sentence: `ApplySafeSubstitutions(ac->T("Earned", "You have unlocked %1 of %2 achievements, earning %3 of %4 points"), …)` in `Achievements::GetGameAchievementSummary` (Core/RetroAchievements.cpp). Shown in the achievements list header, the pause screen, and the game-load OSD placard. `%1` = unlocked count, `%2` = total achievements, `%3` = points earned, `%4` = points available; status flags (`Hardcore Mode`, `Encore Mode`, `Unofficial achievements`) are appended as extra lines.
  - Meaning: overall progress sentence for the game (or subset). **Misleading key**: the key is the single word `Earned` but the value is a full sentence — translate the value; do not translate it as a standalone word.
  - Note: the code's fallback text ("…earning %3 of %4 points") differs slightly from the ini ("…and earned %3 of %4 points"); the ini value is authoritative. Keep all four `%N` placeholders (reorder freely).

- **`%1 achievements, %2 points`** — "%1 achievements, %2 points"
  - Where: second line of the OSD when a game or subset is completed (`RC_CLIENT_EVENT_GAME_COMPLETED` / `RC_CLIENT_EVENT_SUBSET_COMPLETED`, Core/RetroAchievements.cpp); `%1` = achievements unlocked, `%2` = points unlocked.
  - Meaning: count recap on the completion celebration popup.

- **`Completed %1`** — "Completed %1"
  - Where: OSD title of the completion popup when hardcore is **not** active: `rc_client_get_hardcore_enabled(...) ? "Mastered %1" : "Completed %1"` (same event handler). `%1` = game title (or subset title for `SUBSET_COMPLETED`).
  - Meaning: "all achievements of %1 earned (outside Hardcore)". Note RA website terminology: *beaten* = progression/win done; *completed* = all achievements softcore — this string is the latter.

- **`Mastered %1`** — "Mastered %1"
  - Where: same popup when hardcore **is** active at completion time.
  - Meaning: RA "Mastery": the whole set earned in Hardcore mode. Distinct from `Completed %1` — do not merge.

### RAIntegration

RAIntegration is an external **Windows-only developer toolkit** (a DLL, `RAIntegration.dll`, placed next to PPSSPP.exe) for creating/editing achievements; PPSSPP loads it via rcheevos' `rc_client_raintegration` API (comment at top of Core/RetroAchievements.cpp points to its GitHub releases). Only relevant in builds compiled with `RC_CLIENT_SUPPORTS_RAINTEGRATION`. Keep "RAIntegration" untranslated.

- **`Enable RAIntegration (for achievement development)`** — "Enable RAIntegration (for achievement development)"
  - Where: **Developer Tools** tab (UI/RetroAchievementScreens.cpp `CreateDeveloperToolsTab`), checkbox `g_Config.bAchievementsEnableRAIntegration`; the control only exists under `#ifdef RC_CLIENT_SUPPORTS_RAINTEGRATION`.
  - Meaning: loads the toolkit at startup instead of the plain runtime (loading is (re)done in `Achievements::InitializeRAIntegration`, Core/RetroAchievements.cpp). The parenthetical says who it's for.

- **`%1 loaded.`** — "%1 loaded."
  - Where: OSD success in `load_integration_callback` (Core/RetroAchievements.cpp) when the DLL loaded fine; `%1` = `RAIntegration.dll`.
  - Meaning: toolkit active. Keep `%1` as a filename slot.

- **`RAIntegration is enabled, but %1 was not found.`** — "RAIntegration is enabled, but %1 was not found."
  - Where: OSD warning in `load_integration_callback` for `RC_MISSING_VALUE`; `%1` = `RAIntegration.dll`.
  - Meaning: setting is on but the DLL isn't next to PPSSPP.exe; PPSSPP continues with plain achievements.

- **`RAIntegrationNotWritable`** — "RAIntegration needs to write next to PPSSPP.exe, which this install doesn't allow. Use the portable .zip version instead."
  - Where: OSD error in `Achievements::InitializeRAIntegration` when the exe directory isn't writable (`!File::IsDirectoryWritable(exeDir)`; see issue #21260 in the code comment). Toolkit loading is skipped entirely.
  - Meaning: RAIntegration writes cache/local achievement data beside the executable; an install under e.g. Program Files forbids that and would take the emulator down when the toolkit loads a set, so PPSSPP refuses. **The key is an untranslated identifier; only the value is user-visible.** Keep the portable-.zip workaround advice.

- **`You have unsaved RAIntegration changes.`** — "You have unsaved RAIntegration changes."
  - Where: first line of the exit-confirmation text from `GetConfirmExitMessage` (UI/PauseScreen.cpp:968) when `Achievements::RAIntegrationDirty()` (`rc_client_raintegration_has_modifications`, Core/RetroAchievements.cpp).
  - Meaning: you edited achievement data in the toolkit without saving; warns developers before quitting. May be followed by further confirmation lines.

### Game states, errors, login

- **`Achievements enabled`** — "Achievements enabled"
  - Where: checkbox in **Account** tab → "Settings" group (`g_Config.bAchievementsEnable`; toggling calls `Achievements::UpdateSettings()` which starts/stops the client).
  - Meaning: master switch for the whole feature.

- **`Achievements are disabled`** — "Achievements are disabled"
  - Where: `NoticeView` at the top of the **Account** tab when `!g_Config.bAchievementsEnable` (UI/RetroAchievementScreens.cpp:393).
  - Meaning: explains why the tab shows no login UI. Not an error.

- **`This game has no achievements`** — "This game has no achievements"
  - Where: `GetGameAchievementSummary` (Core/RetroAchievements.cpp) when the summary has 0 achievements; shown wherever the summary appears (list screen, pause screen, load placard).
  - Meaning: the game was identified but has no achievement set (or the set is empty for this subset).

- **`RetroAchievements are not available for this game`** — "RetroAchievements are not available for this game"
  - Where: OSD info in `identify_and_load_callback` for `RC_NO_GAME_LOADED`, and in `change_media_callback` (Core/RetroAchievements.cpp) — shown with the game's title as message title.
  - Meaning: the game's hash isn't in the RetroAchievements database (or the file type isn't hashable), so no achievements will work for it. Neutral info, not a failure of the connection.

- **`Failed to identify game. Achievements will not unlock.`** — "Failed to identify game. Achievements will not unlock."
  - Where: OSD error in `identify_and_load_callback` / `change_media_callback` default error branch (Core/RetroAchievements.cpp).
  - Meaning: the hash/identify request failed (server/other error) — unlike "not available for this game", this is an error state; achievements won't work this session.

- **`Failed to connect to RetroAchievements. Achievements will not unlock.`** — "Failed to connect to RetroAchievements. Achievements will not unlock."
  - Where: `Achievements::ShowNotLoggedInMessage` (Core/RetroAchievements.cpp), shown when achievements are enabled and a token exists but login failed/absent at game start (`SetGame`) or game load returned `RC_NO_RESPONSE`.
  - Meaning: no session with the server → unlocks can't be recorded.

- **`Can't log in to RetroAchievements right now`** — "Can't log in to RetroAchievements right now"
  - Where: **no reference in code** (see Needs Clarification).

- **`Failed logging in to RetroAchievements`** — "Failed logging in to RetroAchievements"
  - Where: (a) OSD warning on failed token login (`login_token_callback`, with the server's error as second line); (b) `NoticeView` header in the **Account** tab when `Achievements::LoginProblems()` reports a stored failure (with error text and a "Log out" action).
  - Meaning: login failure headline; the actual reason is the second line.

- **`Contacting RetroAchievements server...`** — "Contacting RetroAchievements server..."
  - Where: progress-bar label for every RA server request (`server_call_callback` in Core/RetroAchievements.cpp passes it to the download manager).
  - Meaning: transient "waiting for server" status. The ellipsis is part of the string.

- **`Reconnected to RetroAchievements.`** — "Reconnected to RetroAchievements."
  - Where: OSD success in `login_token_callback` when a **background** (non-initial) token login succeeds (Core/RetroAchievements.cpp; background retries happen from `Idle()` at the menu).
  - Meaning: connection restored after a drop; achievement syncing continues.

- **`Syncing achievements data...`** — "Syncing achievements data..."
  - Where: **no reference in code** (see Needs Clarification).

- **`Save state loaded without achievement data`** — "Save state loaded without achievement data"
  - Where: OSD warning in `Achievements::DoState` (Core/RetroAchievements.cpp) — shown when a loaded savestate contains no "Achievements" section (made by an older PPSSPP or when achievements were off) or contains zero bytes of progress data; the achievement runtime progress is then reset (`rc_client_reset`).
  - Meaning: the savestate didn't carry achievement progress, so tracked progress/challenge state was reset — achievements won't retroactively unlock from it.

- **`Register on www.retroachievements.org`** — "Register on www.retroachievements.org"
  - Where: link-style `Choice` in the **Account** tab (shown while logged out); opens `https://retroachievements.org/createaccount.php` (UI/RetroAchievementScreens.cpp `CreateAccountTab`).
  - Meaning: call-to-action to create an account. Keep the site name; "Register on" is the part to translate.

- **`RetroAchievements website`** — "RetroAchievements website"
  - Where: **Account** tab → "Links" group; opens `https://www.retroachievements.org/`.
  - Meaning: link to the site's front page.

- **`How to use RetroAchievements`** — "How to use RetroAchievements"
  - Where: **Account** tab → "Links" group; opens the PPSSPP docs page `https://www.ppsspp.org/docs/reference/retro-achievements`.
  - Meaning: link to PPSSPP's own usage guide (not RA's docs).

### Notifications & on-screen display

- **`Achievement unlocked`** — "Achievement unlocked"
  - Where: (a) label of the audio-file chooser for the unlock sound (**Customize** tab → "Sound Effects": `g_Config.sAchievementsUnlockAudioFile`, `UISound::ACHIEVEMENT_UNLOCKED`); (b) label of the position picker for `g_Config.iAchievementsUnlockedPos` (**Customize** tab → "Notifications"). The unlock sound is triggered by `RC_CLIENT_EVENT_ACHIEVEMENT_TRIGGERED` ("achievement_unlocked", Core/RetroAchievements.cpp).
  - Meaning: names the event (an achievement was earned). The on-screen popup itself renders the achievement card (badge, title, description, points — `RenderAchievement` with `AchievementRenderStyle::UNLOCKED`), not this text.
  - Note: one key serves as both the sound label and the notification-position label; translation must fit both.

- **`Achievement progress`** — "Achievement progress"
  - Where: label of the position picker for `g_Config.iAchievementsProgressPos` (**Customize** tab).
  - Meaning: the progress indicator overlay — achievement icon plus its `measured_progress` text (e.g. "3/5") shown while a progress-tracked achievement's counter changes (`OSDType::ACHIEVEMENT_PROGRESS`, "auto-hide after 2s" — Common/System/OSD.h; events `RC_CLIENT_EVENT_ACHIEVEMENT_PROGRESS_INDICATOR_SHOW/UPDATE/HIDE`).

- **`Notifications`** — "Notifications"
  - Where: group header in the **Customize** tab above the six notification-position pickers; also a header on the Developer tools "Tests" page (UI/DeveloperSettingsPages.cpp, `I18NCat::ACHIEVEMENTS`).
  - Meaning: plain section label for on-screen notification settings.

- **`None`** — "None"
  - Where: first entry of the position choice lists `{"None", "Bottom Left", …}` in `CreateCustomizeTab` (and the same array in Core/Config.cpp `achievementsLeaderboardTrackerPosNames`/`notificationPosNames`).
  - Meaning: "do not show this at all" — the OSD layout code skips entries whose position resolves to -1 ("NONE", UI/OnScreenDisplay.cpp ~line 185). So "None" here means hidden/off, not "empty position".
  - Note: context is a screen-position dropdown; translate as the "disabled/none" option of a list.

- **Screen position names** — see under Self-evident for the eight names; they all mean literal screen positions (Top/Bottom/Center × Left/Center/Right) in the same dropdowns as `None`. **Two translation copies exist**: the Customize-tab dropdowns translate the choice strings via `I18NCat::DIALOG` (UI/RetroAchievementScreens.cpp passes `I18NCat::DIALOG` to `PopupMultiChoice`, which does `T(category_, choice)` — Common/UI/PopupScreens.cpp `TranslateChoice`), while the generic SettingsPage path (Developer tools "Tests" page) translates choices "in the same category as the text", i.e. `[Achievements]` (UI/SettingsPage.h:103). Keep the `[Achievements]` and `[Dialog]` copies identical.

### Statistics (debug builds only)

- **`Statistics`** — "Statistics"
  - Where: tab added in `RetroAchievementsListScreen::CreateTabs` **only `#ifdef _DEBUG`** (UI/RetroAchievementScreens.cpp).
  - Meaning: developer tab header. Not visible in release builds.

- **`Bad memory accesses`** — "Bad memory accesses"
  - Where: `InfoItem` in `CreateStatisticsTab` (debug builds only), showing `stats.badMemoryAccessCount`.
  - Meaning: count of reads the achievement definitions attempted at invalid RAM addresses (`read_memory_callback` in Core/RetroAchievements.cpp increments the counter and returns 0). A set-quality diagnostic number.

- **`Log bad memory accesses`** — "Log bad memory accesses"
  - Where: checkbox in **Developer Tools** (`g_Config.bAchievementsLogBadMemReads`).
  - Meaning: when on, each bad access also emits a WARN log line ("RetroAchievements PeekMemory: Bad address …"); when off they're only counted. For set developers.

### Settings screen structure

- **`Sound Effects`** — "Sound Effects"
  - Where: (a) checkbox in the **Account** tab → "Settings" group (`g_Config.bAchievementsSoundEffects`, UI/RetroAchievementScreens.cpp `CreateAccountTab`); (b) group header in the **Customize** tab above the two audio-file choosers and the volume slider (`CreateCustomizeTab`).
  - Meaning: (a) on/off switch for achievement/leaderboard sounds; (b) section label for sound customization. One key, two roles — translation must work as both a checkbox label and a section header.

- **`Account`** — "Account"
  - Where: first tab of RetroAchievementsSettingsScreen (`CreateTabs`).
  - Meaning: login/user info + enable/hardcore/sounds settings + links. Standard account section.

- **`Customize`** — "Customize"
  - Where: second tab of RetroAchievementsSettingsScreen; header of its `PaneTitleBar` too.
  - Meaning: sound + notification-position customization.

- **`Name`** — "Name"
  - Where: `InfoItem` in the **Account** tab (logged-in state), shown **only when `display_name != username`** (UI/RetroAchievementScreens.cpp `CreateAccountTab`).
  - Meaning: the user's display name (RA is gaining display names distinct from login names). Not a text field — a read-only info row.

- **`Links`** — "Links"
  - Where: **category caveat** — the visible group header in the **Account** tab is `di->T("Links")`, i.e. looked up in the **[Dialog]** category (UI/RetroAchievementScreens.cpp:469). The `[Achievements]` copy of `Links` is not referenced anywhere in this clone's code (see Needs Clarification).
  - Meaning: plain group label above the two external-link buttons. If you translate the `[Achievements]` copy, translate the `[Dialog]` copy identically.

### Unreferenced in this clone (probably leftovers)

Verified by repo-wide search of all source files (only `assets/lang/*.ini` match):

- **`Test Mode`** — no code reference.
- **`In Encore mode - listings may be wrong below`** — no code reference (only the "…unlock state may not be accurate" variant is used).
- **`Leaderboard submission is enabled`** — no code reference.
- **`Can't log in to RetroAchievements right now`** — no code reference.
- **`Syncing achievements data...`** — no code reference.

See Needs Clarification for the open questions.

### Self-evident (no notes needed)

- `Achievements` — Achievements (tab title / pause-screen button / default subset title)
- `Top Left` — Top left
- `Top Center` — Top center
- `Top Right` — Top right
- `Bottom Left` — Bottom left
- `Bottom Center` — Bottom center
- `Bottom Right` — Bottom right
- `Center Left` — Center left
- `Center Right` — Center right

### Skipped

- (none — every key in this section needed at least a usage note.)

## Needs Clarification

1. **`Test Mode`** — no usage found in any C++/header/resource file. Is this a leftover from an older dev screen (or intended for the RAIntegration workflow), or is it displayed by external tooling? If unused, should it be removed from the ini?
2. **`In Encore mode - listings may be wrong below`** — only the companion string `In Encore mode - unlock state may not be accurate` is used (UI/RetroAchievementScreens.cpp:121). Was this one meant for a different screen (e.g. the Leaderboards tab or the pause screen) or for older list code? Translators need to know whether it will ever be shown.
3. **`Leaderboard submission is enabled`** — no usage found. Is it a leftover planned notification ("submission is enabled/active"), or does some other build (RAIntegration menu?) use it?
4. **`Can't log in to RetroAchievements right now`** — no usage found. Was it replaced by `Failed logging in to RetroAchievements` / `Failed to connect to RetroAchievements. Achievements will not unlock.`?
5. **`Syncing achievements data...`** — no usage found. Planned for the game-load/identify flow (`g_isIdentifying`)?
6. **`Rich Presence`** — the key is never looked up (`T("Rich Presence")` absent; the rich-presence message is displayed without a label, UI/PauseScreen.cpp:598). Was it intended as a label for that line? If so, where?
7. **`Links`** ([Achievements] copy) — the code looks up `Links` in the [Dialog] category (`di->T("Links")`), so the `[Achievements]` entry appears dead. Confirm whether it should be removed or whether some code should switch to `ac->T("Links")`.
8. **`Almost completed` / `Recently unlocked` thresholds** — grounded from rcheevos master (≥80% measured progress; unlocks within 10 minutes), but this clone's pinned rcheevos submodule isn't checked out; confirm the pinned version behaves the same before translating around the exact thresholds.
# Translation Grounding: [System] (en_US.ini lines 1615–1800)

Scope: keys under the `[System]` section only. Every behavior claim below was verified against the C++ source in `ppsspp/`. Where a key's live usage sits in another UI category/screen despite living in [System], that is noted. Legacy keys (present in the ini but with no lookup in current code) are flagged explicitly.

## [System]

### CPU core options (Developer tools screen — "CPU Core" header)

- **`CPU Core`** — "CPU core"
  - Where: setting `CPUCore` (`Core/Config.cpp` `cpuSettings`, `.Label(I18NCat::SYSTEM, "CPU Core").Choices(cPUCoreNames)`), but the picker is rendered on the **Developer tools** screen: `UI/DeveloperSettingsPages.cpp` `BuildGeneralPage()` — `page->Header({I18NCat::SYSTEM, "CPU Core"})` then `page->Setting(&g_Config.iCpuCore)`. Same string also heads the crash/debug overlay text (`UI/DebugOverlay.cpp:520` "CPU Core: %s").
  - Meaning: selects the execution engine for the emulated PSP's MIPS CPU (enum `CPUCore`, `Core/ConfigValues.h:80`: INTERPRETER=0, JIT=1, IR_INTERPRETER=2, JIT_IR=3). Changing it clears the JIT cache (`OnJitAffectingSetting` → `REQUEST_CLEAR_JIT`).
  - Note: "Core" = CPU execution core (like "engine"), not "core of the app".

- **`Dynarec/JIT (recommended)`** — "Dynarec/JIT (recommended)"
  - Where: choice 1 of `cPUCoreNames[]` (`Core/Config.cpp:565`); enum `CPUCore::JIT`. Hidden from the picker where `SYSPROP_CAN_JIT` is false (`UI/DeveloperSettingsPages.cpp:146-148`, e.g. iOS App Store builds).
  - Meaning: dynamic recompilation — blocks of guest MIPS instructions are translated to host machine code at runtime. The default and by far the fastest core. "(recommended)" is part of the source string and reflects that this is the default.
  - Note: "Dynarec" and "JIT" are the same thing; the slash-pair is historical. Keep whichever term your language's emulator community knows.

- **`Interpreter`** — "Interpreter"
  - Where: choice 0 of `cPUCoreNames[]`; enum `CPUCore::INTERPRETER`.
  - Meaning: executes every guest MIPS instruction directly, one by one, without generating host code. Slowest, but the reference/fallback core. Behavioral difference: it is the only core that raises the emulated FPU exceptions — comment at `UI/DeveloperSettingsPages.cpp:155-156` ("Only the interpreter raises these") next to the FPU exception traps setting.

- **`IR Interpreter`** — "IR interpreter" (en_US value is lowercase "interpreter")
  - Where: choice 2 of `cPUCoreNames[]`; enum `CPUCore::IR_INTERPRETER`.
  - Meaning: compiles MIPS to PPSSPP's intermediate representation (IR) and then *interprets* the IR instead of emitting host code. Slower than the JIT; mainly for debugging/verifying the IR pipeline.
  - Note: "IR" = intermediate representation, a fixed technical acronym; do not translate.

- **`JIT using IR`** — "JIT using IR"
  - Where: choice 3 of `cPUCoreNames[]`; enum `CPUCore::JIT_IR`. The UI **always hides this choice** (`UI/DeveloperSettingsPages.cpp:149-152`): "Every native JIT is IR-based, so 'JIT using IR' is the same as 'JIT'."
  - Meaning: JIT that compiles via the IR stage. Effectively identical to "Dynarec/JIT" today; kept for old config files.

- **`(broken)`** — "(broken)"
  - Where: `Core/SaveState.cpp` `SaveState::GetTitle()` (~line 668-675): when a savestate file cannot be opened/parsed at all ("The file can't be loaded - let's note that"), the savestate browser title becomes `"<filename> (broken)"`.
  - Meaning: marker appended to an unreadable/corrupt savestate file's display name in the load/save list.
  - Note: **not** a CPU-core suffix (a common guess) — no "(broken)" entry exists in the CPU core list. Keep it short; it labels a corrupt file.

### Memory & emulation timing (System page, "Emulation" section)

- **`Fast Memory`** — "Fast memory (unstable)"
  - Where: System page, "Emulation" (`UI/Settings/SystemPage.cpp:240`); config `FastMemoryAccess` / `g_Config.bFastMemory`, **default on** (`Core/Config.cpp:573`). Toggling clears the JIT cache.
  - Meaning: when on, generated code omits explicit address validation before guest memory loads/stores: `Core/MIPS/IR/IRCompVFPU.cpp:749,763` skip `ValidateAddress128` ops when `bFastMemory`; the `ApplyMemoryValidation` optimization pass disables itself when `bFastMemory` (`Core/MIPS/IR/IRPassSimplify.cpp:1906`). Invalid addresses are caught *after the fact* by the host fault handler (`Core/MemFault.cpp`) which either recovers (see next entry) or raises a memory exception ("Game crashed" screen). When off, validation ops run before accesses, failing early/precisely like real PSP error handling.
  - Why "unstable": games that deliberately probe or rely on bad-address behavior can crash or misbehave, since error detection is delayed/lossy with fast memory. "Memory" = emulated PSP RAM addressing, not app memory.

- **`Ignore bad memory accesses`** — "Ignore bad memory accesses"
  - Where: System page, right below Fast memory (`UI/Settings/SystemPage.cpp:243`); config `bIgnoreBadMemAccess`.
  - Meaning: when a bad guest memory access is caught (`Core/MemFault.cpp` ~line 275-292, log "Bad memory access detected and ignored"), skip the faulting instruction and continue (destination register left 0/untouched for reads) instead of halting with a memory exception. There is a per-frame cap to avoid app-not-responding hangs. In short: "don't crash the game on bad memory access".

- **`Change CPU Clock`** — "Change emulated PSP's CPU clock (unstable)"
  - Where: System page, "Emulation" (`UI/Settings/SystemPage.cpp:250`); config `CPUSpeed` / `g_Config.iLockedCPUSpeed`, range 0–1000, `.Units("MHz").ZeroLabel("Auto")` (`Core/Config.cpp:579-582`).
  - Meaning: locks the **emulated** PSP's CPU clock instead of letting the game choose it. `Core/HLE/scePower.cpp` `PowerApplyClocks()`: when > 0, PLL/bus frequencies and `CoreTiming::SetClockFrequencyHz()` are derived from the locked value, and games' own `scePowerSetClockFrequency` results read back as the locked value (log "locked by user config"). When 0 ("Auto"), the game's own clock settings apply (real PSP: 222 MHz default, games may raise to 333).
  - Note: does **not** change host performance — it changes emulated timing. "unstable" because games calibrate delays/loops for specific clocks; under/overclocking desyncs music, race timers, loading behavior. Compat flag `RequireDefaultCPUClock` forces 0 for some games (`scePower.cpp:82-84`). Make clear it's the *emulated PSP's* CPU.

- **`MHz, 0:default`** — "MHz, 0 = default"
  - Where: **no lookup found in current C++ code** (only lang files). The live CPU-clock row uses `.Units("MHz")` and `.ZeroLabel("Auto")` instead.
  - Meaning: legacy hint for the CPU clock slider ("0 = default clock"). See Needs Clarification.

- **`Low-latency CPU timing`** — "Low-latency CPU timing" (formerly `Force real clock sync (slower, less lag)`)
  - Where: System page, "Emulation"; disabled while auto frameskip is on (`UI/Settings/SystemPage.cpp` `.Disabled(&g_Config.bAutoFrameSkip)`); config `ForceLagSync2` / `bForceLagSync`. Hint: `LowLatencyCPUTiming Tip`.
  - Meaning: `Core/HLE/sceDisplay.cpp` `hleLagSync()` — an event run every 1 ms that keeps **emulated time in step with the wall clock**, sleeping until real time catches up, instead of emulating each frame as fast as possible and then waiting. That spreads the emulator's output (audio, network packets) evenly over the frame and reads input closer to when it's used, so delay goes down. It costs a little more CPU, and when the device has little time to spare it falls back to normal per-frame timing by itself. See `docs/lag-sync.md`.
  - Note: "CPU timing" means the timing of the emulated PSP's CPU against real time, not the host CPU's speed. "Low-latency" = less delay, not "lower quality".

- **`LowLatencyCPUTiming Tip`** — "Runs emulation in step with real time instead of a frame at a time, for less audio, input and network delay. Uses a little more CPU, and goes back to normal timing when the device is too busy. Previously called "Force real clock sync"."
  - Where: hint under the setting above.
  - Note: "a frame at a time" = emulating a whole video frame in one go, then waiting. The old name must be the one this language actually showed: the translation of `Force real clock sync (slower, less lag)`, without the part in parentheses.

- **`I/O timing method`** — "I/O timing method"
  - Where: System page, "Emulation" (`UI/Settings/SystemPage.cpp:248`); config `IOTimingMethod`, choices `iOTimingMethodNames[]` (`Core/Config.cpp:563`), enum `IOTIMING_FAST/HOST/REALISTIC/UMDSLOWREALISTIC` (`Core/ConfigValues.h:204-207`), default FAST. Per-game.
  - Meaning: how long emulated file I/O (UMD disc / Memory Stick reads, file opens, module loads, async callbacks) *appears* to take inside the emulated PSP. Games often depend on I/O timing (loading screens, streaming). The four choices:

- **`Fast (lag on slow storage)`** — "Fast (lag on slow storage)" (IOTIMING_FAST, default)
  - Where: choice 0; `Core/HLE/sceIo.cpp` `__IoAsyncNotify`/`__IoSyncNotify` "else" branch — completes the emulated I/O as soon as the host operation is done, no simulated delay.
  - Meaning: fastest, no artificial delays. If your real storage is slow (SD card, network drive), the game visibly stalls waiting on real I/O — hence "lag on slow storage". Games needing UMD-like delays may break.

- **`Host (bugs, less lag)`** — "Host (bugs, less lag)" (IOTIMING_HOST)
  - Where: choice 1; `Core/HLE/sceIo.cpp:452-458` and `513-519` — polls the host async-I/O manager every 0.5 ms and completes the emulated I/O once the **host** file operation has actually finished.
  - Meaning: emulated I/O completion tracks real host completion closely (low latency = "less lag"). "bugs": not all operations queue on the host ("Not all async operations actually queue up", comment `sceIo.cpp:453`), so timing deviates from real hardware and can confuse games.
  - Note: **false friend** — "Host" = the computer/device running PPSSPP (its OS file system), NOT the emulated PSP.

- **`Simulate UMD delays`** — "Simulate UMD delays" (IOTIMING_REALISTIC)
  - Where: choice 2; `sceIo.cpp` — completion is rescheduled to computed finish ticks; delays from `__IoOpenDelayUs` (UMD ≈ 4 ms per open, Memory Stick ≈ 10 ms; `sceIo.cpp:1103-1107`) and `__IoReadDelayUs` size-based estimates.
  - Meaning: adds realistic fixed delays so file I/O takes about as long as on a real PSP (UMD drive / Memory Stick). Games that depend on loading timing behave more correctly. (Compat flag `ForceUMDDelay` forces this mode per game.)

- **`Simulate UMD slow reading speed`** — "Simulate UMD slow reading speed" (IOTIMING_UMDSLOWREALISTIC)
  - Where: choice 3; like realistic, but reads use the slow UMD transfer rate: `__IoReadDelayUs` (`sceIo.cpp:1111-1114`) computes `size / 4.2` µs (≈ 4.2 MB/s, real UMD streaming speed) instead of the fast-path `size / 100` µs; floor 100 µs.
  - Meaning: for games that stream data from the UMD and break (or misbehave) if data arrives too fast; heavier simulated read delays than "Simulate UMD delays". (Compat flag `ForceUMDReadSpeed` selects this behavior per game.)

- **`Cache full ISO in RAM`** — "Cache full ISO in RAM"
  - Where: System page, "Emulation"/storage area, only shown when `SYSPROP_ENOUGH_RAM_FOR_FULL_ISO` (`UI/Settings/SystemPage.cpp:246-248`); config `CacheFullIsoInRam`, per-game.
  - Meaning: `Core/System.cpp:895-903` — when booting a PSP ISO/CSO (`PSP_ISO`, `PSP_ISO_NP`), wraps the file in a `RamCachingFileLoader` that loads the whole disc image into host RAM so in-game loads never touch slow storage. Ignored for non-ISO files. Needs a device with enough RAM.

### Savestates & rewind (System page, "Save states" section)

- **`Auto load savestate`** — "Auto load savestate"
  - Where: System page, "Save states" (`UI/Settings/SystemPage.cpp:270-275`); config `AutoLoadSaveState`, choices `autoLoadSaveStateNames[] = { "Off", "Oldest Save", "Newest Save", "Slot 1".."Slot 5" }` (`Core/Config.cpp:264`).
  - Meaning: `UI/EmuScreen.cpp` `EmuScreen::AutoLoadSaveState()` (~line 2085-2130): at game boot, automatically load a savestate for that game. "Off" = never. A fixed Slot = load that slot if it has a save. `Core/Config.h:394` comment: "0 = off, 1 = oldest (deprecated), 2 = newest, 3+ = slot number". The UI hides "Oldest Save" unless already selected (`SystemPage.cpp:264-266`, "Hide 'Oldest Save' if not using that mode. It doesn't make sense."). On success an OSD message "Auto load savestate: N" appears (`EmuScreen.cpp:2123`). Also reused in the savestate list to tag the auto-load slot: `" (Auto load savestate)"` (`UI/PauseScreen.cpp:332`).

- **`Newest Save`** — "Newest save" / **`Oldest Save`** — "Oldest save"
  - Where: choices 2 / 1 of `autoLoadSaveStateNames`.
  - Meaning: at boot, pick the newest / oldest (by file modification time) of the game's existing savestate slots: `Core/SaveState.cpp` `GetNewestSlot()` / `GetOldestSlot()` compare mod-times of `<game>_<slot>.ppst` across slots 0..`iSaveStateSlotCount`. Newest = most recently saved state (typical choice); Oldest = least recently saved (deprecated legacy option).

- **`Savestate slot backups`** — "Savestate slot backups"
  - Where: System page, "Save states" (`UI/Settings/SystemPage.cpp:257`); config `EnableStateUndo` / `bEnableStateUndo`; default ON on desktop, **OFF on Android/iOS to save disk space** (`DefaultEnableStateUndo`, `Core/Config.cpp:396-402`).
  - Meaning: keeps a backup copy when a savestate operation would overwrite something: saving to a slot first copies the old state to `<game>_<slot>.undo.ppst` (`UNDO_STATE_EXTENSION`, `Core/SaveState.cpp:81`, used at `job.keepUndo` ~line 1296); loading a state stores the replaced state as `load_undo.ppst` (line 1416). The pause menu then offers "undo" buttons (`UI/PauseScreen.cpp:125,499`) — `UndoSaveSlot()` even swaps so you can "undo again to redo" (`Core/SaveState.cpp:863-870`). Disabled in RetroAchievements hardcore/networked sessions.
  - Note: this is what makes the "backup %c" / load-undo features possible (below). Translator: "backups" = copies kept for undoing, not compressed archives.

- **`undo %c`** — "backup %c"
  - Where: `Core/SaveState.cpp` `AppendSlotTitle()` (~line 623-628): when a savestate file is a *backup/undo* file (`*.undo.ppst`), its savestate-browser title is built as `"<game title> (" + StringFromFormat(translated, slotChar) + ")"`.
  - Meaning: `%c` is a printf-style format replaced with the **slot number as a digit character** — the filename's slot digit ('0'–'8') incremented to '1'–'9' (`detectSlot` lambda in the same function). English renders e.g. "Some Game (backup 3)" for slot 3's backup. It is **not** a key binding or the letter 'u'.
  - Note: the translation must keep `%c` (it goes through `StringFromFormat`); you may reposition it ("备份 %c"). Normal savestates render as "title (3)" with no extra word.

- **`Error: load undo state is from a different game`** — "Error: load undo state is from a different game"
  - Where: `Core/SaveState.cpp` `UndoLoad()` (~line 796-803), delivered through the savestate action callback as a failure message (OSD).
  - Meaning: "load undo" restores the backup taken just before a savestate load (undoing the load). Only one such backup exists at a time (`LOAD_UNDO_NAME = "load_undo.ppst"`), tagged with its game (`sStateLoadUndoGame`). If you press undo-load while a *different* game is running than the backup's game, this error is shown and nothing happens. "load undo state" = the backup state that undoes a load.

- **`Failed to load state. Error in the file system.`** / **`Failed to save state. Error in the file system.`** / **`Failed to load state for load undo. Error in the file system.`**
  - Where: `Core/SaveState.cpp` (~lines 777, 787, 805-810) savestate callbacks when the state path is empty / file access fails.
  - Meaning: generic host-filesystem I/O failures for loading, saving, and loading the undo backup respectively. "file system" = the host's filesystem (disk full, permissions), not the emulated PSP's.

- **`Ask to confirm on load`** — "Ask to confirm on load"
  - Where: System page, "Save states" (`UI/Settings/SystemPage.cpp:258`); config `ConfirmLoadState` / `bConfirmLoadState`, default off.
  - Meaning: before loading a savestate (which discards current in-game progress), show a yes/no prompt first. Savestate loads only — the exit prompt is a different feature (`Ask for exit confirmation after seconds`).

- **`Savestate slot count`** — "Savestate slot count"
  - Where: System page, "Save states" (`UI/Settings/SystemPage.cpp:263`); config `SaveStateSlotCount`, range 1–30, default 5 (`Core/Config.cpp:413`).
  - Meaning: how many savestate slots each game has (length of the slot list in the save/load UI). Auto-load is limited to the first five slots ("we'll keep this niche feature limited to the first five", `SystemPage.cpp:267-268`).

- **`Rewind Snapshot Interval`** — "Rewind Snapshot Interval (mem hog)"
  - Where: rendered on the **General** settings page (`UI/Settings/GeneralPage.cpp:300`) though its label/units live in [System]; config `RewindSnapshotInterval`, range 0–60, `.Units("seconds").ZeroLabel("Off")` (`Core/Config.cpp:410-411`).
  - Meaning: when > 0, PPSSPP snapshots the full emulated state into **RAM** at that wall-clock interval while the game runs (`Core/SaveStateRewind.cpp` `StateRingbuffer::Process()`), enabling "rewind". `Core/SaveStateRewind.h` header comment: "This ring buffer of states is for rewind save states, which are kept in RAM." It keeps up to 20 compressed snapshots (`REWIND_NUM_STATES = 20`) plus reference buffers, each a full PSP state (up to 64 MB RAM + GPU state) — hence "(mem hog)": potentially hundreds of MB of RAM. 0 = "Off" = no snapshots/rewind. Rewind button appears on the pause screen only when interval > 0 (`UI/PauseScreen.cpp:516`).
  - Note: preserve the "(mem hog)" warning sense ("memory hungry/greedy") — it is an honest warning.

- **`Savestate Slot`** — "Savestate slot"
  - Where: `UI/NativeApp.cpp:1488-1492`, `HandleGlobalMessage(UIMessage::SAVESTATE_DISPLAY_SLOT)`: OSD text `"Savestate slot: N"` shown when the current slot is changed (e.g. by hotkey), next to the savestate preview.
  - Meaning: names the numbered slot a savestate goes to/comes from. Slot numbers shown are 1-based (`GetCurrentSlot() + 1`).

### Confirmation button (System page, "PSP system settings" subpage)

- **`Confirmation Button`** — "Confirmation button"
  - Where: `UI/Settings/SystemPage.cpp:223`; config `ButtonPreference`, choices `buttonPreferenceNames[] = { "Use O to confirm", "Use X to confirm" }` (`Core/Config.cpp:1465,1493-1494`), **default X** (`PSP_SYSTEMPARAM_BUTTON_CROSS`).
  - Meaning: which face button the **emulated PSP** treats as "confirm" in its dialogs/menus — the registry value `CONFIG/SYSTEM/XMB/button_assign` (`Core/HLE/sceReg.cpp:775`). It also drives PPSSPP's own dialog confirm button (`Core/Dialog/PSPDialog.cpp:428-435`) and the touch control mapping (`Core/KeyMap.cpp:139`). Some games force circle-confirm via compat flag `ForceCircleButtonConfirm` (`Core/HLE/sceImpose.cpp:56`, `sceUtility.cpp:1611`).
  - Note: the famous regional convention — Japan: O (circle) = confirm; West: X (cross) = confirm. PPSSPP's default is X (`PSP_SYSTEMPARAM_BUTTON_CROSS`) regardless of region; on real hardware the factory default differs by region (O for Japanese units, X elsewhere). Keep O/X as letters.

- **`Use O to confirm`** — "Use O as confirmation button" / **`Use X to confirm`** — "Use X as confirmation button"
  - Where: the two choices of `Confirmation Button` (above).
  - Meaning: O = the PSP's Circle (○) button, X = the Cross (×) button. Choose which one confirms/accepts in PSP dialogs.

### PSP model & system settings (System page, "PSP system settings" subpage)

- **`PSP Model`** — "PSP model"
  - Where: `UI/Settings/SystemPage.cpp:216-220` (disabled while a game runs: `!PSP_IsInited()`); config `PSPModel`, choices `pSPModelNames[] = { "PSP-1000", "PSP-2000/3000" }` (`Core/Config.cpp:1469,1477`), default slim.
  - Meaning: which PSP hardware model is emulated. The decisive difference is **RAM size**: `Core/MemMap.cpp:619` `g_MemorySize = g_PSPModel == PSP_MODEL_FAT ? RAM_NORMAL_SIZE : RAM_DOUBLE_SIZE`, with `RAM_NORMAL_SIZE = 0x02000000` (32 MB) and `RAM_DOUBLE_SIZE` = 64 MB (`Core/MemMap.h:101-103`). PSP-1000 ("fat") = 32 MB; PSP-2000/3000 ("slim") = 64 MB. Games/homebrew needing >32 MB or detecting the model must run as slim. Changing the model after installing firmware triggers the `FirmwareOtherModel` warning (firmware files are model-specific). Also affects registry/system-param values games read.
  - Note: there is **no** separate "PSP-2000" or "PSP-3000" key — one combined choice "PSP-2000/3000" covers both models.

- **`PSP-1000`** — "PSP-1000" / **`PSP-2000/3000`** — "PSP-2000/3000"
  - Where: the two `PSP Model` choices.
  - Meaning: real PSP hardware model names (do not translate the designations). "PSP-1000" = original "fat" PSP (32 MB RAM); "PSP-2000/3000" = later "slim" models (64 MB RAM).

- **`Nickname`** — "Nickname"
  - Where: `UI/Settings/SystemPage.cpp:217` (PSP system settings) and `UI/Settings/NetworkingPage.cpp:210` — both call `AddNicknameChoice()` (`NetworkingPage.cpp:98`, a text input into `g_Config.sNickName`, max 32 chars).
  - Meaning: the emulated PSP's owner nickname (registry `NickName`, default "PPSSPP", `Core/Config.cpp` `systemParamSettings`) — shown by the XMB and games that greet the player.

- **`Time Format`** — "Time format" / **`Date Format`** — "Date format" / **`Daylight savings`** — "Daylight savings"
  - Where: `UI/Settings/SystemPage.cpp:219-221`; config `ParamTimeFormat`/`ParamDateFormat`/`DayLightSavings` in `systemParamSettings` (`Core/Config.cpp:1483-1492`).
  - Meaning: emulated PSP system parameters, exactly mirroring the real PSP's settings (`PSP_SYSTEMPARAM_TIME_FORMAT_*`, `PSP_SYSTEMPARAM_DATE_FORMAT_*`): the time/Date formats used by the XMB and games displaying the clock, and the DST flag. Choices are the format codes (see Skipped).

- **`Language`** — "Language"
  - Where: `UI/Settings/GeneralPage.cpp:219-235` — opens `NewLanguageScreen` to pick PPSSPP's UI language (list of installed lang ini files). Label category SYSTEM.
  - Meaning: the display language of PPSSPP's own interface (the lang files translators work on). Not to be confused with the game language setting ([PSPSETTINGS] "Game language").

### Firmware & XMB (Firmware screen — `UI/FirmwareScreen.cpp`; title `sy->T("PSP Firmware")`)

- **`PSP Firmware`** — "PSP Firmware"
  - Where: `UI/FirmwareScreen.cpp:42` — the title of the firmware management screen. Also referenced by name inside help texts of other categories ("Settings > Tools > PSP Firmware").
  - Meaning: the installed PSP firmware (Sony's real system software) kept in PPSSPP's NAND folder. See the following strings.

- **`FirmwareSources`** — "Installing a PSP firmware can improve game compatibility, gives some games their proper fonts, and lets you run the XMB, the real PSP's main menu. PPSSPP works without one, but having one installed is highly recommended."
  - Where: `UI/FirmwareScreen.cpp:104`, hint at the top of the firmware screen (`HintView`).
  - Meaning: explains why install a firmware. "XMB" = the real PSP's menu system (eXtended Media Bar / VSH). Accurate as written.

- **`Install or upgrade firmware from game discs`** — "Install or upgrade firmware from game discs"
  - Where: `UI/FirmwareScreen.cpp:108`, checkbox bound to `g_Config.bAutoUpgradeFirmware` (default on, `Core/Config.cpp:444`).
  - Meaning: when a booted game disc contains a firmware updater and (no firmware yet or disc's is newer), PPSSPP installs it automatically. See next entry.

- **`AutoUpgradeFirmwareDesc`** — "Most game discs carry a firmware updater. Install it when there's no firmware yet, or when the disc's is newer."
  - Where: `UI/FirmwareScreen.cpp:109`, the hint under the checkbox above.
  - Meaning: describes the auto-upgrade behavior of `bAutoUpgradeFirmware` (installs from the disc's updater in those two situations).

- **`No firmware installed`** — "No firmware installed"
  - Where: `UI/FirmwareScreen.cpp:118`, an info notice when nothing is installed in the NAND.
  - Meaning: plain status: the firmware (NAND) folder is empty.

- **`Firmware version`** — "Firmware version"
  - Where: `UI/FirmwareScreen.cpp:127` — label of the version line ("Firmware version: 6.61" style) of the installed firmware (read from `flash0:/vsh/etc/version.txt`).
  - Meaning: version number of the installed PSP firmware.

- **`Partial firmware installed`** — "Partial firmware installed" / **`Some system files are present, but not a complete firmware`** — "Some system files are present, but not a complete firmware"
  - Where: `UI/FirmwareScreen.cpp:139-140`, notice title + detail when `info_.version` is empty (no `version.txt`) but files exist.
  - Meaning: comment in code: "No flash0:/vsh/etc/version.txt, so this isn't a full firmware - most likely just the fonts, which is what installing from a running game's disc leaves behind." A complete firmware has a version file and kernel modules; a partial one is usually only fonts.

- **`Contents`** — "Contents" / **`Fonts`** — "Fonts" / **`Kernel modules`** — "Kernel modules" / **`XMB (VSH)`** — "XMB (VSH)" / **`Files`** — "Files"
  - Where: `UI/FirmwareScreen.cpp:148-162` — a "Contents" group listing what the installed firmware contains: font count ("Fonts"), kernel module count ("Kernel modules"), whether the XMB/VSH is present ("XMB (VSH)" with Yes/No), total file count ("Files"), plus size.
  - Meaning: inventory of the installed firmware's parts. VSH = the XMB shell module (`vshmain.prx`). "Kernel modules" = `flash0` .prx system modules; only listed for full firmwares.

- **`XMB unsupported`** — "XMB unsupported" / **`XMBUnsupportedVersion`** — "Only firmware 6.61 can boot the XMB so far"
  - Where: `UI/FirmwareScreen.cpp:174-175` — a warning notice (title + text) shown when VSH is installed but `FirmwareVersionSupportsVSH(info_.version)` is false.
  - Meaning: PPSSPP can currently only boot the XMB from firmware 6.61 (per the source text). Other versions' XMB won't start.

- **`Launch XMB`** — "Launch XMB"
  - Where: `UI/FirmwareScreen.cpp:179-186` — button that boots the firmware's main menu: `LaunchVSH()` starts `flash0/vsh/module/vshmain.prx` (`FirmwareScreen.cpp:210-212`). Disabled while a game runs or when no supported VSH is installed.
  - Meaning: start the real PSP menu (XMB) instead of a game.

- **`FirmwareNeedsNoGame`** — "Stop the game to change the installed firmware"
  - Where: `UI/FirmwareScreen.cpp:216-219`, info notice while `PSP_IsInited()`; the screen is read-only during a game ("Booting anything replaces the running game, and erasing the NAND out from under a game that has flash0 mounted is worse", comment at `FirmwareScreen.cpp:155-156`).
  - Meaning: firmware install/uninstall/launch is blocked while a game is running.

- **`Uninstall firmware`** — "Uninstall firmware" / **`EraseFirmwareConfirm`** — "This deletes everything in the NAND folder, including the fonts."
  - Where: `UI/FirmwareScreen.cpp:196` button; `FirmwareScreen.cpp:246-251` the confirmation dialog text (`AskToErase()`), confirm button = DIALOG "Delete".
  - Meaning: deletes all installed firmware files (the whole NAND folder, fonts included). Destructive; the fonts may later be reinstalled from a game disc's updater (see `Installed fonts`).

- **`Show firmware folder`** — "Show firmware folder"
  - Where: `UI/FirmwareScreen.cpp:201-203` (when `SYSPROP_HAS_OPEN_DIRECTORY`) — opens the NAND folder in the OS file browser.
  - Meaning: reveal where the firmware files live on disk.

- **`Installing firmware`** — "Installing firmware" / **`Installed firmware`** — "Installed firmware" / **`Firmware installation failed`** — "Firmware installation failed"
  - Where: `Core/Util/PSARUnpack.cpp:1816-1838` OSD messages during/after unpacking a firmware update (PSAR) — both the auto-install from game discs and the manual install (`UI/InstallUpdateScreen.cpp`). Progress text is `"Installing firmware <version>"`; success shows `"Installed firmware"` with the version as detail; failure shows the error.
  - Meaning: status messages for the firmware unpack/install job.

- **`Installed fonts`** — "Installed fonts"
  - Where: `Core/HLE/sceFont.cpp:992` — OSD success message (detail = firmware version) when fonts were pulled from a game disc's firmware updater because the NAND had none ("No fonts in NAND, trying the firmware updater on the disc", comment `sceFont.cpp:965-967`; only the font files are extracted).
  - Meaning: the real PSP fonts (needed by some games' text) were installed from the disc's updater.

- **`%1 needs a firmware installed to run without HLE. Install one, or re-enable HLE for it.`** — same text
  - Where: `Core/HLE/sceUtility.cpp:136-143` — OSD warning when a game requests a real firmware library (e.g. sceMp4) that is not in the NAND while HLE fallback for it is disabled (`DisableHLEFlags`). `%1` = library name.
  - Meaning: with HLE disabled for that library, the game needs the real firmware module; without firmware there is nothing to load, so either install firmware or re-enable HLE.

- **`MP4 playback needs PSP firmware 6.00 or later to be installed`** — same text
  - Where: `Core/HLE/sceUtility.cpp:231-238` — OSD warning when something requests MP4 playback and `flash0:/kd/libmp4.prx`/`mp4msv.prx` are missing from the NAND. The comment notes PPSSPP's own HLE for this is "nearly all stubs", so a real firmware (6.00+, which ships these modules) is required.
  - Meaning: MP4 (video) playback won't work without a real, recent-enough firmware installed.

- **`FirmwareOtherModel`** — "Firmware %1 was installed for a different PSP model than the selected one, so the XMB may not work until you install it again."
  - Where: `UI/Settings/SystemPage.cpp:180-186` (`OnPSPModelChanged`, confirm dialog) and `Core/System.cpp:885-896` (OSD warning when booting `vshmain.prx`). `%1` = firmware version.
  - Meaning: firmware files are model-specific (32 vs 64 MB generation); after switching PSP model the installed firmware may lack the files the XMB expects. Reinstalling it for the current model fixes it.

### Memory Stick / storage (System page, "PSP storage settings")

- **`Memory Stick inserted`** — "Memory Stick inserted"
  - Where: System page, "PSP storage settings" (`UI/Settings/SystemPage.cpp:327`); config `MemStickInserted`, per-game, default on.
  - Meaning: whether the emulated PSP reports a Memory Stick card inserted: `Core/HLE/sceIo.cpp:625` `MemoryStick_SetState(... INSERTED / NOT_INSERTED)`; also `Core/HW/MemoryStick.cpp:233`. With it off, games see "no Memory Stick" and can't save to it (some games warn/require it).

- **`Memory Stick size`** — "Memory Stick size"
  - Where: System page, "PSP storage settings"; config `MemStickSize`, range 1–32, `.Units("GB")`, default 16 (`Core/Config.cpp:1510-1511`).
  - Meaning: capacity of the simulated Memory Stick, used when reporting free space to games (`Core/HW/MemoryStick.cpp` simulated free space; real free space may also factor in — see "Report free storage space accurately"). Affects games/apps that check free space before saving.

- **`Report free storage space accurately`** — "Report free storage space accurately"
  - Where: System page, Windows desktop only, when `!SYSPROP_CAN_GET_FREE_SPACE_FAST` (`UI/Settings/SystemPage.cpp:321-324`); config `ReportAccurateFreeStorageSpace`.
  - Meaning: `Core/HW/MemoryStick.cpp:165-175` — when computing free space reported to games, query the real free disk space of the Memory Stick folder (`pspFileSystem.FreeDiskSpace("ms0:/")`, which can be slow) and report min(simulated, real); without it, only the simulated free space (from "Memory Stick size") is reported.

- **`Show Memory Stick folder`** — "Show Memory Stick folder"
  - Where: System page, "PSP storage settings" when `SYSPROP_HAS_OPEN_DIRECTORY` (`UI/Settings/SystemPage.cpp:288-293`) — opens `g_Config.memStickDirectory` in the OS file manager.
  - Meaning: reveal the Memory Stick data folder (saves, savestates, etc.) on disk. (The key appears twice in en_US.ini; same string.)

- **`Set Memory Stick folder`** — "Set Memory Stick folder"
  - Where: System page, macOS/iOS only (`UI/Settings/SystemPage.cpp:295-303`, `SetMemStickDirDarwin`).
  - Meaning: pick which folder is used as the Memory Stick on Mac/iOS.

- **`Memory Stick folder`** — "Memory Stick folder" / **`Memory Stick Folder`** — "Memory Stick folder" (capital F key)
  - Where: "Memory Stick folder" = System page row showing the current folder (`UI/Settings/SystemPage.cpp:310-321`, opens `MemStickScreen`). "Memory Stick Folder" (capital F) = the **title of the manual path input box** on Android/Switch (`UI/MemStickScreen.cpp:343`, `System_InputBoxGetString(... sy->T("Memory Stick Folder") ...)`).
  - Meaning: the folder that backs the emulated Memory Stick (`ms0:/`). Two distinct keys with identical English — keep translations identical unless your language distinguishes title vs. label contexts.

- **`Memory Stick in installed.txt`** — "Memory Stick in installed.txt" / **`Memory Stick in My Documents`** — "Memory Stick in My Documents"
  - Where: Windows desktop (non-UWP) only, System page (`UI/Settings/SystemPage.cpp:337-343`); handlers `OnMemoryStickMyDoc`/`OnMemoryStickOther` + `CheckMemstickInstallState` (`SystemPage.cpp:76-148`).
  - Meaning: choose where the Memory Stick lives on Windows. "In My Documents" = `Documents/PPSSPP/`; toggling creates/deletes an empty `installed.txt` marker next to PPSSPP.exe. "In installed.txt" = a **custom folder** whose path is stored inside `installed.txt` (with UTF-8 BOM); checking it opens a folder browser ("Choose PPSSPP save folder", DIALOG category). Unchecking reverts to `<exe>/memstick`. The marker file survives updates — hence the odd names.

- **`ChangingMemstickPath`** — "Save games, save states, and other data will not be copied to this folder.\n\nChange the Memory Stick folder?"
  - Where: **no lookup found in current C++ code** (only `ChangingMemstickPathInvalid` is used). The current flow is `UI/MemStickScreen.cpp` `ConfirmMemstickMoveScreen` with its own [MEMSTICK] strings and a "Move Data" checkbox.
  - Meaning (as written): older warning shown when switching the Memory Stick folder without moving data — existing saves/savestates stay in the old folder. Legacy; see Needs Clarification.

- **`ChangingMemstickPathInvalid`** — "That path couldn't be used to save Memory Stick files."
  - Where: `UI/MemStickScreen.cpp:354`, OSD warning in `SetFolderManually()` when the typed path is empty/unusable (Android/Switch manual path entry).
  - Meaning: the entered Memory Stick path is invalid.

- **`Path does not exist!`** — "Path does not exist!"
  - Where: `UI/MemStickScreen.cpp:363,388`, toasts in `SetFolderManually()` when the typed folder doesn't exist (even after case-fixing).
  - Meaning: the entered path must point to an existing folder.

### UI appearance (General settings page → "User interface")

- **`User interface`** — "User interface"
  - Where: `UI/Settings/GeneralPage.cpp:217` — link label opening the UI settings subpage (`BuildUISettingsPage`).
  - Meaning: settings for PPSSPP's own interface (not the emulated PSP's).

- **`UI size adjustment (DPI)`** — "UI size adjustment (DPI)"
  - Where: UI settings subpage (`UI/Settings/GeneralPage.cpp:158-163`); config `UIScaleFactor`, range −8…+8, `.ZeroLabel("Off")` (`Core/Config.cpp:523-524`).
  - Meaning: scales all UI text/controls up or down relative to the display's DPI (`UIScaleFactorToMultiplier`, `g_display.Recalculate(..., dpiMul)`). "Off" (0) = automatic scaling only.

- **`Set UI background...`** — "Set UI background..." / **`Clear UI background`** — "Clear UI background"
  - Where: UI settings (`UI/Settings/GeneralPage.cpp:168-180`): "Set UI background..." shown when no background image exists and an image/file browser is available; "Clear UI background" shown when `background.png`/`background.jpg` exists in the SYSTEM directory. `OnChangeBackground` copies a picked JPG/PNG to `background.jpg`/`background.png` (`GeneralPage.cpp:95-125`).
  - Meaning: set or remove a custom wallpaper behind PPSSPP's UI.

- **`Only JPG and PNG images are supported`** — "Only JPG and PNG images are supported"
  - Where: `UI/Settings/GeneralPage.cpp:119`, OSD error when the picked background image is neither JPG nor PNG.
  - Meaning: background image format restriction.

- **`Transparent UI background`** — "Transparent UI background"
  - Where: UI settings (`GeneralPage.cpp:178`); config `TransparentBackground`, default true (`Core/Config.cpp:465-466`). Used in `UI/EmuScreen.cpp:1590-1598` (`renderRole`) to decide whether the game screen renders behind the UI.
  - Meaning: when on, PPSSPP's menus draw over a see-through background so the running game (or nothing) shows through; when off, an opaque background is painted instead. Mostly relevant with a game running behind menus (run-behind).

- **`UI background animation`** — "UI background animation"
  - Where: UI settings (`GeneralPage.cpp:179`); config `BackgroundAnimation`, choices `backgroundAnimationNames[]` (`Core/Config.cpp:266`), enum `BackgroundAnimation` (`Core/ConfigValues.h:228`), default WAVE.
  - Meaning: picks the animated backdrop behind PPSSPP's menus (`UI/Background.cpp`). The choices (all seven, matching the enum):

- **`No animation`** / **`Floating symbols`** / **`Recent games`** / **`Waves`** / **`Moving background`** / **`Bouncing icon`** / **`Colored floating symbols`**
  - Where: choices 0–6 of `backgroundAnimationNames`; classes in `UI/Background.cpp`.
  - Meaning: "No animation" = static background. "Waves" (`WaveAnimation`, line 80) = the classic PPSSPP blue wave. "Floating symbols" (`FloatingSymbolsAnimation`, line 115) = PSP-button symbols floating around (the COLORS comment "X O D A" reveals them as the PSP's face/D-pad symbols, like the PSP XMB decorations); "Colored floating symbols" = same but colored. "Recent games" (`RecentGamesAnimation`, line 175) = cycles through the background art of recently played games ("skips games with no BG", comment line 21-26). "Moving background" (`MovingBackground`, line 38) = slowly pans/zooms your custom UI background image with the focus point. "Bouncing icon" (`BouncingIconAnimation`, line 261) = the PPSSPP app icon (gold icon for the Gold build) bouncing around the screen.
  - Note: an identical `Recent games = Recent games` string also exists in [RemoteISO] (a share-type choice in remote ISO settings, `UI/RemoteISOScreen.cpp:398-399` uses I18NCat::REMOTEISO) — separate lookups; don't assume the two are one context.

- **`Color tint`** — "Color tint" / **`Color saturation`** — "Color saturation"
  - Where: UI settings (`GeneralPage.cpp:200-201`, live-updating sliders, hidden on a Raspberry Pi driver bug); config `UITint` (0.0–1.0) / `UISaturation` (0.0–2.0) (`Core/Config.cpp:468-471`). Applied via `UIContext::SetTintSaturation` (`UI/NativeApp.cpp:1410`); reset to 0.0/1.0 when the theme changes (`GeneralPage.cpp:186-191`).
  - Meaning: hue tint and color saturation applied to PPSSPP's UI theme colors (not the game's graphics).

- **`Theme`** — "Theme"
  - Where: UI settings (`GeneralPage.cpp:183-194`), `PopupMultiChoiceDynamic` over theme names (I18NCat::THEMES).
  - Meaning: color/appearance theme of PPSSPP's UI.

- **`Hide navigation bar`** — "Hide navigation bar" / **`Hide status bar`** — "Hide status bar"
  - Where: one setting, `ImmersiveMode` (config `Core/Config.cpp:841-847`): label is "Hide status bar" on iOS, "Hide navigation bar" everywhere else. Rendered in the display layout settings (Graphics).
  - Meaning: full-screen immersion — hides the Android navigation bar (or iOS status bar) while playing. Per-game.

### Games list settings (popup "Games list settings" — `UI/MainScreen.cpp` `GridSettingsPopupScreen`, opened from `UI/GameBrowser.cpp:790`)

- **`Games list settings`** — "Games list settings"
  - Where: title of the grid/list settings popup (`UI/GameBrowser.cpp:790`).
  - Meaning: settings for the game list views (tabs on the main screen).

- **`Default tab`** — "Default tab"
  - Where: `UI/MainScreen.cpp:1043-1044`, `PopupMultiChoice(&g_Config.iDefaultTab, ..., defaultTabs, ...)` with `defaultTabs[] = { "Recent", "Games", "Homebrew & Demos" }` (choice names in I18NCat::MAINMENU).
  - Meaning: which tab of the main screen is shown at startup (`UI/MainScreen.cpp:483` applies it; a 4th "Remote" tab exists when enabled).

- **`Icon size`** — "Icon size" / **`Increase size`** — "Increase size" / **`Decrease size`** — "Decrease size"
  - Where: `UI/MainScreen.cpp:1046-1054` — group with two buttons stepping `fGameGridScale`/`fGameListScale` up/down (`StepGameBrowserIconScale`, `UI/GameBrowser.cpp:113`).
  - Meaning: adjusts the size of game icons/entries in the current view mode.

- **`Display Extra Info`** — "Display extra info" / **`Show ID`** — "Show ID" / **`Show region flag`** — "Show region flag"
  - Where: `UI/MainScreen.cpp:1056-1058` — group + two checkboxes (`bShowIDOnGameIcon`, `bShowRegionOnGameIcon`, `Core/Config.cpp:417-418`).
  - Meaning: extra badges on game entries (`UI/GameButton.cpp`): "Show ID" displays the game's disc ID + version (e.g. ULUS-12345) on the entry (lines 275, 327); "Show region flag" shows a small region flag icon (JP/US/EU/HK/AS/KO, lines 374, `regionIcons`).

- **`Clear Recent`** — "Clear \"Recent\"" / **`Clear Recent Games List`** — "Clear \"Recent\" list" / **`ClearRecentConfirm`** — "Clear the \"Recent\" list? Your games won't be deleted."
  - Where: `UI/MainScreen.cpp:1059-1071` — group "Clear Recent", button "Clear Recent Games List", confirmation dialog `MessagePopupScreen(sy->T("Clear Recent"), sy->T("ClearRecentConfirm", ...), ...)` which calls `g_recentFiles.Clear()`.
  - Meaning: empties the "Recent" tab's list of recently played games. Only the list is cleared — game files are untouched (the confirm text says exactly that).

- **`Grid`** — "Grid" / **`List`** — "List" / **`Large list`** — "Large list"
  - Where: **no [SYSTEM] usage found in current code.** The live view-mode toggle is icon-only (`UI/GameBrowser.cpp:515-519`, `I_GRID` / `I_LARGE_LIST` images); view modes are `GameBrowserViewMode::GRID`/`LIST` with comment "Old configs may have 2 (the large list)" (`Core/ConfigValues.h:321-324`). ("Grid" is separately used in [DIALOG] for the touch-control snap grid slider — different context.)
  - Meaning (historical): the three game-list layouts — icon grid, XMB-style list (full-size icons + ICON1.PMF video), and an older compact "large list". Legacy labels; see Needs Clarification.

- **`Display Games on a grid`** — "Display \"Games\" on a grid" / **`Display Homebrew on a grid`** — "Display \"Homebrew & Demos\" on a grid" / **`Display Recent on a grid`** — "Display \"Recent\" on a grid"
  - Where: **no usage found in current code.** Per-tab grid/list mode is now stored directly (`GameBrowserView1..4`, `Core/Config.cpp:424-427`) via the icon toggle.
  - Meaning (historical): per-tab checkboxes choosing grid view. Legacy; see Needs Clarification.

### Notifications & messages

- **`Notification screen position`** — "Notification screen position"
  - Where: UI settings (`UI/Settings/GeneralPage.cpp:206`) and developer settings (`UI/DeveloperSettingsPages.cpp:460`); config `NotificationPos`, `Choices(notificationPosNames, -1)` (`Core/Config.cpp:509-511`).
  - Meaning: where on the screen OSD messages (save/load toasts, warnings, "AVI Dump started.", plugin loaded, etc.) appear: `UI/OnScreenDisplay.cpp:151-153` assigns `typeEdges[...] = (ScreenEdgePosition)g_Config.iNotificationPos` as the default edge for message types (achievements use their own settings). Value −1 = "None" skips drawing (`OnScreenDisplay.cpp:185`).
  - Note: this positions ordinary notifications only; some special messages are forced to fixed positions (e.g. centered warnings, progress bars at top, `OnScreenDisplay.cpp:166-174`).

- **`None`** — "None" / **`Bottom Left`** — "Bottom left" / **`Bottom Center`** — "Bottom center" / **`Bottom Right`** — "Bottom right" / **`Top Left`** — "Top left" / **`Top Center`** — "Top center" / **`Top Right`** — "Top right" / **`Center Left`** — "Center left" / **`Center Right`** — "Center right"
  - Where: choices of `Notification screen position` (`notificationPosNames`, `Core/Config.cpp:268`).
  - Meaning: screen edge/corner where notifications are stacked ("None" = don't show them). Same word set is reused (in other categories) for achievements overlay positions.

- **`WARNING: Android battery save mode is on`** — "WARNING: Android battery save mode is on" / **`WARNING: Battery save mode is on`** — "WARNING: Battery save mode is on"
  - Where: `UI/EmuScreen.cpp:424-432` (at game boot) and `UI/NativeApp.cpp:1515-1524` (on `UIMessage::POWER_SAVING`), both when `Core_GetPowerSaving()`. Android builds use the first string, others the second (`#ifdef __ANDROID__` / `PPSSPP_PLATFORM(ANDROID)`).
  - Meaning: the OS reports battery/power-saving mode, which throttles the device and can cause emulation slowdown — hence the warning. Short OSD warning (2 s).

- **`Game crashed`** — "Game crashed"
  - Where: `UI/DebugOverlay.cpp:346`, `DrawCrashDump()` — heading of the crash screen shown when the emulated game hits `CORE_RUNTIME_ERROR` (unhandled bad memory access, crash dump) — see `UI/EmuScreen.cpp:2077-2083`.
  - Meaning: the emulated game has crashed; the screen shows the crash dump (registers/PC) with Resume/Reset options. Not an app crash.

- **`Pause when not focused`** — "Pause when not focused"
  - Where: `UI/Settings/GeneralPage.cpp:278`; config `PauseOnLostFocus`, desktop only (Windows `Windows/MainWindow.cpp:817`, SDL `SDL/SDLMain.cpp:1612`).
  - Meaning: automatically pause emulation when the PPSSPP window loses focus while in-game.

- **`Ask for exit confirmation after seconds`** — "Ask for exit confirmation after seconds"
  - Where: General settings (`UI/Settings/GeneralPage.cpp:281`); config `AskForExitConfirmationAfterSeconds`, range 0–1200, `.Units("seconds").ZeroLabel("Off")`, default 300 (`Core/Config.cpp:476-477`).
  - Meaning: `UI/PauseScreen.cpp:955-992` `GetConfirmExitMessage()` — when exiting/stopping a game, if the time since the last save (savestate or savedata) exceeds this, the exit dialog adds "You haven't saved your progress for %1." ("Off"/0 = never ask for this reason; network/RA conditions can still add their own warnings).

- **`Sustained performance mode`** — "Sustained performance mode"
  - Where: General settings, only when `SYSPROP_SUPPORTS_SUSTAINED_PERF_MODE` (`UI/Settings/GeneralPage.cpp:287-290`); config `SustainedPerformanceMode`, per-game. Android: `PpssppActivity.java:577-584` calls `getWindow().setSustainedPerformanceMode(...)` (`android/.../PpssppActivity.java`, plumbed via `android/jni/app-android.cpp:599`).
  - Meaning: asks Android's sustained-performance API to keep CPU/GPU clocks stable (avoid thermal throttling spikes) for steadier frame rates, at some power cost.

- **`App switching mode`** — "App switching mode" / **`Swipe once to switch app (indicator auto-hides)`** / **`Swipe twice to switch app (indicator stays visible)`**
  - Where: iOS only (`Core/Config.cpp:999-1003`, `#if PPSSPP_PLATFORM(IOS)`), choices `appSwitchModeNames[]` (`Core/Config.cpp:913`), default DOUBLE_SWIPE_INDICATOR. Behavior: `ios/ViewController.mm:299-307` `prefersHomeIndicatorAutoHidden` — returns NO (indicator visible) in DOUBLE_SWIPE_INDICATOR mode, YES (auto-hide) otherwise.
  - Meaning: controls how the iOS home indicator/gesture area behaves during play. "Swipe once…" = a single swipe up leaves the app (switch app); the home-indicator bar auto-hides. "Swipe twice…" = you must swipe twice to leave (harder to do accidentally mid-game) and the indicator bar stays visible as a hint.

- **`Use system native keyboard`** — "Use system native keyboard"
  - Where: PSP system settings (`UI/Settings/SystemPage.cpp:234-236`) and General (`GeneralPage.cpp:283-285`), when `SYSPROP_HAS_KEYBOARD`; config `BypassOSKWithKeyboard` (`Core/Config.cpp:1498-1500`).
  - Meaning: when a game asks for text input, use the host OS's text entry instead of drawing the emulated PSP on-screen keyboard (OSK) — `Core/Dialog/PSPOskDialog.cpp:884` (skipped in fullscreen), also used by `UI/ChatScreen.cpp:205`, `UI/EmuScreen.cpp:666`.

### Screenshots & recording

- **`Screenshots as PNG`** — "Save screenshots in PNG format"
  - Where: General settings (`UI/Settings/GeneralPage.cpp:312`); config `ScreenshotsAsPNG`, per-game (`Core/Config.cpp:390-391`).
  - Meaning: `Core/Screenshot.cpp:514-524` — screenshots are saved as `.png` instead of `.jpg` (lossless, bigger files).

- **`Screenshot mode`** — "Screenshot mode" / **`Final processed image`** — "Final processed image" / **`Raw game image`** — "Raw game image"
  - Where: General settings (`GeneralPage.cpp:313`); config `ScreenshotMode`, choices `screenshotModeNames[] = { "Final processed image", "Raw game image" }` (`Core/Config.cpp:270,388-389`), enum `ScreenshotMode { FinalOutput = 0, GameImage = 1 }` (`Core/ConfigValues.h:128`). `Core/Screenshot.cpp:524` maps GameImage→`ScreenshotType::Display`, else `ScreenshotType::Output`.
  - Meaning: what a screenshot captures (`Core/Screenshot.h:36-43`): "Raw game image" (Display) = what the game has rendered and is presenting, at render resolution — the game's own framebuffer. "Final processed image" (Output) = what's shown on the host device's screen, i.e. after PPSSPP's post-processing and including on-screen elements like FPS counters.

- **`Recording`** — "Recording"
  - Where: section header on the System page, Windows desktop only (`UI/Settings/SystemPage.cpp:330-337`).
  - Meaning: header for the AVI/WAV dump settings below.

- **`Record Display`** — "Record display" / **`AVI Dump started.`** — "AVI dump started" / **`AVI Dump stopped.`** — "AVI dump stopped"
  - Where: `DumpFrames` setting (`UI/Settings/SystemPage.cpp:333`); behavior `UI/EmuScreen.cpp:1641-1663` `HandleFlip()` (desktop only, `#ifndef MOBILE_DEVICE`): turning it on starts an AVI capture at render resolution and shows "AVI Dump started."; each displayed frame is appended (`avi.AddFrame()`); turning it off stops and shows "AVI Dump stopped." with a click-callback to reveal the file. Code comment: "This video dumping stuff is bad. Or at least completely broken with frameskip.."
  - Meaning: "Record Display" = record video of the game's rendered frames to an AVI file ("display" = the emulated display output).

- **`Record Audio`** — "Record audio"
  - Where: `DumpAudio` setting (`SystemPage.cpp:335`); audio is written to WAV files in the audio directory (`Core/HLE/__sceAudio.cpp:672-683`, `WAVDump`, filename `<discID>_<timestamp>.wav`).
  - Meaning: record the emulated audio output to WAV alongside (or instead of) video.

- **`Use output buffer (with overlay) for recording`** — "Use output buffer (with overlay) for recording"
  - Where: `DumpVideoOutput` setting (`SystemPage.cpp:334`, also Windows menu `Windows/MainWindowMenu.cpp:968`); behavior `Core/AVIDump.cpp:195-204` `AVIDump::AddFrame()`: when on, the recorded frames come from `gpu->GetOutputFramebuffer` (the final output buffer — post-processing/shaders/overlays included); when off, from `gpu->GetCurrentFramebuffer(GPU_DBG_FRAMEBUF_RENDER)` (the game's render target at render resolution).
  - Meaning: switch the video recording source from the raw game rendering to the final composited output (what you see on screen).

- **`Reset Recording on Save/Load State`** — "Reset recording on Save/Load state"
  - Where: `SaveLoadResetsAVdumping` setting (`SystemPage.cpp:336`); behavior `Core/SaveState.cpp:1197-1208` `ResetAVDumping()` (stops and restarts the AVI; resets WAV) and `Core/HLE/__sceAudio.cpp:671-683` (starts a new timestamped WAV), on savestate save/load. Desktop only.
  - Meaning: when saving/loading a savestate during recording, restart the AVI/WAV files so the recording doesn't contain time-skips from the state change (each recording file stays continuous).

- **`Use Lossless Video Codec (FFV1)`** — "Use lossless video codec (FFV1)"
  - Where: **no lookup found in current C++ code.** `Core/AVIDump.cpp:134-135` now hard-codes the lossless FFV1 codec ("Lossless. For lossy footage, capture the output with a screen recorder instead.").
  - Meaning (historical): option to record with the lossless FFV1 codec. Legacy; see Needs Clarification.

- **`Done`** — "Done" / **`Failed`** — "Failed"
  - Where: `UI/GamePacker.cpp:343,358` — OSD results of a game compression/extraction job (CSO↔ISO conversion, see `GamePacker::Job`, `UI/GamePacker.cpp:42-60`): success shows `"Done: <filename>"` with before/after sizes; failure shows `"Failed: <filename>"` with the error.
  - Meaning: job result verbs for the game packer (compress/convert game image).

### Misc messages

- **`Not a PSP game`** — "Not a PSP game"
  - Where: boot error (`Core/Loaders.cpp:211`, `Core/System.cpp:511`) and install error (`Core/Util/GameManager.cpp:480`).
  - Meaning: the file is not a bootable PSP game/disc image (wrong format).

- **`PS1 support is still experimental. The game might not work, or there may be glitches.`** — same text
  - Where: `Core/PSPLoaders.cpp:701` — OSD warning when booting a PS1 (POPS) game.
  - Meaning: PS1 games run through the PSP's built-in PS1 emulator support, which is experimental in PPSSPP.

- **`Too large`** — "Too large"
  - Where: `Core/Util/GameManager.cpp:795-803` — install error when a zip's declared content size exceeds the safety limit ("zip bomb" guard) during homebrew/zip install.
  - Meaning: the archive contents exceed the maximum allowed install size.

- **`Storage full`** — "Storage full"
  - Where: `Core/Util/GameManager.cpp:379,852,869,896` — install error when file writes fail (out of disk space) while installing a zip/game to storage.
  - Meaning: not enough free host storage to complete the install.

- **`Unable to open zip file`** — "Unable to open zip file"
  - Where: `Core/Util/GameManager.cpp:383,401`, install error when the zip can't be opened/parsed.
  - Meaning: the archive is unreadable or not a valid zip.

- **`Enable Cheats`** — "Enable cheats" / **`Enable plugins`** — "Enable plugins" / **`Cheats`** — "Cheats" / **`Plugins`** — "Plugins"
  - Where: System page "Cheats" section (`UI/Settings/SystemPage.cpp:236-240`: header "Cheats", then `bEnableCheats`, `bEnablePlugins`). "Plugins" is also the header of the loaded-plugin list on the game info screen (`UI/GameScreen.cpp:574`, I18NCat::SYSTEM).
  - Meaning: "Enable Cheats" = allow CWCheat cheat files to apply; "Enable plugins" = load the game's/your PSP PRX plugins (custom-firmware-style plugins, `Core/HLE/Plugins.cpp`); "Plugins" lists the plugin .prx files detected for the game.

- **`Loaded plugin: '%1'`** — "Loaded plugin: '%1'"
  - Where: `Core/HLE/Plugins.cpp:206` — OSD success message when a plugin module is started (`%1` = plugin name).
  - Meaning: confirms a PSP plugin was loaded. (`Loaded plugin: %1` without quotes is a legacy duplicate — see below.)

- **`Enable Compatibility Server Reports`** — "Enable compatibility server reports"
  - Where: General settings under the "Help the PPSSPP team" header (`UI/Settings/GeneralPage.cpp:315-318`), bound to the reporting enable flag; also a checkbox on the compatibility-report screen (`UI/ReportScreen.cpp:287`). Enabled only when `Reporting::IsSupported()`.
  - Meaning: sends anonymous game-compatibility reports to PPSSPP's compatibility server when you rate a game.

- **`Help the PPSSPP team`** — "Help the PPSSPP team"
  - Where: `UI/Settings/GeneralPage.cpp:315` — section header above the compatibility-report checkbox.
  - Meaning: settings that support the PPSSPP project.

- **`RetroAchievements`** — "RetroAchievements"
  - Where: `UI/DeveloperSettingsPages.cpp:429` (OSD test entry on the developer UI-tests page) and `UI/RetroAchievementScreens.cpp:388` (pane title of the RetroAchievements screen).
  - Meaning: proper name of the achievements service (do not translate).

- **`Developer Tools`** — "Developer tools"
  - Where: `UI/DevScreens.cpp:174` — menu entry opening the Developer tools settings screen (also `UI/RetroAchievementScreens.cpp:368,517` uses the phrase as a tab title, there possibly under another category).
  - Meaning: the advanced/debug settings area.

- **`VersionCheck`** — "Check for new versions of PPSSPP"
  - Where: General settings (`UI/Settings/GeneralPage.cpp:295`); config `CheckForNewVersion`, default on (`Core/Config.cpp:356-358`). `Core/Config.cpp:2201-2217` runs an online check periodically ("Check for new version on every 10 runs") when enabled and supported.
  - Meaning: lets PPSSPP occasionally check online and notify you about updates.

- **`Restore Default Settings`** — "Restore PPSSPP's settings to default"
  - Where: General settings (`UI/Settings/GeneralPage.cpp:296-299`, `OnRestoreDefaultSettings` at 137-152): normally opens `RestoreSettingsScreen` (a popup that restores settings); for a game-specific config it shows a confirm dialog ("Are you sure you want to restore the game-specific settings back to the ppsspp defaults?", key in [DEVELOPER]) and calls `g_Config.RestoreDefaults(...)`.
  - Meaning: reset PPSSPP's configuration to factory defaults (global, or per-game overrides when in a game's settings).

- **`Emulation`** — "Emulation"
  - Where: section header on the System page (`UI/Settings/SystemPage.cpp:242`) above the memory/I/O/clock settings.
  - Meaning: header; "emulation" = the simulated PSP, as usual.

- **`PSP system settings`** — "PSP system settings" / **`PSP storage settings`** — "PSP storage settings"
  - Where: `UI/Settings/SystemPage.cpp:208` (link to the `BuildPSPSettingsPage` subpage, whose header is the same string at line 203) and `SystemPage.cpp:285` (header of the storage section).
  - Meaning: subpage for emulated-PSP system parameters (language, model, nickname, date/time, button pref) / section for Memory Stick settings.

- **`Auto`** — "Auto"
  - Where: `ZeroLabel("Auto")` for the CPU clock slider (`Core/Config.cpp:582`) — shown when the value is 0.
  - Meaning: automatic (let the game decide the CPU clock).

- **`Off`** — "Off"
  - Where: `ZeroLabel("Off")` for "Rewind Snapshot Interval" (`Config.cpp:411`) and "Ask for exit confirmation after seconds" (`Config.cpp:461`); also the first choice of "Auto load savestate" (`autoLoadSaveStateNames`).
  - Meaning: feature disabled.

- **`seconds`** — "seconds"
  - Where: `.Units("seconds")` suffix for the rewind-interval and exit-confirmation sliders (`Core/Config.cpp:411,461`).
  - Meaning: unit label for those values.

### Legacy/unused keys (no lookup in current code — verify before investing translation effort)

- **`MHz, 0:default`** — "MHz, 0 = default" — old hint for the CPU clock slider; superseded by `.Units("MHz")` + `.ZeroLabel("Auto")`.
- **`ChangingMemstickPath`** — old Memory Stick folder-change warning; current flow uses [MEMSTICK] strings and a "Move Data" option (`UI/MemStickScreen.cpp` `ConfirmMemstickMoveScreen`).
- **`Grid`**, **`List`**, **`Large list`** — old game-browser view-mode labels; the current toggle is icon-only (`UI/GameBrowser.cpp:515-519`), values kept in `GameBrowserViewMode` for config compatibility.
- **`Display Games on a grid`**, **`Display Homebrew on a grid`**, **`Display Recent on a grid`** — old per-tab view checkboxes; replaced by the icon toggle storing `GameBrowserView1..4`.
- **`Loaded plugin: %1`** — superseded by the quoted variant `Loaded plugin: '%1'`.
- **`Use Lossless Video Codec (FFV1)`** — the AVI recorder now always uses FFV1 (`Core/AVIDump.cpp:134-135`).
- **`USB`** — see Needs Clarification.

### Self-evident (no notes needed)

- `Slot 1` — Slot 1
- `Slot 2` — Slot 2
- `Slot 3` — Slot 3
- `Slot 4` — Slot 4
- `Slot 5` — Slot 5 (choices of "Auto load savestate": load this fixed slot at game boot)

### Skipped

- `12HR` / `24HR` — time format codes; verbatim.
- `MMDDYYYY` / `DDMMYYYY` / `YYYYMMDD` — date format codes; verbatim.

## Needs Clarification

- **`USB`** — "USB": the only "USB" string in code is the untranslated wait-type name `{ WAITTYPE_USB, "USB" }` (`Core/HLE/sceKernelThread.cpp:87`, used by the ImGui debugger's thread list without translation). No `T("USB")` lookup found. Is this key used by a platform-specific UI (e.g. a storage-volume label), or is it legacy? If legacy, translators can skip it.
- **`ChangingMemstickPath`** — "Save games, save states, and other data will not be copied to this folder.\n\nChange the Memory Stick folder?": no lookup found in current code. Was it dropped when `ConfirmMemstickMoveScreen` (with "Move Data") replaced the old folder-change flow? If unused it can be skipped.
- **`Grid` / `List` / `Large list`** and **`Display Games/Homebrew/Recent on a grid`** — no [SYSTEM] lookups found. Are these kept for a fallback/older UI, or safe to drop from translation scope?
- **`Loaded plugin: %1`** (unquoted) vs **`Loaded plugin: '%1'`** (quoted, used): is the unquoted variant still reachable anywhere (e.g. platforms not covered by `Core/HLE/Plugins.cpp`)?
- **`Use Lossless Video Codec (FFV1)`** — no lookup found; `AVIDump` hard-codes FFV1. Confirm this is retired.
- **`MHz, 0:default`** — no lookup found in code. Confirm retired in favor of `.Units("MHz")`/`.ZeroLabel("Auto")`.
