#include "ctrldisasmview.h"
#include <QPainter>
#include <QKeyEvent>
#include <QClipboard>
#include <QMenu>
#include <QAction>
#include <QApplication>
#include <QInputDialog>
#include <QMessageBox>

#include "debugger_disasm.h"
#include "Core/Debugger/SymbolMap.h"

namespace
{
	u32 halfAndHalf(u32 a, u32 b)
	{
		return ((a>>1)&0x7f7f7f7f) + ((b>>1)&0x7f7f7f7f);
	}
}

CtrlDisAsmView::CtrlDisAsmView(QWidget *parent) :
	QWidget(parent)
{
	curAddress=0;
	rowHeight=14;
	align=2;
	selecting=false;
	showHex=false;
}

void CtrlDisAsmView::keyPressEvent(QKeyEvent *e)
{
	int page=(rect().bottom()/rowHeight)/2-1;

	switch (e->key())
	{
	case Qt::Key_Down: curAddress += align; break;
	case Qt::Key_Up: curAddress -= align; break;
	case Qt::Key_PageDown: curAddress += page*align; break;
	case Qt::Key_PageUp: curAddress -= page*align; break;
	default: QWidget::keyPressEvent(e); break;
	}

	update();
}

void CtrlDisAsmView::mousePressEvent(QMouseEvent *e)
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
	else
	{
		EmuThread_LockDraw(true);
		debugger->toggleBreakpoint(yToAddress(y));
		EmuThread_LockDraw(false);
		parentWindow->Update();
		redraw();
	}
}

void CtrlDisAsmView::wheelEvent(QWheelEvent* e)
{
	int numDegrees = e->delta() / 8;
	int numSteps = numDegrees / 15;
	if (e->orientation() == Qt::Vertical)
	{
		 curAddress -= numSteps*align;
		 update();
	}
}

void CtrlDisAsmView::redraw()
{
	update();
}

void CtrlDisAsmView::contextMenu(const QPoint &pos)
{
	QMenu menu(this);

	QAction *copyAddress = new QAction(tr("Copy &address"), this);
	connect(copyAddress, SIGNAL(triggered()), this, SLOT(CopyAddress()));
	menu.addAction(copyAddress);

	QAction *copyInstrHex = new QAction(tr("Copy instruction (&hex)"), this);
	connect(copyInstrHex, SIGNAL(triggered()), this, SLOT(CopyInstrHex()));
	menu.addAction(copyInstrHex);

	QAction *copyInstrDisAsm = new QAction(tr("Copy instruction (&disasm)"), this);
	connect(copyInstrDisAsm, SIGNAL(triggered()), this, SLOT(CopyInstrDisAsm()));
	menu.addAction(copyInstrDisAsm);

	menu.addSeparator();

	QAction *runToHere = new QAction(tr("&Run to here"), this);
	connect(runToHere, SIGNAL(triggered()), this, SLOT(RunToHere()));
	menu.addAction(runToHere);

	QAction *setNextStatement = new QAction(tr("&Set Next Statement"), this);
	connect(setNextStatement, SIGNAL(triggered()), this, SLOT(SetNextStatement()));
	menu.addAction(setNextStatement);

	QAction *toggleBreakpoint = new QAction(tr("&Toggle breakpoint"), this);
	connect(toggleBreakpoint, SIGNAL(triggered()), this, SLOT(ToggleBreakpoint()));
	menu.addAction(toggleBreakpoint);

	QAction *followBranch = new QAction(tr("&Follow branch"), this);
	connect(followBranch, SIGNAL(triggered()), this, SLOT(FollowBranch()));
	menu.addAction(followBranch);

	menu.addSeparator();

	//QAction *showDynarecResults = new QAction(tr("&Show Dynarec Results"), this);
	//connect(showDynarecResults, SIGNAL(triggered()), this, SLOT(ShowDynarecResults()));
	//menu.addAction(showDynarecResults);

	QAction *goToMemoryView = new QAction(tr("Go to in &Memory View"), this);
	connect(goToMemoryView, SIGNAL(triggered()), this, SLOT(GoToMemoryView()));
	menu.addAction(goToMemoryView);

	menu.addSeparator();

	//QAction *killFunction = new QAction(tr("&Kill function"), this);
	//connect(killFunction, SIGNAL(triggered()), this, SLOT(KillFunction()));
	//menu.addAction(killFunction);

	QAction *renameFunction = new QAction(tr("&Rename function..."), this);
	connect(renameFunction, SIGNAL(triggered()), this, SLOT(RenameFunction()));
	menu.addAction(renameFunction);


	menu.exec( mapToGlobal(pos));
}

void CtrlDisAsmView::GoToMemoryView()
{
	parentWindow->ShowMemory(selection);
}

void CtrlDisAsmView::CopyAddress()
{
	QApplication::clipboard()->setText(QString("%1").arg(selection,8,16,QChar('0')));
}

void CtrlDisAsmView::CopyInstrDisAsm()
{
	EmuThread_LockDraw(true);
	QApplication::clipboard()->setText(debugger->disasm(selection,align));
	EmuThread_LockDraw(false);
}

void CtrlDisAsmView::CopyInstrHex()
{
	EmuThread_LockDraw(true);
	QApplication::clipboard()->setText(QString("%1").arg(debugger->readMemory(selection),8,16,QChar('0')));
	EmuThread_LockDraw(false);
}

void CtrlDisAsmView::SetNextStatement()
{
	EmuThread_LockDraw(true);
	debugger->setPC(selection);
	EmuThread_LockDraw(false);
	redraw();
}

void CtrlDisAsmView::ToggleBreakpoint()
{
	EmuThread_LockDraw(true);
	debugger->toggleBreakpoint(selection);
	EmuThread_LockDraw(false);
	parentWindow->Update();
	redraw();
}

void CtrlDisAsmView::FollowBranch()
{
	EmuThread_LockDraw(true);
	const char *temp = debugger->disasm(selection,align);
	EmuThread_LockDraw(false);
	const char *mojs=strstr(temp,"->$");
	if (mojs)
	{
		u32 dest;
		sscanf(mojs+3,"%08x",&dest);
		if (dest)
		{
			marker = selection;
			gotoAddr(dest);
		}
	}
}

void CtrlDisAsmView::RunToHere()
{
	EmuThread_LockDraw(true);
	debugger->setBreakpoint(selection);
	debugger->runToBreakpoint();
	EmuThread_LockDraw(false);
	redraw();
}

void CtrlDisAsmView::RenameFunction()
{
	int sym = symbolMap.GetSymbolNum(selection);
	if (sym != -1)
	{
		QString name = symbolMap.GetSymbolName(sym);
		bool ok;
		QString newname = QInputDialog::getText(this, tr("New function name"),
									tr("New function name:"), QLineEdit::Normal,
									name, &ok);
		if (ok && !newname.isEmpty())
		{
			symbolMap.SetSymbolName(sym,newname.toStdString().c_str());
			redraw();
			parentWindow->NotifyMapLoaded();
		}
	}
	else
	{
		QMessageBox::information(this,tr("Warning"),tr("No symbol selected"),QMessageBox::Ok);
	}
}

void CtrlDisAsmView::paintEvent(QPaintEvent *)
{
	QPainter painter(this);
	painter.setBrush(Qt::white);
	painter.setPen(Qt::white);
	painter.drawRect(rect());

	struct branch
	{
		int src,dst,srcAddr;
		bool conditional;
	};
	branch branches[256];
	int numBranches=0;

	int width = rect().width();
	int numRows=(rect().height()/rowHeight)/2+1;

	QColor bgColor(0xFFFFFFFF);
	QPen nullPen(bgColor);
	QPen currentPen(QColor(0,0,0));
	QPen selPen(QColor(0xFF808080));
	QPen condPen(QColor(0xFFFF3020));

	QBrush lbr;
	lbr.setColor(bgColor);
	QBrush currentBrush(QColor(0xFFFFEfE8));
	QBrush pcBrush(QColor(0xFF70FF70));

	QFont normalFont("Arial", 10);
	QFont boldFont("Arial", 10);
	QFont alignedFont("Monospace", 10);
	boldFont.setBold(true);
	painter.setFont(normalFont);


	QImage breakPoint(":/images/breakpoint");
	int i;
	curAddress&=~(align-1);

	align=(debugger->getInstructionSize(0));
	for (i=-numRows; i<=numRows; i++)
	{
		unsigned int address=curAddress + i*align;

		int rowY1 = rect().bottom()/2 + rowHeight*i - rowHeight/2;
		int rowY2 = rect().bottom()/2 + rowHeight*i + rowHeight/2 - 1;

		lbr.setColor((unsigned int)marker == address ? QColor(0xFFFFEEE0) : QColor(debugger->getColor(address)));
		QColor bg = lbr.color();
		painter.setPen(nullPen);
		painter.drawRect(0,rowY1,16-1,rowY2-rowY1);

		if (selecting && address == (unsigned int)selection)
			painter.setPen(selPen);
		else
			painter.setPen(i==0 ? currentPen : nullPen);

		QBrush mojsBrush(lbr.color());
		painter.setBrush(mojsBrush);

		if (address == debugger->getPC())
		{
			painter.setBrush(pcBrush);
		}

		painter.drawRect(16,rowY1,width-16-1,rowY2-rowY1);
		painter.setBrush(currentBrush);
		QPen textPen(QColor(halfAndHalf(bg.rgba(),0)));
		painter.setPen(textPen);
		painter.setFont(alignedFont);
		painter.drawText(17,rowY1-3+rowHeight,QString("%1").arg(address,8,16,QChar('0')));
		painter.setFont(normalFont);
		textPen.setColor(QColor(0xFF000000));
		painter.setPen(textPen);
		if (debugger->isAlive())
		{
			const char *dizz = debugger->disasm(address, align);
			char dis[512];
			strcpy(dis, dizz);
			char *dis2 = strchr(dis,'\t');
			char desc[256]="";
			if (dis2)
			{
				*dis2=0;
				dis2++;
				const char *mojs=strstr(dis2,"->$");
				if (mojs)
				{
					for (int i=0; i<8; i++)
					{
						bool found=false;
						for (int j=0; j<22; j++)
						{
							if (mojs[i+3]=="0123456789ABCDEFabcdef"[j])
								found=true;
						}
						if (!found)
						{
							mojs=0;
							break;
						}
					}
				}
				if (mojs)
				{
					int offs;
					sscanf(mojs+3,"%08x",&offs);
					branches[numBranches].src=rowY1 + rowHeight/2;
					branches[numBranches].srcAddr=address/align;
					branches[numBranches].dst=(int)(rowY1+((s64)offs-(s64)address)*rowHeight/align + rowHeight/2);
					branches[numBranches].conditional = (dis[1]!=0); //unconditional 'b' branch
					numBranches++;
					const char *t = debugger->getDescription(offs);
					if (memcmp(t,"z_",2)==0)
						t+=2;
					if (memcmp(t,"zz_",3)==0)
						t+=3;
					sprintf(desc,"-->%s", t);
					textPen.setColor(QColor(0xFF600060));
					painter.setPen(textPen);
				}
				else
				{
					textPen.setColor(QColor(0xFF000000));
					painter.setPen(textPen);
				}
				painter.drawText(149,rowY1-3+rowHeight,QString(dis2));
			}
			textPen.setColor(QColor(0xFF007000));
			painter.setPen(textPen);
			painter.setFont(boldFont);
			painter.drawText(84,rowY1-3+rowHeight,QString(dis));
			painter.setFont(normalFont);
			if (desc[0]==0)
			{
				const char *t = debugger->getDescription(address);
				if (memcmp(t,"z_",2)==0)
					t+=2;
				if (memcmp(t,"zz_",3)==0)
					t+=3;
				strcpy(desc,t);
			}
			if (memcmp(desc,"-->",3) == 0)
			{
				textPen.setColor(QColor(0xFF0000FF));
				painter.setPen(textPen);
			}
			else
			{
				textPen.setColor(halfAndHalf(halfAndHalf(bg.rgba(),0),bg.rgba()));
				painter.setPen(textPen);
			}
			if (strlen(desc))
				painter.drawText(std::max(280,width/3+190),rowY1-3+rowHeight,QString(desc));
			if (debugger->isBreakpoint(address))
			{
				painter.drawImage(2,rowY1+2,breakPoint);
			}
		}
	}
	for (i=0; i<numBranches; i++)
	{
		painter.setPen(branches[i].conditional ? condPen : currentPen);
		int x=280+(branches[i].srcAddr%9)*8;
		QPoint curPos(x-2,branches[i].src);

		if (branches[i].dst<rect().bottom()+200 && branches[i].dst>-200)
		{
			painter.drawLine(curPos, QPoint(x+2,branches[i].src));
			curPos = QPoint(x+2,branches[i].src);
			painter.drawLine(curPos, QPoint(x+2,branches[i].dst));
			curPos = QPoint(x+2,branches[i].dst);
			painter.drawLine(curPos, QPoint(x-4,branches[i].dst));

			curPos = QPoint(x,branches[i].dst-4);
			painter.drawLine(curPos, QPoint(x-4,branches[i].dst));
			curPos = QPoint(x-4,branches[i].dst);
			painter.drawLine(curPos, QPoint(x+1,branches[i].dst+5));
		}
		else
		{
			painter.drawLine(curPos, QPoint(x+4,branches[i].src));
		}
	}
}

int CtrlDisAsmView::yToAddress(int y)
{
	int ydiff=y-rect().bottom()/2-rowHeight/2;
	ydiff=(int)(floor((float)ydiff / (float)rowHeight))+1;
	return curAddress + ydiff * align;
}
