#ifndef QTMAIN_H
#define QTMAIN_H

#include <QTouchEvent>
#include <QMouseEvent>
#include "gfx_es2/glsl_program.h"
#include <QGLWidget>

#include <QAudioOutput>
#include <QAudioFormat>

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
	Qt::Key_X,          //A
	Qt::Key_S,          //B
	Qt::Key_Z,          //X
	Qt::Key_A,          //Y
	Qt::Key_W,          //LBUMPER
	Qt::Key_Q,          //RBUMPER
	Qt::Key_1,          //START
	Qt::Key_2,          //SELECT
	Qt::Key_Up,         //UP
	Qt::Key_Down,       //DOWN
	Qt::Key_Left,       //LEFT
	Qt::Key_Right,      //RIGHT
	0,                  //MENU (event)
	Qt::Key_Backspace,  //BACK
	Qt::Key_I,          //JOY UP
	Qt::Key_K,          //JOY DOWN
	Qt::Key_J,          //JOY LEFT
	Qt::Key_L,          //JOY RIGHT
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
	}
	~MainUI() {
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
			for (int b = 0; b < 14; b++) {
				if (((QKeyEvent*)e)->key() == buttonMappings[b])
					input_state.pad_buttons |= (1<<b);
			}
		break;
		case QEvent::KeyRelease:
			for (int b = 0; b < 14; b++) {
				if (((QKeyEvent*)e)->key() == buttonMappings[b])
					input_state.pad_buttons &= ~(1<<b);
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
		SimulateGamepad(&input_state);
		UpdateInputState(&input_state);
		NativeUpdate(input_state);
		EndInputState(&input_state);
		NativeRender();

		update();
	}

private:
	InputState input_state;
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

