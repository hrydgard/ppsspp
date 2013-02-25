#include "debugger_vfpu.h"
#include "ui_debugger_vfpu.h"
#include "EmuThread.h"
#include "mainwindow.h"
#include <QTimer>

Debugger_VFPU::Debugger_VFPU(DebugInterface *_cpu, MainWindow* mainWindow_, QWidget *parent) :
	QDialog(parent),
	ui(new Ui::Debugger_VFPU),
	cpu(_cpu),
	mainWindow(mainWindow_)
{
	ui->setupUi(this);

	setWindowTitle(QString("VFPU - %1").arg(cpu->GetName()));

	ui->vfpu->setCPU(_cpu);
}

Debugger_VFPU::~Debugger_VFPU()
{
	delete ui;
}


void Debugger_VFPU::showEvent(QShowEvent *)
{

#ifdef Q_WS_X11
	// Hack to remove the X11 crash with threaded opengl when opening the first dialog
	EmuThread_LockDraw(true);
	QTimer::singleShot(100, this, SLOT(releaseLock()));
#endif
}

void Debugger_VFPU::releaseLock()
{
	EmuThread_LockDraw(false);
}

void Debugger_VFPU::Update()
{
	ui->vfpu->redraw();
}

void Debugger_VFPU::Goto(u32 addr)
{
	show();
	mainWindow->GetDialogMemory()->Goto(addr & ~3);
}

void Debugger_VFPU::on_comboBox_currentIndexChanged(int index)
{
	ui->vfpu->setMode(index);
}
