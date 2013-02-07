#pragma once
#include <QThread>
#include <QMutex>
#include "input/input_state.h"

class QtEmuGL;

class EmuThread : public QThread
{
public:
	EmuThread() : running(false), gameRunning(false), needInitGame(false), frames_(0) {}
	void init(InputState* inputState);
	void run();
	void FinalShutdown();
	void setRunning(bool value);
	void startGame(QString filename);
	void stopGame();
	QMutex gameMutex;
public slots:
	void Shutdown();
private:
	InputState* input_state;
	bool running;
	bool gameRunning;
	bool needInitGame;
	int frames_;

};

void EmuThread_Start(QtEmuGL* w);
void EmuThread_Stop();
void EmuThread_StartGame(QString filename);
void EmuThread_StopGame();
void EmuThread_LockDraw(bool value);
