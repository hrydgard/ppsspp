#ifndef DEBUGGER_MEMORY_H
#define DEBUGGER_MEMORY_H

#include "Core/Debugger/DebugInterface.h"
#include <QDialog>
#include <QListWidgetItem>

class MainWindow;
namespace Ui {
class Debugger_Memory;
}

class Debugger_Memory : public QDialog
{
	Q_OBJECT
	
public:
	explicit Debugger_Memory(DebugInterface *_cpu, MainWindow* mainWindow_, QWidget *parent = 0);
	~Debugger_Memory();
	
	void Update();
	void Goto(u32 addr);

	void NotifyMapLoaded();
public slots:
	void releaseLock();
protected:
	void showEvent(QShowEvent *);
private slots:
	void on_editAddress_textChanged(const QString &arg1);

	void on_normalBtn_clicked();

	void on_symbolsBtn_clicked();

	void on_memView_customContextMenuRequested(const QPoint &pos);

	void on_regions_currentIndexChanged(int index);

	void on_symbols_itemClicked(QListWidgetItem *item);

private:
	Ui::Debugger_Memory *ui;
	DebugInterface* cpu;
	MainWindow* mainWindow;

};

#endif // DEBUGGER_MEMORY_H
