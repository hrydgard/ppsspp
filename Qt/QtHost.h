#ifndef QTHOST_H
#define QTHOST_H

#include <QObject>
#include "../Core/Host.h"
#include "mainwindow.h"

#include "base/NativeApp.h"
#include "file/vfs.h"
#include "file/zip_read.h"
#include "gfx_es2/gl_state.h"
#include "gfx/texture.h"
#include "input/input_state.h"
#include "math/math_util.h"
#include "math/lin/matrix4x4.h"
#include <QGLWidget>

// Globals
static PMixer *g_mixer;
static QString fileToStart;
static QtEmuGL* glWindow;

class QtHost : public QObject, public Host
{
	Q_OBJECT
public:
	QtHost(MainWindow* mainWindow);

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
	void UpdateSound() { }
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
