#include "qtemugl.h"

QtEmuGL::QtEmuGL(QWidget *parent) :
	QGLWidget(parent),
	running_(false),
	thread()
{
	setAutoBufferSwap(false);
}

void QtEmuGL::init(InputState *inputState)
{
	thread.init(this, inputState);
}

void QtEmuGL::SetRunning(bool value)
{
	running_ = value;
}

void QtEmuGL::initializeGL()
{
}
void QtEmuGL::paintGL()
{
	update();
}

void QtEmuGL::start_rendering()
{
	thread.start();
}

void QtEmuGL::stop_rendering()
{
	thread.setRunning(false);
	thread.wait();
	thread.Shutdown();
}

void QtEmuGL::resizeEvent(QResizeEvent *evt)
{
	// TODO
	//glt.resizeViewport(evt->size());
}

void QtEmuGL::paintEvent(QPaintEvent *)
{
}

void QtEmuGL::closeEvent(QCloseEvent *evt)
{
	//TODO stopRendering();
	QGLWidget::closeEvent(evt);
}
