# Swap Layout Feature for Touch Controls

## Overview
Fitur swap layout memungkinkan pengguna PPSSPP untuk menyimpan dan menukar antara dua konfigurasi layout touch control yang berbeda dengan cepat dan mudah. Setiap layout dapat memiliki posisi dan konfigurasi tombol yang independen untuk mode landscape maupun portrait.

## Fitur Utama

### 1. Dua Layout Independen
- **Layout 1 (Primary)**: Layout default/utama
- **Layout 2 (Secondary)**: Layout alternatif yang dapat dikustomisasi sesuai kebutuhan

Setiap layout menyimpan konfigurasi berikut secara terpisah untuk landscape dan portrait:
- Posisi setiap tombol (Action buttons, D-pad, analog stick, trigger buttons, dll)
- Skala tombol
- Spacing antar tombol
- Visibility settings

### 2. Layout Selection UI
Di dalam TouchControlLayoutScreen, pengguna sekarang dapat:
- **Memilih layout aktif** melalui dropdown selector ("Layout 1" atau "Layout 2")
- **Menukar layout dengan cepat** menggunakan tombol "Swap Layouts"

### 3. Persistent Configuration
Semua pengaturan disimpan di file `ppsspp.ini` di bawah section `[Control]`:
- `TouchLayoutSelection`: Menyimpan layout mana yang sedang aktif (1 atau 2)
- Semua konfigurasi layout 2 memiliki key yang bersesuaian dengan layout 1

## Implementation Details

### File yang Dimodifikasi

#### 1. `Core/Config.h`
- Menambah `TouchControlConfig touchControlsLandscapeLayout2` dan `touchControlsPortraitLayout2` untuk menyimpan konfigurasi layout alternatif
- Menambah `int iTouchLayoutSelection` untuk melacak layout mana yang sedang aktif
- Menambah method `GetCurrentTouchControlsConfig()` untuk mendapatkan layout yang sedang aktif
- Menambah method `SwapTouchControlsLayouts()` untuk menukar layout

#### 2. `Core/Config.cpp`
- Implementasi `SwapTouchControlsLayouts()` yang menukar layout configuration menggunakan `std::swap()`
- Menambah `ConfigSetting` untuk `TouchLayoutSelection` agar dapat disimpan/dimuat dari INI file

#### 3. `UI/TouchControlLayoutScreen.h`
- Menambah event handler `OnLayoutSelection()` untuk menangani perubahan layout selection
- Menambah event handler `OnSwapLayouts()` untuk menangani tombol swap

#### 4. `UI/TouchControlLayoutScreen.cpp`
- Menambah ChoiceStrip untuk memilih antara Layout 1 dan Layout 2
- Menambah tombol "Swap Layouts"
- Implementasi kedua event handler untuk mengupdate visual dan reload layout

## Usage Flow

### Menggunakan Swap Layout Feature

1. **Buka Layout Editor**
   - Masuk ke Controls → Touch Control Layout

2. **Konfigurasi Layout 1**
   - Pastikan "Layout 1" dipilih di selector
   - Edit posisi dan ukuran tombol sesuai keinginan
   - Klik "Back" untuk menyimpan

3. **Beralih ke Layout 2**
   - Buka kembali Layout Editor
   - Pilih "Layout 2" dari selector
   - Konfigurasi layout alternate yang berbeda
   - Klik "Back" untuk menyimpan

4. **Menukar Layouts**
   - Di Layout Editor, klik tombol "Swap Layouts"
   - Kedua layout akan bertukar posisi
   - Layout yang sebelumnya aktif sekarang menjadi yang lain

5. **Memilih Layout di Game**
   - Buka Layout Editor kapan saja
   - Pilih Layout 1 atau Layout 2 dari selector
   - Pilihan akan langsung diterapkan

## Technical Architecture

### Data Flow
```
tonglation Selection
    ↓
GetCurrentTouchControlsConfig()
    ↓
Returns correct layout (Layout 1 or 2)
    ↓
InitPadLayout() & Rendering
```

### Layout Storage
```
ppsspp.ini [Control] section
├── TouchLayoutSelection = 1 or 2
├── Primary Layout (touchControlsLandscape/Portrait)
│   ├── ActionButtonCenterX, Y, Scale
│   ├── DPadX, Y, Scale
│   ├── Custom1-20 positions
│   └── ... other button configs
└── Secondary Layout (Layout2 variants)
    ├── ActionButtonCenterX, Y, Scale
    ├── DPadX, Y, Scale
    ├── Custom1-20 positions
    └── ... other button configs
```

## Config Structure

Dalam `ppsspp.ini`, untuk setiap setting yang ada di Layout 1, akan ada variannya untuk Layout 2:

Contoh:
```ini
[Control]
ActionButtonCenterX = 0.924881
ActionButtonCenterY = 0.840213
; Layout 2 variants akan ditambahkan otomatis oleh sistem
```

## Benefits

1. **Quick Switching**: User dapat dengan cepat beralih antara dua layout yang telah dikonfigurasi
2. **Game-Specific Configs**: Simpan satu layout untuk action games, satu untuk RPGs
3. **Different Orientations**: Bisa punya setup berbeda untuk landscape vs portrait
4. **Easy Backup**: Bisa menggunakan Layout 2 sebagai backup dari Layout 1
5. **Experimental Layout**: Test layout baru tanpa menghilangkan yang sekarang

## Future Enhancements

1. Tambah lebih dari 2 layout (Layout 3, 4, dll)
2. Import/Export layout dari file eksternal
3. Preset layout populer
4. Layout templates untuk game genre tertentu
5. Cloud sync layouts antar device

## Testing Checklist

- [ ] Layout selection UI muncul di TouchControlLayoutScreen
- [ ] Dapat beralih antara Layout 1 dan Layout 2
- [ ] Perubahan di satu layout tidak mempengaruhi layout lain
- [ ] Tombol "Swap Layouts" bekerja dan menukar konfigurasi
- [ ] Setting disimpan ke ppsspp.ini
- [ ] Setting dapat dimuat kembali saat restart aplikasi
- [ ] Works di landscape dan portrait orientation
- [ ] Tidak ada memory leak atau crash

## Version Information

- **Feature Branch**: `swap-layout`
- **Base Commit**: From hrydgard/ppsspp master branch
- **Implementation Date**: February 8, 2026

---

Created as part of PPSSPP enhancement initiative for improved touch control management.
