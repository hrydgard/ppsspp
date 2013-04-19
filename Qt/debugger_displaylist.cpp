#include "debugger_displaylist.h"

#include <QTimer>
#include <set>
#include <QMenu>

#include "Core/CPU.h"
#include "ui_debugger_displaylist.h"
#include "GPU/GPUInterface.h"
#include "GPU/GeDisasm.h"
#include "EmuThread.h"
#include "Core/Host.h"
#include "base/display.h"
#include "mainwindow.h"
#include "GPU/GLES/VertexDecoder.h"


Debugger_DisplayList::Debugger_DisplayList(DebugInterface *_cpu, MainWindow* mainWindow_, QWidget *parent) :
	QDialog(parent),
	ui(new Ui::Debugger_DisplayList),
	cpu(_cpu),
	mainWindow(mainWindow_),
	currentRenderFrameDisplay(0),
	currentTextureDisplay(0),
	fboZoomFactor(1),
	maxVtxDisplay(20),
	maxIdxDisplay(20)
{
	ui->setupUi(this);

	QObject::connect(this, SIGNAL(updateDisplayList_()), this, SLOT(UpdateDisplayListGUI()));
	QObject::connect(this, SIGNAL(updateRenderBufferList_()), this, SLOT(UpdateRenderBufferListGUI()));
	QObject::connect(this, SIGNAL(updateRenderBuffer_()), this, SLOT(UpdateRenderBufferGUI()));

}

Debugger_DisplayList::~Debugger_DisplayList()
{
	delete ui;
}


void Debugger_DisplayList::showEvent(QShowEvent *)
{

#ifdef Q_WS_X11
	// Hack to remove the X11 crash with threaded opengl when opening the first dialog
	EmuThread_LockDraw(true);
	QTimer::singleShot(100, this, SLOT(releaseLock()));
#endif

}

void Debugger_DisplayList::releaseLock()
{
	EmuThread_LockDraw(false);
}


void Debugger_DisplayList::UpdateDisplayList()
{
	emit updateDisplayList_();
}


void Debugger_DisplayList::UpdateDisplayListGUI()
{
	u32 curDlId = 0;
	QTreeWidgetItem* curItem = ui->displayList->currentItem();
	if(curItem)
		curDlId = ui->displayList->currentItem()->data(0,Qt::UserRole).toInt();

	displayListRowSelected = 0;
	ui->displayList->clear();
	ui->displayListData->clear();

	EmuThread_LockDraw(true);
	const std::list<int>& dlQueue = gpu->GetDisplayLists();

	for(auto listIdIt = dlQueue.begin(); listIdIt != dlQueue.end(); ++listIdIt)
	{
		DisplayList *it = gpu->getList(*listIdIt);
		QTreeWidgetItem* item = new QTreeWidgetItem();
		item->setText(0,QString::number(it->id));
		item->setData(0, Qt::UserRole, it->id);
		switch(it->state)
		{
		case PSP_GE_DL_STATE_NONE: item->setText(1,"None"); break;
		case PSP_GE_DL_STATE_QUEUED: item->setText(1,"Queued"); break;
		case PSP_GE_DL_STATE_RUNNING: item->setText(1,"Running"); break;
		case PSP_GE_DL_STATE_COMPLETED: item->setText(1,"Completed"); break;
		case PSP_GE_DL_STATE_PAUSED: item->setText(1,"Paused"); break;
		default: break;
		}
		item->setText(2,QString("%1").arg(it->startpc,8,16,QChar('0')));
		item->setData(2, Qt::UserRole, it->startpc);
		item->setText(3,QString("%1").arg(it->pc,8,16,QChar('0')));
		item->setData(3, Qt::UserRole, it->pc);
		ui->displayList->addTopLevelItem(item);
		if(curDlId == (u32)it->id)
		{
			ui->displayList->setCurrentItem(item);
			displayListRowSelected = item;
			ShowDLCode();
		}
	}
	for(int i = 0; i < ui->displayList->columnCount(); i++)
		ui->displayList->resizeColumnToContents(i);

	if (ui->displayList->selectedItems().size() == 0 && ui->displayList->topLevelItemCount() != 0)
	{
		ui->displayList->setCurrentItem(ui->displayList->topLevelItem(0));
		displayListRowSelected = ui->displayList->topLevelItem(0);
		ShowDLCode();
	}

	EmuThread_LockDraw(false);
}


void Debugger_DisplayList::ShowDLCode()
{
	ui->displayListData->clear();
	ui->displayListData->setColumnCount(4);

	u32 startPc = displayListRowSelected->data(2,Qt::UserRole).toInt();
	u32 curPc = displayListRowSelected->data(3,Qt::UserRole).toInt();

	std::map<int,DListLine> data;
	GPUgstate listState;
	memset(&listState,0,sizeof(GPUgstate));
	drawGPUState.clear();
	vtxBufferSize.clear();
	idxBufferSize.clear();

	FillDisplayListCmd(data, startPc,0, listState);

	u32 curTexAddr;
	u32 curVtxAddr;
	u32 curIdxAddr;

	for(std::map<int,DListLine>::iterator it = data.begin(); it != data.end(); it++)
	{
		QTreeWidgetItem* item = new QTreeWidgetItem();
		item->setText(0,QString("%1").arg(it->first,8,16,QChar('0')));
		item->setData(0, Qt::UserRole, it->first);
		item->setText(1,QString("%1").arg(it->second.cmd,2,16,QChar('0')));
		item->setText(2,QString("%1").arg(it->second.data,6,16,QChar('0')));
		item->setText(3,it->second.comment);
		if(curPc == (u32)it->first)
		{
			curTexAddr = it->second.texAddr;
			curVtxAddr = it->second.vtxAddr;
			curIdxAddr = it->second.idxAddr;
			setCurrentFBO(it->second.fboAddr);
			for(int j = 0; j < ui->displayListData->columnCount(); j++)
				item->setTextColor(j, Qt::green);
		}
		if(it->second.implementationNotFinished)
		{
			for(int j = 0; j < ui->displayListData->columnCount(); j++)
				item->setBackgroundColor(j, Qt::red);
		}
		ui->displayListData->addTopLevelItem(item);

		if(curPc == (u32)it->first)
		{
			ui->displayListData->setCurrentItem(item);
		}
	}
	for(int j = 0; j < ui->displayListData->columnCount(); j++)
		ui->displayListData->resizeColumnToContents(j);

	ui->texturesList->clear();
	ui->vertexData->clear();
	ui->vertexList->clear();
	ui->indexData->clear();
	ui->indexList->clear();

	std::set<u32> usedTexAddr;
	std::set<u32> usedVtxAddr;
	std::set<u32> usedIdxAddr;
	for(int i = 0; i < drawGPUState.size(); i++)
	{
		// Textures
		QTreeWidgetItem* item = new QTreeWidgetItem();
		u32 texaddr = (drawGPUState[i].texaddr[0] & 0xFFFFF0) | ((drawGPUState[i].texbufwidth[0]<<8) & 0x0F000000);
		if(usedTexAddr.find(texaddr) == usedTexAddr.end() && Memory::IsValidAddress(texaddr))
		{
			u32 format = drawGPUState[i].texformat & 0xF;
			int w = 1 << (drawGPUState[i].texsize[0] & 0xf);
			int h = 1 << ((drawGPUState[i].texsize[0]>>8) & 0xf);

			item->setText(0,QString("%1").arg(texaddr,8,16,QChar('0')));
			item->setData(0, Qt::UserRole, i);
			item->setText(1,QString::number(w));
			item->setText(2,QString::number(h));
			item->setText(3,QString::number(format,16));
			ui->texturesList->addTopLevelItem(item);
			if(curTexAddr == texaddr)
			{
				ui->texturesList->setCurrentItem(item);
				for(int j = 0; j < ui->texturesList->columnCount(); j++)
					item->setTextColor(j,Qt::green);
			}
			usedTexAddr.insert(texaddr);
		}

		// Vertex
		QTreeWidgetItem* vertexItem = new QTreeWidgetItem();
		u32 baseExtended = ((drawGPUState[i].base & 0x0F0000) << 8) | (drawGPUState[i].vaddr & 0xFFFFFF);
		u32 vaddr = ((drawGPUState[i].offsetAddr & 0xFFFFFF) + baseExtended) & 0x0FFFFFFF;
		if(drawGPUState[i].vaddr != 0 && Memory::IsValidAddress(vaddr) && usedVtxAddr.find(vaddr) == usedVtxAddr.end())
		{
			vertexItem->setText(0, QString("%1").arg(vaddr,8,16,QChar('0')));
			vertexItem->setData(0,Qt::UserRole, i);
			if((drawGPUState[i].vertType & GE_VTYPE_THROUGH_MASK) == GE_VTYPE_TRANSFORM)
				vertexItem->setText(1, "Transform");
			else
				vertexItem->setText(1, "Raw");
			vertexItem->setText(2, QString::number((drawGPUState[i].vertType & GE_VTYPE_MORPHCOUNT_MASK) >> GE_VTYPE_MORPHCOUNT_SHIFT));
			vertexItem->setText(3, QString::number((drawGPUState[i].vertType & GE_VTYPE_WEIGHTCOUNT_MASK) >> GE_VTYPE_WEIGHTCOUNT_SHIFT));
			switch(drawGPUState[i].vertType & GE_VTYPE_WEIGHT_MASK)
			{
			case GE_VTYPE_WEIGHT_8BIT: vertexItem->setText(4, "8bit"); break;
			case GE_VTYPE_WEIGHT_16BIT: vertexItem->setText(4, "16bit"); break;
			case GE_VTYPE_WEIGHT_FLOAT: vertexItem->setText(4, "float"); break;
			default: vertexItem->setText(4, "No"); break;
			}
			switch(drawGPUState[i].vertType & GE_VTYPE_POS_MASK)
			{
			case GE_VTYPE_POS_8BIT: vertexItem->setText(5, "8bit"); break;
			case GE_VTYPE_POS_16BIT: vertexItem->setText(5, "16bit"); break;
			case GE_VTYPE_POS_FLOAT: vertexItem->setText(5, "float"); break;
			default: vertexItem->setText(5, "No"); break;
			}
			switch(drawGPUState[i].vertType & GE_VTYPE_NRM_MASK)
			{
			case GE_VTYPE_NRM_8BIT: vertexItem->setText(6, "8bit"); break;
			case GE_VTYPE_NRM_16BIT: vertexItem->setText(6, "16bit"); break;
			case GE_VTYPE_NRM_FLOAT: vertexItem->setText(6, "float"); break;
			default: vertexItem->setText(6, "No"); break;
			}
			switch(drawGPUState[i].vertType & GE_VTYPE_COL_MASK)
			{
			case GE_VTYPE_COL_4444: vertexItem->setText(7, "4444"); break;
			case GE_VTYPE_COL_5551: vertexItem->setText(7, "5551"); break;
			case GE_VTYPE_COL_565: vertexItem->setText(7, "565"); break;
			case GE_VTYPE_COL_8888: vertexItem->setText(7, "8888"); break;
			default: vertexItem->setText(7, "No"); break;
			}
			switch(drawGPUState[i].vertType & GE_VTYPE_TC_MASK)
			{
			case GE_VTYPE_TC_8BIT: vertexItem->setText(8, "8bit"); break;
			case GE_VTYPE_TC_16BIT: vertexItem->setText(8, "16bit"); break;
			case GE_VTYPE_TC_FLOAT: vertexItem->setText(8, "float"); break;
			default: vertexItem->setText(8, "No"); break;
			}

			ui->vertexList->addTopLevelItem(vertexItem);
			if(curVtxAddr == vaddr)
			{
				ui->vertexList->setCurrentItem(vertexItem);
				for(int j = 0; j < ui->vertexList->columnCount(); j++)
					vertexItem->setTextColor(j,Qt::green);
			}
			usedVtxAddr.insert(vaddr);
		}


		// Index
		QTreeWidgetItem* indexItem = new QTreeWidgetItem();
		baseExtended = ((drawGPUState[i].base & 0x0F0000) << 8) | (drawGPUState[i].iaddr & 0xFFFFFF);
		u32 iaddr = ((drawGPUState[i].offsetAddr & 0xFFFFFF) + baseExtended) & 0x0FFFFFFF;
		if((drawGPUState[i].iaddr & 0xFFFFFF) != 0 && Memory::IsValidAddress(iaddr) && usedIdxAddr.find(iaddr) == usedIdxAddr.end())
		{
			indexItem->setText(0, QString("%1").arg(iaddr,8,16,QChar('0')));
			indexItem->setData(0,Qt::UserRole, i);

			ui->indexList->addTopLevelItem(indexItem);
			if(curIdxAddr == iaddr)
			{
				ui->indexList->setCurrentItem(indexItem);
				for(int j = 0; j < ui->indexList->columnCount(); j++)
					indexItem->setTextColor(j,Qt::green);
			}
			usedIdxAddr.insert(iaddr);
		}
	}


	for(int i = 0; i < ui->texturesList->columnCount(); i++)
		ui->texturesList->resizeColumnToContents(i);
	for(int i = 0; i < ui->vertexList->columnCount(); i++)
		ui->vertexList->resizeColumnToContents(i);
	for(int i = 0; i < ui->indexList->columnCount(); i++)
		ui->indexList->resizeColumnToContents(i);

	UpdateVertexInfo();
	UpdateIndexInfo();
}


QString Debugger_DisplayList::DisassembleOp(u32 pc, u32 op, u32 prev, const GPUgstate& state) {
	u32 cmd = op >> 24;
	u32 data = op & 0xFFFFFF;

	// Handle control and drawing commands here directly. The others we delegate.
	switch (cmd)
	{
	case GE_CMD_BASE:
		return QString("BASE: %1").arg(data & 0xFFFFFF,6,16,QChar('0'));
		break;

	case GE_CMD_VADDR:		/// <<8????
	{
		u32 baseExtended = ((state.base & 0x0F0000) << 8) | (data & 0xFFFFFF);
		baseExtended = (state.offsetAddr + baseExtended) & 0x0FFFFFFF;
		return QString("VADDR: %1").arg(baseExtended,6,16,QChar('0'));
		break;
	}

	case GE_CMD_IADDR:
	{
		u32 baseExtended = ((state.base & 0x0F0000) << 8) | (data & 0xFFFFFF);
		baseExtended = (state.offsetAddr + baseExtended) & 0x0FFFFFFF;
		return QString("IADDR: %1").arg(baseExtended,6,16,QChar('0'));
		break;
	}

	case GE_CMD_PRIM:
		{
			u32 count = data & 0xFFFF;
			u32 type = data >> 16;
			static const char* types[7] = {
				"POINTS",
				"LINES",
				"LINE_STRIP",
				"TRIANGLES",
				"TRIANGLE_STRIP",
				"TRIANGLE_FAN",
				"RECTANGLES",
			};
			return QString("DrawPrim type: %1 count: %2").arg(type < 7 ? types[type] : "INVALID").arg(count);
		}
		break;

	// The arrow and other rotary items in Puzbob are bezier patches, strangely enough.
	case GE_CMD_BEZIER:
		{
			int bz_ucount = data & 0xFF;
			int bz_vcount = (data >> 8) & 0xFF;
			return QString("DRAW BEZIER: U=%1 x V=%2").arg(bz_ucount).arg(bz_vcount);
		}
		break;

	case GE_CMD_SPLINE:
		{
			int sp_ucount = data & 0xFF;
			int sp_vcount = (data >> 8) & 0xFF;
			static const char* type[4] = {
				"Close/Close",
				"Open/Close",
				"Close/Open",
				"Open/Open"
			};
			int sp_utype = (data >> 16) & 0x3;
			int sp_vtype = (data >> 18) & 0x3;
			return QString("DRAW SPLINE: U=%1 x V=%2, U Type = %3 , V Type = %4").arg(sp_ucount).arg(sp_vcount).arg(type[sp_utype]).arg(type[sp_vtype]);
		}
		break;

	case GE_CMD_JUMP:
		{
			u32 target = (((state.base & 0x00FF0000) << 8) | (op & 0xFFFFFC)) & 0x0FFFFFFF;
			return QString("CMD JUMP - %1 to %2").arg(pc,8,16,QChar('0')).arg(target,8,16,QChar('0'));
		}
		break;

	case GE_CMD_CALL:
		{
			u32 retval = pc + 4;
			u32 baseExtended = ((state.base & 0x0F0000) << 8) | (op & 0xFFFFFF);
			u32 target = (state.offsetAddr + baseExtended) & 0x0FFFFFFF;
			return QString("CMD CALL - %1 to %2, ret=%3").arg(pc,8,16,QChar('0')).arg(target,8,16,QChar('0')).arg(retval,8,16,QChar('0'));
		}
		break;

	case GE_CMD_RET:
		return QString("CMD RET");
		break;

	case GE_CMD_SIGNAL:
		return QString("GE_CMD_SIGNAL %1").arg(data,6,16,QChar('0'));
		break;

	case GE_CMD_FINISH:
		return QString("CMD FINISH %1").arg(data,6,16,QChar('0'));
		break;

	case GE_CMD_END:
		switch (prev >> 24)
		{
		case GE_CMD_SIGNAL:
			{
				// TODO: see http://code.google.com/p/jpcsp/source/detail?r=2935#
				int behaviour = (prev >> 16) & 0xFF;
				int signal = prev & 0xFFFF;
				int enddata = data & 0xFFFF;
				// We should probably defer to sceGe here, no sense in implementing this stuff in every GPU
				switch (behaviour) {
				case 1:  // Signal with Wait
					return QString("Signal with Wait UNIMPLEMENTED! signal/end: %1 %2").arg(signal,4,16,QChar('0')).arg(enddata,4,16,QChar('0'));
					break;
				case 2:
					return QString("Signal without wait. signal/end: %1 %2").arg(signal,4,16,QChar('0')).arg(enddata,4,16,QChar('0'));
					break;
				case 3:
					return QString("Signal with Pause UNIMPLEMENTED! signal/end: %1 %2").arg(signal,4,16,QChar('0')).arg(enddata,4,16,QChar('0'));
					break;
				case 0x10:
					return QString("Signal with Jump UNIMPLEMENTED! signal/end: %1 %2").arg(signal,4,16,QChar('0')).arg(enddata,4,16,QChar('0'));
					break;
				case 0x11:
					return QString("Signal with Call UNIMPLEMENTED! signal/end: %1 %2").arg(signal,4,16,QChar('0')).arg(enddata,4,16,QChar('0'));
					break;
				case 0x12:
					return QString("Signal with Return UNIMPLEMENTED! signal/end: %1 %2").arg(signal,4,16,QChar('0')).arg(enddata,4,16,QChar('0'));
					break;
				default:
					return QString("UNKNOWN Signal UNIMPLEMENTED %1 ! signal/end: %1 %2").arg(behaviour).arg(signal,4,16,QChar('0')).arg(enddata,4,16,QChar('0'));
					break;
				}
			}
			break;
		case GE_CMD_FINISH:
			break;
		default:
			return QString("Ah, not finished: %1").arg(prev & 0xFFFFFF,6,16,QChar('0'));
			break;
		}
		return "CMD END";
		break;

	case GE_CMD_BJUMP:
	{
		// bounding box jump. Let's just not jump, for now.
		u32 target = (((state.base & 0x00FF0000) << 8) | (op & 0xFFFFFC)) & 0x0FFFFFFF;
		return QString("BBOX JUMP - %1 to %2").arg(pc,8,16,QChar('0')).arg(target,8,16,QChar('0'));
		break;
	}
	case GE_CMD_BOUNDINGBOX:
		// bounding box test. Let's do nothing.
		return QString("BBOX TEST - number : %1").arg(data & 0xFFFF,4,16,QChar('0'));
		break;

	case GE_CMD_ORIGIN:
		return QString("Origin: %1").arg(data,6,16,QChar('0'));
		break;

	case GE_CMD_VERTEXTYPE:
	{
		const char* format[4] =
		{
			"No",
			"8 Bits fixed",
			"16 Bits fixed",
			"Float"
		};
		const char* colFormat[8] =
		{
			"No",
			"",
			"",
			"",
			"16-bit BGR-5650",
			"16-bit ABGR-5551",
			"16-bit ABGR-4444",
			"32-bit ABGR-8888"
		};
		QString retString = "SetVertexType:";

		u32 transform = data & GE_VTYPE_THROUGH_MASK;
		retString += QString(" Transform : %1").arg(transform==0?"Transformed":"Raw");

		u32 numVertMorph = (data & GE_VTYPE_MORPHCOUNT_MASK) >> GE_VTYPE_MORPHCOUNT_SHIFT;
		retString += QString(", Num Vtx Morph : %1").arg(numVertMorph);

		u32 numWeight = (data & GE_VTYPE_WEIGHTCOUNT_MASK) >> GE_VTYPE_WEIGHTCOUNT_SHIFT;
		retString += QString(", Num Weights : %1").arg(numWeight);

		u32 indexFmt = (data & GE_VTYPE_IDX_MASK) >> GE_VTYPE_IDX_SHIFT;
		retString += QString(", Index Format : %1").arg(format[indexFmt]);

		u32 weightFmt = (data & GE_VTYPE_WEIGHT_MASK) >> GE_VTYPE_WEIGHT_SHIFT;
		retString += QString(", Weight Format : %1").arg(format[weightFmt]);

		u32 posFmt = (data & GE_VTYPE_POS_MASK) >> GE_VTYPE_POS_SHIFT;
		retString += QString(", Position Format : %1").arg(format[posFmt]);

		u32 nrmFmt = (data & GE_VTYPE_NRM_MASK) >> GE_VTYPE_NRM_SHIFT;
		retString += QString(", Normal Format : %1").arg(format[nrmFmt]);

		u32 colFmt = (data & GE_VTYPE_COL_MASK) >> GE_VTYPE_COL_SHIFT;
		retString += QString(", Color Format : %1").arg(colFormat[colFmt]);

		u32 tcFmt = (data & GE_VTYPE_TC_MASK) >> GE_VTYPE_TC_SHIFT;
		retString += QString(", Texture UV Format : %1").arg(format[tcFmt]);

		return retString;
		break;
	}
	case GE_CMD_OFFSETADDR:
		return QString("OffsetAddr: %1").arg(data,6,16,QChar('0'));
		break;

	case GE_CMD_REGION1:
		{
			int x1 = data & 0x3ff;
			int y1 = data >> 10;
			//topleft
			return QString("Region TL: %1 %2").arg(x1).arg(y1);
		}
		break;

	case GE_CMD_REGION2:
		{
			int x2 = data & 0x3ff;
			int y2 = data >> 10;
			return QString("Region BR: %1 %2").arg(x2).arg(y2);
		}
		break;

	case GE_CMD_CLIPENABLE:
		return QString("Clip Enable: %1").arg(data);
		break;

	case GE_CMD_CULLFACEENABLE:
		return QString("CullFace Enable: %1").arg(data);
		break;

	case GE_CMD_TEXTUREMAPENABLE:
		return QString("Texture map enable: %1").arg(data);
		break;

	case GE_CMD_LIGHTINGENABLE:
		return QString("Lighting enable: %1").arg(data);
		break;

	case GE_CMD_FOGENABLE:
		return QString("Fog Enable: %1").arg(data);
		break;

	case GE_CMD_DITHERENABLE:
		return QString("Dither Enable: %1").arg(data);
		break;

	case GE_CMD_OFFSETX:
		return QString("Offset X: %1").arg(data);
		break;

	case GE_CMD_OFFSETY:
		return QString("Offset Y: %1").arg(data);
		break;

	case GE_CMD_TEXSCALEU:
		return QString("Texture U Scale: %1").arg(getFloat24(data));
		break;

	case GE_CMD_TEXSCALEV:
		return QString("Texture V Scale: %1").arg(getFloat24(data));
		break;

	case GE_CMD_TEXOFFSETU:
		return QString("Texture U Offset: %1").arg(getFloat24(data));
		break;

	case GE_CMD_TEXOFFSETV:
		return QString("Texture V Offset: %1").arg(getFloat24(data));
		break;

	case GE_CMD_SCISSOR1:
		{
			int x1 = data & 0x3ff;
			int y1 = data >> 10;
			return QString("Scissor TL: %1, %2").arg(x1).arg(y1);
		}
		break;
	case GE_CMD_SCISSOR2:
		{
			int x2 = data & 0x3ff;
			int y2 = data >> 10;
			return QString("Scissor BR: %1, %2").arg(x2).arg(y2);
		}
		break;

	case GE_CMD_MINZ:
		{
			float zMin = getFloat24(data) / 65535.f;
			return QString("MinZ: %1").arg(zMin);
		}
		break;

	case GE_CMD_MAXZ:
		{
			float zMax = getFloat24(data) / 65535.f;
			return QString("MaxZ: %1").arg(zMax);
		}
		break;

	case GE_CMD_FRAMEBUFPTR:
		{
			u32 ptr = op & 0xFFE000;
			return QString("FramebufPtr: %1").arg(data,8,16,QChar('0'));
		}
		break;

	case GE_CMD_FRAMEBUFWIDTH:
		{
			return QString("FramebufWidth: %1").arg(data);
		}
		break;

	case GE_CMD_FRAMEBUFPIXFORMAT:
	{
		const char* fmt[4] =
		{
			"16-bit BGR 5650",
			"16-bit ABGR 5551",
			"16-bit ABGR 4444",
			"32-bit ABGR 8888"
		};
		return QString("FramebufPixeFormat: %1").arg(fmt[data]);
		break;
	}
	case GE_CMD_TEXADDR0:
	case GE_CMD_TEXADDR1:
	case GE_CMD_TEXADDR2:
	case GE_CMD_TEXADDR3:
	case GE_CMD_TEXADDR4:
	case GE_CMD_TEXADDR5:
	case GE_CMD_TEXADDR6:
	case GE_CMD_TEXADDR7:
		return QString("Texture address %1: %2").arg(cmd-GE_CMD_TEXADDR0).arg(data,6,16,QChar('0'));
		break;

	case GE_CMD_TEXBUFWIDTH0:
	case GE_CMD_TEXBUFWIDTH1:
	case GE_CMD_TEXBUFWIDTH2:
	case GE_CMD_TEXBUFWIDTH3:
	case GE_CMD_TEXBUFWIDTH4:
	case GE_CMD_TEXBUFWIDTH5:
	case GE_CMD_TEXBUFWIDTH6:
	case GE_CMD_TEXBUFWIDTH7:
		return QString("Texture BUFWIDTHess %1: %2 width : %3").arg(cmd-GE_CMD_TEXBUFWIDTH0).arg(data,6,16,QChar('0')).arg(data & 0xFFFF);
		break;

	case GE_CMD_CLUTADDR:
		return QString("CLUT base addr: %1").arg(data,6,16,QChar('0'));
		break;

	case GE_CMD_CLUTADDRUPPER:
		return QString("CLUT addr upper %1").arg(data,8,16,QChar('0'));
		break;

	case GE_CMD_LOADCLUT:
		// This could be used to "dirty" textures with clut.
		return QString("Clut load, numColor : %1").arg(data*8);
		break;

	case GE_CMD_TEXMAPMODE:
	{
		const char* texMapMode[3] =
		{
			"Texture Coordinates (UV)",
			"Texture Matrix",
			"Environment Map"
		};
		const char* texProjMode[4] =
		{
			"Position",
			"Texture Coordinates",
			"Normalized Normal",
			"Normal"
		};
		return QString("Tex map mode: Map mode : %1, Proj Mode : %2").arg(texMapMode[data & 0x3]).arg(texProjMode[(data >> 8) & 0x3]);
		break;
	}
	case GE_CMD_TEXSHADELS:
		return QString("Tex shade light sources: %1").arg(data,6,16,QChar('0'));
		break;

	case GE_CMD_CLUTFORMAT:
		{
		const char* fmt[4] =
		{
			"16-bit BGR 5650",
			"16-bit ABGR 5551",
			"16-bit ABGR 4444",
			"32-bit ABGR 8888"
		};
		return QString("Clut format: %1 , Fmt : %2").arg(data,6,16,QChar('0')).arg(fmt[data & 0x3]);
		}
		break;

	case GE_CMD_TRANSFERSRC:
		{
			return QString("Block Transfer Src: %1").arg(data,6,16,QChar('0'));
			// Nothing to do, the next one prints
		}
		break;

	case GE_CMD_TRANSFERSRCW:
		{
			u32 xferSrc = state.transfersrc | ((data&0xFF0000)<<8);
			u32 xferSrcW = state.transfersrcw & 1023;
			return QString("Block Transfer Src: %1	W: %2").arg(xferSrc,8,16,QChar('0')).arg(xferSrcW);
			break;
		}

	case GE_CMD_TRANSFERDST:
		{
			// Nothing to do, the next one prints
			return QString("Block Transfer Dst: %1").arg(data,6,16,QChar('0'));
		}
		break;

	case GE_CMD_TRANSFERDSTW:
		{
			u32 xferDst= state.transferdst | ((data&0xFF0000)<<8);
			u32 xferDstW = state.transferdstw & 1023;
			return QString("Block Transfer Dest: %1	W: %2").arg(xferDst,8,16,QChar('0')).arg(xferDstW);
			break;
		}

	case GE_CMD_TRANSFERSRCPOS:
		{
			u32 x = (data & 1023)+1;
			u32 y = ((data>>10) & 1023)+1;
			return QString("Block Transfer Src Rect TL: %1, %2").arg(x).arg(y);
			break;
		}

	case GE_CMD_TRANSFERDSTPOS:
		{
			u32 x = (data & 1023)+1;
			u32 y = ((data>>10) & 1023)+1;
			return QString("Block Transfer Dest Rect TL: %1, %2").arg(x).arg(y);
			break;
		}

	case GE_CMD_TRANSFERSIZE:
		{
			u32 w = (data & 1023)+1;
			u32 h = ((data>>10) & 1023)+1;
			return QString("Block Transfer Rect Size: %1 x %2").arg(w).arg(h);
			break;
		}

	case GE_CMD_TRANSFERSTART:  // Orphis calls this TRXKICK
		{
		return QString("Block Transfer Start : %1").arg(data ? "32-bit texel size" : "16-bit texel size");
			break;
		}

	case GE_CMD_TEXSIZE0:
	case GE_CMD_TEXSIZE1:
	case GE_CMD_TEXSIZE2:
	case GE_CMD_TEXSIZE3:
	case GE_CMD_TEXSIZE4:
	case GE_CMD_TEXSIZE5:
	case GE_CMD_TEXSIZE6:
	case GE_CMD_TEXSIZE7:
		{
			int w = 1 << (data & 0xf);
			int h = 1 << ((data>>8) & 0xf);
			return QString("Texture Size %1: %2, width : %3, height : %4").arg(cmd - GE_CMD_TEXSIZE0).arg(data,6,16,QChar('0')).arg(w).arg(h);
		}
		break;

	case GE_CMD_ZBUFPTR:
		{
			u32 ptr = op & 0xFFE000;
			return QString("Zbuf Ptr: %1").arg(ptr,6,16,QChar('0'));
		}
		break;

	case GE_CMD_ZBUFWIDTH:
		{
			return QString("Zbuf Width: %1").arg(data,6,16,QChar('0'));
		}
		break;

	case GE_CMD_AMBIENTCOLOR:
		return QString("Ambient Color: %1").arg(data,6,16,QChar('0'));
		break;

	case GE_CMD_AMBIENTALPHA:
		return QString("Ambient Alpha: %1").arg(data,6,16,QChar('0'));
		break;

	case GE_CMD_MATERIALAMBIENT:
		return QString("Material Ambient Color: %1").arg(data,6,16,QChar('0'));
		break;

	case GE_CMD_MATERIALDIFFUSE:
		return QString("Material Diffuse Color: %1").arg(data,6,16,QChar('0'));
		break;

	case GE_CMD_MATERIALEMISSIVE:
		return QString("Material Emissive Color: %1").arg(data,6,16,QChar('0'));
		break;

	case GE_CMD_MATERIALSPECULAR:
		return QString("Material Specular Color: %1").arg(data,6,16,QChar('0'));
		break;

	case GE_CMD_MATERIALALPHA:
		return QString("Material Alpha Color: %1").arg(data,6,16,QChar('0'));
		break;

	case GE_CMD_MATERIALSPECULARCOEF:
		return QString("Material specular coef: %1").arg(getFloat24(data));
		break;

	case GE_CMD_SHADEMODE:
		return QString("Shade: %1 (%2)").arg(data,6,16,QChar('0')).arg(data ? "gouraud" : "flat");
		break;

	case GE_CMD_LIGHTMODE:
		return QString("Lightmode: %1 (%2)").arg(data,6,16,QChar('0')).arg(data ? "separate spec" : "single color");
		break;

	case GE_CMD_LIGHTTYPE0:
	case GE_CMD_LIGHTTYPE1:
	case GE_CMD_LIGHTTYPE2:
	case GE_CMD_LIGHTTYPE3:
	{
		const char* lightType[3] =
		{
			"Directional Light",
			"Point Light",
			"Spot Light"
		};
		const char* lightComp[3] =
		{
			"Ambient & Diffuse",
			"Diffuse & Specular",
			"Unknown (diffuse color, affected by specular power)"
		};
		return QString("Light %1 type: %2 %3").arg(cmd-GE_CMD_LIGHTTYPE0).arg(lightType[(data) >> 8 & 0x3]).arg(lightComp[data & 0x3]);
		break;
	}
	case GE_CMD_LX0:case GE_CMD_LY0:case GE_CMD_LZ0:
	case GE_CMD_LX1:case GE_CMD_LY1:case GE_CMD_LZ1:
	case GE_CMD_LX2:case GE_CMD_LY2:case GE_CMD_LZ2:
	case GE_CMD_LX3:case GE_CMD_LY3:case GE_CMD_LZ3:
		{
			int n = cmd - GE_CMD_LX0;
			int l = n / 3;
			int c = n % 3;
			float val = getFloat24(data);
			return QString("Light %1 %2 pos: %3").arg(l).arg(QChar('X')+c).arg(val);
		}
		break;

	case GE_CMD_LDX0:case GE_CMD_LDY0:case GE_CMD_LDZ0:
	case GE_CMD_LDX1:case GE_CMD_LDY1:case GE_CMD_LDZ1:
	case GE_CMD_LDX2:case GE_CMD_LDY2:case GE_CMD_LDZ2:
	case GE_CMD_LDX3:case GE_CMD_LDY3:case GE_CMD_LDZ3:
		{
			int n = cmd - GE_CMD_LDX0;
			int l = n / 3;
			int c = n % 3;
			float val = getFloat24(data);
			return QString("Light %1 %2 dir: %3").arg(l).arg(QChar('X')+c).arg(val);
		}
		break;

	case GE_CMD_LKA0:case GE_CMD_LKB0:case GE_CMD_LKC0:
	case GE_CMD_LKA1:case GE_CMD_LKB1:case GE_CMD_LKC1:
	case GE_CMD_LKA2:case GE_CMD_LKB2:case GE_CMD_LKC2:
	case GE_CMD_LKA3:case GE_CMD_LKB3:case GE_CMD_LKC3:
		{
			int n = cmd - GE_CMD_LKA0;
			int l = n / 3;
			int c = n % 3;
			float val = getFloat24(data);
			return QString("Light %1 %2 att: %3").arg(l).arg(QChar('X')+c).arg(val);
		}
		break;

	case GE_CMD_LAC0:case GE_CMD_LAC1:case GE_CMD_LAC2:case GE_CMD_LAC3:
	case GE_CMD_LDC0:case GE_CMD_LDC1:case GE_CMD_LDC2:case GE_CMD_LDC3:
	case GE_CMD_LSC0:case GE_CMD_LSC1:case GE_CMD_LSC2:case GE_CMD_LSC3:
		{
			float r = (float)(data & 0xff)/255.0f;
			float g = (float)((data>>8) & 0xff)/255.0f;
			float b = (float)(data>>16)/255.0f;

			int l = (cmd - GE_CMD_LAC0) / 3;
			int t = (cmd - GE_CMD_LAC0) % 3;
			return QString("Light %1 color %2: %3 %4 %5").arg(l).arg(t).arg(r).arg(g).arg(b);
		}
		break;

	case GE_CMD_VIEWPORTX1:
	case GE_CMD_VIEWPORTY1:
	case GE_CMD_VIEWPORTX2:
	case GE_CMD_VIEWPORTY2:
		return QString("Viewport param %1: %2").arg(cmd-GE_CMD_VIEWPORTX1).arg(getFloat24(data));
		break;
	case GE_CMD_VIEWPORTZ1:
		{
			float zScale = getFloat24(data) / 65535.f;
			return QString("Viewport Z scale: %1").arg(zScale);
		}
		break;
	case GE_CMD_VIEWPORTZ2:
		{
			float zOff = getFloat24(data) / 65535.f;
			return QString("Viewport Z pos: %1").arg(zOff);
		}
		break;

	case GE_CMD_LIGHTENABLE0:
	case GE_CMD_LIGHTENABLE1:
	case GE_CMD_LIGHTENABLE2:
	case GE_CMD_LIGHTENABLE3:
		return QString("Light %1 enable: %2").arg(cmd-GE_CMD_LIGHTENABLE0).arg(data);
		break;

	case GE_CMD_CULL:
	{
		const char* cull[2] =
		{
			"Clockwise visible",
			"Counter-clockwise visible"
		};
		return QString("cull: %1").arg(cull[data & 0x1]);
		break;
	}
	case GE_CMD_PATCHDIVISION:
		{
			int patch_div_s = data & 0xFF;
			int patch_div_t = (data >> 8) & 0xFF;
			return QString("Patch subdivision: S=%1 x T=%2").arg(patch_div_s).arg(patch_div_t);
		}
		break;

	case GE_CMD_PATCHPRIMITIVE:
	{
		const char* type[3] =
		{
			"Triangles",
			"Lines",
			"Points"
		};
		return QString("Patch Primitive: %1").arg(type[data]);
		break;
	}
	case GE_CMD_PATCHFACING:
	{
		const char* val[2] =
		{
			"Clockwise",
			"Counter-Clockwise"
		};
		return QString( "Patch Facing: %1").arg(val[data]);
		break;
	}
	case GE_CMD_REVERSENORMAL:
		return QString("Reverse normal: %1").arg(data);
		break;

	case GE_CMD_MATERIALUPDATE:
	{
		QString txt = "";
		if(data & 1) txt += " Ambient";
		if(data & 2) txt += " Diffuse";
		if(data & 4) txt += " Specular";
		return QString("Material Update: %1").arg(txt);
		break;
	}

	//////////////////////////////////////////////////////////////////
	//	CLEARING
	//////////////////////////////////////////////////////////////////
	case GE_CMD_CLEARMODE:
	{
		// If it becomes a performance problem, check diff&1
		const char* clearMode[8] =
		{
			"",
			"Clear Color Buffer",
			"Clear Stencil/Alpha Buffer",
			"",
			"Clear Depth Buffer",
			"",
			"",
			""
		};
		return QString("Clear mode: %1, enabled : %2").arg(clearMode[(data >> 8) & 0xF]).arg(data & 0x1);
		break;
	}

	//////////////////////////////////////////////////////////////////
	//	ALPHA BLENDING
	//////////////////////////////////////////////////////////////////
	case GE_CMD_ALPHABLENDENABLE:
		return QString("Alpha blend enable: %1").arg(data);
		break;

	case GE_CMD_BLENDMODE:
	{
		const char* func[9] =
		{
			"Source Color",
			"One Minus Source Color",
			"Source Alpha",
			"One Minus Source Alpha",
			"Destination Color",
			"One Minus Destination Color",
			"Destination Alpha",
			"One Minus Destination Alpha",
			"Fix"
		};
		const char* op[6] =
		{
			"Add",
			"Subtract",
			"Reverse Subtract",
			"Minimum Value",
			"Maximum Value",
			"Absolute Value"
		};
		return QString("Blend mode: Src : %1, Dest : %2, Op : %3").arg(func[(data >> 4) & 0xF]).arg(func[(data >> 8) & 0xF]).arg(op[(data) & 0x7]);
		break;
	}
	case GE_CMD_BLENDFIXEDA:
		return QString("Blend fix A: %1").arg(data,6,16,QChar('0'));
		break;

	case GE_CMD_BLENDFIXEDB:
		return QString("Blend fix B: %1").arg(data,6,16,QChar('0'));
		break;

	case GE_CMD_ALPHATESTENABLE:
		return QString("Alpha test enable: %1").arg(data);
		break;

	case GE_CMD_ALPHATEST:
	{
		const char* testFunc[8] =
		{
			"Never pass pixel",
			"Always pass pixel",
			"Pass pixel if match",
			"Pass pixel if difference",
			"Pass pixel if less",
			"Pass pixel if less or equal",
			"Pass pixel if greater",
			"Pass pixel if greater or equal"
		};
		return QString("Alpha test settings, Mask : %1, Ref : %2, Test : %3").arg((data >> 8) & 0xFF).arg((data >> 16) & 0xFF).arg(testFunc[data & 0x7]);
		break;
	}
	case GE_CMD_ANTIALIASENABLE:
		return QString("Antialias enable: %1").arg(data);
		break;

	case GE_CMD_PATCHCULLENABLE:
		return QString("Antialias enable: %1").arg(data);
		break;

	case GE_CMD_COLORTESTENABLE:
	{
		const char* colorTest[4] =
		{
			"Never pass pixel",
			"Always pass pixel",
			"Pass pixel if color matches",
			"Pass pixel if color differs"
		};
		return QString("Color Test enable: %1").arg(colorTest[data]);
		break;
	}
	case GE_CMD_LOGICOPENABLE:
	{
		const char* logicOp[16] =
		{
			"Clear",
			"And",
			"Reverse And",
			"Copy",
			"Inverted And",
			"No Operation",
			"Exclusive Or",
			"Or",
			"Negated Or",
			"Equivalence",
			"Inverted",
			"Reverse Or",
			"Inverted Copy",
			"Inverted Or",
			"Negated And",
			"Set"
		};
		return QString("Logic op enable: %1").arg(logicOp[data]);
		break;
	}
	case GE_CMD_TEXFUNC:
	{
		const char* effect[8] =
		{
			"Modulate",
			"Decal",
			"Blend",
			"Replace",
			"Add",
			"","",""
		};
		return QString("TexFunc %1 / %2 / %3").arg((data&0x100)?"Texture alpha is read":"Texture alpha is ignored").arg((data & 0x10000)?"Fragment color is doubled":"Fragment color is untouched").arg(effect[data & 0x7]);
		break;
	}
	case GE_CMD_TEXFILTER:
		{
		const char* filter[8]=
		{
			"Nearest",
			"Linear",
			"",
			"",
			"Nearest; Mipmap Nearest",
			"Linear; Mipmap Nearest",
			"Nearest; Mipmap Linear",
			"Linear; Mipmap Linear"
		};
			int min = data & 0x7;
			int mag = (data >> 8) & 0x7;
			return QString("TexFilter min: %1 mag: %2").arg(filter[min]).arg(filter[mag]);
		}
		break;

	case GE_CMD_TEXENVCOLOR:
		return QString("TexEnvColor %1").arg(data,6,16,QChar('0'));
		break;

	case GE_CMD_TEXMODE:
	{
		u32 maxMipMap = (data >> 16) & 0xF;
		return QString("TexMode MaxmipMap : %1, Swizzle : %2").arg(maxMipMap).arg(data & 0x1);
		break;
	}
	case GE_CMD_TEXFORMAT:
	{
		const char* texFmt[11] =
		{
			"16-bit BGR 5650",
			"16-bit ABGR 5551",
			"16-bit ABGR 4444",
			"32-bit ABGR 8888",
			"4-bit indexed",
			"8-bit indexed",
			"16-bit indexed",
			"32-bit indexed",
			"DXT1",
			"DXT3",
			"DXT5"
		};
		return QString("TexFormat %1").arg(texFmt[data]);
		break;
	}
	case GE_CMD_TEXFLUSH:
		return QString("TexFlush");
		break;

	case GE_CMD_TEXSYNC:
		return QString("TexSync");
		break;

	case GE_CMD_TEXWRAP:
	{
		const char* wrapMode[2] =
		{
			"Repeat",
			"Clamp"
		};
		return QString("TexWrap U : %1, V : %2").arg(wrapMode[data & 0x1]).arg(wrapMode[(data >> 8) & 0x1]);
		break;
	}
	case GE_CMD_TEXLEVEL:
		return QString("TexWrap Mode: %1 Offset: %2").arg(data&3).arg(data >> 16);
		break;

	case GE_CMD_FOG1:
		return QString("Fog1 %1").arg(getFloat24(data));
		break;

	case GE_CMD_FOG2:
		return QString( "Fog2 %1").arg(getFloat24(data));
		break;

	case GE_CMD_FOGCOLOR:
		return QString("FogColor %1").arg(data,6,16,QChar('0'));
		break;

	case GE_CMD_TEXLODSLOPE:
		return QString( "TexLodSlope %1").arg(data,6,16,QChar('0'));
		break;

	//////////////////////////////////////////////////////////////////
	//	Z/STENCIL TESTING
	//////////////////////////////////////////////////////////////////

	case GE_CMD_ZTESTENABLE:
		return QString( "Z test enable: %1").arg(data&1);
		break;

	case GE_CMD_STENCILOP:
	{
		const char* stencilOp[8] =
		{
			"Keep stencil value",
			"Zero stencil value",
			"Replace stencil value",
			"Invert stencil value",
			"Increment stencil value",
			"Decrement stencil value",
			"",""
		};
		return QString("Stencil op: ZFail : %1, Fail : %2, Pass : %3").arg(stencilOp[(data >> 16) & 0x7]).arg(stencilOp[(data >> 8) & 0x7]).arg(stencilOp[(data) & 0x7]);
		break;
	}
	case GE_CMD_STENCILTEST:
	{
		const char* testFunc[8] =
		{
			"Never pass stencil pixel",
			"Always pass stencil pixel",
			"Pass test if match",
			"Pass test if difference",
			"Pass test if less",
			"Pass test if less or equal",
			"Pass test if greater",
			"Pass test if greater or equal"
		};
		return QString("Stencil test, Mask : %1, Ref : %2, Test : %3").arg((data >> 8) & 0xFF).arg((data >> 16) & 0xFF).arg(testFunc[data & 0x7]);
		break;
	}

	case GE_CMD_STENCILTESTENABLE:
		return QString("Stencil test enable: %1").arg(data);
		break;

	case GE_CMD_ZTEST:
	{
		const char* testFunc[8] =
		{
			"Never pass stencil pixel",
			"Always pass stencil pixel",
			"Pass pixel if match",
			"Pass pixel if difference",
			"Pass pixel if less",
			"Pass pixel if less or equal",
			"Pass pixel if greater",
			"Pass pixel if greater or equal"
		};
		return QString("Z test mode: %1").arg(testFunc[data & 0x7]);
		break;
	}
	case GE_CMD_MORPHWEIGHT0:
	case GE_CMD_MORPHWEIGHT1:
	case GE_CMD_MORPHWEIGHT2:
	case GE_CMD_MORPHWEIGHT3:
	case GE_CMD_MORPHWEIGHT4:
	case GE_CMD_MORPHWEIGHT5:
	case GE_CMD_MORPHWEIGHT6:
	case GE_CMD_MORPHWEIGHT7:
		{
			int index = cmd - GE_CMD_MORPHWEIGHT0;
			float weight = getFloat24(data);
			return QString("MorphWeight %1 = %2").arg(index).arg(weight);
		}
		break;

	case GE_CMD_DITH0:
	case GE_CMD_DITH1:
	case GE_CMD_DITH2:
	case GE_CMD_DITH3:
		return QString("DitherMatrix %1 = %2").arg(cmd-GE_CMD_DITH0).arg(data,6,16,QChar('0'));
		break;

	case GE_CMD_LOGICOP:
		return QString("LogicOp: %1").arg(data,6,16,QChar('0'));
		break;

	case GE_CMD_ZWRITEDISABLE:
		return QString("ZMask: %1").arg(data,6,16,QChar('0'));
		break;

	case GE_CMD_COLORTEST:
		return QString("ColorTest: %1").arg(data,6,16,QChar('0'));
		break;

	case GE_CMD_COLORREF:
		return QString("ColorRef: %1").arg(data,6,16,QChar('0'));
		break;

	case GE_CMD_COLORTESTMASK:
		return QString( "ColorTestMask: %1").arg(data,6,16,QChar('0'));
		break;

	case GE_CMD_MASKRGB:
		return QString("MaskRGB: %1").arg(data,6,16,QChar('0'));
		break;

	case GE_CMD_MASKALPHA:
		return QString("MaskAlpha: %1").arg(data,6,16,QChar('0'));
		break;

	case GE_CMD_WORLDMATRIXNUMBER:
		return QString("World # %1").arg(data & 0xF);
		break;

	case GE_CMD_WORLDMATRIXDATA:
		return QString("World data # %1").arg(getFloat24(data));
		break;

	case GE_CMD_VIEWMATRIXNUMBER:
		return QString("VIEW # %1").arg(data & 0xF);
		break;

	case GE_CMD_VIEWMATRIXDATA:
		return QString("VIEW data # %1").arg(getFloat24(data));
		break;

	case GE_CMD_PROJMATRIXNUMBER:
		return QString("PROJECTION # %1").arg(data & 0xF);
		break;

	case GE_CMD_PROJMATRIXDATA:
		return QString("PROJECTION matrix data # %1").arg(getFloat24(data));
		break;

	case GE_CMD_TGENMATRIXNUMBER:
		return QString("TGEN # %1").arg(data & 0xF);
		break;

	case GE_CMD_TGENMATRIXDATA:
		return QString("TGEN data # %1").arg(getFloat24(data));
		break;

	case GE_CMD_BONEMATRIXNUMBER:
		return QString("BONE #%1").arg(data);
		break;

	case GE_CMD_BONEMATRIXDATA:
		return QString("BONE data #%1 %2").arg(state.boneMatrixNumber & 0x7f).arg(getFloat24(data));
		break;

	default:
		return QString("Unknown: %1").arg(op,8,16,QChar('0'));
		break;
	}
}


void Debugger_DisplayList::FillDisplayListCmd(std::map<int,DListLine>& data, u32 pc, u32 prevAddr, GPUgstate& state)
{
	u32 curPc = pc;
	int debugLimit = 10000; // Anti crash if this code is bugged
	while(Memory::IsValidAddress(curPc) && debugLimit > 0)
	{
		if(data.find(curPc) != data.end())
			return;

		u32 op = Memory::ReadUnchecked_U32(curPc); //read from memory
		u32 cmd = op >> 24;
		u32 data_ = op & 0xFFFFFF;
		u32 diff = op ^ gstate.cmdmem[cmd];
		state.cmdmem[cmd] = op;
		u32 prevOp = 0;
		if(Memory::IsValidAddress(prevAddr))
			Memory::ReadUnchecked_U32(prevAddr);
		data[curPc].comment = DisassembleOp(curPc, op, prevOp, state);
		data[curPc].addr = curPc;
		data[curPc].cmd = cmd;
		data[curPc].data = data_;
		data[curPc].implementationNotFinished = false;
		data[curPc].texAddr = (gstate.texaddr[0] & 0xFFFFF0) | ((gstate.texbufwidth[0]<<8) & 0x0F000000);
		data[curPc].fboAddr = state.fbptr & 0xFFFFFF;
		u32 baseExtended = ((state.base & 0x0F0000) << 8) | (state.vaddr & 0xFFFFFF);
		data[curPc].vtxAddr = ((state.offsetAddr & 0xFFFFFF) + baseExtended) & 0x0FFFFFFF;
		baseExtended = ((state.base & 0x0F0000) << 8) | (state.iaddr & 0xFFFFFF);
		data[curPc].idxAddr = ((state.offsetAddr & 0xFFFFFF) + baseExtended) & 0x0FFFFFFF;
		// Add or remove bugged functions for highlight
		if(cmd == GE_CMD_BEZIER ||
				cmd == GE_CMD_SPLINE ||
				cmd == GE_CMD_BJUMP ||
				cmd == GE_CMD_BOUNDINGBOX)
		{
			data[curPc].implementationNotFinished = true;
		}

		// We are drawing, save the GPU state for texture, vertex and index list
		if(cmd == GE_CMD_PRIM || cmd == GE_CMD_BEZIER || cmd == GE_CMD_SPLINE)
		{
			drawGPUState.push_back(state);
		}

		if(cmd == GE_CMD_JUMP)
		{
			u32 baseExtended = ((state.base & 0x0F0000) << 8) | (data_ & 0xFFFFFF);
			u32 target = (((state.offsetAddr & 0xFFFFFF) << 8) + baseExtended) & 0x0FFFFFFF;
			FillDisplayListCmd(data, target, prevAddr, state);
			return;
		}
		else if(cmd == GE_CMD_CALL)
		{
			u32 baseExtended = ((state.base & 0x0F0000) << 8) | (data_ & 0xFFFFFF);
			u32 target = (((state.offsetAddr & 0xFFFFFF) << 8) + baseExtended) & 0x0FFFFFFF;
			FillDisplayListCmd(data, target, prevAddr, state);
		}
		else if(cmd == GE_CMD_RET)
		{
			return;
		}
		else if(cmd == GE_CMD_FINISH)
		{
			return;
		}
		else if(cmd == GE_CMD_END)
		{
			if(prevOp >> 24 == GE_CMD_FINISH)
				return;
		}
		prevAddr = curPc;
		curPc += 4;
		debugLimit--;
	}
}

void Debugger_DisplayList::Update()
{
	if(!isVisible())
		return;
	UpdateRenderBuffer();
	UpdateRenderBufferList();
	UpdateDisplayList();
}


void Debugger_DisplayList::on_displayList_itemClicked(QTreeWidgetItem *item, int column)
{
	displayListRowSelected = item;
	ShowDLCode();
}

void Debugger_DisplayList::on_stepBtn_clicked()
{
	host->SetGPUStep(true);
	host->NextGPUStep();
}

void Debugger_DisplayList::on_runBtn_clicked()
{
	ui->displayList->clear();
	ui->displayListData->clear();
	host->SetGPUStep(false);
	host->NextGPUStep();
}

void Debugger_DisplayList::on_stopBtn_clicked()
{
	host->SetGPUStep(true);
}

void Debugger_DisplayList::UpdateRenderBuffer()
{
	emit updateRenderBuffer_();
}

void Debugger_DisplayList::UpdateRenderBufferGUI()
{
	EmuThread_LockDraw(true);

	gpu->Flush();

	int FRAME_WIDTH;
	int FRAME_HEIGHT;
	u8 *data = 0;
	int curTex;
	glGetIntegerv(GL_TEXTURE_BINDING_2D, &curTex);
	if(currentTextureDisplay == 0)
	{
		FRAME_WIDTH = pixel_xres;
		FRAME_HEIGHT = pixel_yres;
		data = new u8[FRAME_WIDTH * FRAME_HEIGHT * 4];
		memset(data,0,FRAME_WIDTH * FRAME_HEIGHT * 4);
		if(currentRenderFrameDisplay == 0)
		{
			glReadBuffer(GL_COLOR_ATTACHMENT0);
			glReadPixels(0, 0, FRAME_WIDTH, FRAME_HEIGHT, GL_BGRA, GL_UNSIGNED_BYTE, data);
		}
		else
		{
			glReadBuffer(GL_DEPTH_ATTACHMENT);
			glReadPixels(0, 0, FRAME_WIDTH, FRAME_HEIGHT, GL_DEPTH_COMPONENT, GL_FLOAT, data);
		}
	}
	else
	{
		fbo_get_dimensions(currentTextureDisplay, &FRAME_WIDTH, &FRAME_HEIGHT);
		data = new u8[FRAME_WIDTH * FRAME_HEIGHT * 4];
		memset(data,0,FRAME_WIDTH * FRAME_HEIGHT * 4);
		if(currentRenderFrameDisplay == 0)
		{
			fbo_bind_color_as_texture(currentTextureDisplay,0);
			glGetTexImage(GL_TEXTURE_2D, 0, GL_BGRA, GL_UNSIGNED_BYTE, data);
		}
	}
	glBindTexture(GL_TEXTURE_2D, curTex);

	QImage img = QImage(data, FRAME_WIDTH, FRAME_HEIGHT, FRAME_WIDTH*4, QImage::Format_ARGB32).mirrored(false,true);
	QPixmap pixmap = QPixmap::fromImage(img);
	ui->fboImg->setPixmap(pixmap);
	ui->fboImg->setMinimumWidth(pixmap.width() * fboZoomFactor);
	ui->fboImg->setMinimumHeight(pixmap.height() * fboZoomFactor);
	ui->fboImg->setMaximumWidth(pixmap.width() * fboZoomFactor);
	ui->fboImg->setMaximumHeight(pixmap.height() * fboZoomFactor);

	delete[] data;

	EmuThread_LockDraw(false);
}

void Debugger_DisplayList::on_nextDrawBtn_clicked()
{
	host->SetGPUStep(true, 1);
	host->NextGPUStep();
}

void Debugger_DisplayList::on_gotoPCBtn_clicked()
{
	if(!displayListRowSelected)
		return;
	u32 currentPC = displayListRowSelected->data(3, Qt::UserRole).toInt();

	for(int i = 0; i < ui->displayListData->topLevelItemCount(); i++)
	{
		if((u32)ui->displayListData->topLevelItem(i)->data(0, Qt::UserRole).toInt() == currentPC)
		{
			ui->displayListData->setCurrentItem(ui->displayListData->topLevelItem(i));
		}
	}
}

void Debugger_DisplayList::on_texturesList_itemDoubleClicked(QTreeWidgetItem *item, int column)
{
	mainWindow->GetDialogMemoryTex()->ShowTex(drawGPUState[item->data(0,Qt::UserRole).toInt()]);
}

void Debugger_DisplayList::on_comboBox_currentIndexChanged(int index)
{
	currentRenderFrameDisplay = index;
	UpdateRenderBufferGUI();
}

void Debugger_DisplayList::UpdateRenderBufferList()
{
	emit updateRenderBufferList_();
}

void Debugger_DisplayList::UpdateRenderBufferListGUI()
{
	ui->fboList->clear();

	QTreeWidgetItem* item = new QTreeWidgetItem();
	item->setText(0,"Framebuffer");
	item->setData(0,Qt::UserRole, 0);
	item->setText(1,QString::number(pixel_xres));
	item->setText(2,QString::number(pixel_yres));
	item->setText(3,QString::number(4));
	ui->fboList->addTopLevelItem(item);

	std::vector<FramebufferInfo> fboList = gpu->GetFramebufferList();

	for(size_t i = 0; i < fboList.size(); i++)
	{
		QTreeWidgetItem* item = new QTreeWidgetItem();
		item->setText(0,QString("%1").arg(fboList[i].fb_address,8,16,QChar('0')));
		u64 addr = (u64)fboList[i].fbo;
		item->setData(0,Qt::UserRole, addr);
		item->setData(0,Qt::UserRole+1, fboList[i].fb_address);
		item->setText(1,QString::number(fboList[i].width));
		item->setText(2,QString::number(fboList[i].height));
		item->setText(3,QString::number(fboList[i].format));

		ui->fboList->addTopLevelItem(item);
	}
}

void Debugger_DisplayList::on_fboList_itemClicked(QTreeWidgetItem *item, int column)
{
	u64 addr = item->data(0,Qt::UserRole).toULongLong();
	FBO* fbo = (FBO*)addr;
	currentTextureDisplay = fbo;
	UpdateRenderBufferGUI();
}

void Debugger_DisplayList::on_nextDLBtn_clicked()
{
	host->SetGPUStep(true,-1);
	host->NextGPUStep();
}

void Debugger_DisplayList::setCurrentFBO(u32 addr)
{
	for(int i = 0; i < ui->fboList->topLevelItemCount(); i++)
	{
		if((u32)ui->fboList->topLevelItem(i)->data(0,Qt::UserRole+1).toInt() == addr)
		{
			for(int j = 0; j < ui->fboList->colorCount(); j++)
				ui->fboList->topLevelItem(i)->setTextColor(j,Qt::green);
		}
		else
		{
			for(int j = 0; j < ui->fboList->colorCount(); j++)
				ui->fboList->topLevelItem(i)->setTextColor(j,Qt::black);
		}
	}
}

void Debugger_DisplayList::on_zoommBtn_clicked()
{
	fboZoomFactor *= 0.5;
	ui->fboImg->setMinimumWidth(ui->fboImg->minimumWidth()*0.5);
	ui->fboImg->setMinimumHeight(ui->fboImg->minimumHeight()*0.5);
	ui->fboImg->setMaximumWidth(ui->fboImg->minimumWidth()*0.5);
	ui->fboImg->setMaximumHeight(ui->fboImg->minimumHeight()*0.5);
}

void Debugger_DisplayList::on_zoompBtn_clicked()
{
	fboZoomFactor *= 2;
	ui->fboImg->setMinimumWidth(ui->fboImg->minimumWidth()*2);
	ui->fboImg->setMinimumHeight(ui->fboImg->minimumHeight()*2);
	ui->fboImg->setMaximumWidth(ui->fboImg->minimumWidth()*2);
	ui->fboImg->setMaximumHeight(ui->fboImg->minimumHeight()*2);
}

void Debugger_DisplayList::UpdateVertexInfo()
{
	ui->vertexData->clear();

	QTreeWidgetItem* item = ui->vertexList->currentItem();
	if(item == 0)
		return;

	GPUgstate state = drawGPUState[item->data(0,Qt::UserRole).toInt()];
	u32 baseExtended = ((state.base & 0x0F0000) << 8) | (state.vaddr & 0xFFFFFF);
	u32 vaddr = ((state.offsetAddr & 0xFFFFFF) + baseExtended) & 0x0FFFFFFF;

	VertexDecoder vtcDec;
	vtcDec.SetVertexType(state.vertType);
	u8* tmp = new u8[20*vtcDec.GetDecVtxFmt().stride];
	vtcDec.DecodeVerts(tmp,Memory::GetPointer(vaddr),0,19);
	VertexReader vtxRead(tmp,vtcDec.GetDecVtxFmt(),state.vertType);

	for(int i = 0; i < maxVtxDisplay; i++)
	{
		vtxRead.Goto(i);
		QTreeWidgetItem* itemTop = new QTreeWidgetItem();
		itemTop->setText(0,QString::number(i));
		itemTop->setText(1,QString("%1").arg(vaddr+i*vtcDec.GetDecVtxFmt().stride,8,16,QChar('0')));
		ui->vertexData->addTopLevelItem(itemTop);

		if (vtxRead.hasNormal())
		{
			float nrm[3];
			vtxRead.ReadNrm(nrm);
			QTreeWidgetItem* item = new QTreeWidgetItem();
			item->setText(1,"Normal");
			item->setText(2,QString("X: %1, Y: %2, Z: %3").arg(nrm[0]).arg(nrm[1]).arg(nrm[2]));
			itemTop->addChild(item);
		}
		if (vtxRead.hasUV()) {
			float uv[2];
			vtxRead.ReadUV(uv);
			QTreeWidgetItem* item = new QTreeWidgetItem();
			item->setText(1,"Uv");
			item->setText(2,QString("X: %1, Y: %2").arg(uv[0]).arg(uv[1]));
			itemTop->addChild(item);
		}
		if (vtxRead.hasColor0()) {
			float col0[4];
			vtxRead.ReadColor0(col0);
			QTreeWidgetItem* item = new QTreeWidgetItem();
			item->setText(1,"Color0");
			item->setText(2,QString("X: %1, Y: %2, Z: %3").arg(col0[0]).arg(col0[1]).arg(col0[2]));
			itemTop->addChild(item);
		}
		if (vtxRead.hasColor0()) {
			float col1[3];
			vtxRead.ReadColor1(col1);
			QTreeWidgetItem* item = new QTreeWidgetItem();
			item->setText(1,"Color1");
			item->setText(2,QString("X: %1, Y: %2, Z: %3").arg(col1[0]).arg(col1[1]).arg(col1[2]));
			itemTop->addChild(item);
		}
		float pos[3];
		vtxRead.ReadPos(pos);
		QTreeWidgetItem* item = new QTreeWidgetItem();
		item->setText(1,"Position");
		item->setText(2,QString("X: %1, Y: %2, Z: %3").arg(pos[0]).arg(pos[1]).arg(pos[2]));
		itemTop->addChild(item);
	}
	delete [] tmp;
	for(int i = 0; i < ui->vertexData->columnCount(); i++)
	{
		ui->vertexData->resizeColumnToContents(i);
	}
}

void Debugger_DisplayList::on_vertexList_itemClicked(QTreeWidgetItem *item, int column)
{
	UpdateVertexInfo();
}

void Debugger_DisplayList::on_pushButton_clicked()
{
	maxVtxDisplay += 20;
	UpdateVertexInfo();
}


void Debugger_DisplayList::UpdateIndexInfo()
{
	ui->indexData->clear();

	QTreeWidgetItem* item = ui->indexList->currentItem();
	if(item == 0)
		return;

	GPUgstate state = drawGPUState[item->data(0,Qt::UserRole).toInt()];
	u32 baseExtended = ((state.base & 0x0F0000) << 8) | (state.iaddr & 0xFFFFFF);
	u32 iaddr = ((state.offsetAddr & 0xFFFFFF) + baseExtended) & 0x0FFFFFFF;

	int sizeIdx = 1;
	if((state.vertType & GE_VTYPE_IDX_MASK) == GE_VTYPE_IDX_16BIT)
		sizeIdx = 2;

	for(int i = 0; i < maxIdxDisplay; i++)
	{
		QTreeWidgetItem* itemTop = new QTreeWidgetItem();
		itemTop->setText(0,QString::number(i));
		itemTop->setText(1,QString("%1").arg(iaddr+i*sizeIdx,8,16,QChar('0')));
		int idx = 0;
		if(sizeIdx == 1)
			idx = Memory::Read_U8(iaddr+i*sizeIdx);
		else
			idx = Memory::Read_U16(iaddr+i*sizeIdx);
		itemTop->setText(2,QString::number(idx));
		ui->indexData->addTopLevelItem(itemTop);
	}
	for(int i = 0; i < ui->indexData->columnCount(); i++)
	{
		ui->indexData->resizeColumnToContents(i);
	}
}


void Debugger_DisplayList::on_nextIdx_clicked()
{
	maxIdxDisplay += 20;
	UpdateIndexInfo();
}

void Debugger_DisplayList::on_indexList_itemClicked(QTreeWidgetItem *item, int column)
{
	UpdateIndexInfo();
}

void Debugger_DisplayList::on_displayListData_customContextMenuRequested(const QPoint &pos)
{
	QTreeWidgetItem* item = ui->displayListData->itemAt(pos);
	if(!item)
		return;
	displayListDataSelected = item;

	QMenu menu(this);

	QAction *runToHere = new QAction(tr("Run to here"), this);
	connect(runToHere, SIGNAL(triggered()), this, SLOT(RunToDLPC()));
	menu.addAction(runToHere);

	menu.exec( ui->displayListData->mapToGlobal(pos));
}

void Debugger_DisplayList::RunToDLPC()
{
	u32 addr = displayListDataSelected->text(0).toUInt(0,16);
	host->SetGPUStep(true, 2, addr);
	host->NextGPUStep();
}

void Debugger_DisplayList::on_texturesList_customContextMenuRequested(const QPoint &pos)
{
	QTreeWidgetItem* item = ui->texturesList->itemAt(pos);
	if(!item)
		return;
	textureDataSelected = item;

	QMenu menu(this);

	QAction *runToDraw = new QAction(tr("Run to draw using this texture"), this);
	connect(runToDraw, SIGNAL(triggered()), this, SLOT(RunToDrawTex()));
	menu.addAction(runToDraw);

	menu.exec( ui->texturesList->mapToGlobal(pos));
}

void Debugger_DisplayList::RunToDrawTex()
{
	u32 addr = textureDataSelected->text(0).toUInt(0,16);
	host->SetGPUStep(true, 3, addr);
	host->NextGPUStep();
}
