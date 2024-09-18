#pragma once

#include <queue>
#include <mutex>
#include <string>

#include <QtCore>
#include <QMenuBar>
#include <QMainWindow>
#include <QActionGroup>

#include "ppsspp_config.h"
#include "Common/System/System.h"
#include "Common/System/NativeApp.h"
#if PPSSPP_PLATFORM(WINDOWS)
#include "Common/Log/ConsoleListener.h"
#endif
#include "Core/Core.h"
#include "Core/Config.h"
#include "Core/System.h"
#include "Qt/QtMain.h"

extern bool g_TakeScreenshot;

class MenuAction;
class MenuTree;

enum {
	FB_NON_BUFFERED_MODE = 0,
	FB_BUFFERED_MODE = 1,
};

// hacky, should probably use qt signals or something, but whatever..
enum class MainWindowMsg {
	BOOT_DONE,
	WINDOW_TITLE_CHANGED,
};

class MainWindow : public QMainWindow
{
	Q_OBJECT

public:
	explicit MainWindow(QWidget *parent = nullptr, bool fullscreen = false);
	~MainWindow() { };

	CoreState GetNextState() { return nextState; }

	void updateMenuGroupInt(QActionGroup *group, int value);

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
	void loadAct();
	void closeAct();
	void openmsAct();
	void saveStateGroup_triggered(QAction *action) { g_Config.iCurrentStateSlot = action->data().toInt(); }
	void qlstateAct();
	void qsstateAct();
	void lstateAct();
	void sstateAct();
	void recordDisplayAct();
	void useLosslessVideoCodecAct();
	void useOutputBufferAct();
	void recordAudioAct();
	void exitAct();

	// Emulation
	void runAct();
	void pauseAct();
	void stopAct();
	void resetAct();
	void switchUMDAct();
	void displayRotationGroup_triggered(QAction *action) { g_Config.iInternalScreenRotation = action->data().toInt(); }

	// Debug
	void breakonloadAct();
	void ignoreIllegalAct() { g_Config.bIgnoreBadMemAccess = !g_Config.bIgnoreBadMemAccess; }
	void lmapAct();
	void smapAct();
	void lsymAct();
	void ssymAct();
	void resetTableAct();
	void dumpNextAct();
	void takeScreen() { g_TakeScreenshot = true; }
	void consoleAct();

	// Game settings
	void languageAct() { System_PostUIMessage(UIMessage::SHOW_LANGUAGE_SCREEN); }
	void controlMappingAct() { System_PostUIMessage(UIMessage::SHOW_CONTROL_MAPPING); }
	void displayLayoutEditorAct() { System_PostUIMessage(UIMessage::SHOW_DISPLAY_LAYOUT_EDITOR); }
	void moreSettingsAct() { System_PostUIMessage(UIMessage::SHOW_SETTINGS); }

	void bufferRenderAct() {
		System_PostUIMessage(UIMessage::GPU_RENDER_RESIZED);
		System_PostUIMessage(UIMessage::GPU_CONFIG_CHANGED);
	}
	void linearAct() { g_Config.iTexFiltering = (g_Config.iTexFiltering != 0) ? 0 : 3; }

	void renderingResolutionGroup_triggered(QAction *action) {
		g_Config.iInternalResolution = action->data().toInt();
		System_PostUIMessage(UIMessage::GPU_RENDER_RESIZED);
	}
	void windowGroup_triggered(QAction *action) { SetWindowScale(action->data().toInt()); }

	void autoframeskipAct() {
		g_Config.bAutoFrameSkip = !g_Config.bAutoFrameSkip;
		if (g_Config.bSkipBufferEffects) {
			g_Config.bSkipBufferEffects = false;
			System_PostUIMessage(UIMessage::GPU_CONFIG_CHANGED);
		}
	}
	void frameSkippingGroup_triggered(QAction *action) { g_Config.iFrameSkip = action->data().toInt(); }
	void frameSkippingTypeGroup_triggered(QAction *action) { g_Config.iFrameSkipType = action->data().toInt(); }
	void textureFilteringGroup_triggered(QAction *action) { g_Config.iTexFiltering = action->data().toInt(); }
	void screenScalingFilterGroup_triggered(QAction *action) { g_Config.iDisplayFilter = action->data().toInt(); }
	void textureScalingLevelGroup_triggered(QAction *action) {
		g_Config.iTexScalingLevel = action->data().toInt();
		System_PostUIMessage(UIMessage::GPU_CONFIG_CHANGED);
	}
	void textureScalingTypeGroup_triggered(QAction *action) {
		g_Config.iTexScalingType = action->data().toInt();
		System_PostUIMessage(UIMessage::GPU_CONFIG_CHANGED);
	}
	void deposterizeAct() {
		g_Config.bTexDeposterize = !g_Config.bTexDeposterize;
		System_PostUIMessage(UIMessage::GPU_CONFIG_CHANGED);
	}
	void transformAct() {
		g_Config.bHardwareTransform = !g_Config.bHardwareTransform;
		System_PostUIMessage(UIMessage::GPU_CONFIG_CHANGED);
	}
	void frameskipAct() { g_Config.iFrameSkip = !g_Config.iFrameSkip; }
	void frameskipTypeAct() { g_Config.iFrameSkipType = !g_Config.iFrameSkipType; }

	// Sound
	void audioAct() {
		g_Config.bEnableSound = !g_Config.bEnableSound;
	}

	// Cheats
	void cheatsAct() { g_Config.bEnableCheats = !g_Config.bEnableCheats; }

	// Chat
	void chatAct() {
		if (GetUIState() == UISTATE_INGAME) {
			System_PostUIMessage(UIMessage::SHOW_CHAT_SCREEN);
		}
	}

	void fullscrAct();
	void raiseTopMost();

	// Help
	void websiteAct();
	void forumAct();
	void goldAct();
	void gitAct();
	void discordAct();
	void aboutAct();

	// Others
	void langChanged(QAction *action) { loadLanguage(action->data().toString(), true); }

private:
	void bootDone();
	void SetWindowScale(int zoom);
	void SetGameTitle(QString text);
	void SetFullScreen(bool fullscreen);
	void loadLanguage(const QString &language, bool retranslate);
	void createMenus();

	QTranslator translator;
	QString currentLanguage;

	CoreState nextState;
	GlobalUIState lastUIState;

	QActionGroup *windowGroup,
	             *textureScalingLevelGroup, *textureScalingTypeGroup,
	             *screenScalingFilterGroup, *textureFilteringGroup,
	             *frameSkippingTypeGroup, *frameSkippingGroup,
	             *renderingResolutionGroup,
	             *displayRotationGroup, *saveStateGroup;

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
		QAction(parent), _text(text)
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
		QAction(parent)
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
	// Event which causes it to be unchecked
	void addEventUnchecked(bool* event) {
		this->setCheckable(true);
		_eventUncheck = event;
	}
	// UI State which causes it to be enabled
	void addEnableState(int state) {
		_enabledFunc = nullptr;
		_stateEnable = state;
		_stateDisable = -1;
	}
	void addDisableState(int state) {
		_enabledFunc = nullptr;
		_stateEnable = -1;
		_stateDisable = state;
	}
	void SetEnabledFunc(std::function<bool()> func) {
		_enabledFunc = func;
		_stateEnable = -1;
		_stateDisable = -1;
	}
public slots:
	void retranslate() {
		setText(qApp->translate("MainWindow", _text));
	}
	void update() {
		if (_eventCheck)
			setChecked(*_eventCheck);
		if (_eventUncheck)
			setChecked(!*_eventUncheck);
		if (_stateEnable >= 0)
			setEnabled(GetUIState() == _stateEnable);
		if (_stateDisable >= 0)
			setEnabled(GetUIState() != _stateDisable);
		if (_enabledFunc)
			setEnabled(_enabledFunc());
	}
private:
	const char *_text;
	bool *_eventCheck = nullptr;
	bool *_eventUncheck = nullptr;
	int _stateEnable = -1;
	int _stateDisable = -1;
	std::function<bool()> _enabledFunc;
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
