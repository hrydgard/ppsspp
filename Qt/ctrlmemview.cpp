#include "ctrlmemview.h"

#include <QPainter>
#include <QKeyEvent>
#include <QMenu>
#include <QMessageBox>
#include <QApplication>
#include <QClipboard>
#include <QInputDialog>

#include "EmuThread.h"
#include "Core/MemMap.h"
#include "Core/Debugger/SymbolMap.h"

CtrlMemView::CtrlMemView(QWidget *parent) :
	QWidget(parent)
{
	curAddress=0;
	rowHeight=14;
	align=4;
	alignMul=4;
	selecting=false;
	mode=MV_NORMAL;
	debugger = 0;

	setMinimumWidth(500);
}

void CtrlMemView::redraw()
{
	update();
}

void CtrlMemView::wheelEvent(QWheelEvent* e)
{
	int numDegrees = e->delta() / 8;
	int numSteps = numDegrees / 15;
	if (e->orientation() == Qt::Vertical)
	{
		 curAddress -= numSteps*align*alignMul;
		 redraw();
	}
}

void CtrlMemView::keyPressEvent(QKeyEvent *e)
{
	int page=(rect().bottom()/rowHeight)/2-1;

	switch (e->key())
	{
	case Qt::Key_Up: curAddress -= align*alignMul; break;
	case Qt::Key_Down: curAddress += align*alignMul; break;
	case Qt::Key_PageUp: curAddress -= page*align*alignMul; break;
	case Qt::Key_PageDown: curAddress += page*align*alignMul; break;
	default: QWidget::keyPressEvent(e); break;
	}

	redraw();
}

void CtrlMemView::paintEvent(QPaintEvent *)
{
	QPainter painter(this);
	painter.setBrush(Qt::white);
	painter.setPen(Qt::white);
	painter.drawRect(rect());

	if (!debugger)
		return;

	int width = rect().width();
	int numRows=(rect().bottom()/rowHeight)/2+1;

	QPen nullPen(0xFFFFFF);
	QPen currentPen(0xFF000000);
	QPen selPen(0x808080);
	QBrush lbr(0xFFFFFF);
	QBrush nullBrush(0xFFFFFF);
	QBrush currentBrush(0xFFEFE8);
	QBrush pcBrush(0x70FF70);
	QPen textPen;

	QFont normalFont("Arial", 10);
	QFont alignedFont("Monospace", 10);
	painter.setFont(normalFont);

	int i;
	curAddress&=~(align-1);
	for (i=-numRows; i<=numRows; i++)
	{
		unsigned int address=curAddress + i*align*alignMul;

		int rowY1 = rect().bottom()/2 + rowHeight*i - rowHeight/2;
		int rowY2 = rect().bottom()/2 + rowHeight*i + rowHeight/2;

		char temp[256];

		painter.setBrush(currentBrush);

		if (selecting && address == (unsigned int)selection)
		  painter.setPen(selPen);
		else
		  painter.setPen(i==0 ? currentPen : nullPen);
		painter.drawRect(0, rowY1, 16-1, rowY2 - rowY1 - 1);

		painter.drawRect(16, rowY1, width - 16 -1, rowY2 - rowY1 - 1);
		painter.setBrush(nullBrush);
		textPen.setColor(0x600000);
		painter.setPen(textPen);
		painter.setFont(alignedFont);
		painter.drawText(17,rowY1-2+rowHeight, QString("%1").arg(address,8,16,QChar('0')));
		textPen.setColor(0xFF000000);
		painter.setPen(textPen);
		if (debugger->isAlive())
		{

			switch(mode) {
			case MV_NORMAL:
				{
					const char *m = "\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0";
					if (Memory::IsValidAddress(address))
					{
						EmuThread_LockDraw(true);
						u32 memory[4] = {
							debugger->readMemory(address),
							debugger->readMemory(address+4),
							debugger->readMemory(address+8),
							debugger->readMemory(address+12)
						};
						EmuThread_LockDraw(false);
						m = (const char*)memory;
						sprintf(temp, "%08x %08x %08x %08x  ................",
							memory[0],memory[1],memory[2],memory[3]);
					}
					for (int i=0; i<16; i++)
					{
						int c = (unsigned char)m[i];
						if (c>=32 && c<255)
							temp[i+37]=c;
					}
				}
				painter.setFont(alignedFont);
				painter.drawText(85,rowY1 - 2 + rowHeight, temp);
			break;

			case MV_SYMBOLS:
				{
					textPen.setColor(0x0000FF);
					painter.setPen(textPen);
					int fn = symbolMap.GetSymbolNum(address);
					if (fn==-1)
					{
						sprintf(temp, "%s (ns)", Memory::GetAddressName(address));
					}
					else
						sprintf(temp, "%s (0x%x b)", symbolMap.GetSymbolName(fn),symbolMap.GetSymbolSize(fn));
					painter.drawText(205,rowY1 - 2 + rowHeight, temp);

					textPen.setColor(0xFF0000000);
					painter.setPen(textPen);

					if (align==4)
					{
						u32 value = Memory::ReadUnchecked_U32(address);
						sprintf(temp, "%08x [%s]", value, symbolMap.GetSymbolName(symbolMap.GetSymbolNum(value)));
					}
					else if (align==2)
					{
						u16 value = Memory::ReadUnchecked_U16(address);
						sprintf(temp, "%04x [%s]", value, symbolMap.GetSymbolName(symbolMap.GetSymbolNum(value)));
					}

					painter.drawText(85,rowY1 - 2 + rowHeight, temp);
					break;
				}
			case MV_MAX: break;
			}
		}
	}
}

void CtrlMemView::mousePressEvent(QMouseEvent *e)
{
	int x = e->pos().x();
	int y = e->pos().y();
	if (x>16)
	{
		oldSelection=selection;
		selection=yToAddress(y);
		bool oldselecting=selecting;
		selecting=true;
		if (!oldselecting || (selection!=oldSelection))
			redraw();
	}
}

void CtrlMemView::contextMenu(const QPoint &pos)
{
	QMenu menu(this);

	QAction *gotoDisAsm = new QAction(tr("Go to in &disasm"), this);
	//connect(gotoDisAsm, SIGNAL(triggered()), this, SLOT(GotoDisAsm()));
	menu.addAction(gotoDisAsm);

	menu.addSeparator();

	QAction *copyValue = new QAction(tr("&Copy value"), this);
	connect(copyValue, SIGNAL(triggered()), this, SLOT(CopyValue()));
	menu.addAction(copyValue);

	QAction *changeValue = new QAction(tr("C&hange value"), this);
	connect(changeValue, SIGNAL(triggered()), this, SLOT(Change()));
	menu.addAction(changeValue);

	QAction *dump = new QAction(tr("Dump..."), this);
	connect(dump, SIGNAL(triggered()), this, SLOT(Dump()));
	menu.addAction(dump);

	menu.exec( mapToGlobal(pos));
}

void CtrlMemView::CopyValue()
{
	EmuThread_LockDraw(true);
	QApplication::clipboard()->setText(QString("%1").arg(Memory::ReadUnchecked_U32(selection),8,16,QChar('0')));
	EmuThread_LockDraw(false);
}

void CtrlMemView::Dump()
{
	QMessageBox::information(this,"Sorry","This feature has not been implemented.",QMessageBox::Ok);
}


void CtrlMemView::Change()
{
	EmuThread_LockDraw(true);
	QString curVal = QString("%1").arg(Memory::ReadUnchecked_U32(selection),8,16,QChar('0'));
	EmuThread_LockDraw(false);

	bool ok;
	QString text = QInputDialog::getText(this, tr("Set new value"),
								tr("Set new value:"), QLineEdit::Normal,
								curVal, &ok);
	if (ok && !text.isEmpty())
	{
		EmuThread_LockDraw(true);
		Memory::WriteUnchecked_U32(text.toUInt(0,16),selection);
		EmuThread_LockDraw(false);
		redraw();
	}
}


int CtrlMemView::yToAddress(int y)
{
	int ydiff=y-rect().bottom()/2-rowHeight/2;
	ydiff=(int)(floor((float)ydiff / (float)rowHeight))+1;
	return curAddress + ydiff * align*alignMul;
}

