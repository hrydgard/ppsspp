#ifndef CTRLMEMVIEW_H
#define CTRLMEMVIEW_H

#include "Core/Debugger/DebugInterface.h"
#include <QWidget>

enum MemViewMode
{
	MV_NORMAL,
	MV_SYMBOLS,
	MV_MAX
};

class CtrlMemView : public QWidget
{
	Q_OBJECT
public:
	explicit CtrlMemView(QWidget *parent = 0);

	void setDebugger(DebugInterface *deb)
	{
		debugger=deb;
		if (debugger)
			align=debugger->getInstructionSize(0);
	}
	DebugInterface *getDebugger()
	{
		return debugger;
	}
	void redraw();

	void setMode(MemViewMode m)
	{
		mode=m;
		switch(mode) {
		case MV_NORMAL:
			alignMul=4;
			break;
		case MV_SYMBOLS:
			alignMul=1;
			break;
		default:
			break;
		}
		redraw();
	}

	void setAlign(int l)
	{
		align=l;
	}
	int yToAddress(int y);

	void gotoAddr(unsigned int addr)
	{
		curAddress=addr&(~(align-1));
		redraw();
	}

	unsigned int getSelection()
	{
		return curAddress;
	}
	void contextMenu(const QPoint &pos);
protected:
	void paintEvent(QPaintEvent *);
	void keyPressEvent(QKeyEvent *e);
	void wheelEvent(QWheelEvent *e);
	void mousePressEvent(QMouseEvent *e);
	
public slots:
	void CopyValue();
	void Dump();
	void Change();
private:
	int curAddress;
	int align;
	int alignMul;
	int rowHeight;

	int selection;
	int oldSelection;
	bool selectionChanged;
	bool selecting;
	bool hasFocus;

	DebugInterface *debugger;
	MemViewMode mode;
};

#endif // CTRLMEMVIEW_H
