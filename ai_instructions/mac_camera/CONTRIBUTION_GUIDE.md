# 🎮 My PPSSPP Contribution Guide

## Step 1: Set Up Development Environment

### For Windows:
```bash
# Install Visual Studio 2022 (Community Edition is free)
# Install CMake
# Install Git

# Clone and build:
git clone --recursive https://github.com/hrydgard/ppsspp.git
cd ppsspp
mkdir build
cd build
cmake ..
# Open PPSSPP.sln in Visual Studio
```

### For Linux/Mac:
```bash
# Install dependencies
sudo apt-get install cmake build-essential libgl1-mesa-dev libsdl2-dev

# Clone and build:
git clone --recursive https://github.com/hrydgard/ppsspp.git
cd ppsspp
./b.sh
```

---

## Step 2: Find Something to Work On

### Easy Contributions:
1. **Translations** - No coding needed!
   - File: `assets/lang/your_language.ini`
   - Copy `en_US.ini` and translate

2. **Documentation** - Help others!
   - Improve the wiki
   - Write setup guides
   - Document game-specific settings

3. **Bug Reports** - Play and report!
   - Test games
   - Report issues at: https://github.com/hrydgard/ppsspp/issues
   - Include: game ID, PPSSPP version, platform, steps to reproduce

### Code Contributions:
1. Look for "TODO" comments in code
2. Check issues labeled "good first issue"
3. Fix game compatibility issues

---

## Step 3: Make Your Changes

```bash
# Create a new branch
git checkout -b my-feature-name

# Make your changes
# Test thoroughly!

# Commit with clear message
git add .
git commit -m "Fix: USB Camera stop functions implementation

- Implemented sceUsbCamStopStill()
- Implemented sceUsbCamStopVideo()
- Tested with Invizimals"
```

---

## Step 4: Submit Pull Request

1. Fork PPSSPP on GitHub
2. Push your branch to your fork
3. Create Pull Request on main repo
4. Describe what you changed and why
5. Be patient and respond to feedback!

---

## 🎯 Current Opportunities (Checked Today)

### USB Camera Implementation
- **File:** Core/HLE/sceUsbCam.cpp
- **Lines:** 266-267
- **Difficulty:** Medium
- **Impact:** Enables camera-based games

### UI Performance Fix
- **File:** UI/SavedataScreen.cpp
- **Line:** 98
- **Difficulty:** Easy-Medium
- **Impact:** Better UI performance

### Translation Updates
- **Files:** assets/lang/*.ini
- **Difficulty:** Easy
- **Impact:** Help non-English speakers!

---

## 💡 Tips for Success

1. **Start Small** - Don't try to rewrite the GPU on day 1!
2. **Read Existing Code** - See how others solved similar problems
3. **Test Everything** - Test your changes with multiple games
4. **Ask Questions** - Join Discord: https://discord.gg/5NJB6dD
5. **Be Patient** - Reviews take time, maintainers are volunteers

---

## 📚 Resources

- **Official Site:** https://www.ppsspp.org/
- **GitHub:** https://github.com/hrydgard/ppsspp
- **Wiki:** https://github.com/hrydgard/ppsspp/wiki
- **Discord:** https://discord.gg/5NJB6dD
- **Forums:** https://forums.ppsspp.org/
- **Issues:** https://github.com/hrydgard/ppsspp/issues

---

## 🏆 Good First Issues

Search for: https://github.com/hrydgard/ppsspp/labels/good%20first%20issue

These are issues specifically marked as beginner-friendly!

---

Happy Contributing! 🎉

