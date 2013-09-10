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
	TouchInput input;
	input_state->pointer_down[0] = true;
	input_state->pointer_x[0] = e->x();
	input_state->pointer_y[0] = e->y();

	input.x = e->x();
	input.y = e->y();
	input.flags = TOUCH_DOWN;
	input.id = 0;
	NativeTouch(input);
}

void QtEmuGL::mouseReleaseEvent(QMouseEvent *e)
{
	TouchInput input;
	input_state->pointer_down[0] = false;

	input.x = e->x();
	input.y = e->y();
	input.flags = TOUCH_UP;
	input.id = 0;
	NativeTouch(input);
}

void QtEmuGL::mouseMoveEvent(QMouseEvent *e)
{
	TouchInput input;
	input.x = e->x();
	input.y = e->y();
	input.flags = TOUCH_MOVE;
	input.id = 0;
	NativeTouch(input);
}

void QtEmuGL::wheelEvent(QWheelEvent *e)
{
	KeyInput key;
	key.deviceId = DEVICE_ID_MOUSE;
	key.keyCode = e->delta()<0 ? NKCODE_EXT_MOUSEWHEEL_DOWN : NKCODE_EXT_MOUSEWHEEL_UP;
	key.flags = KEY_DOWN;
	NativeKey(key);
}
