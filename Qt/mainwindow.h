#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QTranslator>
#include <QTimer>
#include "Core/Core.h"
#include "input/input_state.h"
#include "debugger_disasm.h"
#include "debugger_memory.h"
#include "debugger_memorytex.h"
#include "debugger_displaylist.h"
#include "controls.h"
#include "gamepaddialog.h"

class QtEmuGL;
namespace Ui {
class MainWindow;
}

class MainWindow : public QMainWindow
{
    Q_OBJECT
    
public:
    explicit MainWindow(QWidget *parent = 0);
	~MainWindow();

	Debugger_Disasm* GetDialogDisasm() { return dialogDisasm; }
	Debugger_Memory* GetDialogMemory() { return memoryWindow; }
	Debugger_MemoryTex* GetDialogMemoryTex() { return memoryTexWindow; }
	Debugger_DisplayList* GetDialogDisplaylist() { return displaylistWindow; }
	CoreState GetNextState() { return nextState; }

	void ShowMemory(u32 addr);
	void UpdateMenus();

protected:
	void changeEvent(QEvent *e);
	void closeEvent(QCloseEvent *);
	void keyPressEvent(QKeyEvent *e);
	void keyReleaseEvent(QKeyEvent *e);

public slots:
	void Boot();
	void CoreEmitWait(bool);
	void Update();

private slots:
	// File
	void on_action_FileLoad_triggered();
	void on_action_FileClose_triggered();
	void on_action_FileQuickloadState_triggered();
	void on_action_FileQuickSaveState_triggered();
	void on_action_FileLoadStateFile_triggered();
	void on_action_FileSaveStateFile_triggered();
	void on_action_FileExit_triggered();

	// Emulation
	void on_action_EmulationRun_triggered();
	void on_action_EmulationPause_triggered();
	void on_action_EmulationReset_triggered();
	void on_action_EmulationRunLoad_triggered();

	// Debug
	void on_action_DebugLoadMapFile_triggered();
	void on_action_DebugSaveMapFile_triggered();
	void on_action_DebugResetSymbolTable_triggered();
	void on_action_DebugDumpFrame_triggered();
	void on_action_DebugDisassembly_triggered();
	void on_action_DebugDisplayList_triggered();
	void on_action_DebugLog_triggered();
	void on_action_DebugMemoryView_triggered();
	void on_action_DebugMemoryViewTexture_triggered();

	// Options
	// Core
	void on_action_CPUDynarec_triggered();
	void on_action_CPUInterpreter_triggered();
	void on_action_OptionsFastMemory_triggered();
	void on_action_OptionsIgnoreIllegalReadsWrites_triggered();

	// Controls
	void on_action_OptionsControls_triggered();
	void on_action_OptionsGamePadControls_triggered();

	// Video
	void on_action_AFOff_triggered();
	void on_action_AF2x_triggered();
	void on_action_AF4x_triggered();
	void on_action_AF8x_triggered();
	void on_action_AF16x_triggered();

	void on_action_OptionsBufferedRendering_triggered();
	void on_action_OptionsLinearFiltering_triggered();
	void on_action_Simple_2xAA_triggered();

	void on_action_OptionsScreen1x_triggered();
	void on_action_OptionsScreen2x_triggered();
	void on_action_OptionsScreen3x_triggered();
	void on_action_OptionsScreen4x_triggered();

	void on_action_Stretch_to_display_triggered();
	void on_action_OptionsHardwareTransform_triggered();
	void on_action_OptionsUseVBO_triggered();
	void on_action_OptionsVertexCache_triggered();
	void on_action_OptionsDisplayRawFramebuffer_triggered();
	void on_actionFrameskip_triggered();

	// Sound
	void on_action_Sound_triggered();

	void on_action_OptionsFullScreen_triggered();
	void on_action_OptionsShowDebugStatistics_triggered();
	void on_action_Show_FPS_counter_triggered();

	// Logs
	void on_actionLogDefDebug_triggered();
	void on_actionLogDefWarning_triggered();
	void on_actionLogDefInfo_triggered();
	void on_actionLogDefError_triggered();

	void on_actionLogG3DDebug_triggered();
	void on_actionLogG3DWarning_triggered();
	void on_actionLogG3DError_triggered();
	void on_actionLogG3DInfo_triggered();

	void on_actionLogHLEDebug_triggered();
	void on_actionLogHLEWarning_triggered();
	void on_actionLogHLEInfo_triggered();
	void on_actionLogHLEError_triggered();

	// Help
	void on_action_HelpOpenWebsite_triggered();
	void on_action_HelpAbout_triggered();

	// Others
	void on_language_changed(QAction *action);

private:
	void SetZoom(float zoom);
	void SetGameTitle(QString text);
	void loadLanguage(const QString &language);
	void createLanguageMenu();
	void notifyMapsLoaded();

	QTranslator translator;
	QString currentLanguage;

	Ui::MainWindow *ui;

	QtEmuGL *emugl;
	QTimer timer;
	CoreState nextState;
	InputState input_state;
	GlobalUIState lastUIState;

	Debugger_Disasm *dialogDisasm;
	Debugger_Memory *memoryWindow;
	Debugger_MemoryTex *memoryTexWindow;
	Debugger_DisplayList *displaylistWindow;
	Controls *controls;
	GamePadDialog *gamePadDlg;

	QSet<int> pressedKeys;
};

#endif // MAINWINDOW_H
