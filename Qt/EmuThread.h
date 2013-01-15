#pragma once
#include <QThread>
#include "input/input_state.h"

class QtEmuGL;

class EmuThread : public QThread
{
public:
	EmuThread() : running(false) {}
	void init(InputState* inputState);
	void run();
	void FinalShutdown();
	void setRunning(bool value);
public slots:
	void Shutdown();
private:
	InputState* input_state;
	bool running;
};

void EmuThread_Start(QString filename, QtEmuGL* w);
void EmuThread_Stop();
