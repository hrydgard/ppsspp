#ifndef QTEMUGL_H
#define QTEMUGL_H

#include "gfx_es2/glsl_program.h"
#include <QGLWidget>
#include "EmuThread.h"

class QtEmuGL : public QGLWidget
{
	Q_OBJECT
public:
	explicit QtEmuGL(QWidget *parent = 0);
	void init(InputState* inputState);

	void SetRunning(bool value);

	void start_rendering();
	void stop_rendering();
protected:
	void initializeGL();

	void paintGL();
	void resizeEvent(QResizeEvent *evt);
	void paintEvent(QPaintEvent *);
	void closeEvent(QCloseEvent *evt);
signals:
	
public slots:

private:
	bool running_;
	EmuThread thread;
};

#endif // QTEMUGL_H
