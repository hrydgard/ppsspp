#ifndef CTRLVFPUVIEW_H
#define CTRLVFPUVIEW_H

#include <QWidget>
#include "Core/Debugger/DebugInterface.h"

class Debugger_Vfpu;
class CtrlVfpuView : public QWidget
{
	Q_OBJECT
public:
	explicit CtrlVfpuView(QWidget *parent = 0);

	void setParentWindow(Debugger_Vfpu* win)
	{
		parentWindow = win;
	}

	void setCPU(DebugInterface *deb)
	{
		cpu = deb;
	}
	DebugInterface *getCPU()
	{
		return cpu;
	}
	void setMode(int newMode);
	void redraw();
protected:
	void paintEvent(QPaintEvent *);

private:
	DebugInterface *cpu;
	Debugger_Vfpu* parentWindow;
	int mode;
};

#endif // CTRLVFPUVIEW_H
