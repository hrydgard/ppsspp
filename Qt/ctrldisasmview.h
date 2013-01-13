#ifndef CTRLDISASMVIEW_H
#define CTRLDISASMVIEW_H

#include <QTableWidget>
#include "Core/Debugger/DebugInterface.h"

class CtrlDisAsmView : public QTableWidget
{
	Q_OBJECT
public:
	explicit CtrlDisAsmView(QWidget *parent = 0);
	

	void redraw();
	void setAlign(int l)
	{
		align=l;
	}

	void setDebugger(DebugInterface *deb)
	{
		debugger=deb;
		curAddress=debugger->getPC();
		align=debugger->getInstructionSize(0);
	}
	DebugInterface *getDebugger()
	{
		return debugger;
	}
	void gotoAddr(unsigned int addr)
	{
		curAddress=addr&(~(align-1));
		redraw();
	}
	void gotoPC()
	{
		curAddress=debugger->getPC()&(~(align-1));
		redraw();
	}
	unsigned int getSelection()
	{
		return curAddress;
	}

	void setShowMode(bool s)
	{
		showHex=s;
	}

	void toggleBreakpoint()
	{
		debugger->toggleBreakpoint(curAddress);
		redraw();
	}

	void contextMenu(const QPoint& pos);
	void click(int row, int col);
signals:
	
public slots:

private:

	int curAddress;
	int align;

	int selection;
	int marker;
	int oldSelection;
	bool selectionChanged;
	bool selecting;
	bool hasFocus;
	bool showHex;

	DebugInterface *debugger;
	
};

#endif // CTRLDISASMVIEW_H
