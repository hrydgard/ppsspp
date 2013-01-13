#ifndef DEBUGGER_DISASM_H
#define DEBUGGER_DISASM_H

#include "Core/Debugger/DebugInterface.h"
#include <QDialog>

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
	void Goto(u32 addr);

	void ShowVFPU();
	void FunctionList();
	void GotoInt();
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
private slots:
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

	void on_DisasmView_cellClicked(int row, int column);

private:
	Ui::Debugger_Disasm *ui;
	DebugInterface* cpu;
	MainWindow* mainWindow;
};

#endif // DEBUGGER_DISASM_H
