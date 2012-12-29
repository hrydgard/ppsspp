#pragma once

#include <vector>
#include "ChunkFile.h"

// Sometimes you want to set something to happen later, without that later place really needing
// to know about all the things that might happen. That's when you use an Action, and add it
// to the appropriate ActionSet.
//
// Unlike CoreTiming events, these are not timed in any way and do not care about the time.
// This code also doesn't depend on anything in PPSSPP so it can live in Common.



// Pretty much a Runnable. Similar to Action from JPCSP.
class Action
{
public:
  virtual ~Action() {}
  virtual void run() = 0;
  virtual void DoState(PointerWrap &p) = 0;
  int actionTypeID;
};
