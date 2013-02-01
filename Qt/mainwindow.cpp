#include "mainwindow.h"
#include "ui_mainwindow.h"

#include <QFileDialog>
#include <QMessageBox>
#include <QDesktopServices>
#include <QUrl>
#include <QKeyEvent>
#include <QDesktopWidget>

#include "Core/MIPS/MIPSDebugInterface.h"
#include "Core/SaveState.h"
#include "Core/System.h"
#include "Core/Config.h"
#include "ConsoleListener.h"
#include "base/display.h"
#include "GPU/GPUInterface.h"
#include "GPU/GPUState.h"

#include "QtHost.h"
#include "EmuThread.h"

const char *stateToLoad = NULL;

MainWindow::MainWindow(QWidget *parent) :
	QMainWindow(parent),
	ui(new Ui::MainWindow),
	nextState(CORE_POWERDOWN),
	dialogDisasm(0)
{
	ui->setupUi(this);
	qApp->installEventFilter(this);

	controls = new Controls(this);
	host = new QtHost(this);
	w = ui->widget;
	w->init(&input_state);
	w->resize(pixel_xres, pixel_yres);
	w->setMinimumSize(pixel_xres, pixel_yres);
	w->setMaximumSize(pixel_xres, pixel_yres);

	/*
	DialogManager::AddDlg(memoryWindow[0] = new CMemoryDlg(_hInstance, hwndMain, currentDebugMIPS));
	DialogManager::AddDlg(vfpudlg = new CVFPUDlg(_hInstance, hwndMain, currentDebugMIPS));
	*/
	// Update();
	UpdateMenus();

	int zoom = g_Config.iWindowZoom;
	if (zoom < 1) zoom = 1;
	if (zoom > 4) zoom = 4;
	SetZoom(zoom);

	if (!fileToStart.isNull())
	{
		SetPlaying(fileToStart);
		//Update();
		UpdateMenus();

		EmuThread_Start(fileToStart, w);
	}
	else
		BrowseAndBoot();

	if (!fileToStart.isNull() && stateToLoad != NULL)
		SaveState::Load(stateToLoad);
}

MainWindow::~MainWindow()
{
	delete ui;
}
void NativeInit(int argc, const char *argv[], const char *savegame_directory, const char *external_directory, const char *installID)
{
	std::string config_filename;
	Common::EnableCrashingOnCrashes();

	std::string user_data_path = savegame_directory;

	VFSRegister("", new DirectoryAssetReader("assets/"));
	VFSRegister("", new DirectoryAssetReader(user_data_path.c_str()));

	config_filename = user_data_path + "ppsspp.ini";

	g_Config.Load(config_filename.c_str());

	const char *fileToLog = 0;

	bool hideLog = true;
#ifdef _DEBUG
	hideLog = false;
#endif

	bool gfxLog = false;
	// Parse command line
	LogTypes::LOG_LEVELS logLevel = LogTypes::LINFO;
	for (int i = 1; i < argc; i++) {
		if (argv[i][0] == '-') {
			switch (argv[i][1]) {
			case 'd':
				// Enable debug logging
				logLevel = LogTypes::LDEBUG;
				break;
			case 'g':
				gfxLog = true;
				break;
			case 'j':
				g_Config.iCpuCore = CPU_JIT;
				g_Config.bSaveSettings = false;
				break;
			case 'f':
				g_Config.iCpuCore = CPU_FASTINTERPRETER;
				g_Config.bSaveSettings = false;
				break;
			case 'i':
				g_Config.iCpuCore = CPU_INTERPRETER;
				g_Config.bSaveSettings = false;
				break;
			case 'l':
				hideLog = false;
				break;
			case 's':
				g_Config.bAutoRun = false;
				g_Config.bSaveSettings = false;
				break;
			case '-':
				if (!strcmp(argv[i], "--log") && i < argc - 1)
					fileToLog = argv[++i];
				if (!strncmp(argv[i], "--log=", strlen("--log=")) && strlen(argv[i]) > strlen("--log="))
					fileToLog = argv[i] + strlen("--log=");
				if (!strcmp(argv[i], "--state") && i < argc - 1)
					stateToLoad = argv[++i];
				if (!strncmp(argv[i], "--state=", strlen("--state=")) && strlen(argv[i]) > strlen("--state="))
					stateToLoad = argv[i] + strlen("--state=");
				break;
			}
		}
		else if (fileToStart.isNull())
		{
			fileToStart = QString(argv[i]);
			if (!QFile::exists(fileToStart))
			{
				qCritical("File '%s' does not exist!", qPrintable(fileToStart));
				exit(1);
			}
		}
		else
		{
			qCritical("Can only boot one file. Ignoring file '%s'", qPrintable(fileToStart));
		}
	}

	if (g_Config.currentDirectory == "")
	{
		g_Config.currentDirectory = getenv("HOME");
	}

	g_Config.memCardDirectory = std::string(getenv("HOME"))+"/.ppsspp/";
	g_Config.flashDirectory = g_Config.memCardDirectory+"/flash/";


	LogManager::Init();
	if (fileToLog != NULL)
			LogManager::GetInstance()->ChangeFileLog(fileToLog);
	//LogManager::GetInstance()->GetConsoleListener()->Open(hideLog, 150, 120, "PPSSPP Debug Console");
	LogManager::GetInstance()->SetLogLevel(LogTypes::G3D, LogTypes::LERROR);
}

void MainWindow::SetPlaying(QString text)
{
	// TODO
	/*if (text == 0)
		SetWindowText(hwndMain,programname);
	else
	{
		char temp[256];
		sprintf(temp, "%s - %s", text, programname);
		SetWindowText(hwndMain,temp);
	}*/
}

void MainWindow::SetNextState(CoreState state)
{
	nextState = state;
}

void MainWindow::BrowseAndBoot(void)
{
	QString filename = QFileDialog::getOpenFileName(NULL, "Load File", g_Config.currentDirectory.c_str(), "PSP ROMs (*.pbp *.elf *.iso *.cso *.prx)");
	if (QFile::exists(filename))
	{
		QFileInfo info(filename);
		g_Config.currentDirectory = info.absolutePath().toStdString();
		EmuThread_Start(filename, w);
	}
}

void MainWindow::Boot()
{
	dialogDisasm = new Debugger_Disasm(currentDebugMIPS, this, this);
	if(g_Config.bShowDebuggerOnLoad)
		dialogDisasm->show();
	/*
	// TODO
	memoryWindow[0] = new CMemoryDlg(MainWindow::GetHInstance(), MainWindow::GetHWND(), currentDebugMIPS);
	DialogManager::AddDlg(memoryWindow[0]);
	if (disasmWindow[0])
		disasmWindow[0]->NotifyMapLoaded();
	if (memoryWindow[0])
		memoryWindow[0]->NotifyMapLoaded();*/

	if (nextState == CORE_RUNNING)
		on_action_EmulationRun_triggered();
	UpdateMenus();
}

void MainWindow::UpdateMenus()
{
	ui->action_OptionsDisplayRawFramebuffer->setChecked(g_Config.bDisplayFramebuffer);
	ui->action_OptionsIgnoreIllegalReadsWrites->setChecked(g_Config.bIgnoreBadMemAccess);
	ui->action_CPUInterpreter->setChecked(g_Config.iCpuCore == CPU_INTERPRETER);
	ui->action_CPUFastInterpreter->setChecked(g_Config.iCpuCore == CPU_FASTINTERPRETER);
	ui->action_CPUDynarec->setChecked(g_Config.iCpuCore == CPU_JIT);
	ui->action_OptionsBufferedRendering->setChecked(g_Config.bBufferedRendering);
	ui->action_OptionsWireframe->setChecked(g_Config.bDrawWireframe);
	ui->action_OptionsHardwareTransform->setChecked(g_Config.bHardwareTransform);
	ui->action_OptionsFastMemory->setChecked(g_Config.bFastMemory);
	ui->action_OptionsLinearFiltering->setChecked(g_Config.bLinearFiltering);

	bool enable = !Core_IsStepping() ? false : true;
	ui->action_EmulationRun->setEnabled(g_State.bEmuThreadStarted ? enable : false);
	ui->action_EmulationPause->setEnabled(g_State.bEmuThreadStarted ? !enable : false);
	ui->action_EmulationReset->setEnabled(g_State.bEmuThreadStarted ? true : false);

	enable = g_State.bEmuThreadStarted ? false : true;
	ui->action_FileLoad->setEnabled(enable);
	ui->action_FileSaveStateFile->setEnabled(!enable);
	ui->action_FileLoadStateFile->setEnabled(!enable);
	ui->action_FileQuickloadState->setEnabled(!enable);
	ui->action_FileQuickSaveState->setEnabled(!enable);
	ui->action_CPUDynarec->setEnabled(enable);
	ui->action_CPUInterpreter->setEnabled(enable);
	ui->action_CPUFastInterpreter->setEnabled(enable);
	ui->action_EmulationStop->setEnabled(!enable);

	ui->action_OptionsScreen1x->setChecked(0 == (g_Config.iWindowZoom - 1));
	ui->action_OptionsScreen2x->setChecked(1 == (g_Config.iWindowZoom - 1));
	ui->action_OptionsScreen3x->setChecked(2 == (g_Config.iWindowZoom - 1));
	ui->action_OptionsScreen4x->setChecked(3 == (g_Config.iWindowZoom - 1));

	ui->actionLogG3DDebug->setChecked(LogManager::GetInstance()->GetLogLevel(LogTypes::G3D) == LogTypes::LDEBUG);
	ui->actionLogG3DInfo->setChecked(LogManager::GetInstance()->GetLogLevel(LogTypes::G3D) == LogTypes::LINFO);
	ui->actionLogG3DWarning->setChecked(LogManager::GetInstance()->GetLogLevel(LogTypes::G3D) == LogTypes::LWARNING);
	ui->actionLogG3DError->setChecked(LogManager::GetInstance()->GetLogLevel(LogTypes::G3D) == LogTypes::LERROR);

	ui->actionLogHLEDebug->setChecked(LogManager::GetInstance()->GetLogLevel(LogTypes::HLE) == LogTypes::LDEBUG);
	ui->actionLogHLEInfo->setChecked(LogManager::GetInstance()->GetLogLevel(LogTypes::HLE) == LogTypes::LINFO);
	ui->actionLogHLEWarning->setChecked(LogManager::GetInstance()->GetLogLevel(LogTypes::HLE) == LogTypes::LWARNING);
	ui->actionLogHLEError->setChecked(LogManager::GetInstance()->GetLogLevel(LogTypes::HLE) == LogTypes::LERROR);

	ui->actionLogDefDebug->setChecked(LogManager::GetInstance()->GetLogLevel(LogTypes::COMMON) == LogTypes::LDEBUG);
	ui->actionLogDefInfo->setChecked(LogManager::GetInstance()->GetLogLevel(LogTypes::COMMON) == LogTypes::LINFO);
	ui->actionLogDefWarning->setChecked(LogManager::GetInstance()->GetLogLevel(LogTypes::COMMON) == LogTypes::LWARNING);
	ui->actionLogDefError->setChecked(LogManager::GetInstance()->GetLogLevel(LogTypes::COMMON) == LogTypes::LERROR);
}

void MainWindow::SetZoom(float zoom) {
	if (zoom < 5)
		g_Config.iWindowZoom = (int) zoom;

	pixel_xres = 480 * zoom;
	pixel_yres = 272 * zoom;

	w->resize(pixel_xres, pixel_yres);
	w->setMinimumSize(pixel_xres, pixel_yres);
	w->setMaximumSize(pixel_xres, pixel_yres);

	ui->centralwidget->setFixedSize(pixel_xres, pixel_yres);
	ui->centralwidget->resize(pixel_xres, pixel_yres);

	setFixedSize(sizeHint());
	resize(sizeHint());

	PSP_CoreParameter().pixelWidth = pixel_xres;
	PSP_CoreParameter().pixelHeight = pixel_yres;
}

void MainWindow::on_action_FileLoad_triggered()
{
	BrowseAndBoot();
}

void MainWindow::on_action_EmulationRun_triggered()
{
	if (g_State.bEmuThreadStarted)
	{
		/*for (int i=0; i<numCPUs; i++)*/
		if(dialogDisasm)
		{
			dialogDisasm->Stop();
			dialogDisasm->Go();
		}
	}
}

void MainWindow::on_action_EmulationStop_triggered()
{
	/*for (int i=0; i<numCPUs; i++)*/

	if(dialogDisasm)
	{
		dialogDisasm->Stop();
	}
	usleep(100); // TODO : UGLY wait for event instead

	/*for (int i=0; i<numCPUs; i++)
		if (disasmWindow[i])
			SendMessage(disasmWindow[i]->GetDlgHandle(), WM_CLOSE, 0, 0);
	for (int i=0; i<numCPUs; i++)
		if (memoryWindow[i])
			SendMessage(memoryWindow[i]->GetDlgHandle(), WM_CLOSE, 0, 0);*/

	EmuThread_Stop();
	SetPlaying(0);
	//Update();
	UpdateMenus();
}

void MainWindow::on_action_EmulationPause_triggered()
{
	/*for (int i=0; i<numCPUs; i++)*/
	if(dialogDisasm)
	{
		dialogDisasm->Stop();
	}
}

void SaveStateActionFinished(bool result, void *userdata)
{
	// TODO: Improve messaging?
	if (!result)
	{
		QMessageBox msgBox;
		msgBox.setWindowTitle("Load Save State");
		msgBox.setText("Savestate failure. Please try again later");
		msgBox.exec();
		return;
	}

	MainWindow* mainWindow = (MainWindow*)userdata;

	if (g_State.bEmuThreadStarted && mainWindow->GetNextState() == CORE_RUNNING)
	{
		/*for (int i=0; i<numCPUs; i++)*/
		if(mainWindow->GetDialogDisasm())
			mainWindow->GetDialogDisasm()->Go();
	}
}

void MainWindow::on_action_FileLoadStateFile_triggered()
{
	if (g_State.bEmuThreadStarted)
	{
		nextState = Core_IsStepping() ? CORE_STEPPING : CORE_RUNNING;
		/*for (int i=0; i<numCPUs; i++)*/
		if(dialogDisasm)
		{
			dialogDisasm->Stop();
		}
	}
	QFileDialog dialog(0,"Load state");
	dialog.setFileMode(QFileDialog::ExistingFile);
	QStringList filters;
	filters << "Save States (*.ppst)" << "|All files (*.*)";
	dialog.setNameFilters(filters);
	dialog.setAcceptMode(QFileDialog::AcceptOpen);
	QStringList fileNames;
	if (dialog.exec())
	{
		fileNames = dialog.selectedFiles();
		SaveState::Load(fileNames[0].toStdString(), SaveStateActionFinished, this);
	}
}


void MainWindow::on_action_FileSaveStateFile_triggered()
{
	if (g_State.bEmuThreadStarted)
	{
		nextState = Core_IsStepping() ? CORE_STEPPING : CORE_RUNNING;
		/*for (int i=0; i<numCPUs; i++)*/
		if(dialogDisasm)
		{
			dialogDisasm->Stop();
		}
	}
	QFileDialog dialog(0,"Save state");
	dialog.setFileMode(QFileDialog::AnyFile);
	dialog.setAcceptMode(QFileDialog::AcceptSave);
	QStringList filters;
	filters << "Save States (*.ppst)" << "|All files (*.*)";
	dialog.setNameFilters(filters);
	QStringList fileNames;
	if (dialog.exec())
	{
		fileNames = dialog.selectedFiles();
		SaveState::Save(fileNames[0].toStdString(), SaveStateActionFinished, this);
	}
}

void MainWindow::on_action_FileQuickloadState_triggered()
{
	if (g_State.bEmuThreadStarted)
	{
		nextState = Core_IsStepping() ? CORE_STEPPING : CORE_RUNNING;
		/*for (int i=0; i<numCPUs; i++)*/
		if(dialogDisasm)
		{
			dialogDisasm->Stop();
		}
	}
	SaveState::LoadSlot(0, SaveStateActionFinished, this);
}

void MainWindow::on_action_FileQuickSaveState_triggered()
{
	if (g_State.bEmuThreadStarted)
	{
		nextState = Core_IsStepping() ? CORE_STEPPING : CORE_RUNNING;
		/*for (int i=0; i<numCPUs; i++)*/
		if(dialogDisasm)
		{
			dialogDisasm->Stop();
		}
	}
	SaveState::SaveSlot(0, SaveStateActionFinished, this);
}

void MainWindow::on_action_OptionsScreen1x_triggered()
{
	SetZoom(1);
	UpdateMenus();
}

void MainWindow::on_action_OptionsScreen2x_triggered()
{
	SetZoom(2);
	UpdateMenus();
}

void MainWindow::on_action_OptionsScreen3x_triggered()
{
	SetZoom(3);
	UpdateMenus();
}

void MainWindow::on_action_OptionsScreen4x_triggered()
{
	SetZoom(4);
	UpdateMenus();
}

void MainWindow::on_action_OptionsBufferedRendering_triggered()
{
	g_Config.bBufferedRendering = !g_Config.bBufferedRendering;
	UpdateMenus();
}

void MainWindow::on_action_OptionsShowDebugStatistics_triggered()
{
	g_Config.bShowDebugStats = !g_Config.bShowDebugStats;
	UpdateMenus();
}

void MainWindow::on_action_OptionsHardwareTransform_triggered()
{
	g_Config.bHardwareTransform = !g_Config.bHardwareTransform;
	UpdateMenus();
}

void MainWindow::on_action_FileExit_triggered()
{
	on_action_EmulationStop_triggered();
	QApplication::exit(0);
}

void MainWindow::on_action_CPUDynarec_triggered()
{
	g_Config.iCpuCore = CPU_JIT;
	UpdateMenus();
}

void MainWindow::on_action_CPUInterpreter_triggered()
{
	g_Config.iCpuCore = CPU_INTERPRETER;
	UpdateMenus();
}

void MainWindow::on_action_CPUFastInterpreter_triggered()
{
	g_Config.iCpuCore = CPU_FASTINTERPRETER;
	UpdateMenus();
}

void MainWindow::on_action_DebugLoadMapFile_triggered()
{
	// TODO
	/*if (W32Util::BrowseForFileName(true, hWnd, "Load .MAP",0,"Maps\0*.map\0All files\0*.*\0\0","map",fn))
	{
		symbolMap.LoadSymbolMap(fn.c_str());
//					HLE_PatchFunctions();
		for (int i=0; i<numCPUs; i++)
			if (disasmWindow[i])
				disasmWindow[i]->NotifyMapLoaded();
		for (int i=0; i<numCPUs; i++)
			if (memoryWindow[i])
				memoryWindow[i]->NotifyMapLoaded();
	}*/
}

void MainWindow::on_action_DebugSaveMapFile_triggered()
{
	// TODO
	/*if (W32Util::BrowseForFileName(false, hWnd, "Save .MAP",0,"Maps\0*.map\0All files\0*.*\0\0","map",fn))
						symbolMap.SaveSymbolMap(fn.c_str());*/
}

void MainWindow::on_action_DebugResetSymbolTable_triggered()
{
	// TODO
	/*symbolMap.ResetSymbolMap();
	for (int i=0; i<numCPUs; i++)
		if (disasmWindow[i])
			disasmWindow[i]->NotifyMapLoaded();
	for (int i=0; i<numCPUs; i++)
		if (memoryWindow[i])
			memoryWindow[i]->NotifyMapLoaded();*/
}

void MainWindow::on_action_DebugDisassembly_triggered()
{
	if(dialogDisasm)
		dialogDisasm->show();
}

void MainWindow::on_action_DebugMemoryView_triggered()
{
	// TODO
	/*if (memoryWindow[0])
		memoryWindow[0]->Show(true);*/
}

void MainWindow::on_action_DebugLog_triggered()
{
	LogManager::GetInstance()->GetConsoleListener()->Show(LogManager::GetInstance()->GetConsoleListener()->Hidden());
}

void MainWindow::on_action_OptionsIgnoreIllegalReadsWrites_triggered()
{
	g_Config.bIgnoreBadMemAccess = !g_Config.bIgnoreBadMemAccess;
	UpdateMenus();
}

void MainWindow::on_action_OptionsFullScreen_triggered()
{
	if(isFullScreen()) {
		showNormal();
		ui->menubar->setVisible(true);
		ui->statusbar->setVisible(true);
		SetZoom(g_Config.iWindowZoom);
	}
	else {
		ui->menubar->setVisible(false);
		ui->statusbar->setVisible(false);

		// Remove constraint
		w->setMinimumSize(0, 0);
		w->setMaximumSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX);
		ui->centralwidget->setFixedSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX);
		setFixedSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX);

		showFullScreen();

		int width = (int) QApplication::desktop()->screenGeometry().width();
		int height = (int) QApplication::desktop()->screenGeometry().height();
		PSP_CoreParameter().pixelWidth = width;
		PSP_CoreParameter().pixelHeight = height;
		PSP_CoreParameter().renderWidth = width;
		PSP_CoreParameter().renderHeight = height;
		PSP_CoreParameter().outputWidth = width;
		PSP_CoreParameter().outputHeight = height;
		pixel_xres = width;
		pixel_yres = height;
		if (gpu)
			gpu->Resized();
	}
}

void MainWindow::on_action_OptionsWireframe_triggered()
{
	g_Config.bDrawWireframe = !g_Config.bDrawWireframe;
	UpdateMenus();
}

void MainWindow::on_action_OptionsDisplayRawFramebuffer_triggered()
{
	g_Config.bDisplayFramebuffer = !g_Config.bDisplayFramebuffer;
	UpdateMenus();
}

void MainWindow::on_action_OptionsFastMemory_triggered()
{
	g_Config.bFastMemory = !g_Config.bFastMemory;
	UpdateMenus();
}

void MainWindow::on_action_OptionsLinearFiltering_triggered()
{
	g_Config.bLinearFiltering = !g_Config.bLinearFiltering;
	UpdateMenus();
}

void MainWindow::on_action_OptionsControls_triggered()
{
	controls->show();
}

void MainWindow::on_action_HelpOpenWebsite_triggered()
{
	QDesktopServices::openUrl(QUrl("http://www.ppsspp.org/"));
}

void MainWindow::on_action_HelpAbout_triggered()
{
	// TODO
	/*DialogManager::EnableAll(FALSE);
	DialogBox(hInst, (LPCTSTR)IDD_ABOUTBOX, hWnd, (DLGPROC)About);
	DialogManager::EnableAll(TRUE);*/
}

void MainWindow::closeEvent(QCloseEvent *event)
{
	on_action_FileExit_triggered();
}

void MainWindow::keyPressEvent(QKeyEvent *e)
{
	if(isFullScreen() && e->key() == Qt::Key_F12)
	{
		on_action_OptionsFullScreen_triggered();
		return;
	}

	for (int b = 0; b < controllistCount; b++) {
		if (e->key() == controllist[b].key)
		{
			input_state.pad_buttons |= (controllist[b].emu_id);
		}
	}
}

void MainWindow::keyReleaseEvent(QKeyEvent *e)
{
	for (int b = 0; b < controllistCount; b++) {
		if (e->key() == controllist[b].key)
		{
			input_state.pad_buttons &= ~(controllist[b].emu_id);
		}
	}
}

void MainWindow::on_MainWindow_destroyed()
{

}

void MainWindow::on_actionLogG3DDebug_triggered()
{
	LogManager::GetInstance()->SetLogLevel(LogTypes::G3D, LogTypes::LDEBUG);
	UpdateMenus();
}

void MainWindow::on_actionLogG3DWarning_triggered()
{
	LogManager::GetInstance()->SetLogLevel(LogTypes::G3D, LogTypes::LWARNING);
	UpdateMenus();
}

void MainWindow::on_actionLogG3DError_triggered()
{
	LogManager::GetInstance()->SetLogLevel(LogTypes::G3D, LogTypes::LERROR);
	UpdateMenus();
}

void MainWindow::on_actionLogG3DInfo_triggered()
{
	LogManager::GetInstance()->SetLogLevel(LogTypes::G3D, LogTypes::LINFO);
	UpdateMenus();
}

void MainWindow::on_actionLogHLEDebug_triggered()
{
	LogManager::GetInstance()->SetLogLevel(LogTypes::HLE, LogTypes::LDEBUG);
	UpdateMenus();
}

void MainWindow::on_actionLogHLEWarning_triggered()
{
	LogManager::GetInstance()->SetLogLevel(LogTypes::HLE, LogTypes::LWARNING);
	UpdateMenus();
}

void MainWindow::on_actionLogHLEInfo_triggered()
{
	LogManager::GetInstance()->SetLogLevel(LogTypes::HLE, LogTypes::LINFO);
	UpdateMenus();
}

void MainWindow::on_actionLogHLEError_triggered()
{
	LogManager::GetInstance()->SetLogLevel(LogTypes::HLE, LogTypes::LERROR);
	UpdateMenus();
}

void MainWindow::on_actionLogDefDebug_triggered()
{
	for (int i = 0; i < LogTypes::NUMBER_OF_LOGS; i++)
	{
		LogTypes::LOG_TYPE type = (LogTypes::LOG_TYPE)i;
		if(type == LogTypes::G3D || type == LogTypes::HLE) continue;
		LogManager::GetInstance()->SetLogLevel(type, LogTypes::LDEBUG);
	}
	UpdateMenus();
}

void MainWindow::on_actionLogDefWarning_triggered()
{
	for (int i = 0; i < LogTypes::NUMBER_OF_LOGS; i++)
	{
		LogTypes::LOG_TYPE type = (LogTypes::LOG_TYPE)i;
		if(type == LogTypes::G3D || type == LogTypes::HLE) continue;
		LogManager::GetInstance()->SetLogLevel(type, LogTypes::LWARNING);
	}
	UpdateMenus();
}

void MainWindow::on_actionLogDefInfo_triggered()
{
	for (int i = 0; i < LogTypes::NUMBER_OF_LOGS; i++)
	{
		LogTypes::LOG_TYPE type = (LogTypes::LOG_TYPE)i;
		if(type == LogTypes::G3D || type == LogTypes::HLE) continue;
		LogManager::GetInstance()->SetLogLevel(type, LogTypes::LINFO);
	}
	UpdateMenus();
}

void MainWindow::on_actionLogDefError_triggered()
{
	for (int i = 0; i < LogTypes::NUMBER_OF_LOGS; i++)
	{
		LogTypes::LOG_TYPE type = (LogTypes::LOG_TYPE)i;
		if(type == LogTypes::G3D || type == LogTypes::HLE) continue;
		LogManager::GetInstance()->SetLogLevel(type, LogTypes::LERROR);
	}
	UpdateMenus();
}
