#pragma once

#include <queue>
#include <mutex>

#include <QtCore>
#include <QMenuBar>
#include <QMainWindow>
#include <QActionGroup>

#include "ConsoleListener.h"
#include "Core/Core.h"
#include "Core/Config.h"
#include "Core/System.h"
#include "Qt/QtMain.h"

extern bool g_TakeScreenshot;

class MenuAction;
class MenuTree;

// hacky, should probably use qt signals or something, but whatever..
enum class MainWindowMsg {
	BOOT_DONE,
	WINDOW_TITLE_CHANGED,
};

class MainWindow : public QMainWindow
{
	Q_OBJECT

public:
	explicit MainWindow(QWidget *parent = 0, bool fullscreen=false);
	~MainWindow() { };

	CoreState GetNextState() { return nextState; }

	void updateMenus();

	void Notify(MainWindowMsg msg) {
		std::unique_lock<std::mutex> lock(msgMutex_);
		msgQueue_.push(msg);
	}

	void SetWindowTitleAsync(std::string title) {
		std::unique_lock<std::mutex> lock(titleMutex_);
		newWindowTitle_ = title;
		Notify(MainWindowMsg::WINDOW_TITLE_CHANGED);
	}

protected:
	void changeEvent(QEvent *e)
	{
		QMainWindow::changeEvent(e);
		// Does not work on Linux for Qt5.2 or Qt5.3 (Qt bug)
		if(e->type() == QEvent::WindowStateChange)
			Core_NotifyWindowHidden(isMinimized());
	}

	void closeEvent(QCloseEvent *) { exitAct(); }

signals:
	void retranslate();
	void updateMenu();

public slots:
	void newFrame();

private slots:
	// File
	void openAct();
	void closeAct();
	void qlstateAct();
	void qsstateAct();
	void lstateAct();
	void sstateAct();
	void exitAct();

	// Emulation
	void runAct();
	void pauseAct();
	void resetAct();
	void runonloadAct();

	// Debug
	void lmapAct();
	void smapAct();
	void resetTableAct();
	void dumpNextAct();
	void takeScreen() { g_TakeScreenshot = true; }
	void consoleAct();

	// Options
	// Core
	void vertexDynarecAct() { g_Config.bVertexDecoderJit = !g_Config.bVertexDecoderJit; }
	void fastmemAct() { g_Config.bFastMemory = !g_Config.bFastMemory; }
	void ignoreIllegalAct() { g_Config.bIgnoreBadMemAccess = !g_Config.bIgnoreBadMemAccess; }

	// Video
	void anisotropicGroup_triggered(QAction *action) { g_Config.iAnisotropyLevel = action->data().toInt(); }

	void bufferRenderAct() { g_Config.iRenderingMode = !g_Config.iRenderingMode; }
	void linearAct() { g_Config.iTexFiltering = (g_Config.iTexFiltering != 0) ? 0 : 3; }

	void screenGroup_triggered(QAction *action) { SetWindowScale(action->data().toInt()); }

	void displayLayoutGroup_triggered(QAction *action) { g_Config.iSmallDisplayZoomType = action->data().toInt(); }
	void transformAct() { g_Config.bHardwareTransform = !g_Config.bHardwareTransform; }
	void vertexCacheAct() { g_Config.bVertexCache = !g_Config.bVertexCache; }
	void frameskipAct() { g_Config.iFrameSkip = !g_Config.iFrameSkip; }

	// Sound
	void audioAct() { g_Config.bEnableSound = !g_Config.bEnableSound; }

	void fullscrAct();
	void raiseTopMost();
	void statsAct() { g_Config.bShowDebugStats = !g_Config.bShowDebugStats; }
	void showFPSAct() { g_Config.iShowFPSCounter = !g_Config.iShowFPSCounter; }

	// Logs
	void defaultLogGroup_triggered(QAction * action) {
		LogTypes::LOG_LEVELS level = (LogTypes::LOG_LEVELS)action->data().toInt();
		for (int i = 0; i < LogTypes::NUMBER_OF_LOGS; i++)
		{
			LogTypes::LOG_TYPE type = (LogTypes::LOG_TYPE)i;
			if(type == LogTypes::G3D || type == LogTypes::HLE)
				continue;
			LogManager::GetInstance()->SetLogLevel(type, level);
		}
	 }
	void g3dLogGroup_triggered(QAction * action) { LogManager::GetInstance()->SetLogLevel(LogTypes::G3D, (LogTypes::LOG_LEVELS)action->data().toInt()); }
	void hleLogGroup_triggered(QAction * action) { LogManager::GetInstance()->SetLogLevel(LogTypes::HLE, (LogTypes::LOG_LEVELS)action->data().toInt()); }

	// Help
	void websiteAct();
	void forumAct();
	void gitAct();
	void aboutAct();

	// Others
	void langChanged(QAction *action) { loadLanguage(action->data().toString(), true); }

private:
	void bootDone();
	void SetWindowScale(int zoom);
	void SetGameTitle(QString text);
	void loadLanguage(const QString &language, bool retranslate);
	void createMenus();

	QTranslator translator;
	QString currentLanguage;

	CoreState nextState;
	GlobalUIState lastUIState;

	QActionGroup *anisotropicGroup, *screenGroup, *displayLayoutGroup,
	             *defaultLogGroup, *g3dLogGroup, *hleLogGroup;

	std::queue<MainWindowMsg> msgQueue_;
	std::mutex msgMutex_;

	std::string newWindowTitle_;
	std::mutex titleMutex_;
};

class MenuAction : public QAction
{
	Q_OBJECT

public:
	// Add to QMenu
	MenuAction(QWidget* parent, const char *callback, const char *text, QKeySequence key = 0) :
		QAction(parent), _text(text), _eventCheck(0), _stateEnable(-1), _stateDisable(-1), _enableStepping(false)
	{
		if (key != (QKeySequence)0) {
			this->setShortcut(key);
			parent->addAction(this); // So we don't lose the shortcut when menubar is hidden
		}
		connect(this, SIGNAL(triggered()), parent, callback);
		connect(parent, SIGNAL(retranslate()), this, SLOT(retranslate()));
		connect(parent, SIGNAL(updateMenu()), this, SLOT(update()));
	}
	// Add to QActionGroup
	MenuAction(QWidget* parent, QActionGroup* group, QVariant data, QString text, QKeySequence key = 0) :
		QAction(parent), _eventCheck(0), _stateEnable(-1), _stateDisable(-1), _enableStepping(false)
	{
		this->setCheckable(true);
		this->setData(data);
		this->setText(text); // Not translatable, yet
		if (key != (QKeySequence)0) {
			this->setShortcut(key);
			parent->addAction(this); // So we don't lose the shortcut when menubar is hidden
		}
		group->addAction(this);
	}
	// Event which causes it to be checked
	void addEventChecked(bool* event) {
		this->setCheckable(true);
		_eventCheck = event;
	}
	// TODO: Possibly handle compares
	void addEventChecked(int* event) {
		this->setCheckable(true);
		_eventCheck = (bool*)event;
	}
	// UI State which causes it to be enabled
	void addEnableState(int state) {
		_stateEnable = state;
	}
	void addDisableState(int state) {
		_stateDisable = state;
	}
	MenuAction* addEnableStepping() {
		_enableStepping = true;
		return this;
	}
public slots:
	void retranslate() {
		setText(qApp->translate("MainWindow", _text));
	}
	void update() {
		if (_eventCheck)
			setChecked(*_eventCheck);
		if (_stateEnable >= 0)
			setEnabled(GetUIState() == _stateEnable);
		if (_stateDisable >= 0)
			setEnabled(GetUIState() != _stateDisable);
		if (_enableStepping && Core_IsStepping())
			setEnabled(true);
	}
private:
	const char *_text;
	bool *_eventCheck;
	int _stateEnable, _stateDisable;
	bool _enableStepping;
};

class MenuActionGroup : public QActionGroup
{
	Q_OBJECT
public:
	MenuActionGroup(QWidget* parent, QMenu* menu, const char* callback, QStringList nameList,
		QList<int> valueList, QList<int> keyList = QList<int>()) :
		QActionGroup(parent)
	{
		QListIterator<int> i(valueList);
		QListIterator<int> k(keyList);
		foreach(QString name, nameList) {
			new MenuAction(parent, this, i.next(), name, keyList.size() ? k.next() : 0);
		}
		connect(this, SIGNAL(triggered(QAction *)), parent, callback);
		menu->addActions(this->actions());
	}
};

class MenuTree : public QMenu
{
	Q_OBJECT
public:
	MenuTree(QWidget* parent, QMenuBar* menu, const char *text) :
		QMenu(parent), _text(text)
	{
		menu->addMenu(this);
		connect(parent, SIGNAL(retranslate()), this, SLOT(retranslate()));
	}
	MenuTree(QWidget* parent, QMenu* menu, const char *text) :
		QMenu(parent), _text(text)
	{
		menu->addMenu(this);
		connect(parent, SIGNAL(retranslate()), this, SLOT(retranslate()));
	}
	MenuAction* add(MenuAction* action)
	{
		addAction(action);
		return action;
	}
public slots:
	void retranslate() {
		setTitle(qApp->translate("MainWindow", _text));
	}
private:
	const char *_text;
};
