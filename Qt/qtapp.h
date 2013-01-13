#ifndef QTAPP_H
#define QTAPP_H

#include <QObject>
#include "../Core/Host.h"
#include "mainwindow.h"

class QtApp : public QObject, public Host
{
	Q_OBJECT
public:
	QtApp(MainWindow* mainWindow);

	void UpdateMemView();
	void UpdateDisassembly();
	void UpdateUI();
	void SetDebugMode(bool mode);

	void AddSymbol(std::string name, u32 addr, u32 size, int type);

	void InitGL();
	void BeginFrame();
	void EndFrame();
	void ShutdownGL();

	void InitSound(PMixer *mixer);
	void UpdateSound();
	void ShutdownSound();

	bool IsDebuggingEnabled();
	void BootDone();
	void PrepareShutdown();
	bool AttemptLoadSymbolMap();
	void SetWindowTitle(const char *message);

signals:
	void BootDoneSignal();
private:
	MainWindow* mainWindow;
};

#endif // QTAPP_H
