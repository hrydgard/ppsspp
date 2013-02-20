#include "ctrlvfpuview.h"
#include <QPainter>

#include "Core/MIPS/MIPS.h" //	BAD

CtrlVfpuView::CtrlVfpuView(QWidget *parent) :
	QWidget(parent)
{
	mode = 0;
}

void CtrlVfpuView::setMode(int newMode)
{
	mode = newMode;
	redraw();
}

void CtrlVfpuView::paintEvent(QPaintEvent *)
{
	QPainter painter(this);
	painter.setBrush(Qt::white);
	painter.setPen(Qt::white);
	painter.drawRect(rect());

	if (!cpu)
		return;

	QFont normalFont("Arial", 10);
	painter.setFont(normalFont);

	QPen currentPen(0xFF000000);
	QBrush nullBrush(0xFFEfE8);
	painter.setPen(currentPen);
	painter.setBrush(nullBrush);

	enum
	{
		rowHeight = 15,
		columnWidth = 80,
		xStart = columnWidth/2,
		yStart = 0
	};

	for (int matrix = 0; matrix<8; matrix++)
	{
		int my = (int)(yStart + matrix * rowHeight * 5.5f);
		painter.drawRect(0, my, xStart-1, rowHeight-1);
		painter.drawText(3, my+rowHeight-3, QString("M%1").arg(matrix)+"00");
		painter.drawRect(xStart, my+rowHeight, columnWidth*4-1, 4*rowHeight-1);

		for (int column = 0; column<4; column++)
		{
			int y = my;
			int x = column * columnWidth + xStart;

			painter.drawRect(x, y, columnWidth-1, rowHeight - 1);
			painter.drawText(x+3, y-3+rowHeight, QString("R%1").arg(matrix)+QString("0%1").arg(column));

			painter.drawRect(0, y+rowHeight*(column+1), xStart - 1, rowHeight - 1);
			painter.drawText(3, y+rowHeight*(column+2)-3, QString("C%1").arg(matrix)+QString("%1").arg(column)+"0");

			y+=rowHeight;

			for (int row = 0; row<4; row++)
			{
				float val = mipsr4k.v[column*32+row+matrix*4];
				u32 hex = *((u32*)&val);
				char temp[256];
				switch (mode)
				{
				case 0: sprintf(temp,"%f",val); break;
//				case 1: sprintf(temp,"??"); break;
				case 2: sprintf(temp,"0x%08x",hex); break;
				default:sprintf(temp,"%f",val); break;
				}

				painter.drawText(x+3, y-3 + rowHeight, temp);
				y+=rowHeight;
			}
		}
	}

	setMinimumHeight((int)(yStart + 8 * rowHeight * 5.5f));
	setMinimumWidth(4*columnWidth+xStart);

}


void CtrlVfpuView::redraw()
{
	update();
}

