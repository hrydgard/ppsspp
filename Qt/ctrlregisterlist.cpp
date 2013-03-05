#include "ctrlregisterlist.h"
#include <QPainter>
#include <QFont>
#include <QKeyEvent>
#include <QScrollBar>
#include <QMenu>
#include <QApplication>
#include <QClipboard>
#include <QInputDialog>
#include "EmuThread.h"
#include "debugger_disasm.h"

CtrlRegisterList::CtrlRegisterList(QWidget *parent) :
	QWidget(parent)
{
	rowHeight = 14;
	selecting=false;
	selection=0;
	category=0;
	showHex=false;
	cpu=0;
	lastPC = 0;
	lastCat0Values = NULL;
	changedCat0Regs = NULL;
	curVertOffset = 0;

}

CtrlRegisterList::~CtrlRegisterList()
{
	if (lastCat0Values != NULL)
		delete [] lastCat0Values;
	if (changedCat0Regs != NULL)
		delete [] changedCat0Regs;
}

void CtrlRegisterList::redraw()
{
	update();
}

void CtrlRegisterList::scrollChanged(int action)
{
	QScrollBar *bar = findChild<QScrollBar*>("RegListScroll");
	switch(action)
	{
	case QScrollBar::SliderSingleStepAdd:
		curVertOffset++;
		break;
	case QScrollBar::SliderSingleStepSub:
		curVertOffset--;
		break;
	case QScrollBar::SliderPageStepAdd:
		curVertOffset+= 4;
		break;
	case QScrollBar::SliderPageStepSub:
		curVertOffset-= 4;
		break;
	case QScrollBar::SliderMove:
		curVertOffset =	bar->sliderPosition();
		break;
	default:
		break;
	}
	redraw();
}


void CtrlRegisterList::keyPressEvent(QKeyEvent *e)
{
	switch (e->key())
	{
	case Qt::Key_Down: selection += 1; break;
	case Qt::Key_Up: selection -= 1; break;
	case Qt::Key_PageDown: selection += 4; break;
	case Qt::Key_PageUp: selection -= 4; break;
	default: QWidget::keyPressEvent(e); break;
	}

	int maxRowsDisplay =rect().bottom()/rowHeight - 1;
	curVertOffset = std::min(std::max(curVertOffset, selection-maxRowsDisplay),selection);
	update();
}

void CtrlRegisterList::mousePressEvent(QMouseEvent *e)
{
	int x = e->pos().x();
	int y = e->pos().y();
	if (x>16)
	{
		oldSelection=selection;

		if (y>rowHeight)
		{
			selection=yToIndex(y);
			bool oldselecting=selecting;
			selecting=true;
			if (!oldselecting || (selection!=oldSelection))
				redraw();
		}
		else
		{
			int lastCat = category;
			category = (x*cpu->GetNumCategories())/(rect().width());
			if (category<0) category=0;
			if (category>=cpu->GetNumCategories())
				category=cpu->GetNumCategories()-1;
			if (category!=lastCat)
			{
				curVertOffset = 0;
				redraw();
			}
		}
	}
	else
	{
		redraw();
	}
}

void CtrlRegisterList::wheelEvent(QWheelEvent* e)
{
	int numDegrees = e->delta() / 8;
	int numSteps = numDegrees / 15;
	if (e->orientation() == Qt::Vertical)
	{
		 curVertOffset -= numSteps;
		 update();
	}
}


void CtrlRegisterList::paintEvent(QPaintEvent *)
{

	int numRowsTotal = cpu->GetNumRegsInCategory(category);
	int maxRowsDisplay =rect().bottom()/rowHeight - 1;

	selection = std::min(std::max(selection,0),numRowsTotal);
	curVertOffset = std::min(std::max(curVertOffset, 0),numRowsTotal - maxRowsDisplay);

	QScrollBar *bar = findChild<QScrollBar*>("RegListScroll");
	if(bar)
	{
		bar->setMinimum(0);
		bar->setMaximum(numRowsTotal - maxRowsDisplay);
		bar->setPageStep(1);
		bar->setValue(curVertOffset);
	}


	QPainter painter(this);
	painter.setBrush(Qt::white);
	painter.setPen(Qt::white);
	painter.drawRect(rect());

	if (!cpu)
		return;

	QFont normalFont = QFont("Arial", 10);
	painter.setFont(normalFont);

	int width = rect().width();

	QColor bgColor(0xffffff);
	QPen nullPen(bgColor);
	QPen currentPen(QColor(0xFF000000));
	QPen selPen(0x808080);
	QPen textPen;

	QBrush lbr;
	lbr.setColor(bgColor);
	QBrush nullBrush(bgColor);
	QBrush currentBrush(0xFFEfE8);
	QBrush pcBrush(0x70FF70);

	int nc = cpu->GetNumCategories();
	for (int i=0; i<nc; i++)
	{
		painter.setPen(i==category?currentPen:nullPen);
		painter.setBrush(i==category?pcBrush:nullBrush);
		painter.drawRect(width*i/nc,0,width*(i+1)/nc - width*i/nc -1,rowHeight-1);
		QString name = cpu->GetCategoryName(i);
		painter.setPen(currentPen);
		painter.drawText(width*i/nc+1,-3+rowHeight,name);
	}

	int numRows=rect().bottom()/rowHeight;

	for (int i=curVertOffset; i<curVertOffset+numRows; i++)
	{
		int rowY1 = rowHeight*(i-curVertOffset+1);
		int rowY2 = rowHeight*(i-curVertOffset+2)-1;

		lbr.setColor(i==selection?0xffeee0:0xffffff);

		painter.setBrush(currentBrush);
		painter.setPen(nullPen);
		painter.drawRect(0,rowY1,16-1,rowY2-rowY1);

		if (selecting && i == selection)
			painter.setPen(selPen);
		else
			painter.setPen(nullPen);

		QBrush mojsBrush(lbr.color());
		painter.setBrush(mojsBrush);

		painter.drawRect(16,rowY1,width-16-1,rowY2-rowY1);

		// Check for any changes in the registers.
		if (lastPC != cpu->GetPC())
		{
			for (int i = 0, n = cpu->GetNumRegsInCategory(0); i < n; ++i)
			{
				u32 v = cpu->GetRegValue(0, i);
				changedCat0Regs[i] = v != lastCat0Values[i];
				lastCat0Values[i] = v;
			}
			lastPC = cpu->GetPC();
		}

		painter.setBrush(currentBrush);
		if (i<cpu->GetNumRegsInCategory(category))
		{
			QString regName = cpu->GetRegName(category,i);
			textPen.setColor(0x600000);
			painter.setPen(textPen);
			painter.drawText(17,rowY1-3+rowHeight,regName);
			textPen.setColor(0xFF000000);
			painter.setPen(textPen);

			char temp[256];
			cpu->PrintRegValue(category,i,temp);
			if (category == 0 && changedCat0Regs[i])
			{
				textPen.setColor(0x0000FF);
				painter.setPen(textPen);
			}
			else
			{
				textPen.setColor(0x004000);
				painter.setPen(textPen);
			}
			painter.drawText(77,rowY1-3+rowHeight,temp);
		}
	}
}

int CtrlRegisterList::yToIndex(int y)
{
	int n = (y/rowHeight) - 1 + curVertOffset;
	if (n<0) n=0;
	return n;
}

void CtrlRegisterList::contextMenu(const QPoint &pos)
{
	QMenu menu(this);

	QAction *gotoMemory = new QAction(tr("Go to in &memory view"), this);
	connect(gotoMemory, SIGNAL(triggered()), this, SLOT(GotoMemory()));
	menu.addAction(gotoMemory);

	QAction *gotoDisAsm = new QAction(tr("Go to in &disasm"), this);
	connect(gotoDisAsm, SIGNAL(triggered()), this, SLOT(GotoDisAsm()));
	menu.addAction(gotoDisAsm);

	menu.addSeparator();

	QAction *copyValue = new QAction(tr("&Copy value"), this);
	connect(copyValue, SIGNAL(triggered()), this, SLOT(CopyValue()));
	menu.addAction(copyValue);

	QAction *change = new QAction(tr("C&hange..."), this);
	connect(change, SIGNAL(triggered()), this, SLOT(Change()));
	menu.addAction(change);

	menu.exec( mapToGlobal(pos));
}

void CtrlRegisterList::GotoMemory()
{
	int cat = category;
	int reg = selection;
	if (selection >= cpu->GetNumRegsInCategory(cat))
		return;

	EmuThread_LockDraw(true);
	u32 val = cpu->GetRegValue(cat,reg);
	EmuThread_LockDraw(false);

	parentWindow->ShowMemory(val);
}

void CtrlRegisterList::GotoDisAsm()
{
	int cat = category;
	int reg = selection;
	if (selection >= cpu->GetNumRegsInCategory(cat))
		return;

	EmuThread_LockDraw(true);
	u32 val = cpu->GetRegValue(cat,reg);
	EmuThread_LockDraw(false);

	emit GotoDisasm(val);
}

void CtrlRegisterList::CopyValue()
{
	int cat = category;
	int reg = selection;
	if (selection >= cpu->GetNumRegsInCategory(cat))
		return;

	EmuThread_LockDraw(true);
	u32 val = cpu->GetRegValue(cat,reg);
	EmuThread_LockDraw(false);

	QApplication::clipboard()->setText(QString("%1").arg(val,8,16,QChar('0')));
}

void CtrlRegisterList::Change()
{
	int cat = category;
	int reg = selection;
	if (selection >= cpu->GetNumRegsInCategory(cat))
		return;

	EmuThread_LockDraw(true);
	u32 val = cpu->GetRegValue(cat,reg);
	EmuThread_LockDraw(false);

	bool ok;
	QString text = QInputDialog::getText(this, tr("Set new value"),
								tr("Set new value:"), QLineEdit::Normal,
								QString::number(val), &ok);
	if (ok && !text.isEmpty())
	{
		EmuThread_LockDraw(true);
		cpu->SetRegValue(cat,reg,text.toInt());
		EmuThread_LockDraw(false);
		redraw();
	}
}
