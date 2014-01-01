#include <QMenu>
#include <QTimer>

#include <deque>

#include "debugger_disasm.h"
#include "ui_debugger_disasm.h"
#include "Core/Debugger/DebugInterface.h"
#include "Core/Debugger/SymbolMap.h"
#include "ctrldisasmview.h"
#include "Core/Debugger/Breakpoints.h"
#include "Core/HLE/HLE.h"
#include "Core/CoreTiming.h"
#include "mainwindow.h"
#include "ctrlregisterlist.h"
#include "native/base/stringutil.h"
#include "Core/Debugger/SymbolMap.h"
#include "GPU/GPUState.h"
#include "GPU/GPUInterface.h"
#include "GPU/GeDisasm.h"
#include "GPU/Common/GPUDebugInterface.h"
#include "Core/Host.h"

Debugger_Disasm::Debugger_Disasm(DebugInterface *_cpu, MainWindow* mainWindow_, QWidget *parent) :
	QDialog(parent),
	ui(new Ui::Debugger_Disasm),
	cpu(_cpu),
	mainWindow(mainWindow_)
{
	ui->setupUi(this);

	vfpudlg = new Debugger_VFPU(_cpu, mainWindow, this);

	ui->DisasmView->setWindowTitle(_cpu->GetName());

	QObject::connect(ui->RegListScroll,SIGNAL(actionTriggered(int)), ui->RegList, SLOT(scrollChanged(int)));
	QObject::connect(ui->RegList,SIGNAL(GotoDisasm(u32)),this,SLOT(Goto(u32)));
	QObject::connect(this, SIGNAL(UpdateCallstack_()), this, SLOT(UpdateCallstackGUI()));
	QObject::connect(this, SIGNAL(UpdateDisplayList_()), this, SLOT(UpdateDisplayListGUI()));
	QObject::connect(this, SIGNAL(UpdateBreakpoints_()), this, SLOT(UpdateBreakpointsGUI()));
	QObject::connect(this, SIGNAL(UpdateThread_()), this, SLOT(UpdateThreadGUI()));

	CtrlDisAsmView *ptr = ui->DisasmView;
	ptr->setDebugger(cpu);
	ptr->setParentWindow(this);
	ptr->gotoAddr(0x00000000);

	CtrlRegisterList *rl = ui->RegList;
	rl->setParentWindow(this);
	rl->setCPU(cpu);

	FillFunctions();

}

Debugger_Disasm::~Debugger_Disasm()
{
	delete ui;
}

void Debugger_Disasm::ShowVFPU()
{
	vfpudlg->show();
}

void Debugger_Disasm::Update()
{
	ui->RegList->redraw();
	mainWindow->updateMenus();
	UpdateDialog();
}

void Debugger_Disasm::Go()
{
	SetDebugMode(false);
	Core_EnableStepping(false);
	mainWindow->updateMenus();
}

void Debugger_Disasm::Step()
{
	Core_DoSingleStep();
	_dbg_update_();
}

void Debugger_Disasm::StepOver()
{
	SetDebugMode(false);
	CBreakPoints::AddBreakPoint(cpu->GetPC()+cpu->getInstructionSize(0),true);
	_dbg_update_();
	Core_EnableStepping(false);
	mainWindow->updateMenus();
}

void Debugger_Disasm::StepHLE()
{
	hleDebugBreak();
	SetDebugMode(false);
	_dbg_update_();
	Core_EnableStepping(false);
	mainWindow->updateMenus();
}

void Debugger_Disasm::UpdateDialog()
{
	if(!isVisible())
		return;
	ui->DisasmView->setAlign(cpu->getInstructionSize(0));
	ui->DisasmView->redraw();
	ui->RegList->redraw();
	vfpudlg->Update();
	UpdateBreakpoints();
	UpdateThread();
	UpdateDisplayList();
	UpdateCallstack();

	char tempTicks[24];
	sprintf(tempTicks, "%lld", CoreTiming::GetTicks());
	ui->debugCount->setText(QString("Ctr : ") + tempTicks);

	if(mainWindow->GetDialogMemory())
		mainWindow->GetDialogMemory()->Update();

}

void Debugger_Disasm::Stop()
{
	SetDebugMode(true);
	Core_EnableStepping(true);
	_dbg_update_();
	mainWindow->updateMenus();
	UpdateDialog();
}

void Debugger_Disasm::Skip()
{
	CtrlDisAsmView *ptr = ui->DisasmView;

	cpu->SetPC(cpu->GetPC() + cpu->getInstructionSize(0));
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
		ui->StepOver->setEnabled(false); // Crash, so disable for now
		ui->NextHLE->setEnabled(true);
		ui->Stop->setEnabled(false);
		ui->Skip->setEnabled(true);
		CtrlDisAsmView *ptr = ui->DisasmView;
		ptr->gotoPC();
		UpdateDialog();
	}
	else
	{
		ui->Go->setEnabled(false);
		ui->StepInto->setEnabled(false);
		ui->StepOver->setEnabled(false);
		ui->NextHLE->setEnabled(false);
		ui->Stop->setEnabled(true);
		ui->Skip->setEnabled(false);
		CtrlRegisterList *reglist = ui->RegList;
		reglist->redraw();
	}
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
	u32 addr = ui->GotoInt->itemData(index,Qt::UserRole).toInt();
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

void Debugger_Disasm::NotifyMapLoaded()
{
	FillFunctions();
	CtrlDisAsmView *ptr = ui->DisasmView;
	ptr->redraw();
}

void Debugger_Disasm::on_RegList_customContextMenuRequested(const QPoint &pos)
{
	ui->RegList->contextMenu(pos);
}

void Debugger_Disasm::ShowMemory(u32 addr)
{
	mainWindow->ShowMemory(addr);
}

void Debugger_Disasm::on_vfpu_clicked()
{
	ShowVFPU();
}

void Debugger_Disasm::on_FuncList_itemClicked(QListWidgetItem *item)
{
	u32 addr = item->data(Qt::UserRole).toInt();

	ui->DisasmView->gotoAddr(addr);
}

void Debugger_Disasm::FillFunctions()
{
	QListWidgetItem* item = new QListWidgetItem();
	item->setText("(0x02000000)");
	item->setData(Qt::UserRole, 0x02000000);
	ui->FuncList->addItem(item);

    std::vector<SymbolEntry> symbols = symbolMap.GetAllSymbols(ST_FUNCTION);
    for(int i = 0; i < (int)symbols.size(); i++)
    {
        QListWidgetItem* item = new QListWidgetItem();
        item->setText(QString("%1 (%2)").arg(QString::fromStdString(symbols[i].name)).arg(symbols[i].size));
        item->setData(Qt::UserRole, symbols[i].address);
        ui->FuncList->addItem(item);
	}
}

void Debugger_Disasm::UpdateCallstack()
{
	emit UpdateCallstack_();
}

void Debugger_Disasm::UpdateCallstackGUI()
{

	auto threads = GetThreadsInfo();

	u32 entry = 0, stackTop = 0;
	for (size_t i = 0; i < threads.size(); i++)
	{
		if (threads[i].isCurrent)
		{
			entry = threads[i].entrypoint;
			stackTop = threads[i].initialStack;
			break;
		}
	}
	if (entry != 0) {
		stackTraceModel = MIPSStackWalk::Walk(
				cpu->GetPC(),
				cpu->GetRegValue(0,MIPS_REG_RA),
				cpu->GetRegValue(0,MIPS_REG_SP),
				entry,
				stackTop);
	} else {
		stackTraceModel.clear();
	}

	ui->callStack->clear();

	QTreeWidgetItem* item;
	for(auto it=stackTraceModel.begin();it!=stackTraceModel.end();it++)
	{
		item = new QTreeWidgetItem();
		item->setText(0,QString("%1").arg(it->pc,8,16,QChar('0')).prepend("0x"));
		item->setData(0,Qt::UserRole,it->pc);
		item->setText(1,QString("%1").arg(it->entry,8,16,QChar('0')).prepend("0x"));
		item->setData(1,Qt::UserRole,it->entry);
		item->setText(2,QString("%1").arg(it->sp,8,16,QChar('0')).prepend("0x"));
		item->setText(3,QString("%1").arg(it->stackSize,8,16,QChar('0')).prepend("0x"));
		ui->callStack->addTopLevelItem(item);
	}
}

void Debugger_Disasm::UpdateBreakpoints()
{
	emit UpdateBreakpoints_();
}

void Debugger_Disasm::UpdateBreakpointsGUI()
{
	u32 curBpAddr = 0;
	QTreeWidgetItem* curItem = ui->breakpointsList->currentItem();
	if(curItem)
		curBpAddr = ui->breakpointsList->currentItem()->data(0,Qt::UserRole).toInt();

	ui->breakpointsList->clear();

	auto breakpoints = CBreakPoints::GetBreakpoints();
	for(size_t i = 0; i < breakpoints.size(); i++)
	{
		u32 addr_ = breakpoints[i].addr;
		if(!CBreakPoints::IsTempBreakPoint(addr_))
		{
			QTreeWidgetItem* item = new QTreeWidgetItem();
			item->setText(0,QString("%1").arg(addr_,8,16,QChar('0')));
			item->setData(0,Qt::UserRole,addr_);
			ui->breakpointsList->addTopLevelItem(item);
			if(curBpAddr == addr_)
				ui->breakpointsList->setCurrentItem(item);
		}
	}
}

void Debugger_Disasm::on_breakpointsList_itemClicked(QTreeWidgetItem *item, int column)
{
	ui->DisasmView->gotoAddr(item->data(column,Qt::UserRole).toInt());
}

void Debugger_Disasm::on_breakpointsList_customContextMenuRequested(const QPoint &pos)
{
	QTreeWidgetItem* item = ui->breakpointsList->itemAt(pos);
	if(item)
	{
		breakpointAddr = item->data(0,Qt::UserRole).toInt();

		QMenu menu(this);

		QAction *removeBP = new QAction(tr("Remove breakpoint"), this);
		connect(removeBP, SIGNAL(triggered()), this, SLOT(RemoveBreakpoint()));
		menu.addAction(removeBP);

		menu.exec( ui->breakpointsList->mapToGlobal(pos));
	}
}

void Debugger_Disasm::RemoveBreakpoint()
{
	CBreakPoints::RemoveBreakPoint(breakpointAddr);
	Update();
}

void Debugger_Disasm::on_clearAllBP_clicked()
{
	CBreakPoints::ClearAllBreakPoints();
	Update();
}

void Debugger_Disasm::UpdateThread()
{
	emit UpdateThread_();
}

void Debugger_Disasm::UpdateThreadGUI()
{
	ui->threadList->clear();

	std::vector<DebugThreadInfo> threads = GetThreadsInfo();

	for(size_t i = 0; i < threads.size(); i++)
	{
		QTreeWidgetItem* item = new QTreeWidgetItem();
		item->setText(0,QString::number(threads[i].id));
		item->setData(0,Qt::UserRole,threads[i].id);
		item->setText(1,threads[i].name);
		QString status = "";
		if(threads[i].status & THREADSTATUS_RUNNING) status += "Running ";
		if(threads[i].status & THREADSTATUS_WAIT) status += "Wait ";
		if(threads[i].status & THREADSTATUS_READY) status += "Ready ";
		if(threads[i].status & THREADSTATUS_SUSPEND) status += "Suspend ";
		if(threads[i].status & THREADSTATUS_DORMANT) status += "Dormant ";
		if(threads[i].status & THREADSTATUS_DEAD) status += "Dead ";
		item->setText(2,status);
		item->setText(3,QString("%1").arg(threads[i].curPC,8,16,QChar('0')));
		item->setData(3,Qt::UserRole,threads[i].curPC);
		item->setText(4,QString("%1").arg(threads[i].entrypoint,8,16,QChar('0')));
		item->setData(4,Qt::UserRole,threads[i].entrypoint);

		if(threads[i].isCurrent)
		{
			for(int j = 0; j < 5; j++)
				item->setTextColor(j,Qt::green);
		}

		ui->threadList->addTopLevelItem(item);
	}
	for(int i = 0; i < ui->threadList->columnCount(); i++)
		ui->threadList->resizeColumnToContents(i);
}

void Debugger_Disasm::on_threadList_itemClicked(QTreeWidgetItem *item, int column)
{
	ui->DisasmView->gotoAddr(item->data(3,Qt::UserRole).toInt());
}

void Debugger_Disasm::on_threadList_customContextMenuRequested(const QPoint &pos)
{
	QTreeWidgetItem* item = ui->threadList->itemAt(pos);
	if(item)
	{
		threadRowSelected = item;

		QMenu menu(this);

		QAction *gotoEntryPoint = new QAction(tr("Go to entry point"), this);
		connect(gotoEntryPoint, SIGNAL(triggered()), this, SLOT(GotoThreadEntryPoint()));
		menu.addAction(gotoEntryPoint);

		QMenu* changeStatus = menu.addMenu(tr("Change status"));

		QAction *statusRunning = new QAction(tr("Running"), this);
		connect(statusRunning, SIGNAL(triggered()), this, SLOT(SetThreadStatusRun()));
		changeStatus->addAction(statusRunning);

		QAction *statusWait = new QAction(tr("Wait"), this);
		connect(statusWait, SIGNAL(triggered()), this, SLOT(SetThreadStatusWait()));
		changeStatus->addAction(statusWait);

		QAction *statusSuspend = new QAction(tr("Suspend"), this);
		connect(statusSuspend, SIGNAL(triggered()), this, SLOT(SetThreadStatusSuspend()));
		changeStatus->addAction(statusSuspend);

		menu.exec( ui->threadList->mapToGlobal(pos));
	}
}

void Debugger_Disasm::GotoThreadEntryPoint()
{
	ui->DisasmView->gotoAddr(threadRowSelected->data(4,Qt::UserRole).toInt());
	Update();
}

void Debugger_Disasm::SetThreadStatus(ThreadStatus status)
{
	__KernelChangeThreadState(threadRowSelected->data(0,Qt::UserRole).toInt(), status);

	UpdateThread();
}

void Debugger_Disasm::SetThreadStatusRun()
{
	SetThreadStatus(THREADSTATUS_RUNNING);
}

void Debugger_Disasm::SetThreadStatusWait()
{
	SetThreadStatus(THREADSTATUS_WAIT);
}

void Debugger_Disasm::SetThreadStatusSuspend()
{
	SetThreadStatus(THREADSTATUS_SUSPEND);
}

void Debugger_Disasm::UpdateDisplayList()
{
	emit UpdateDisplayList_();
}

void Debugger_Disasm::UpdateDisplayListGUI()
{
	u32 curDlId = 0;
	QTreeWidgetItem* curItem = ui->displayList->currentItem();
	if(curItem)
		curDlId = ui->displayList->currentItem()->data(0,Qt::UserRole).toInt();

	ui->displayList->clear();

	const std::list<int>& dlQueue = gpu->GetDisplayLists();

	DisplayList dlist;
	DisplayList* dl = &dlist;
	if(gpuDebug->GetCurrentDisplayList(dlist))
	{
		QTreeWidgetItem* item = new QTreeWidgetItem();
		item->setText(0,QString::number(dl->id));
		item->setData(0, Qt::UserRole, dl->id);
		switch(dl->state)
		{
		case PSP_GE_DL_STATE_NONE: item->setText(1,"None"); break;
		case PSP_GE_DL_STATE_QUEUED: item->setText(1,"Queued"); break;
		case PSP_GE_DL_STATE_RUNNING: item->setText(1,"Running"); break;
		case PSP_GE_DL_STATE_COMPLETED: item->setText(1,"Completed"); break;
		case PSP_GE_DL_STATE_PAUSED: item->setText(1,"Paused"); break;
		default: break;
		}
		item->setText(2,QString("%1").arg(dl->startpc,8,16,QChar('0')));
		item->setData(2, Qt::UserRole, dl->startpc);
		item->setText(3,QString("%1").arg(dl->pc,8,16,QChar('0')));
		item->setData(3, Qt::UserRole, dl->pc);
		ui->displayList->addTopLevelItem(item);
		if(curDlId == (u32)dl->id)
		{
			ui->displayList->setCurrentItem(item);
			displayListRowSelected = item;
		}
	}

	for(auto listIdIt = dlQueue.begin(); listIdIt != dlQueue.end(); ++listIdIt)
	{
		DisplayList *it = gpu->getList(*listIdIt);
		if(dl && it->id == dl->id)
			continue;
		QTreeWidgetItem* item = new QTreeWidgetItem();
		item->setText(0,QString::number(it->id));
		item->setData(0, Qt::UserRole, it->id);
		switch(it->state)
		{
		case PSP_GE_DL_STATE_NONE: item->setText(1,"None"); break;
		case PSP_GE_DL_STATE_QUEUED: item->setText(1,"Queued"); break;
		case PSP_GE_DL_STATE_RUNNING: item->setText(1,"Running"); break;
		case PSP_GE_DL_STATE_COMPLETED: item->setText(1,"Completed"); break;
		case PSP_GE_DL_STATE_PAUSED: item->setText(1,"Paused"); break;
		default: break;
		}
		item->setText(2,QString("%1").arg(it->startpc,8,16,QChar('0')));
		item->setData(2, Qt::UserRole, it->startpc);
		item->setText(3,QString("%1").arg(it->pc,8,16,QChar('0')));
		item->setData(3, Qt::UserRole, it->pc);
		ui->displayList->addTopLevelItem(item);
		if(curDlId == (u32)it->id)
		{
			ui->displayList->setCurrentItem(item);
			displayListRowSelected = item;
		}
	}
	for(int i = 0; i < ui->displayList->columnCount(); i++)
		ui->displayList->resizeColumnToContents(i);
}

void Debugger_Disasm::on_displayList_customContextMenuRequested(const QPoint &pos)
{
	QTreeWidgetItem* item = ui->displayList->itemAt(pos);
	if(item)
	{
		displayListRowSelected = item;

		/*QMenu menu(this);

		QAction *showCode = new QAction(tr("Show code"), this);
		connect(showCode, SIGNAL(triggered()), this, SLOT(ShowDLCode()));
		menu.addAction(showCode);*/

		//menu.exec( ui->displayList->mapToGlobal(pos));
	}
}
