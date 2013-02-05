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
	thread.init(inputState);
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

void QtEmuGL::start_game(QString filename)
{
	thread.startGame(filename);
}

void QtEmuGL::stop_game()
{
	thread.stopGame();
}

void QtEmuGL::LockDraw(bool value)
{
	if(value)
	{
		thread.gameMutex.lock();
	}
	else
	{
		thread.gameMutex.unlock();
	}
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
