#ifndef DEBUGGER_DISPLAYLIST_H
#define DEBUGGER_DISPLAYLIST_H

#include "Core/Debugger/DebugInterface.h"
#include <QDialog>
#include <QTreeWidgetItem>
#include "GPU/GPUState.h"
#include "native/gfx_es2/fbo.h"

class MainWindow;
namespace Ui {
class Debugger_DisplayList;
}


class DListLine
{
public:
	u32 addr;
	u32 cmd;
	u32 data;
	QString comment;
	bool implementationNotFinished;
	u32 texAddr;
	u32 fboAddr;
	u32 vtxAddr;
	int vtxStart;
	int vtxCount;
	u32 idxAddr;
	int idxStart;
	int idxCount;
};

class Debugger_DisplayList : public QDialog
{
	Q_OBJECT
	
public:
	explicit Debugger_DisplayList(DebugInterface *_cpu, MainWindow *mainWindow_, QWidget *parent = 0);
	~Debugger_DisplayList();

	void UpdateDisplayList();

	void ShowDLCode();
	void FillDisplayListCmd(std::map<int,DListLine> &data, u32 pc, u32 prev, GPUgstate &state);
	void Update();
	void UpdateRenderBuffer();
	void UpdateRenderBufferList();
	void UpdateVertexInfo();
	void UpdateIndexInfo();
protected:
	void showEvent(QShowEvent *);

signals:
	void updateDisplayList_();
	void updateRenderBufferList_();
	void updateRenderBuffer_();

private slots:
	void UpdateDisplayListGUI();
	void UpdateRenderBufferListGUI();
	void UpdateRenderBufferGUI();
	void releaseLock();

	void on_displayList_itemClicked(QTreeWidgetItem *item, int column);
	void on_stepBtn_clicked();
	void on_runBtn_clicked();
	void on_stopBtn_clicked();
	void on_nextDrawBtn_clicked();
	void on_gotoPCBtn_clicked();
	void on_texturesList_itemDoubleClicked(QTreeWidgetItem *item, int column);
	void on_comboBox_currentIndexChanged(int index);
	void on_fboList_itemClicked(QTreeWidgetItem *item, int column);
	void on_nextDLBtn_clicked();
	void setCurrentFBO(u32 addr);
	void on_zoommBtn_clicked();
	void on_zoompBtn_clicked();
	void on_vertexList_itemClicked(QTreeWidgetItem *item, int column);
	void on_pushButton_clicked();
	void on_nextIdx_clicked();
	void on_indexList_itemClicked(QTreeWidgetItem *item, int column);
	void on_displayListData_customContextMenuRequested(const QPoint &pos);
	void on_texturesList_customContextMenuRequested(const QPoint &pos);
	void RunToDLPC();
	void RunToDrawTex();

private:
	QString DisassembleOp(u32 pc, u32 op, u32 prev, const GPUgstate &state);

	Ui::Debugger_DisplayList *ui;
	DebugInterface* cpu;
	MainWindow* mainWindow;
	QTreeWidgetItem* displayListRowSelected;
	QTreeWidgetItem* displayListDataSelected;
	QTreeWidgetItem* textureDataSelected;
	int currentRenderFrameDisplay;
	FBO* currentTextureDisplay;
	float fboZoomFactor;
	int maxVtxDisplay;
	int maxIdxDisplay;

	std::vector<GPUgstate> drawGPUState;
	std::map<u32, int> vtxBufferSize;
	std::map<u32, int> idxBufferSize;
};

#endif // DEBUGGER_DISPLAYLIST_H
