#include "controls.h"
#include "ui_controls.h"
#include "Core/Config.h"
#include "EmuThread.h"
#include <QTimer>

Controls_ controllist[] = {
	{"Edit_Start",		"Start",			Qt::Key_1,		PAD_BUTTON_START,	CTRL_START},
	{"Edit_Select",		"Select",			Qt::Key_2,		PAD_BUTTON_SELECT,	CTRL_SELECT},
	{"Edit_S",			"Square",			Qt::Key_Z,		PAD_BUTTON_X,		CTRL_SQUARE},
	{"Edit_T",			"Triangle",			Qt::Key_A,		PAD_BUTTON_Y,		CTRL_TRIANGLE},
	{"Edit_O",			"Circle",			Qt::Key_S,		PAD_BUTTON_B,		CTRL_CIRCLE},
	{"Edit_X",			"Cross",			Qt::Key_X,		PAD_BUTTON_A,		CTRL_CROSS},
	{"Edit_LT",			"Left Trigger",		Qt::Key_Q,		PAD_BUTTON_LBUMPER,	CTRL_LTRIGGER},
	{"Edit_RT",			"Right Trigger",	Qt::Key_W,		PAD_BUTTON_RBUMPER,	CTRL_RTRIGGER},
	{"Edit_Up",			"Up",				Qt::Key_Up,		PAD_BUTTON_UP,		CTRL_UP},
	{"Edit_Down",		"Down",				Qt::Key_Down,	PAD_BUTTON_DOWN,	CTRL_DOWN},
	{"Edit_Left",		"Left",				Qt::Key_Left,	PAD_BUTTON_LEFT,	CTRL_LEFT},
	{"Edit_Right",		"Right",			Qt::Key_Right,	PAD_BUTTON_RIGHT,	CTRL_RIGHT},
	{"",				"Analog Up",		Qt::Key_I,		PAD_BUTTON_JOY_UP,	0},
	{"",				"Analog Down",		Qt::Key_K,		PAD_BUTTON_JOY_DOWN,0},
	{"",				"Analog Left",		Qt::Key_J,		PAD_BUTTON_JOY_LEFT,0},
	{"",				"Analog Right",		Qt::Key_L,		PAD_BUTTON_JOY_RIGHT,0},
};

Controls::Controls(QWidget *parent) :
	QDialog(parent),
	ui(new Ui::Controls)
{
	ui->setupUi(this);

	for(int i = 0; i < controllistCount; i++)
	{
		if(g_Config.iMappingMap.find(i) != g_Config.iMappingMap.end())
		{
			controllist[i].key = (Qt::Key)g_Config.iMappingMap[i];
		}
	}
}

Controls::~Controls()
{
	delete ui;
}

void Controls::showEvent(QShowEvent*)
{
#ifdef Q_WS_X11
	// Hack to remove the X11 crash with threaded opengl when opening the first dialog
	EmuThread_LockDraw(true);
	QTimer::singleShot(100, this, SLOT(releaseLock()));
#endif

	for(int i = 0; i < controllistCount; i++)
	{
		if(g_Config.iMappingMap.find(i) != g_Config.iMappingMap.end())
		{
			controllist[i].key = (Qt::Key)g_Config.iMappingMap[i];
		}

		if(controllist[i].editName != "")
		{
			QLineEdit* edit = findChild<QLineEdit*>(controllist[i].editName);
			if(edit)
			{
				QKeySequence sec(controllist[i].key);
				edit->setText(sec.toString());
			}
		}
	}
}

void Controls::changeEvent(QEvent *event)
{
	if (event)
		if (event->type() == QEvent::LanguageChange)
			ui->retranslateUi(this);

	QDialog::changeEvent(event);
}

void Controls::releaseLock()
{
	EmuThread_LockDraw(false);
}

void Controls::on_buttonBox_accepted()
{
	for(int i = 0; i < controllistCount; i++)
	{
		if(controllist[i].editName != "")
		{
			QLineEdit* edit = findChild<QLineEdit*>(controllist[i].editName);
			if(edit)
			{
				QKeySequence sec(edit->text());
				controllist[i].key = (Qt::Key)sec[0];
				g_Config.iMappingMap[i] = sec[0];
			}
		}
	}
}
