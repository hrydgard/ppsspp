#include "mainui.h"

#include <QAudioOutput>
#include <QAudioFormat>
#include "base/display.h"
#include "base/logging.h"
#include "base/timeutil.h"
#include "file/zip_read.h"
#include "base/NativeApp.h"
#include "net/resolve.h"
#include "base/display.h"


void SimulateGamepad(InputState *input) {
	input->pad_lstick_x = 0;
	input->pad_lstick_y = 0;
	input->pad_rstick_x = 0;
	input->pad_rstick_y = 0;

	if (input->pad_buttons & (1<<14))
		input->pad_lstick_y=1;
	else if (input->pad_buttons & (1<<15))
		input->pad_lstick_y=-1;
	if (input->pad_buttons & (1<<16))
		input->pad_lstick_x=-1;
	else if (input->pad_buttons & (1<<17))
		input->pad_lstick_x=1;
}

MainUI::~MainUI() {
	NativeShutdownGraphics();
}

bool MainUI::event(QEvent *e)
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

void MainUI::initializeGL()
{
#ifndef USING_GLES2
	glewInit();
#endif
	NativeInitGraphics();
}
void MainUI::paintGL()
{
	SimulateGamepad(&input_state);
	UpdateInputState(&input_state);
	NativeUpdate(input_state);
	EndInputState(&input_state);
	NativeRender();

	update();
}
