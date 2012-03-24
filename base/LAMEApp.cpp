#ifndef _WIN32
#include <unistd.h>
#endif

#include "LAMEApp.h"

LAMEApp::LAMEApp() {
  penDown=false;
  wannaQuit=false;
  active=0;

  TCHAR tmp[256];
#ifdef _WIN32
  hWnd=0;
  GetModuleFileName(0,tmp,255);
#else
  char *discard = getcwd(tmp, 256);
  discard++;
#endif

  path=tmp;
  path=path.getPath()+String(TEXT("/"));
}

LAMEApp::~LAMEApp() {
}

bool LAMEApp::init(int width, int height) {
  this->width = width;
  this->height = height;
  return true;
}

int LAMEApp::run() {
  return -1;
}

void LAMEApp::close() {
  wannaQuit = true;
}

void LAMEApp::startTimer(int ID, int interval) {
//  SetTimer(hWnd, ID, interval, 0);
}

void LAMEApp::stopTimer(int ID) {
//  KillTimer(hWnd, ID);
}
