#ifndef GAMEPADDIALOG_H
#define GAMEPADDIALOG_H

#include <QDialog>
#if QT_HAS_SDL
#include "SDL/SDL.h"
#endif
#include "native/input/input_state.h"

namespace Ui {
class GamePadDialog;
}
class QTimer;

class GamePadDialog : public QDialog
{
	Q_OBJECT
	
public:
	explicit GamePadDialog(InputState* inputState, QWidget *parent = 0);
	~GamePadDialog();
	
	void SetViewMode();
protected:
	void showEvent(QShowEvent *);
	void changeEvent(QEvent *);
private slots:
	void releaseLock();
	void on_refreshListBtn_clicked();
	void on_SelectPadBtn_clicked();
	void pollJoystick();
	void on_AssignBtn_clicked();
	void on_buttonBox_accepted();
private:
	int GetIntFromMapping(int inputId, int type, int sign);
	void GetMappingFromInt(int value, int &inputId, int &type, int &sign);

	Ui::GamePadDialog *ui;
#if QT_HAS_SDL
	SDL_Joystick* m_joystick;
	int m_joyId;
#endif
	InputState* m_inputState;
	bool m_isInit;
	QTimer *data_timer;
};

#endif // GAMEPADDIALOG_H
