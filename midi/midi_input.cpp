#ifdef _WIN32
// Windows implementation.

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <MMSystem.h>

#include <vector>
#include <string>

#include "base/basictypes.h"
#include "base/logging.h"
#include "midi/midi_input.h"

std::vector<std::string> MidiInGetDevices() {
  int numDevs = midiInGetNumDevs();
  std::vector<std::string> devices;
  for (int i = 0; i < numDevs; i++) {
    MIDIINCAPS caps;
    midiInGetDevCaps(i, &caps, sizeof(caps));
    devices.push_back(caps.szPname);
  }
  return devices;
}

static void CALLBACK MidiCallback(HMIDIIN hMidiIn, UINT wMsg, DWORD_PTR dwInstance, DWORD_PTR dwParam1, DWORD_PTR dwParam2) {
  MidiListener *listener = (MidiListener*)dwInstance;
  uint8_t cmd[3] = {0};

  switch (wMsg) {
  case MM_MIM_OPEN:
    ILOG("Got MIDI Open message");
    break;
  case MM_MIM_CLOSE:
    ILOG("Got MIDI Close message");
    break;
  case MM_MIM_DATA:
    cmd[0] = dwParam1 & 0xFF;
    cmd[1] = (dwParam1 >> 8) & 0xFF;
    cmd[2] = (dwParam1 >> 16) & 0xFF;
    // time = dwParam2 & 0xFFFF;
    ILOG("Got MIDI Data: %02x %02x %02x", cmd[0], cmd[1], cmd[2]);
    listener->midiEvent(cmd);
    break;
  default:
    WLOG("Got unexpected MIDI message: %08x", (uint32_t)wMsg);
    break;
  }
}

MidiDevice MidiInStart(int deviceID, MidiListener *listener) {
  HMIDIIN hMidiIn;
  MMRESULT result = midiInOpen(&hMidiIn, deviceID, (DWORD_PTR)(&MidiCallback), (DWORD_PTR)listener, CALLBACK_FUNCTION);
  midiInStart(hMidiIn);
  return (MidiDevice)hMidiIn;
}

void MidiInStop(MidiDevice device) {
  HMIDIIN hMidiIn = (HMIDIIN)device;
  midiInStop(hMidiIn);
  midiInClose(hMidiIn);
}

#else

#include <vector>
#include <string>

#include "base/basictypes.h"
#include "base/logging.h"
#include "midi/midi_input.h"

// Stubs for other platforms.

std::vector<std::string> MidiInGetDevices() {
  return std::vector<std::string>();
}

MidiDevice MidiInStart(int deviceID, MidiListener *listener) {
  FLOG("Invalid MIDI device");
  return 0;
}

void MidiInStop(MidiDevice device) {
  FLOG("Invalid MIDI device");
}

#endif