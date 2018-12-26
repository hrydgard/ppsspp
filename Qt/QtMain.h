#ifndef QTMAIN_H
#define QTMAIN_H

#include <QTouchEvent>
#include <QMouseEvent>
#include <QInputDialog>
#include "gfx_es2/glsl_program.h"
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

#include "base/display.h"
#include "base/logging.h"
#include "base/timeutil.h"
#include "file/zip_read.h"
#include "gfx/gl_common.h"
#include "gfx_es2/gpu_features.h"
#include "input/input_state.h"
#include "input/keycodes.h"
#include "thin3d/thin3d.h"
#include "base/NativeApp.h"
#include "net/resolve.h"
#include "base/NKCodeFromQt.h"

#include "Common/GraphicsContext.h"
#include "Core/Core.h"
#include "Core/Config.h"
#include "Core/ConfigValues.h"
#include "Core/System.h"
#include "thin3d/thin3d_create.h"
#include "thin3d/GLRenderManager.h"

// Input
void SimulateGamepad();

class QtGLGraphicsContext : public GraphicsContext {
public:
	QtGLGraphicsContext() {
		CheckGLExtensions();
		draw_ = Draw::T3DCreateGLContext();
		SetGPUBackend(GPUBackend::OPENGL);
		renderManager_ = (GLRenderManager *)draw_->GetNativeObject(Draw::NativeObject::RENDER_MANAGER);
		bool success = draw_->CreatePresets();
		_assert_msg_(G3D, success, "Failed to compile preset shaders");
	}

	~QtGLGraphicsContext() {
		delete draw_;
		draw_ = nullptr;
		renderManager_ = nullptr;
	}

	void Shutdown() override {}
	void SwapInterval(int interval) override {}
	void SwapBuffers() override {}
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
		renderManager_->WaitUntilQueueIdle();
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
	QtGLGraphicsContext *graphicsContext;

	float xscale, yscale;
#if defined(MOBILE_DEVICE)
	QAccelerometer* acc;
#endif

	std::thread emuThread;
	std::atomic<int> emuThreadState;
};

extern MainUI* emugl;

#ifndef SDL

// Audio
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

