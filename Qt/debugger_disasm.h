#ifndef DEBUGGER_DISASM_H
#define DEBUGGER_DISASM_H

#include "Core/HLE/sceKernelThread.h"
#include "Core/Debugger/DebugInterface.h"
#include "debugger_vfpu.h"
#include <QDialog>
#include <QListWidgetItem>
#include <QTreeWidgetItem>

class MainWindow;
namespace Ui {
class Debugger_Disasm;
}

class Debugger_Disasm : public QDialog
{
	Q_OBJECT
	
public:
	explicit Debugger_Disasm(DebugInterface *_cpu, MainWindow* mainWindow_, QWidget *parent = 0);
	~Debugger_Disasm();
	void SetDebugMode(bool _bDebug);

	void ShowVFPU();
	void Go();
	void Step();
	void StepOver();
	void StepHLE();
	void Stop();
	void Skip();
	void GotoPC();
	void GotoLR();
	void UpdateDialog();
	void NotifyMapLoaded();
	void Update();
	void ShowMemory(u32 addr);
	void FillFunctions();
	void UpdateBreakpoints();
	void UpdateThread();
	void UpdateDisplayList();
protected:
	void showEvent(QShowEvent *);

signals:
	void updateDisplayList_();
	void UpdateBreakpoints_();
	void UpdateThread_();

public slots:
	void Goto(u32 addr);
	void RemoveBreakpoint();
	void GotoThreadEntryPoint();

private slots:
	void UpdateDisplayListGUI();
	void UpdateBreakpointsGUI();
	void UpdateThreadGUI();

	void on_GotoPc_clicked();
	void on_Go_clicked();
	void on_Stop_clicked();
	void on_StepInto_clicked();
	void on_StepOver_clicked();
	void on_Skip_clicked();
	void on_NextHLE_clicked();
	void on_GotoLr_clicked();
	void on_GotoInt_currentIndexChanged(int index);
	void on_Address_textChanged(const QString &arg1);
	void on_DisasmView_customContextMenuRequested(const QPoint &pos);

	void on_RegList_customContextMenuRequested(const QPoint &pos);
	void on_vfpu_clicked();
	void on_FuncList_itemClicked(QListWidgetItem *item);
	void on_breakpointsList_itemClicked(QTreeWidgetItem *item, int column);
	void on_breakpointsList_customContextMenuRequested(const QPoint &pos);
	void on_clearAllBP_clicked();
	void on_threadList_itemClicked(QTreeWidgetItem *item, int column);
	void on_threadList_customContextMenuRequested(const QPoint &pos);

	void SetThreadStatusRun();
	void SetThreadStatusWait();
	void SetThreadStatusSuspend();
	void on_displayList_customContextMenuRequested(const QPoint &pos);
	void releaseLock();

private:
	void SetThreadStatus(ThreadStatus status);

	Ui::Debugger_Disasm *ui;
	DebugInterface* cpu;
	MainWindow* mainWindow;
	Debugger_VFPU* vfpudlg;
	u32 breakpointAddr;
	QTreeWidgetItem* threadRowSelected;
	QTreeWidgetItem* displayListRowSelected;
};

#endif // DEBUGGER_DISASM_H
