#include "debugger_vfpu.h"
#include "ui_debugger_vfpu.h"
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
