#include "qtemugl.h"

#include <QMouseEvent>

#include "base/display.h"
#include "base/timeutil.h"

QtEmuGL::QtEmuGL(QWidget *parent) :
	QGLWidget(parent)
{
}

void QtEmuGL::init(InputState *inputState)
{
	input_state = inputState;
}

void QtEmuGL::initializeGL()
{
#ifndef USING_GLES2
	glewInit();
#endif
	NativeInitGraphics();
}
void QtEmuGL::paintGL()
{
	NativeUpdate(*input_state);
	NativeRender();
	EndInputState(input_state);

	time_update();
}

void QtEmuGL::mouseDoubleClickEvent(QMouseEvent *)
{
	emit doubleClick();
}

void QtEmuGL::mousePressEvent(QMouseEvent *e)
{
	input_state->pointer_down[0] = true;
	input_state->pointer_x[0] = e->x();
	input_state->pointer_y[0] = e->y();
}

void QtEmuGL::mouseReleaseEvent(QMouseEvent *e)
{
	input_state->pointer_down[0] = false;
}
