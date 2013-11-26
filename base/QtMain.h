#ifndef QTMAIN_H
#define QTMAIN_H

#include <QTouchEvent>
#include <QMouseEvent>
#include "gfx_es2/glsl_program.h"
#include <QGLWidget>

#include <QAudioOutput>
#include <QAudioFormat>
#ifdef USING_GLES2
#include <QAccelerometer>
QTM_USE_NAMESPACE
#endif

#include "base/display.h"
#include "base/logging.h"
#include "base/timeutil.h"
#include "file/zip_read.h"
#include "gfx_es2/gl_state.h"
#include "input/input_state.h"
#include "input/keycodes.h"
#include "base/NativeApp.h"
#include "net/resolve.h"
#include "display.h"
#include "base/NKCodeFromQt.h"

// Bad: PPSSPP includes from native
#include "Core/Core.h"
#include "Core/Config.h"

// Input
void SimulateGamepad(InputState *input);

//GUI
class MainUI : public QGLWidget
{
	Q_OBJECT
public:
	explicit MainUI(QWidget *parent = 0):
		QGLWidget(parent)
	{
		setAttribute(Qt::WA_AcceptTouchEvents);
#if QT_VERSION < 0x50000
		setAttribute(Qt::WA_LockLandscapeOrientation);
#endif
#ifdef USING_GLES2
		acc = new QAccelerometer(this);
		acc->start();
#endif
		setFocus();
		setFocusPolicy(Qt::StrongFocus);
		startTimer(16);
	}
	~MainUI() {
#ifdef USING_GLES2
		delete acc;
#endif
		NativeShutdownGraphics();
	}

signals:
	void doubleClick();
	void newFrame();

protected:
	void resizeEvent(QResizeEvent * e)
	{
		pixel_xres = e->size().width();
		pixel_yres = e->size().height();
		dp_xres = pixel_xres * g_dpi_scale;
		dp_yres = pixel_yres * g_dpi_scale;
		PSP_CoreParameter().pixelWidth = pixel_xres;
		PSP_CoreParameter().pixelHeight = pixel_yres;
		PSP_CoreParameter().outputWidth = dp_xres;
		PSP_CoreParameter().outputHeight = dp_yres;
	}

	void timerEvent(QTimerEvent *) {
		updateGL();
		emit newFrame();
	}
	bool event(QEvent *e)
	{
		TouchInput input;
		QList<QTouchEvent::TouchPoint> touchPoints;
		switch(e->type())
		{
		case QEvent::TouchBegin:
		case QEvent::TouchUpdate:
		case QEvent::TouchEnd:
			touchPoints = static_cast<QTouchEvent *>(e)->touchPoints();
			foreach (const QTouchEvent::TouchPoint &touchPoint, touchPoints) {
				switch (touchPoint.state()) {
				case Qt::TouchPointStationary:
					break;
				case Qt::TouchPointPressed:
				case Qt::TouchPointReleased:
					input_state.pointer_down[touchPoint.id()] = (touchPoint.state() == Qt::TouchPointPressed);
					input_state.pointer_x[touchPoint.id()] = touchPoint.pos().x() * g_dpi_scale;
					input_state.pointer_y[touchPoint.id()] = touchPoint.pos().y() * g_dpi_scale;

					input.x = touchPoint.pos().x() * g_dpi_scale;
					input.y = touchPoint.pos().y() * g_dpi_scale;
					input.flags = (touchPoint.state() == Qt::TouchPointPressed) ? TOUCH_DOWN : TOUCH_UP;
					input.id = touchPoint.id();
					NativeTouch(input);
					break;
				case Qt::TouchPointMoved:
					input_state.pointer_x[touchPoint.id()] = touchPoint.pos().x() * g_dpi_scale;
					input_state.pointer_y[touchPoint.id()] = touchPoint.pos().y() * g_dpi_scale;

					input.x = touchPoint.pos().x() * g_dpi_scale;
					input.y = touchPoint.pos().y() * g_dpi_scale;
					input.flags = TOUCH_MOVE;
					input.id = touchPoint.id();
					NativeTouch(input);
					break;
				default:
					break;
				}
			}
			break;
		case QEvent::MouseButtonDblClick:
			if (!g_Config.bShowTouchControls || globalUIState != UISTATE_INGAME)
				emit doubleClick();
			break;
		case QEvent::MouseButtonPress:
		case QEvent::MouseButtonRelease:
			input_state.pointer_down[0] = (e->type() == QEvent::MouseButtonPress);
			input_state.pointer_x[0] = ((QMouseEvent*)e)->pos().x() * g_dpi_scale;
			input_state.pointer_y[0] = ((QMouseEvent*)e)->pos().y() * g_dpi_scale;

			input.x = ((QMouseEvent*)e)->pos().x() * g_dpi_scale;
			input.y = ((QMouseEvent*)e)->pos().y() * g_dpi_scale;
			input.flags = (e->type() == QEvent::MouseButtonPress) ? TOUCH_DOWN : TOUCH_UP;
			input.id = 0;
			NativeTouch(input);
			break;
		case QEvent::MouseMove:
			input_state.pointer_x[0] = ((QMouseEvent*)e)->pos().x() * g_dpi_scale;
			input_state.pointer_y[0] = ((QMouseEvent*)e)->pos().y() * g_dpi_scale;

			input.x = ((QMouseEvent*)e)->pos().x() * g_dpi_scale;
			input.y = ((QMouseEvent*)e)->pos().y() * g_dpi_scale;
			input.flags = TOUCH_MOVE;
			input.id = 0;
			NativeTouch(input);
			break;
		case QEvent::Wheel:
			NativeKey(KeyInput(DEVICE_ID_MOUSE, ((QWheelEvent*)e)->delta()<0 ? NKCODE_EXT_MOUSEWHEEL_DOWN : NKCODE_EXT_MOUSEWHEEL_UP, KEY_DOWN));
			break;
		case QEvent::KeyPress:
			NativeKey(KeyInput(DEVICE_ID_KEYBOARD, KeyMapRawQttoNative.find(((QKeyEvent*)e)->key())->second, KEY_DOWN));
			break;
		case QEvent::KeyRelease:
			NativeKey(KeyInput(DEVICE_ID_KEYBOARD, KeyMapRawQttoNative.find(((QKeyEvent*)e)->key())->second, KEY_UP));
			break;
		default:
			return QWidget::event(e);
		}
		e->accept();
		return true;
	}

	void initializeGL()
	{
#ifndef USING_GLES2
		glewInit();
#endif
		NativeInitGraphics();
	}

	void paintGL()
	{
		updateAccelerometer();
		UpdateInputState(&input_state);
		NativeUpdate(input_state);
		NativeRender();
		EndInputState(&input_state);
		time_update();
	}

	void updateAccelerometer()
	{
#ifdef USING_GLES2
		// TODO: Toggle it depending on whether it is enabled
		QAccelerometerReading *reading = acc->reading();
		if (reading) {
			input_state.acc.x = reading->x();
			input_state.acc.y = reading->y();
			input_state.acc.z = reading->z();
		}
#endif
	}

private:
	InputState input_state;
#ifdef USING_GLES2
	QAccelerometer* acc;
#endif
};

// Audio
#define AUDIO_FREQ 44100
#define AUDIO_CHANNELS 2
#define AUDIO_SAMPLES 2048
#define AUDIO_SAMPLESIZE 16
class MainAudio: public QObject
{
	Q_OBJECT
public:
	MainAudio() {
	}
	~MainAudio() {
		if (feed != NULL) {
			killTimer(timer);
			feed->close();
		}
		if (output) {
			output->stop();
			delete output;
		}
		if (mixbuf)
			free(mixbuf);
	}
public slots:
	void run() {
		QAudioFormat fmt;
		fmt.setSampleRate(AUDIO_FREQ);
		fmt.setCodec("audio/pcm");
		fmt.setChannelCount(AUDIO_CHANNELS);
		fmt.setSampleSize(AUDIO_SAMPLESIZE);
		fmt.setByteOrder(QAudioFormat::LittleEndian);
		fmt.setSampleType(QAudioFormat::SignedInt);
		mixlen = 5*2*AUDIO_CHANNELS*AUDIO_SAMPLES;
		mixbuf = (char*)malloc(mixlen);
		output = new QAudioOutput(fmt);
		output->setBufferSize(mixlen);
		feed = output->start();
		if (feed != NULL)
			timer = startTimer(1000*AUDIO_SAMPLES / AUDIO_FREQ);
	}

protected:
	void timerEvent(QTimerEvent *) {
		memset(mixbuf, 0, mixlen);
		size_t frames = NativeMix((short *)mixbuf, 5*AUDIO_SAMPLES);
		if (frames > 0)
			feed->write(mixbuf, sizeof(short) * 2 * frames);
	}
private:
	QIODevice* feed;
	QAudioOutput* output;
	int mixlen;
	char* mixbuf;
	int timer;
};

#endif

