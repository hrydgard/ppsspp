#include "ctrlmemview.h"

#include <QPainter>
#include <QKeyEvent>
#include <QMenu>
#include <QMessageBox>
#include <QApplication>
#include <QClipboard>

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
	if (e->orientation() == Qt::Horizontal) {
	 } else {
		 curAddress -= numSteps*align*alignMul;
		 e->accept();
		 redraw();
	 }
}


void CtrlMemView::keyPressEvent(QKeyEvent *e)
{
	int page=(rect().bottom()/rowHeight)/2-1;

	if(e->key() == Qt::Key_Up)
	{
		curAddress-=align*alignMul;
		e->accept();
	}
	else if(e->key() == Qt::Key_Down)
	{
		curAddress+=align*alignMul;
		e->accept();
	}
	else if(e->key() == Qt::Key_PageUp)
	{
		curAddress-=page*align*alignMul;
		e->accept();
	}
	else if(e->key() == Qt::Key_PageDown)
	{
		curAddress+=page*align*alignMul;
		e->accept();
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

	QPen nullPen=QPen(0xFFFFFF);
	QPen currentPen=QPen(0xFF000000);
	QPen selPen=QPen(0x808080);
	QBrush lbr = QBrush(0xFFFFFF);
	QBrush nullBrush=QBrush(0xFFFFFF);
	QBrush currentBrush=QBrush(0xFFEfE8);
	QBrush pcBrush=QBrush(0x70FF70);
	QPen textPen;

	QFont normalFont = QFont("Arial", 10);
	QFont alignedFont = QFont("Monospace", 10);
	painter.setFont(normalFont);

	int i;
	curAddress&=~(align-1);
	for (i=-numRows; i<=numRows; i++)
	{
		unsigned int address=curAddress + i*align*alignMul;

		int rowY1 = rect().bottom()/2 + rowHeight*i - rowHeight/2;
		int rowY2 = rect().bottom()/2 + rowHeight*i + rowHeight/2;

		char temp[256];
		sprintf(temp,"%08x",address);

		painter.setBrush(currentBrush);

		if (selecting && address == selection)
		  painter.setPen(selPen);
		else
		  painter.setPen(i==0 ? currentPen : nullPen);
		painter.drawRect(0, rowY1, 16-1, rowY2 - rowY1 - 1);

		painter.drawRect(16, rowY1, width - 16 -1, rowY2 - rowY1 - 1);
		painter.setBrush(nullBrush);
		textPen.setColor(0x600000);
		painter.setPen(textPen);
		painter.setFont(alignedFont);
		painter.drawText(17,rowY1-2+rowHeight, temp);
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

	QAction *dump = new QAction(tr("Dump..."), this);
	connect(dump, SIGNAL(triggered()), this, SLOT(Dump()));
	menu.addAction(dump);

	menu.exec( mapToGlobal(pos));
}

void CtrlMemView::CopyValue()
{
	char temp[24];
	sprintf(temp,"%08x",Memory::ReadUnchecked_U32(selection));
	QApplication::clipboard()->setText(temp);
}

void CtrlMemView::Dump()
{
	QMessageBox::information(this,"Sorry","This feature has not been implemented.",QMessageBox::Ok);
}

int CtrlMemView::yToAddress(int y)
{
	int ydiff=y-rect().bottom()/2-rowHeight/2;
	ydiff=(int)(floor((float)ydiff / (float)rowHeight))+1;
	return curAddress + ydiff * align*alignMul;
}

