# Swap Layout Feature - Usage Guide

## 📱 Fitur Overview

Fitur **Swap Layout** memungkinkan Anda memiliki dua konfigurasi layout touch control yang berbeda dan dapat dengan mudah beralih antara keduanya. Fitur ini sangat berguna ketika Anda memiliki preferensi layout berbeda untuk game yang berbeda.

## 🎮 Skenario Penggunaan

### Skenario 1: Layout untuk Aksi Game vs RPG
```
Layout 1: Action Game Setup
- Action buttons lebih besar dan ditempatkan di tengah
- D-Pad lebih kecil, ditempatkan di sudut kiri
- Analog stick jauh dari tombol

Layout 2: RPG Game Setup  
- Action buttons lebih kecil, ditempatkan di sudut kanan
- D-Pad lebih besar dan terpusat
- Analog stick lebih dekat untuk kontrol karakter
```

### Skenario 2: Layout untuk Orientasi Berbeda
```
Layout 1: Landscape (main gameplay)
- Buttons tersebar penuh di landscape screen

Layout 2: Portrait (alternative setup)
- Buttons diatur untuk portrait orientation
```

### Skenario 3: Default vs Custom Backup
```
Layout 1: Default/Safe configuration
- Terbukti worked well untuk kebanyakan game

Layout 2: Custom/Experimental
- Testing layout baru sebelum commit ke default
```

## 📝 Step-by-Step Tutorial

### A. Membuat Layout 1 (Initial Setup)

1. **Buka Controls Menu**
   ```
   Main Menu → Settings → Controls → Touch Control Layout
   ```

2. **Pastikan Layout 1 Dipilih**
   - Di sidebar kiri, Anda akan melihat selector "Layout:" 
   - Pilih "Layout 1"

3. **Konfigurasi Tombol**
   - Mode "Move": Geser tombol ke posisi yang diinginkan
   - Mode "Resize": Ubah ukuran tombol dengan drag
   
4. **Edit Posisi Individual Tombol**
   - **Action Buttons** (Triangle, Circle, Square, Cross)
     - Ubah `ActionButtonCenterX` dan `ActionButtonCenterY`
     - Ubah `ActionButtonScale` untuk ukuran
   
   - **D-Pad**
     - Ubah `DPadX` dan `DPadY`
     - Ubah `DPadScale` untuk ukuran
   
   - **Analog Stick**
     - Ubah `AnalogStickX` dan `AnalogStickY`
     - Ubah `AnalogStickScale`
   
   - **Trigger Buttons** (L dan R)
     - Ubah `LKeyX`, `LKeyY`, `LKeyScale`
     - Ubah `RKeyX`, `RKeyY`, `RKeyScale`

5. **Simpan Konfigurasi**
   - Klik "Back" untuk menyimpan secara otomatis

### B. Membuat Layout 2 (Alternatif)

1. **Buka Layout Editor**
   ```
   Main Menu → Settings → Controls → Touch Control Layout
   ```

2. **Pilih Layout 2**
   - Dari selector di sidebar, pilih "Layout 2"

3. **Konfigurasi Layout 2**
   - Lakukan pengaturan sama seperti Layout 1 tapi dengan parameter berbeda
   - Contoh: perbesar action buttons, perkecil D-pad, dll

4. **Simpan**
   - Klik "Back"

### C. Menukar Antara Layout

#### Cara 1: Menggunakan Swap Layouts Button
```
1. Buka Touch Control Layout editor
2. Klik tombol "Swap Layouts"
3. Kedua layout akan langsung menukar posisi
4. Layout yang sebelumnya Layout 1 sekarang menjadi Layout 2
```

#### Cara 2: Menggunakan Layout Selector
```
1. Buka Touch Control Layout editor
2. Gunakan dropdown "Layout:" untuk memilih Layout 1 atau 2
3. Layout akan langsung berubah
```

#### Cara 3: Menggunakan Custom Button Binding (NEW)
```
1. Buka Touch Control Layout editor
2. Klik "Customize" untuk membuka Touch Control Visibility
3. Klik pada "Custom X" button yang ingin digunakan untuk swap
4. Di CustomButtonMappingScreen, pilih "Swap layout" dari action list
5. Atur posisi, shape, dan icon sesuai keinginan
6. Sekarang tombol tersebut akan swap layout saat di-tekan dalam game!
```

Ini memungkinkan Anda untuk:
- Memiliki dedicated button untuk swap layout
- Akses swap layout tanpa membuka menu
- Combine dengan button mapping untuk keyboard/gamepad
- Quick toggle antara 2 layout saat bermain

## ⚙️ Configuration File Format

Pengaturan disimpan di `PSP/SYSTEM/ppsspp.ini` di bawah section `[Control]`:

```ini
[Control]
; Current layout selection (1 = Layout 1, 2 = Layout 2)
TouchLayoutSelection = 1

; Layout 1 Settings (Primary)
ActionButtonCenterX = 0.924881
ActionButtonCenterY = 0.840213
ActionButtonScale = 1.150000
DPadX = 0.089484
DPadY = 0.616511
DPadScale = 1.150000
AnalogStickX = 0.089484
AnalogStickY = 0.877497
AnalogStickScale = 1.150000
; ... more Layout 1 settings

; Layout 2 Settings (Secondary) - akan ditambahkan otomatis
; Setiap setting Layout 1 memiliki pasangan Layout 2
```

## 🔄 Data & Penyimpanan

### Automatic Backup Feature
Ketika Anda menggunakan Swap Layouts:
```
Before Swap:              After Swap:
Layout 1 → Config1   →   Layout 2 → Config1
Layout 2 → Config2   →   Layout 1 → Config2
```

Ini seperti memiliki backup otomatis - tidak ada data yang hilang, hanya ditukar!

## 💾 Backup & Restore

### Manual Backup
1. Buka Windows Explorer / File Manager
2. Navigasi ke `PSP/SYSTEM/`
3. Copy `ppsspp.ini` dengan nama baru `ppsspp_backup.ini`

### Restore Layout
1. Lakukan Swap Layouts, atau
2. Edit direct di ppsspp.ini
3. Atau restore dari backup file

## ⚡ Pro Tips

### Tip 1: Preset Layouts untuk Game Genre
```
Sesuaikan:
- Action Games: Large buttons, accessible placement
- Puzzle Games: Precise smaller buttons  
- RPGs: Balanced layout with easy access
- Fighting Games: Quick access buttons
```

### Tip 2: Per-Game Customization
Jika ppsspp.ini support per-game config:
- Buat game folder specific
- Input different layouts untuk setiap game
- Swap layout saat switch game

### Tip 2.5: Button Binding untuk Quick Swap (NEW)
Setup dedicated button untuk instant layout swap saat bermain:
```
1. Buka Touch Control Layout → Customize
2. Pilih Custom Button (misalnya Custom 1)
3. Set action ke "Swap layout"
4. Tempatkan di corner yang mudah diakses
5. Sekarang tekan button anytime untuk swap layout!
```

Keuntungan:
- Swap layout tanpa buka menu
- Improve gaming experience
- Quick toggle antara 2 setup

### Tip 3: Share Layouts
```
Format sharing (manual edit ppsspp.ini):
[Shared Layouts]
Layout1_ActionX=0.924881
Layout1_ActionY=0.840213
Layout2_ActionX=0.80000
Layout2_ActionY=0.75000
```

### Tip 4: Combine dengan Customize Feature
```
1. Edit visibility (Customize button)
2. Edit positions (Move/Resize mode)
3. Save sebagai Layout 1
4. Modify lagi untuk Layout 2
5. Swap sesuai kebutuhan
```

## 🚀 Quick Reference Table

| Action | Menu Path | Steps |
|--------|-----------|-------|
| **Open Layout Editor** | Settings → Controls → Touch Control Layout | 1 |
| **Change Layout** | Dropdown "Layout:" selector | 1 |
| **Move Button** | Move mode + Drag button | 2 |
| **Resize Button** | Resize mode + Drag button | 2 |
| **Swap Layouts** | Click "Swap Layouts" button | 1 |
| **Reset Layout** | Click "Reset" button | 1 |
| **Save Changes** | Click "Back" button | 1 |

## ❓ Troubleshooting

### Q: Perubahan tidak tersimpan?
**A:** Pastikan click "Back" button untuk save. Jangan tutup UI dengan ESC.

### Q: Layout 2 kosong/default?
**A:** Normal, Layout 2 menggunakan default PPSSPP hingga dikonfigurasi. Setup sesuai kebutuhan.

### Q: Swap tidak bekerja?
**A:** Pastikan kedua layout sudah dikonfigurasi. Reload app jika issue persist.

### Q: Bagaimana restore ke Layout 1 saja?
**A:** Buka ppsspp.ini, set `TouchLayoutSelection = 1`, atau klik Layout 1 selector.

### Q: Bisa lebih dari 2 layout?
**A:** Tidak untuk saat ini. Fitur ini support 2 layout. Future enhancement bisa add lebih banyak.

## 📊 Performance Notes

- **Memory**: Minimal overhead (sekitar 2x config size untuk Layout 2)
- **Speed**: Swap instant (hanya swap references, bukan copy data)
- **Save/Load**: Automatic dengan ppsspp.ini

## 🎯 Best Practices

1. ✅ **DO:** Test layout thoroughly sebelum production use
2. ✅ **DO:** Backup ppsspp.ini sebelum bulk changes
3. ✅ **DO:** Document layout setup di text file untuk reference
4. ❌ **DON'T:** Edit ppsspp.ini langsung tanpa backup
5. ❌ **DON'T:** Mix touches dari berbagai layout
6. ✅ **DO:** Use consistent naming/positioning across layouts

---

**Version**: PPSSPP Swap Layout v1.1  
**Branch**: swap-layout  
**Last Updated**: February 8, 2026

**See Also**: [Swap Layout Button Binding Guide](SWAP_LAYOUT_BUTTON_BINDING.md) for advanced button mapping options
