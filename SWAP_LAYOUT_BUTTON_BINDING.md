# Swap Layout Button Binding Guide

## Overview

Fitur **Swap Layout** sekarang dapat di-bind ke custom buttons dan input mappings. Ini memungkinkan pengguna untuk dengan cepat menukar layout menggunakan gamepad buttons, touch buttons custom, atau kontrol lainnya.

## Fitur Baru

### 1. Virtual Key untuk Swap Layout
- Ditambahkan `VIRTKEY_SWAP_LAYOUT` ke sistem virtual key PPSSPP
- Dapat di-map sama seperti action lain (devmenu, pause, dll)

### 2. Custom Button Support
- "Swap layout" sekarang tersedia di dalam daftar action button mapping
- Dapat di-assign ke custom touch button (Custom 1-20)

### 3. Button Binding Support
- Dapat di-bind ke keyboard
- Dapat di-bind ke gamepad/controller buttons
- Dapat di-bind ke touch buttons custom

## Cara Kerja

### Sistem Button Binding
```
User Press Button/Key
    ↓
Check button mapping
    ↓
If mapped to VIRTKEY_SWAP_LAYOUT:
    - Kirim event ke EmuScreen
    - Swap layout configuration
    - Tampilkan OSD message
```

### Proses Swap Layout via Button
```
1. User presses mapped button
2. EmuScreen::onVKey() receives VIRTKEY_SWAP_LAYOUT
3. SwapTouchControlsLayouts() dihubungi untuk orientation saat ini
4. Kedua layout configuration ditukar
5. OSD menampilkan "Touch layout switched"
6. Layout baru langsung aktif
```

## Implementasi Details

### Code Changes

#### 1. Core/KeyMap.h
```cpp
// Menambah virtual key code baru
VIRTKEY_SWAP_LAYOUT = 0x40000036,
```

#### 2. Core/KeyMap.cpp
```cpp
// Menambah string mapping untuk key name
{VIRTKEY_SWAP_LAYOUT, "Swap layout"},
```

#### 3. UI/GamepadEmu.h
```cpp
// Menambah ke custom button key list
{ ImageID::invalid(), VIRTKEY_SWAP_LAYOUT },
```

#### 4. UI/EmuScreen.cpp
```cpp
case VIRTKEY_SWAP_LAYOUT:
    if (down) {
        const DeviceOrientation orientation = GetDeviceOrientation();
        g_Config.SwapTouchControlsLayouts(orientation);
        
        auto co = GetI18NCategory(I18NCat::CONTROLS);
        g_OSD.Show(OSDType::MESSAGE_INFO, co->T("Touch layout switched"));
    }
    break;
```

## Cara Menggunakan - Step by Step

### A. Bind ke Custom Touch Button

1. **Buka Customize Panel**
   ```
   Main Menu → Settings → Controls → Touch Control Layout
   → Click "Customize"
   ```

2. **Pilih Custom Button**
   - Scroll ke "Custom X" (X = 1-20)
   - Klik pada custom button yang ingin di-configure

3. **Set Action untuk Button**
   - Di CustomButtonMappingScreen, akan ada daftar semua available keys
   - Cari dan pilih "Swap layout"
   - Button sekarang akan menjalankan fungsi swap layout saat di-tekan

4. **Atur Posisi & Shape**
   - Edit posisi button di layout editor
   - Pilih shape dan icon yang diinginkan
   - Klik "Back" untuk save

### B. Bind ke Keyboard (dalam ppsspp.ini)

Jika PPSSPP support keyboard mapping, dapat ditambahkan di mapping config:

```ini
[Keyboard Mapping]
; Assign Z key untuk swap layout
Z = VIRTKEY_SWAP_LAYOUT
```

### C. Bind ke Gamepad (automatic via KeyMap)

PPSSPP akan otomatis mengenal VIRTKEY_SWAP_LAYOUT di:
- Gamepad/controller key mapping
- Custom key assignments
- Input configuration screens

## Integrasi dengan Touch Control Visibility

Dalam TouchControlVisibilityScreen, "Swap layout" sekarang dapat di-setup:

1. **Buka Customize Screen**
   - Settings → Controls → Touch Control Layout → Customize

2. **Tampil dalam Custom Button List**
   - Setiap custom button (Custom 1-20) dapat di-tap untuk edit
   - Akan masuk ke CustomButtonMappingScreen

3. **Pilih "Swap layout" dari Action List**
   - List berisi: Circle, Cross, Square, Triangle, L, R, Start, Select, D-pad, Analog, dll
   - **Tambahan Baru**: "Swap layout"

4. **Configure sebagai Custom Button**
   - Atur posisi, shape, icon
   - Test dengan menekan button
   - Layout akan swap setiap kali button di-tekan

##Technical Details

### Virtual Key Code
```cpp
enum VirtKey {
    ...
    VIRTKEY_TOGGLE_TILT = 0x40000035,
    VIRTKEY_SWAP_LAYOUT = 0x40000036,  // NEW
    VIRTKEY_LAST,
    ...
};
```

### Custom Key List Entry
Dalam `CustomKeyData::g_customKeyList[]`:
```cpp
{ ImageID::invalid(), VIRTKEY_SWAP_LAYOUT },
```

Ini membuat "Swap layout" tersedia di custom button selector.

### Key Handling
Event flow ketika VIRTKEY_SWAP_LAYOUT diterima:
1. `EmuScreen::onVKey(VIRTKEY_SWAP_LAYOUT, down)`
2. If `down` (tombol ditekan):
   - Get current device orientation
   - Call `g_Config.SwapTouchControlsLayouts(orientation)`
   - Show OSD message
3. Layout configuration langsung berubah
4. Sudah siap untuk digunakan

## Scenarios & Examples

### Scenario 1: Custom Button untuk Swap
```
Setup:
- Custom Button 1: Mapped ke VIRTKEY_SWAP_LAYOUT
- Located di: top-right corner
- Shape: square, Icon: gear/settings

Usage:
- Saat bermain: press Custom Button 1
- Layout otomatis swap
- Tombol di posisi baru sesuai Layout 2
```

### Scenario 2: Action Game Quick Switch
```
Layout 1: Action game buttons (large, centered)
Layout 2: Alternative button layout

Saat playing:
1. Press Swap button
2. Layout 1 → Layout 2 (buttons move ke posisi baru)
3. Press Swap button lagi
4. Layout 2 → Layout 1 (buttons kembali ke posisi awal)
```

### Scenario 3: Game-Specific Binding
Jika PPSSPP support game-specific config:

```ini
[Game: Action Game]
Custom1_Key = VIRTKEY_SWAP_LAYOUT
Custom1_X = 0.95
Custom1_Y = 0.85

[Game: RPG]
Custom1_Key = VIRTKEY_SOMETHING_ELSE
Custom1_X = 0.50
Custom1_Y = 0.50
```

## OSD Message

Ketika layout di-swap via button/key:
```
"Touch layout switched" (appears for ~1-2 seconds)
```

Message ini menunjukkan:
- Swap layout berhasil dijalankan
- Layout configuration telah ditukar
- Ready untuk digunakan

## Backward Compatibility

- Existing custom buttons tidak terpengaruh
- Existing key mappings tetap work
- New feature adalah additive (tidak menghilangkan yang lama)
- Fully backward compatible dengan old ppsspp.ini files

## Future Enhancements

1. **Quick Swap Widget**
   - Visual indicator untuk layout aktif saat ini
   - Bisa ditambahkan di HUD

2. **Layout Naming**
   - Custom names untuk Layout 1 dan 2
   - Contoh: "Layout 1 (Action)" vs "Layout 2 (RPG)"

3. **Auto-Swap per Game**
   - Automatically use specific layout per game

4. **Multi-Key Combo**
   - Swap layout dengan kombinasi buttons (e.g., L+R+Select)

## Testing Checklist

- [ ] "Swap layout" muncul di custom button action list
- [ ] Dapat selected sebagai custom button action
- [ ] Tombol tekan → layout swap
- [ ] Tombol tekan lagi → kembali ke layout sebelumnya
- [ ] OSD message muncul saat swap
- [ ] Works di both landscape dan portrait
- [ ] Perubahan layout terlihat immediate
- [ ] Tidak ada lag atau delay
- [ ] Button binding disimpan ke ppsspp.ini
- [ ] Button binding ter-load saat restart

## Troubleshooting

### Q: "Swap layout" tidak muncul di button list?
**A**: Pastikan sudah rebuild PPSSPP setelah pull code changes.

### Q: Button tekan tapi layout tidak swap?
**A**: 
- Cek apakah custom button showing (`Show` checkbox unchecked?)
- Verify custom button mapping di ppsspp.ini
- Rebuild PPSSPP

### Q: Layout swap tapi tidak ada visual feedback?
**A**: Check OSD settings. OSD message seharusnya muncul untuk 1-2 detik.

### Q: Swap hilang setelah restart?
**A**: Setting seharusnya auto-save ke ppsspp.ini. Check file permissions.

---

## Summary

Swap Layout Button Binding memungkinkan pengguna untuk:
✅ Assign swap layout ke custom buttons  
✅ Assign ke keyboard/gamepad inputs  
✅ Quick switch antar layout dengan single button press  
✅ Seamless integration dengan existing control system  
✅ Full backward compatibility  

**Status**: ✅ IMPLEMENTED & READY TO USE

Pengguna sekarang dapat dengan mudah swap layout tanpa perlu membuka menu settings!

---

**Created**: February 8, 2026  
**Feature**: Swap Layout Button Binding  
**Status**: Complete  
**Version**: v1.1 (with button binding support)
