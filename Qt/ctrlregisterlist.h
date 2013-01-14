#ifndef CTRLREGISTERLIST_H
#define CTRLREGISTERLIST_H

#include <QListWidget>
#include "Core/Debugger/DebugInterface.h"

class CtrlRegisterList : public QListWidget
{
	Q_OBJECT
public:
	explicit CtrlRegisterList(QWidget *parent = 0);

	void redraw();

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
signals:
	
public slots:
private:

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

	u32 lastPC;
	u32 *lastCat0Values;
	bool *changedCat0Regs;
};

#endif // CTRLREGISTERLIST_H
