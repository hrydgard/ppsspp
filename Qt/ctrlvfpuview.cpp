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

	QFont normalFont = QFont("Arial", 10);
	painter.setFont(normalFont);

	QPen currentPen=QPen(0xFF000000);
	QBrush nullBrush=QBrush(0xFFEfE8);
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
		char temp[256];
		sprintf(temp, "M%i00", matrix);
		painter.drawText(3, my+rowHeight-3, temp);
		painter.drawRect(xStart, my+rowHeight, columnWidth*4-1, 4*rowHeight-1);

		for (int column = 0; column<4; column++)
		{
			int y = my;
			int x = column * columnWidth + xStart;

			painter.drawRect(x, y, columnWidth-1, rowHeight - 1);
			char temp[256];
			sprintf(temp, "R%i0%i", matrix, column);
			painter.drawText(x+3, y-3+rowHeight, temp);

			painter.drawRect(0, y+rowHeight*(column+1), xStart - 1, rowHeight - 1);
			sprintf(temp, "C%i%i0", matrix, column);
			painter.drawText(3, y+rowHeight*(column+2)-3, temp);

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

