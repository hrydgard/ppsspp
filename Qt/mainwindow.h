#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QtCore>
#include <QMenuBar>
#include <QMainWindow>
#include <QActionGroup>

#include "ConsoleListener.h"
#include "Core/Core.h"
#include "Core/Config.h"
#include "input/input_state.h"
#include "debugger_disasm.h"
#include "debugger_memory.h"
#include "debugger_memorytex.h"
#include "debugger_displaylist.h"

class QtEmuGL;

class MainWindow : public QMainWindow
{
	Q_OBJECT

public:
	explicit MainWindow(QWidget *parent = 0);
	~MainWindow() { };

	Debugger_Disasm* GetDialogDisasm() { return dialogDisasm; }
	Debugger_Memory* GetDialogMemory() { return memoryWindow; }
	Debugger_MemoryTex* GetDialogMemoryTex() { return memoryTexWindow; }
	Debugger_DisplayList* GetDialogDisplaylist() { return displaylistWindow; }
	CoreState GetNextState() { return nextState; }

	void ShowMemory(u32 addr);
	void updateMenus();

protected:
	void closeEvent(QCloseEvent *);
	void keyPressEvent(QKeyEvent *e);
	void keyReleaseEvent(QKeyEvent *e);
	void timerEvent(QTimerEvent *);

signals:
	void retranslate();

public slots:
	void Boot();

private slots:
	// File
	void openAct_triggered();
	void closeAct_triggered();
	void qlstateAct_triggered();
	void qsstateAct_triggered();
	void lstateAct_triggered();
	void sstateAct_triggered();
	void exitAct_triggered();

	// Emulation
	void runAct_triggered();
	void pauseAct_triggered();
	void resetAct_triggered();
	void runonloadAct_triggered();

	// Debug
	void lmapAct_triggered();
	void smapAct_triggered();
	void resetTableAct_triggered();
	void dumpNextAct_triggered();
	void disasmAct_triggered();
	void dpyListAct_triggered();
	void consoleAct_triggered();
	void memviewAct_triggered();
	void memviewTexAct_triggered();

	// Options
	// Core
	void dynarecAct_triggered() { g_Config.bJit = !g_Config.bJit; }
	void vertexDynarecAct_triggered() { g_Config.bVertexDecoderJit = !g_Config.bVertexDecoderJit; }
	void fastmemAct_triggered() { g_Config.bFastMemory = !g_Config.bFastMemory; }
	void ignoreIllegalAct_triggered() { g_Config.bIgnoreBadMemAccess = !g_Config.bIgnoreBadMemAccess; }

	// Video
	void anisotropicGroup_triggered(QAction *action) { g_Config.iAnisotropyLevel = action->data().toInt(); }

	void bufferRenderAct_triggered() { g_Config.iRenderingMode = !g_Config.iRenderingMode; }
	void linearAct_triggered() { g_Config.iTexFiltering = (g_Config.iTexFiltering != 0) ? 0 : 3; }

	void screenGroup_triggered(QAction *action) { SetZoom(action->data().toInt()); }

	void stretchAct_triggered();
	void transformAct_triggered() { g_Config.bHardwareTransform = !g_Config.bHardwareTransform; }
	void vertexCacheAct_triggered() { g_Config.bVertexCache = !g_Config.bVertexCache; }
	void frameskipAct_triggered() { g_Config.iFrameSkip = !g_Config.iFrameSkip; }

	// Sound
	void audioAct_triggered() { g_Config.bEnableSound = !g_Config.bEnableSound; }

	void fullscreenAct_triggered();
	void statsAct_triggered() { g_Config.bShowDebugStats = !g_Config.bShowDebugStats; }
	void showFPSAct_triggered() { g_Config.iShowFPSCounter = !g_Config.iShowFPSCounter; }

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
	void websiteAct_triggered();
	void aboutAct_triggered();

	// Others
	void langChanged(QAction *action) { loadLanguage(action->data().toString(), true); }
	void retranslateUi();

private:
	void SetZoom(int zoom);
	void SetGameTitle(QString text);
	void loadLanguage(const QString &language, bool retranslate);
	void createMenus();
	void notifyMapsLoaded();

	QTranslator translator;
	QString currentLanguage;

	QtEmuGL *emugl;
	CoreState nextState;
	InputState input_state;
	GlobalUIState lastUIState;

	Debugger_Disasm *dialogDisasm;
	Debugger_Memory *memoryWindow;
	Debugger_MemoryTex *memoryTexWindow;
	Debugger_DisplayList *displaylistWindow;

	// Menus
	// File
	QMenu *fileMenu;
	QAction *openAct, *closeAct, *qlstateAct, *qsstateAct,
	        *lstateAct, *sstateAct, *exitAct;
	// Emulation
	QMenu *emuMenu;
	QAction *runAct, *pauseAct, *resetAct, *runonloadAct;
	// Debug
	QMenu *debugMenu;
	QAction *lmapAct, *smapAct, *resetTableAct, *dumpNextAct,
	        *disasmAct, *dpyListAct, *consoleAct, *memviewAct,
	        *memviewTexAct;
	// Options
	QMenu *optionsMenu, *coreMenu, *videoMenu, *anisotropicMenu,
	      *screenMenu, *levelsMenu, *langMenu;
	QAction *dynarecAct, *vertexDynarecAct, *fastmemAct,
	        *ignoreIllegalAct, *bufferRenderAct,
	        *linearAct, *stretchAct, *transformAct, *vertexCacheAct,
	        *frameskipAct, *audioAct, *fullscreenAct, *statsAct,
	        *showFPSAct;
	QActionGroup *anisotropicGroup, *screenGroup, *langGroup,
	             *defaultLogGroup, *g3dLogGroup, *hleLogGroup;
	// Help
	QMenu *helpMenu;
	QAction *websiteAct, *aboutAct;
	
};

class MenuAction : public QAction
{
	Q_OBJECT

public:
	MenuAction(QWidget* parent, QMenu* menu, const char* callback, char* text, bool isCheckable = false, QKeySequence key = 0) :
		QAction(parent), _text(text)
	{
		this->setCheckable(isCheckable);
		menu->addAction(this);
		if (key != 0) {
			this->setShortcut(key);
			parent->addAction(this); // So we don't lose the shortcut when menubar is hidden
		}
		connect(this, SIGNAL(triggered()), parent, callback);
		connect(parent, SIGNAL(retranslate()), this, SLOT(retranslate()));
	}
	MenuAction(QWidget* parent, QActionGroup* group, QVariant data, QString text, QKeySequence key = 0) :
		QAction(parent)
	{
		this->setCheckable(true);
		this->setData(data);
		this->setText(text); // Not translatable, yet
		if (key != 0) {
			this->setShortcut(key);
			parent->addAction(this); // So we don't lose the shortcut when menubar is hidden
		}
		group->addAction(this);
	}
public slots:
	void retranslate() {
		setText(qApp->translate("MainWindow", _text));
	}
private:
	char* _text;
};

#endif // MAINWINDOW_H
