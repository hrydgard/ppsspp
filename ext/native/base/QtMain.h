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
#if defined(MOBILE_DEVICE) && !defined(MAEMO)
#include <QAccelerometer>
#if QT_VERSION < QT_VERSION_CHECK(5, 0, 0)
QTM_USE_NAMESPACE
#endif
#endif

#include "base/display.h"
#include "base/logging.h"
#include "base/timeutil.h"
#include "file/zip_read.h"
#include "gfx/gl_common.h"
#include "input/input_state.h"
#include "input/keycodes.h"
#include "thin3d/thin3d.h"
#include "base/NativeApp.h"
#include "net/resolve.h"
#include "base/NKCodeFromQt.h"

#include "Common/GraphicsContext.h"
#include "Core/System.h"
#include "Core/Core.h"
#include "Core/Config.h"

// Input
void SimulateGamepad(InputState *input);

class QtDummyGraphicsContext : public DummyGraphicsContext {
public:
	Thin3DContext *CreateThin3DContext() override {
		return T3DCreateGLContext();
	}
};

//GUI
class MainUI : public QGLWidget
{
	Q_OBJECT
public:
    explicit MainUI(QWidget *parent = 0);
    ~MainUI();

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
    void resizeGL(int w, int h);
    void paintGL();

    void updateAccelerometer();

private:
	InputState input_state;
	QtDummyGraphicsContext *graphicsContext;

    float xscale, yscale;
#if defined(MOBILE_DEVICE) && !defined(MAEMO)
	QAccelerometer* acc;
#endif
};

extern MainUI* emugl;

#ifndef SDL

// Audio
class MainAudio: public QObject
{
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

