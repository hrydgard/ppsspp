#include <QMenu>
#include "debugger_disasm.h"
#include "ui_debugger_disasm.h"
#include "Core/CPU.h"
#include "Core/Debugger/DebugInterface.h"
#include "ctrldisasmview.h"
#include "Core/Debugger/Breakpoints.h"
#include "Core/HLE/HLE.h"
#include "mainwindow.h"
#include "ctrlregisterlist.h"
#include "native/base/stringutil.h"
#include "Core/Debugger/SymbolMap.h"

Debugger_Disasm::Debugger_Disasm(DebugInterface *_cpu, MainWindow* mainWindow_, QWidget *parent) :
	QDialog(parent),
	ui(new Ui::Debugger_Disasm),
	cpu(_cpu),
	mainWindow(mainWindow_)
{
	ui->setupUi(this);

	ui->DisasmView->setWindowTitle(_cpu->GetName());

	CtrlDisAsmView *ptr = ui->DisasmView;
	ptr->setDebugger(cpu);
	ptr->gotoAddr(0x00000000);

	CtrlRegisterList *rl = ui->RegList;
	rl->setCPU(cpu);

	//symbolMap.FillSymbolComboBox(GetDlgItem(m_hDlg, IDC_FUNCTIONLIST),ST_FUNCTION)

}

Debugger_Disasm::~Debugger_Disasm()
{
	delete ui;
}

void Debugger_Disasm::ShowVFPU()
{
	//vfpudlg->Show(true);
}

void Debugger_Disasm::FunctionList()
{

}

void Debugger_Disasm::GotoInt()
{

}

void Debugger_Disasm::Go()
{
	SetDebugMode(false);
	Core_EnableStepping(false);
	mainWindow->UpdateMenus();
}

void Debugger_Disasm::Step()
{
	CtrlDisAsmView *ptr = ui->DisasmView;
	CtrlRegisterList *reglist = ui->RegList;

	Core_DoSingleStep();
	//Sleep(1);
	sleep(1);
	_dbg_update_();
	ptr->gotoPC();
	reglist->redraw();
	//vfpudlg->Update();
}

void Debugger_Disasm::StepOver()
{
	CtrlDisAsmView *ptr = ui->DisasmView;
	CtrlRegisterList *reglist = ui->RegList;

	SetDebugMode(false);
	CBreakPoints::AddBreakPoint(cpu->GetPC()+cpu->getInstructionSize(0),true);
	_dbg_update_();
	Core_EnableStepping(false);
	mainWindow->UpdateMenus();
	//Sleep(1);
	sleep(1);
	ptr->gotoPC();
	reglist->redraw();
}

void Debugger_Disasm::StepHLE()
{
	hleDebugBreak();
	SetDebugMode(false);
	_dbg_update_();
	Core_EnableStepping(false);
	mainWindow->UpdateMenus();
}

void Debugger_Disasm::UpdateDialog()
{
	CtrlDisAsmView *ptr = ui->DisasmView;
	CtrlRegisterList *reglist = ui->RegList;

	ptr->setAlign(cpu->getInstructionSize(0));
	ptr->redraw();
	reglist->redraw();

	/*ui->callStack->clear();
	u32 pc = currentMIPS->pc;
	u32 ra = currentMIPS->r[MIPS_REG_RA];
	u32 addr = Memory::ReadUnchecked_U32(pc);
	int count=1;
	char addr_[12];
	sprintf(addr_, "0x%08x",pc);
	ui->callStack->addItem(new QListWidgetItem(addr_));

	u32 addr2 = Memory::ReadUnchecked_U32(ra);
	sprintf(addr_, "0x%08x",ra);
	ui->callStack->addItem(new QListWidgetItem(addr_));
	count++;

	while (addr != 0xFFFFFFFF && addr!=0 && count++<20)
	{
		u32 fun = Memory::ReadUnchecked_U32(addr+4);
		sprintf(addr_, "0x%08x",fun);
		ui->callStack->addItem(new QListWidgetItem(addr_));
		addr = Memory::ReadUnchecked_U32(addr);
	}*/

	/*
	for (int i=0; i<numCPUs; i++)
		if (memoryWindow[i])
			memoryWindow[i]->Update();
	*/
}

void Debugger_Disasm::Stop()
{
	CtrlDisAsmView *ptr = ui->DisasmView;
	CtrlRegisterList *reglist = ui->RegList;

	SetDebugMode(true);
	Core_EnableStepping(true);
	_dbg_update_();
	mainWindow->UpdateMenus();
	UpdateDialog();
	//Sleep(1); //let cpu catch up
	sleep(1);
	ptr->gotoPC();
	reglist->redraw();
	//vfpudlg->Update();
}

void Debugger_Disasm::Skip()
{
	CtrlDisAsmView *ptr = ui->DisasmView;

	cpu->SetPC(cpu->GetPC() + cpu->getInstructionSize(0));
	//Sleep(1);
	sleep(1);
	ptr->gotoPC();
	UpdateDialog();
}

void Debugger_Disasm::GotoPC()
{
	CtrlDisAsmView *ptr = ui->DisasmView;

	ptr->gotoPC();
	UpdateDialog();
}

void Debugger_Disasm::GotoLR()
{
	CtrlDisAsmView *ptr = ui->DisasmView;

	ptr->gotoAddr(cpu->GetLR());
}

void Debugger_Disasm::SetDebugMode(bool _bDebug)
{
	if (_bDebug)
	{
		ui->Go->setEnabled(true);
		ui->StepInto->setEnabled(true);
		ui->StepOver->setEnabled(true);
		ui->NextHLE->setEnabled(true);
		ui->Stop->setEnabled(false);
		ui->Skip->setEnabled(true);
		CtrlDisAsmView *ptr = ui->DisasmView;
		ptr->gotoPC();
	}
	else
	{
		ui->Go->setEnabled(false);
		ui->StepInto->setEnabled(false);
		ui->StepOver->setEnabled(false);
		ui->NextHLE->setEnabled(false);
		ui->Stop->setEnabled(true);
		ui->Skip->setEnabled(false);
	}
	CtrlRegisterList *reglist = ui->RegList;
	reglist->redraw();
}

void Debugger_Disasm::Goto(u32 addr)
{
	CtrlDisAsmView *ptr = ui->DisasmView;
	ptr->gotoAddr(addr);
	ptr->redraw();
}

void Debugger_Disasm::on_GotoPc_clicked()
{
	GotoPC();
}

void Debugger_Disasm::on_Go_clicked()
{
	Go();
}

void Debugger_Disasm::on_Stop_clicked()
{
	Stop();
}

void Debugger_Disasm::on_StepInto_clicked()
{
	Step();
}

void Debugger_Disasm::on_StepOver_clicked()
{
	StepOver();
}

void Debugger_Disasm::on_Skip_clicked()
{
	Skip();
}

void Debugger_Disasm::on_NextHLE_clicked()
{
	StepHLE();
}

void Debugger_Disasm::on_GotoLr_clicked()
{
	GotoLR();
}

void Debugger_Disasm::on_GotoInt_currentIndexChanged(int index)
{
	CtrlDisAsmView *ptr = ui->DisasmView;
	int addr = ui->GotoInt->itemData(index,Qt::UserRole).toInt();
	if (addr != 0xFFFFFFFF)
		ptr->gotoAddr(addr);
}

void Debugger_Disasm::on_Address_textChanged(const QString &arg1)
{
	CtrlDisAsmView *ptr = ui->DisasmView;
	ptr->gotoAddr(parseHex(ui->Address->text().toStdString().c_str()));
	UpdateDialog();
}

void Debugger_Disasm::on_DisasmView_customContextMenuRequested(const QPoint &pos)
{
	ui->DisasmView->contextMenu(pos);
}

void Debugger_Disasm::on_DisasmView_cellClicked(int row, int column)
{
	ui->DisasmView->click(row, column);
}

void Debugger_Disasm::NotifyMapLoaded()
{
	//symbolMap.FillSymbolListBox(GetDlgItem(m_hDlg, IDC_FUNCTIONLIST),ST_FUNCTION);
	CtrlDisAsmView *ptr = ui->DisasmView;
	ptr->redraw();
}
