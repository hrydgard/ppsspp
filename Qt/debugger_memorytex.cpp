#include "debugger_memorytex.h"
#include "gfx_es2/gl_state.h"
#include "gfx/gl_common.h"
#include "gfx/gl_lost_manager.h"
#include "ui_debugger_memorytex.h"
#include "Core/MemMap.h"
#include <QImage>
#include <QTimer>
#include "Core/HLE/sceDisplay.h"
#include "GPU/GPUInterface.h"
#include "EmuThread.h"
#include "base/display.h"

Debugger_MemoryTex::Debugger_MemoryTex(QWidget *parent) :
	QDialog(parent),
	ui(new Ui::Debugger_MemoryTex)
{
	ui->setupUi(this);
}

Debugger_MemoryTex::~Debugger_MemoryTex()
{
	delete ui;
}


void Debugger_MemoryTex::showEvent(QShowEvent *)
{

#ifdef Q_WS_X11
	// Hack to remove the X11 crash with threaded opengl when opening the first dialog
	EmuThread_LockDraw(true);
	QTimer::singleShot(100, this, SLOT(releaseLock()));
#endif
}

void Debugger_MemoryTex::releaseLock()
{
	EmuThread_LockDraw(false);
}


void Debugger_MemoryTex::ShowTex(const GPUgstate &state)
{
	ui->texaddr->setText(QString("%1").arg(state.texaddr[0] & 0xFFFFFF,8,16,QChar('0')));
	ui->texbufwidth0->setText(QString("%1").arg(state.texbufwidth[0] & 0xFFFFFF,8,16,QChar('0')));
	ui->texformat->setText(QString("%1").arg(state.texformat & 0xFFFFFF,8,16,QChar('0')));
	ui->texsize->setText(QString("%1").arg(state.texsize[0] & 0xFFFFFF,8,16,QChar('0')));
	ui->texmode->setText(QString("%1").arg(state.texmode & 0xFFFFFF,8,16,QChar('0')));
	ui->clutformat->setText(QString("%1").arg(state.clutformat & 0xFFFFFF,8,16,QChar('0')));
	ui->clutaddr->setText(QString("%1").arg(state.clutaddr & 0xFFFFFF,8,16,QChar('0')));
	ui->clutaddrupper->setText(QString("%1").arg(state.clutaddrupper & 0xFFFFFF,8,16,QChar('0')));
	ui->loadclut->setText(QString("%1").arg(state.loadclut & 0xFFFFFF,8,16,QChar('0')));
	on_readBtn_clicked();

	show();
}

void Debugger_MemoryTex::on_readBtn_clicked()
{
	EmuThread_LockDraw(true);

	GPUgstate state;
	state.texaddr[0] = ui->texaddr->text().toUInt(0,16);
	state.texbufwidth[0] = ui->texbufwidth0->text().toUInt(0,16);
	state.texformat = ui->texformat->text().toUInt(0,16);
	state.texsize[0] = ui->texsize->text().toUInt(0,16);
	state.texmode = ui->texmode->text().toUInt(0,16);
	state.clutformat = ui->clutformat->text().toUInt(0,16);
	state.clutaddr = ui->clutaddr->text().toUInt(0,16);
	state.clutaddrupper = ui->clutaddrupper->text().toUInt(0,16);
	state.loadclut = ui->loadclut->text().toUInt(0,16);
	int bufW = state.texbufwidth[0] & 0x3ff;
	int w = 1 << (state.texsize[0] & 0xf);
	int h = 1 << ((state.texsize[0]>>8) & 0xf);
	w = std::max(bufW,w);
	uchar* newData = new uchar[w*h*4];

	if(gpu->DecodeTexture(newData, state))
	{
		QImage img = QImage(newData, w, h, w*4, QImage::Format_ARGB32); // EmuThread_GrabBackBuffer();

		QPixmap pixmap = QPixmap::fromImage(img);
		ui->textureImg->setPixmap(pixmap);
		ui->textureImg->setMinimumWidth(pixmap.width());
		ui->textureImg->setMinimumHeight(pixmap.height());
		ui->textureImg->setMaximumWidth(pixmap.width());
		ui->textureImg->setMaximumHeight(pixmap.height());
	}

	delete[] newData;
	EmuThread_LockDraw(false);


}
