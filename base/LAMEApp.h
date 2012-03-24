#pragma once

//LAME: the Lean And MEan Extensions.

// #include "gfx/draw_buffer.h"
#include "LAMEString.h"

class LAMEApp {
 protected:
  bool wannaQuit;
  bool active;
  int width;
  int height;
#ifndef WINCE
 public:
#endif
  bool penDown;
 protected:
  String path;
 public:
  LAMEApp();
  virtual ~LAMEApp();

  bool init(int width, int height);
  virtual int run();
  void close();

  void startTimer(int ID, int interval);
  void stopTimer(int ID);

  void refresh();

  virtual void onLoop() { }

  virtual void onButtonDown(int b) {}
  virtual void onButtonUp(int b) {}

  virtual void onPenDown(int x, int y) { }
  virtual void onPenMove(int x, int y, bool pressed) { }
  virtual void onPenUp(int x, int y) { }

  virtual void onKeyDown(int key) { }
  virtual void onKeyChar(int key) { }
  virtual void onKeyUp(int key) { }

  virtual void onLostFocus() { }
  virtual void onGotFocus() { }

  virtual void onCreate() { }
  virtual void onDestroy() { }
  virtual void onTimer(int timerID) { }

  virtual void draw() { }

  int getWidth() const {return width;}
  int getHeight() const { return height;}
  String getPath() const {
    return path;
  }
};

inline String getRelativePath(String temp)
{
#if defined(ANDROID) || defined(SDL)
  return temp;
#else
  #error TODO: check this
  if (!theApp) return temp;
#ifdef WINCE
  if (temp[0] != TCHAR('\\')) //if it's a relative path (assume to the program location), then let's expand it to a full path
    if (temp.find(theApp->getPath(),0)==-1)
    {
      return theApp->getPath() + temp;
    }
#else
  if (temp[2] != TCHAR('\\')) //if it's a relative path (assume to the program location), then let's expand it to a full path
    if (temp.find(theApp->getPath(),0)==-1)
    {
      return theApp->getPath() + temp;
    }
#endif
    return temp;
#endif
}
