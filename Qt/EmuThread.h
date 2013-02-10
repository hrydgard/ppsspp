#pragma once
#include <QThread>
#include <QMutex>
#include "input/input_state.h"

class QtEmuGL;

class EmuThread : public QThread
{
public:
	EmuThread();
	~EmuThread();
	void init(InputState* inputState);
	void run();
	void FinalShutdown();
	void setRunning(bool value);
	void startGame(QString filename);
	void stopGame();
	void LockGL(bool value);
public slots:
	void Shutdown();
private:
	InputState* input_state;
	bool running;
	bool gameRunning;
	bool needInitGame;
	int frames_;
	QMutex *gameMutex;
	int mutexLockNum;

};

void EmuThread_Start(QtEmuGL* w);
void EmuThread_Stop();
void EmuThread_StartGame(QString filename);
void EmuThread_StopGame();
void EmuThread_LockDraw(bool value);
QString GetCurrentFilename();
