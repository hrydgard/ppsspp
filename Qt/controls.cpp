#include "controls.h"
#include "ui_controls.h"

struct Controls_
{
	char* command;
	char* key;
};

const Controls_ controllist[] = {
	{"Start","1"},
	{"Select","2"},
	{"Square","Z"},
	{"Triangle","A"},
	{"Circle","S"},
	{"Cross","X"},
	{"Left Trigger","Q"},
	{"Right Trigger","W"},
	{"Up","Arrow Up"},
	{"Down","Arrow Down"},
	{"Left","Arrow Left"},
	{"Right","Arrow Right"},
	{"Analog Up","I"},
	{"Analog Down","K"},
	{"Analog Left","J"},
	{"Analog Right","L"},
};

Controls::Controls(QWidget *parent) :
	QDialog(parent),
	ui(new Ui::Controls)
{
	ui->setupUi(this);

	int numRows = sizeof(controllist)/sizeof(Controls_);

	ui->listControls->setRowCount(numRows);

	for(int i = 0; i < numRows; i++)
	{
		QTableWidgetItem* item = new QTableWidgetItem();
		item->setText(controllist[i].command);
		ui->listControls->setItem(i,0,item);

		item = new QTableWidgetItem();
		item->setText(controllist[i].key);
		ui->listControls->setItem(i,1,item);
		ui->listControls->setRowHeight(i,15);
	}
}

Controls::~Controls()
{
	delete ui;
}
