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
#include "input/input_state.h"
#include "base/NativeApp.h"
#include "net/resolve.h"
#include "display.h"

// Input
const int buttonMappings[18] = {
	Qt::Key_X + 0x20,   //A
	Qt::Key_S + 0x20,   //B
	Qt::Key_Z + 0x20,   //X
	Qt::Key_A + 0x20,   //Y
	Qt::Key_Q + 0x20,   //LBUMPER
	Qt::Key_W + 0x20,   //RBUMPER
	Qt::Key_1,          //START
	Qt::Key_2,          //SELECT
	Qt::Key_Up,         //UP
	Qt::Key_Down,       //DOWN
	Qt::Key_Left,       //LEFT
	Qt::Key_Right,      //RIGHT
	0,                  //MENU (event)
	Qt::Key_Backspace,  //BACK
	Qt::Key_I + 0x20,   //JOY UP
	Qt::Key_K + 0x20,   //JOY DOWN
	Qt::Key_J + 0x20,   //JOY LEFT
	Qt::Key_L + 0x20,   //JOY RIGHT
};
void SimulateGamepad(InputState *input);

//GUI
class MainUI : public QGLWidget
{
	Q_OBJECT
public:
	explicit MainUI(float scale, QWidget *parent = 0):
		QGLWidget(parent), dpi_scale(scale)
	{
		setAttribute(Qt::WA_AcceptTouchEvents);
		setAttribute(Qt::WA_LockLandscapeOrientation);
		pad_buttons = 0;
#ifdef USING_GLES2
		acc = new QAccelerometer(this);
		acc->start();
#endif
	}
	~MainUI() {
#ifdef USING_GLES2
		delete acc;
#endif
		NativeShutdownGraphics();
	}

protected:
	void resizeEvent(QResizeEvent * e)
	{
		pixel_xres = e->size().width();
		pixel_yres = e->size().height();
		dp_xres = pixel_xres * dpi_scale;
		dp_yres = pixel_yres * dpi_scale;
	}

	bool event(QEvent *e)
	{
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
				case Qt::TouchPointMoved:
					input_state.pointer_x[touchPoint.id()] = touchPoint.pos().x() * dpi_scale;
					input_state.pointer_y[touchPoint.id()] = touchPoint.pos().y() * dpi_scale;
					break;
				default:
					break;
				}
			}
		break;
		case QEvent::MouseButtonPress:
		case QEvent::MouseButtonRelease:
			input_state.pointer_down[0] = (e->type() == QEvent::MouseButtonPress);
		case QEvent::MouseMove:
			input_state.pointer_x[0] = ((QMouseEvent*)e)->pos().x() * dpi_scale;
			input_state.pointer_y[0] = ((QMouseEvent*)e)->pos().y() * dpi_scale;
		break;
		case QEvent::KeyPress:
			for (int b = 0; b < 18; b++) {
				if (((QKeyEvent*)e)->key() == buttonMappings[b])
					pad_buttons |= (1<<b);
			}
		break;
		case QEvent::KeyRelease:
			for (int b = 0; b < 18; b++) {
				if (((QKeyEvent*)e)->key() == buttonMappings[b])
					pad_buttons &= ~(1<<b);
			}
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
		input_state.pad_buttons = pad_buttons;
		SimulateGamepad(&input_state);
		updateAccelerometer();
		UpdateInputState(&input_state);
		NativeUpdate(input_state);
		EndInputState(&input_state);
		NativeRender();
		time_update();
		update();
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
	int pad_buttons;
	InputState input_state;
#ifdef USING_GLES2
	QAccelerometer* acc;
#endif
	float dpi_scale;
};

// Audio
#define AUDIO_FREQ 44100
#define AUDIO_CHANNELS 2
#define AUDIO_SAMPLES 1024
#define AUDIO_SAMPLESIZE 16
class MainAudio: public QObject
{
	Q_OBJECT
public:
	MainAudio() {
		QAudioFormat fmt;
		fmt.setFrequency(AUDIO_FREQ);
		fmt.setCodec("audio/pcm");
		fmt.setChannelCount(AUDIO_CHANNELS);
		fmt.setSampleSize(AUDIO_SAMPLESIZE);
		fmt.setByteOrder(QAudioFormat::LittleEndian);
		fmt.setSampleType(QAudioFormat::SignedInt);
		mixlen = 2*AUDIO_CHANNELS*AUDIO_SAMPLES;
		mixbuf = (char*)malloc(mixlen);
		output = new QAudioOutput(fmt);
		output->setBufferSize(mixlen);
		feed = output->start();
		timer = startTimer(1000*AUDIO_SAMPLES / AUDIO_FREQ);
	}
	~MainAudio() {
		killTimer(timer);
		feed->close();
		output->stop();
		delete output;
		free(mixbuf);
	}

protected:
	void timerEvent(QTimerEvent *) {
		memset(mixbuf, 0, mixlen);
		NativeMix((short *)mixbuf, mixlen / 4);
		feed->write(mixbuf, mixlen);
	}
private:
	QIODevice* feed;
	QAudioOutput* output;
	int mixlen;
	char* mixbuf;
	int timer;
};

#endif

