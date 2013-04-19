#include "qtemugl.h"

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
