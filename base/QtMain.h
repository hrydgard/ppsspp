#ifndef SYMBIANMAIN_H
#define SYMBIANMAIN_H

#include <QTouchEvent>
#include <QGLWidget>

#include <math.h>
#include <locale.h>

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

#endif

