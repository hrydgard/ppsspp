# 🎮 Testing Your USB Camera Implementation

## ✅ Build Successful!

Your custom PPSSPP with USB Camera still image capture is ready!

**Location:** `/Users/lazycoder/WorkSpace/OpenSource/PPSSPP/build/PPSSPPSDL.app`

---

## 🚀 How to Test

### Option 1: Run from Terminal (Best for Debugging)

```bash
# Run PPSSPP from terminal to see logs
cd /Users/lazycoder/WorkSpace/OpenSource/PPSSPP/build
open PPSSPPSDL.app

# OR run directly to see console output:
./PPSSPPSDL.app/Contents/MacOS/PPSSPPSDL
```

### Option 2: Open with Finder

1. Navigate to: `/Users/lazycoder/WorkSpace/OpenSource/PPSSPP/build/`
2. Double-click `PPSSPPSDL.app`
3. Load your PSP game ROM

---

## 🎮 Testing with Your Game

1. **Launch PPSSPP**
   - The app should open showing the game browser

2. **Load Your Game**
   - Click "Load" 
   - Navigate to your Downloads folder
   - Select your PSP game file (.iso, .cso, .pbp)

3. **Play the Game**
   - If it's a camera game (like Invizimals), try using camera features
   - The game should no longer crash when calling camera functions!

---

## 📊 Check if Our Functions Are Being Called

### View Live Logs (Terminal Method):

```bash
# Run with full logging
cd /Users/lazycoder/WorkSpace/OpenSource/PPSSPP/build
./PPSSPPSDL.app/Contents/MacOS/PPSSPPSDL 2>&1 | grep -i "usbcam"
```

### Look for These Messages:

✅ **Success indicators:**
```
sceUsbCamStillInputBlocking(...)
sceUsbCamStillInput(...)
sceUsbCamStillWaitInputEnd()
sceUsbCamStillPollInputEnd()
```

❌ **Old behavior (would have been):**
```
UNIMPL sceUsbCamStill...   (This won't appear anymore!)
```

---

## 🎯 What Games to Test

### Best for Testing:
- **Invizimals** series - Uses camera heavily
- **EyePet** - Camera pet interaction
- **Go! Cam** - Camera application

### Note:
Most camera games won't have perfect camera functionality (since we're using dummy images), 
but they **should NOT crash** anymore when calling still image functions!

---

## 📝 Expected Results

### ✅ Success:
- Game loads without crashing
- No "UNIMPL" errors for our functions
- Our function names appear in logs
- Game can proceed past camera screens

### ❌ If Issues:
- Check log files in: `~/Library/Application Support/PPSSPP/`
- Look for error messages
- Share logs in Discord for help

---

## 🐛 Debugging Tips

### Enable Verbose Logging:
1. Open PPSSPP
2. Go to Settings → System
3. Enable "Developer Tools"
4. Change Log Level to "Verbose" or "Debug"

### Check Log File:
```bash
cat ~/Library/Application\ Support/PPSSPP/PSP/SYSTEM/ppsspp.log | grep -i "usbcam"
```

---

## 🎉 Success Criteria

Your implementation is working if:
- [x] PPSSPP builds successfully ✅
- [ ] Game loads without crashing
- [ ] Our functions appear in logs (not "UNIMPL")
- [ ] Game progresses past camera screens
- [ ] No errors related to sceUsbCamStill* functions

---

## 📸 Take a Screenshot!

If it works, take a screenshot of:
1. The game running
2. The terminal showing our function calls
3. Share your success! 🎉

---

## 💡 Next Steps After Testing

1. **If it works:** Submit a pull request!
2. **If there are issues:** We'll debug together
3. **Share results:** Discord community will be interested!

---

## 🔗 Useful Commands

```bash
# Run PPSSPP with verbose logging
./PPSSPPSDL.app/Contents/MacOS/PPSSPPSDL --loglevel verbose

# Monitor logs in real-time
tail -f ~/Library/Application\ Support/PPSSPP/PSP/SYSTEM/ppsspp.log

# Check what functions are being called
cat ~/Library/Application\ Support/PPSSPP/PSP/SYSTEM/ppsspp.log | grep "sceUsbCam"
```

---

Have fun testing! 🎮 Let me know how it goes!

