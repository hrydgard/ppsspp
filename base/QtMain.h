#ifndef QTMAIN_H
#define QTMAIN_H

#include <QTouchEvent>
#include <QGLWidget>

#include <QAudioOutput>
#include <QAudioFormat>

#include "base/display.h"
#include "base/logging.h"
#include "base/timeutil.h"
#include "gfx_es2/glsl_program.h"
#include "file/zip_read.h"
#include "input/input_state.h"
#include "base/NativeApp.h"
#include "net/resolve.h"
#include "display.h"

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
		default:
			return QWidget::event(e);
		}
		e->accept();
		return true;
	}

	void initializeGL()
	{
		NativeInitGraphics();
	}

	void paintGL()
	{
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
		output->setNotifyInterval(1000*AUDIO_SAMPLES / AUDIO_FREQ);
		output->setBufferSize(mixlen);
		this->connect(output, SIGNAL(notify()), this, SLOT(writeData()));
		feed = output->start();
	}
	~MainAudio() {
		delete feed;
		delete output;
		free(mixbuf);
	}

private slots:
	void writeData() {
		memset(mixbuf, 0, mixlen);
		NativeMix((short *)mixbuf, mixlen / 4);
		feed->write(mixbuf, mixlen);
	}
private:
	QIODevice* feed;
	QAudioOutput* output;
	int mixlen;
	char* mixbuf;
};

#endif

