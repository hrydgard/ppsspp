#ifndef DEBUGGER_MEMORYTEX_H
#define DEBUGGER_MEMORYTEX_H

#include <QDialog>
#include "GPU/GPUState.h"

namespace Ui {
class Debugger_MemoryTex;
}

class Debugger_MemoryTex : public QDialog
{
	Q_OBJECT
	
public:
	explicit Debugger_MemoryTex(QWidget *parent = 0);
	~Debugger_MemoryTex();

	void ShowTex(const GPUgstate& state);
protected:
	void showEvent(QShowEvent *);
private slots:
	void releaseLock();
	void on_readBtn_clicked();

private:
	Ui::Debugger_MemoryTex *ui;
};

#endif // DEBUGGER_MEMORYTEX_H
