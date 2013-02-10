#ifndef DEBUGGER_VFPU_H
#define DEBUGGER_VFPU_H

#include "Core/Debugger/DebugInterface.h"
#include <QDialog>

class MainWindow;
namespace Ui {
class Debugger_VFPU;
}

class Debugger_VFPU : public QDialog
{
	Q_OBJECT
	
public:
	explicit Debugger_VFPU(DebugInterface *_cpu, MainWindow *mainWindow_, QWidget *parent = 0);
	~Debugger_VFPU();

	void Update();
	void Goto(u32 addr);
protected:
	void showEvent(QShowEvent *);
public slots:
	void releaseLock();
private slots:
	void on_comboBox_currentIndexChanged(int index);

private:
	Ui::Debugger_VFPU *ui;
	DebugInterface* cpu;
	MainWindow* mainWindow;
};

#endif // DEBUGGER_VFPU_H
