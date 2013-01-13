#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>

#include "Core/Core.h"
#include "input/input_state.h"
#include "debugger_disasm.h"
#include "controls.h"

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
	void SetPlaying(const char *text);

	Debugger_Disasm* GetDialogDisasm() { return dialogDisasm; }
	CoreState GetNextState() { return nextState; }
	void closeEvent(QCloseEvent *event);
	void keyPressEvent(QKeyEvent *);
	void keyReleaseEvent(QKeyEvent *e);
public slots:

	void Boot();

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

private:
    Ui::MainWindow *ui;

	QtEmuGL* w;
	CoreState nextState;

	InputState input_state;
	bool g_bFullScreen;

	Debugger_Disasm *dialogDisasm;
	Controls* controls;
};

#endif // MAINWINDOW_H
