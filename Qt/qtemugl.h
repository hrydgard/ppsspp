#ifndef QTEMUGL_H
#define QTEMUGL_H

#include "gfx_es2/glsl_program.h"
#include "input/input_state.h"
#include <QGLWidget>
#include "QtHost.h"

class QtEmuGL : public QGLWidget
{
	Q_OBJECT
public:
	explicit QtEmuGL(QWidget *parent = nullptr);
	~QtEmuGL() {
		NativeShutdownGraphics();
	}

	void init(InputState* inputState);
signals:
	void doubleClick();
protected:
	void initializeGL();
	void paintGL();
	void mouseDoubleClickEvent(QMouseEvent *);
	void mousePressEvent(QMouseEvent *e);
	void mouseReleaseEvent(QMouseEvent *e);

private:
	InputState *input_state;
};

#endif // QTEMUGL_H
