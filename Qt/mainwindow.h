#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QTranslator>

#include "Core/Core.h"
#include "input/input_state.h"
#include "debugger_disasm.h"
#include "debugger_memory.h"
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

	void UpdateMenus();
	void SetZoom(float zoom);
	void Create(int argc, const char *argv[], const char *savegame_directory, const char *external_directory, const char *installID);
	void BrowseAndBoot();
	void SetNextState(CoreState state);
	void SetPlaying(QString text);

	Debugger_Disasm* GetDialogDisasm() { return dialogDisasm; }
	Debugger_Memory* GetDialogMemory() { return memoryWindow; }
	CoreState GetNextState() { return nextState; }
	void closeEvent(QCloseEvent *event);
	void keyPressEvent(QKeyEvent *);
	void keyReleaseEvent(QKeyEvent *e);
	void ShowMemory(u32 addr);
	void Update();
public slots:

	void Boot();
	void CoreEmitWait(bool);

private slots:
	void on_action_FileLoad_triggered();

	void on_action_EmulationRun_triggered();

	void on_action_EmulationStop_triggered();

	void on_action_EmulationPause_triggered();

	void on_action_FileLoadStateFile_triggered();

	void on_action_FileSaveStateFile_triggered();

	void on_action_FileQuickloadState_triggered();

	void on_action_FileQuickSaveState_triggered();

	void on_action_OptionsScreen1x_triggered();

	void on_action_OptionsScreen2x_triggered();

	void on_action_OptionsScreen3x_triggered();

	void on_action_OptionsScreen4x_triggered();

	void on_action_OptionsBufferedRendering_triggered();

	void on_action_OptionsShowDebugStatistics_triggered();

	void on_action_OptionsHardwareTransform_triggered();

	void on_action_FileExit_triggered();

	void on_action_CPUDynarec_triggered();

	void on_action_CPUInterpreter_triggered();

	void on_action_CPUFastInterpreter_triggered();

	void on_action_DebugLoadMapFile_triggered();

	void on_action_DebugSaveMapFile_triggered();

	void on_action_DebugResetSymbolTable_triggered();

	void on_action_DebugDisassembly_triggered();

	void on_action_DebugMemoryView_triggered();

	void on_action_DebugLog_triggered();

	void on_action_OptionsIgnoreIllegalReadsWrites_triggered();

	void on_action_OptionsFullScreen_triggered();

	void on_action_OptionsWireframe_triggered();

	void on_action_OptionsDisplayRawFramebuffer_triggered();

	void on_action_OptionsFastMemory_triggered();

	void on_action_OptionsLinearFiltering_triggered();

	void on_action_OptionsControls_triggered();

	void on_action_HelpOpenWebsite_triggered();

	void on_action_HelpAbout_triggered();

	void on_MainWindow_destroyed();

	void on_actionLogG3DDebug_triggered();

	void on_actionLogG3DWarning_triggered();

	void on_actionLogG3DError_triggered();

	void on_actionLogG3DInfo_triggered();

	void on_actionLogHLEDebug_triggered();

	void on_actionLogHLEWarning_triggered();

	void on_actionLogHLEInfo_triggered();

	void on_actionLogHLEError_triggered();

	void on_actionLogDefDebug_triggered();

	void on_actionLogDefWarning_triggered();

	void on_actionLogDefInfo_triggered();

	void on_actionLogDefError_triggered();

	void on_action_OptionsGamePadControls_triggered();

	void on_language_changed(QAction *action);

	void on_action_EmulationReset_triggered();

	void on_action_DebugDumpFrame_triggered();

	void on_action_EmulationRunLoad_triggered();

	void on_action_OptionsVertexCache_triggered();

	void on_action_OptionsUseVBO_triggered();

private:
	void loadLanguage(const QString &language);
	void createLanguageMenu();
	void changeEvent(QEvent *);

	QTranslator translator;
	QTranslator qtTranslator;
	QString currentLanguage;
	QString languagePath;

    Ui::MainWindow *ui;

	QtEmuGL* w;
	CoreState nextState;

	InputState input_state;

	Debugger_Disasm *dialogDisasm;
	Debugger_Memory *memoryWindow;
	Controls* controls;
	GamePadDialog* gamePadDlg;
};

#endif // MAINWINDOW_H
