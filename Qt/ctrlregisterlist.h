#ifndef CTRLREGISTERLIST_H
#define CTRLREGISTERLIST_H

#include <QWidget>
#include "Core/Debugger/DebugInterface.h"

class Debugger_Disasm;
class CtrlRegisterList : public QWidget
{
	Q_OBJECT
public:
	explicit CtrlRegisterList(QWidget *parent = 0);


	void setParentWindow(Debugger_Disasm* win)
	{
		parentWindow = win;
	}

	void setCPU(DebugInterface *deb)
	{
		cpu = deb;

		int regs = cpu->GetNumRegsInCategory(0);
		lastCat0Values = new u32[regs];
		changedCat0Regs = new bool[regs];
		memset(lastCat0Values, 0, regs * sizeof(u32));
		memset(changedCat0Regs, 0, regs * sizeof(bool));
	}
	DebugInterface *getCPU()
	{
		return cpu;
	}
	~CtrlRegisterList();
	void contextMenu(const QPoint &pos);
protected:
	void paintEvent(QPaintEvent *);
	void keyPressEvent(QKeyEvent *e);
	void mousePressEvent(QMouseEvent *e);
	void wheelEvent(QWheelEvent *e);
signals:
	void GotoDisasm(u32);

public slots:
	void redraw();
	void scrollChanged(int);

	void GotoDisAsm();
	void CopyValue();
	void Change();
	void GotoMemory();

private:
	int yToIndex(int y);

	int rowHeight;
	int selection;
	int marker;
	int category;

	int oldSelection;

	bool selectionChanged;
	bool selecting;
	bool hasFocus;
	bool showHex;
	DebugInterface *cpu;
	int curVertOffset;

	u32 lastPC;
	u32 *lastCat0Values;
	bool *changedCat0Regs;

	Debugger_Disasm* parentWindow;
};

#endif // CTRLREGISTERLIST_H
