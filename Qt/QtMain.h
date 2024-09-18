#ifndef QTMAIN_H
#define QTMAIN_H

#include <QTouchEvent>
#include <QMouseEvent>
#include <QInputDialog>
#include "Common/GPU/OpenGL/GLSLProgram.h"
#include <QGLWidget>

#ifndef SDL
#include <QAudioOutput>
#include <QAudioFormat>
#endif
#if defined(MOBILE_DEVICE)
#include <QAccelerometer>
#if QT_VERSION < QT_VERSION_CHECK(5, 0, 0)
QTM_USE_NAMESPACE
#endif
#endif

#include <cassert>
#include <atomic>
#include <thread>

#include "Common/System/Display.h"
#include "Common/TimeUtil.h"
#include "Common/File/VFS/VFS.h"
#include "Common/File/VFS/DirectoryReader.h"
#include "Common/GPU/OpenGL/GLCommon.h"
#include "Common/GPU/OpenGL/GLFeatures.h"
#include "Common/Input/InputState.h"
#include "Common/Input/KeyCodes.h"
#include "Common/GPU/thin3d.h"
#include "Common/Net/Resolve.h"
#include "NKCodeFromQt.h"

#include "Common/GraphicsContext.h"
#include "Core/Core.h"
#include "Core/Config.h"
#include "Core/ConfigValues.h"
#include "Core/System.h"
#include "Common/GPU/thin3d_create.h"
#include "Common/GPU/OpenGL/GLRenderManager.h"

// Input
void SimulateGamepad();

class QtGLGraphicsContext : public GraphicsContext {
public:
	QtGLGraphicsContext() {
		CheckGLExtensions();
		draw_ = Draw::T3DCreateGLContext(false);
		SetGPUBackend(GPUBackend::OPENGL);
		renderManager_ = (GLRenderManager *)draw_->GetNativeObject(Draw::NativeObject::RENDER_MANAGER);
		renderManager_->SetInflightFrames(g_Config.iInflightFrames);
		bool success = draw_->CreatePresets();
		_assert_msg_(success, "Failed to compile preset shaders");

		// TODO: Need to figure out how to implement SetSwapInterval for Qt.
	}

	~QtGLGraphicsContext() {
		delete draw_;
		draw_ = nullptr;
		renderManager_ = nullptr;
	}

	void Shutdown() override {}
	void Resize() override {}

	Draw::DrawContext *GetDrawContext() override {
		return draw_;
	}

	void ThreadStart() override {
		renderManager_->ThreadStart(draw_);
	}

	bool ThreadFrame() override {
		return renderManager_->ThreadFrame();
	}

	void ThreadEnd() override {
		renderManager_->ThreadEnd();
	}

	void StopThread() override {
		renderManager_->StopThread();
	}

private:
	Draw::DrawContext *draw_ = nullptr;
	GLRenderManager *renderManager_ = nullptr;
};

enum class EmuThreadState {
	DISABLED,
	START_REQUESTED,
	RUNNING,
	QUIT_REQUESTED,
	STOPPED,
};


// GUI, thread manager
class MainUI : public QGLWidget
{
	Q_OBJECT
public:
	explicit MainUI(QWidget *parent = 0);
	~MainUI();

	void resizeGL(int w, int h);

public slots:
	QString InputBoxGetQString(QString title, QString defaultValue);

signals:
	void doubleClick();
	void newFrame();

protected:
	void timerEvent(QTimerEvent *);
	void changeEvent(QEvent *e);
	bool event(QEvent *e);

	void initializeGL();
	void paintGL();

	void updateAccelerometer();

	void EmuThreadFunc();
	void EmuThreadStart();
	void EmuThreadStop();
	void EmuThreadJoin();

private:
	bool HandleCustomEvent(QEvent *e);
	QtGLGraphicsContext *graphicsContext;

	float xscale, yscale;
#if defined(MOBILE_DEVICE)
	QAccelerometer* acc;
#endif

	std::thread emuThread;
	std::atomic<int> emuThreadState;
};

class QTCamera : public QObject {
	Q_OBJECT
public:
	QTCamera() {}
	~QTCamera() {};

signals:
	void onStartCamera(int width, int height);
	void onStopCamera();

public slots:
	void startCamera(int width, int height);
	void stopCamera();
};

extern MainUI* emugl;

#ifndef SDL

// AUDIO
class MainAudio : public QObject {
	Q_OBJECT
public:
	MainAudio() {}
	~MainAudio();
public slots:
	void run();
protected:
	void timerEvent(QTimerEvent *);
private:
	QIODevice* feed;
	QAudioOutput* output;
	int mixlen;
	char* mixbuf;
	int timer;
};

#endif //SDL

#endif
