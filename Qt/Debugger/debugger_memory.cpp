#include "debugger_memory.h"
#include "ui_debugger_memory.h"
#include "Core/Debugger/SymbolMap.h"
#include <QTimer>

Debugger_Memory::Debugger_Memory(DebugInterface *_cpu, MainWindow* mainWindow_, QWidget *parent) :
	QDialog(parent),
	ui(new Ui::Debugger_Memory),
	cpu(_cpu),
	mainWindow(mainWindow_)
{
	ui->setupUi(this);

	setWindowTitle(tr("Memory Viewer - %1").arg(cpu->GetName()));

	ui->memView->setDebugger(_cpu);
}

Debugger_Memory::~Debugger_Memory()
{
	delete ui;
}

void Debugger_Memory::Update()
{
	ui->memView->redraw();
}

void Debugger_Memory::Goto(u32 addr)
{
	show();
	ui->memView->gotoAddr(addr & ~3);
}

void Debugger_Memory::on_editAddress_textChanged(const QString &arg1)
{
	ui->memView->gotoAddr(arg1.toUInt(0,16) & ~3);
}

void Debugger_Memory::on_normalBtn_clicked()
{
	ui->memView->setMode(MV_NORMAL);
}

void Debugger_Memory::on_symbolsBtn_clicked()
{
	ui->memView->setMode(MV_SYMBOLS);
}

void Debugger_Memory::on_memView_customContextMenuRequested(const QPoint &pos)
{
	ui->memView->contextMenu(pos);
}

void Debugger_Memory::NotifyMapLoaded()
{
	QListWidgetItem* item = new QListWidgetItem();
	item->setText("(0x80000000)");
	item->setData(Qt::UserRole, 0x80000000);
	ui->symbols->addItem(item);

    std::vector<SymbolEntry> symbols = symbolMap.GetAllSymbols(ST_DATA);
    for(int i = 0; i < (int)symbols.size(); i++)
    {
        QListWidgetItem* item = new QListWidgetItem();
        item->setText(QString("%1 (%2)").arg(QString::fromStdString(symbols[i].name)).arg(symbols[i].size));
        item->setData(Qt::UserRole, symbols[i].address);
        ui->symbols->addItem(item);
    }

	ui->regions->clear();
	/*
	for (int i = 0; i < cpu->getMemMap()->numRegions; i++)
	{
		int n = ComboBox_AddString(lb,cpu->getMemMap()->regions[i].name);
		ComboBox_SetItemData(lb,n,cpu->getMemMap()->regions[i].start);
	}
	*/
}

void Debugger_Memory::on_regions_currentIndexChanged(int index)
{
	ui->memView->gotoAddr(ui->regions->itemData(index,Qt::UserRole).toInt());
}

void Debugger_Memory::on_symbols_itemClicked(QListWidgetItem *item)
{
	ui->memView->gotoAddr(item->data(Qt::UserRole).toInt());
}
