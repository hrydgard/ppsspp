#pragma once
#include <QThread>
#include "input/input_state.h"

class QtEmuGL;

class EmuThread : public QThread
{
public:

	EmuThread() : running(false) {}
	void init(QtEmuGL* _glw, InputState* inputState);
	void run();
	void FinalShutdown();
	void setRunning(bool value);
public slots:
	void Shutdown();
private:
	QtEmuGL* glw;
	InputState* input_state;
	bool running;
};

void EmuThread_Start(const char *filename, QtEmuGL* glWindow);
void EmuThread_Stop();

char *GetCurrentFilename();
