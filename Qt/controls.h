#ifndef CONTROLS_H
#define CONTROLS_H

#include <QDialog>
#include "native/input/input_state.h"
#include "Core/HLE/sceCtrl.h"

namespace Ui {
class Controls;
}

struct Controls_
{
public:
	QString editName;
	QString command;
	Qt::Key key;
	int emu_id;
	int psp_id;
};

const int controllistCount = 16;
extern Controls_ controllist[];

class Controls : public QDialog
{
	Q_OBJECT
	
public:
	explicit Controls(QWidget *parent = 0);
	~Controls();

	void showEvent(QShowEvent *);
	void changeEvent(QEvent *);
private slots:
	void releaseLock();
	void on_buttonBox_accepted();

private:
	Ui::Controls *ui;
};

#endif // CONTROLS_H
