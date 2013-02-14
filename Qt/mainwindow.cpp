#include "mainwindow.h"
#include "ui_mainwindow.h"

#include <QFileDialog>
#include <QMessageBox>
#include <QDesktopServices>
#include <QUrl>
#include <QKeyEvent>
#include <QDesktopWidget>

#include "Core/MIPS/MIPSDebugInterface.h"
#include "Core/Debugger/SymbolMap.h"
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
	dialogDisasm(0),
	memoryWindow(0)
{
	ui->setupUi(this);
	qApp->installEventFilter(this);

	controls = new Controls(this);
#if QT_HAS_SDL
	gamePadDlg = new GamePadDialog(&input_state, this);
#endif

	host = new QtHost(this);
	emugl = ui->widget;
	emugl->init(&input_state);
	emugl->resize(pixel_xres, pixel_yres);
	emugl->setMinimumSize(pixel_xres, pixel_yres);
	emugl->setMaximumSize(pixel_xres, pixel_yres);
	QObject::connect( emugl, SIGNAL(doubleClick()), this, SLOT(on_action_OptionsFullScreen_triggered()) );

	createLanguageMenu();
	UpdateMenus();

	int zoom = g_Config.iWindowZoom;
	if (zoom < 1) zoom = 1;
	if (zoom > 4) zoom = 4;
	SetZoom(zoom);

	EmuThread_Start(emugl);

	if (!fileToStart.isNull())
	{
		SetGameTitle(fileToStart);
		UpdateMenus();

		EmuThread_StartGame(fileToStart);

		if (stateToLoad != NULL)
			SaveState::Load(stateToLoad);
	}
	else
		SetGameTitle("");
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

	LogManager::GetInstance()->SetLogLevel(LogTypes::G3D, LogTypes::LERROR);

#if !defined(USING_GLES2)
	// Start Desktop UI
	MainWindow* mainWindow = new MainWindow();
	mainWindow->show();
#endif
}

void MainWindow::SetNextState(CoreState state)
{
	nextState = state;
}

void MainWindow::SetGameTitle(QString text)
{
	QString title = "PPSSPP " + QString(PPSSPP_VERSION_STR);
	if (text != "")
		title += QString(" - %1").arg(text);

	setWindowTitle(title);
}

void MainWindow::BrowseAndBoot(void)
{
	QString filename = QFileDialog::getOpenFileName(NULL, "Load File", g_Config.currentDirectory.c_str(), "PSP ROMs (*.pbp *.elf *.iso *.cso *.prx)");
	if (QFile::exists(filename))
	{
		QFileInfo info(filename);
		g_Config.currentDirectory = info.absolutePath().toStdString();
		EmuThread_StartGame(filename);
	}
}

void MainWindow::Boot()
{
	dialogDisasm = new Debugger_Disasm(currentDebugMIPS, this, this);
	if(g_Config.bShowDebuggerOnLoad)
		dialogDisasm->show();

	if(g_Config.bFullScreen != isFullScreen())
		on_action_OptionsFullScreen_triggered();

	memoryWindow = new Debugger_Memory(currentDebugMIPS, this, this);

	if (dialogDisasm)
		dialogDisasm->NotifyMapLoaded();
	if (memoryWindow)
		memoryWindow->NotifyMapLoaded();

	if (nextState == CORE_RUNNING)
		on_action_EmulationRun_triggered();
	UpdateMenus();
}

void MainWindow::CoreEmitWait(bool isWaiting)
{
	// Unlock mutex while core is waiting;
	EmuThread_LockDraw(!isWaiting);
}

void MainWindow::UpdateMenus()
{
	ui->action_OptionsDisplayRawFramebuffer->setChecked(g_Config.bDisplayFramebuffer);
	ui->action_OptionsIgnoreIllegalReadsWrites->setChecked(g_Config.bIgnoreBadMemAccess);
	ui->action_CPUInterpreter->setChecked(g_Config.iCpuCore == CPU_INTERPRETER);
	ui->action_CPUFastInterpreter->setChecked(g_Config.iCpuCore == CPU_FASTINTERPRETER);
	ui->action_CPUDynarec->setChecked(g_Config.iCpuCore == CPU_JIT);
	ui->action_OptionsBufferedRendering->setChecked(g_Config.bBufferedRendering);
	ui->action_OptionsShowDebugStatistics->setChecked(g_Config.bShowDebugStats);
	ui->action_OptionsWireframe->setChecked(g_Config.bDrawWireframe);
	ui->action_OptionsHardwareTransform->setChecked(g_Config.bHardwareTransform);
	ui->action_OptionsFastMemory->setChecked(g_Config.bFastMemory);
	ui->action_OptionsLinearFiltering->setChecked(g_Config.bLinearFiltering);
	ui->action_EmulationRunLoad->setChecked(g_Config.bAutoRun);
	ui->action_OptionsUseVBO->setChecked(g_Config.bUseVBO);
	ui->action_OptionsVertexCache->setChecked(g_Config.bVertexCache);
	ui->action_AFOff->setChecked(g_Config.iAnisotropyLevel == 0);
	ui->action_AF2x->setChecked(g_Config.iAnisotropyLevel == 2);
	ui->action_AF4x->setChecked(g_Config.iAnisotropyLevel == 4);
	ui->action_AF8x->setChecked(g_Config.iAnisotropyLevel == 8);
	ui->action_AF16x->setChecked(g_Config.iAnisotropyLevel == 16);
	ui->action_Simple_2xAA->setChecked(g_Config.SSAntiAliasing);
	ui->action_Show_FPS_counter->setChecked(g_Config.bShowFPSCounter);
	ui->action_Stretch_to_display->setChecked(g_Config.bStretchToDisplay);
	ui->action_Sound->setChecked(g_Config.bEnableSound);

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
	ui->action_DebugDumpFrame->setEnabled(!enable);
	ui->action_DebugDisassembly->setEnabled(!enable);
	ui->action_DebugMemoryView->setEnabled(!enable);

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
	dp_xres = pixel_xres;
	dp_yres = pixel_yres;

	emugl->resize(pixel_xres, pixel_yres);
	emugl->setMinimumSize(pixel_xres, pixel_yres);
	emugl->setMaximumSize(pixel_xres, pixel_yres);

	ui->centralwidget->setFixedSize(pixel_xres, pixel_yres);
	ui->centralwidget->resize(pixel_xres, pixel_yres);

	setFixedSize(sizeHint());
	resize(sizeHint());

	PSP_CoreParameter().pixelWidth = pixel_xres;
	PSP_CoreParameter().pixelHeight = pixel_yres;
	PSP_CoreParameter().outputWidth = pixel_xres;
	PSP_CoreParameter().outputHeight = pixel_yres;

	if (g_Config.SSAntiAliasing)
	{
		zoom *= 2;
		PSP_CoreParameter().renderWidth = 480 * zoom;
		PSP_CoreParameter().renderHeight = 272 * zoom;
	}

	if (gpu)
		gpu->Resized();
}

void MainWindow::on_action_FileLoad_triggered()
{
	BrowseAndBoot();
}

void MainWindow::on_action_EmulationRun_triggered()
{
	if (g_State.bEmuThreadStarted)
	{
		if(dialogDisasm)
		{
			dialogDisasm->Stop();
			dialogDisasm->Go();
		}
	}
}

void MainWindow::on_action_EmulationStop_triggered()
{
	if(dialogDisasm)
	{
		dialogDisasm->Stop();
	}
	// This will wait for ppsspp to pause
	EmuThread_LockDraw(true);
	EmuThread_LockDraw(false);

	if(dialogDisasm && dialogDisasm->isVisible())
		dialogDisasm->close();
	if(memoryWindow && memoryWindow->isVisible())
		memoryWindow->close();

	EmuThread_StopGame();
	SetGameTitle("");
	UpdateMenus();
}

void MainWindow::on_action_EmulationPause_triggered()
{
	if(dialogDisasm)
		dialogDisasm->Stop();
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
		if(mainWindow->GetDialogDisasm())
			mainWindow->GetDialogDisasm()->Go();
	}
}

void MainWindow::on_action_FileLoadStateFile_triggered()
{
	if (g_State.bEmuThreadStarted)
	{
		nextState = Core_IsStepping() ? CORE_STEPPING : CORE_RUNNING;
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
	EmuThread_Stop();
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
	QFileDialog dialog(0,"Load .MAP");
	dialog.setFileMode(QFileDialog::ExistingFile);
	QStringList filters;
	filters << "Maps (*.map)" << "|All files (*.*)";
	dialog.setNameFilters(filters);
	dialog.setAcceptMode(QFileDialog::AcceptOpen);
	QStringList fileNames;
	if (dialog.exec())
	{
		fileNames = dialog.selectedFiles();
		symbolMap.LoadSymbolMap(fileNames[0].toStdString().c_str());
		if (dialogDisasm)
			dialogDisasm->NotifyMapLoaded();
		if (memoryWindow)
			memoryWindow->NotifyMapLoaded();
	}
}

void MainWindow::on_action_DebugSaveMapFile_triggered()
{
	QFileDialog dialog(0,"Save .MAP");
	dialog.setFileMode(QFileDialog::AnyFile);
	dialog.setAcceptMode(QFileDialog::AcceptSave);
	QStringList filters;
	filters << "Save .MAP (*.map)" << "|All files (*.*)";
	dialog.setNameFilters(filters);
	QStringList fileNames;
	if (dialog.exec())
	{
		fileNames = dialog.selectedFiles();
		symbolMap.SaveSymbolMap(fileNames[0].toStdString().c_str());
	}
}

void MainWindow::on_action_DebugResetSymbolTable_triggered()
{
	symbolMap.ResetSymbolMap();
	if (dialogDisasm)
		dialogDisasm->NotifyMapLoaded();
	if (memoryWindow)
		memoryWindow->NotifyMapLoaded();
}

void MainWindow::on_action_DebugDisassembly_triggered()
{
	if(dialogDisasm)
		dialogDisasm->show();
}

void MainWindow::on_action_DebugMemoryView_triggered()
{
	if (memoryWindow)
		memoryWindow->show();
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
		g_Config.bFullScreen = false;
		showNormal();
		ui->menubar->setVisible(true);
		ui->statusbar->setVisible(true);
		SetZoom(g_Config.iWindowZoom);
	}
	else {
		g_Config.bFullScreen = true;
		ui->menubar->setVisible(false);
		ui->statusbar->setVisible(false);

		// Remove constraint
		emugl->setMinimumSize(0, 0);
		emugl->setMaximumSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX);
		ui->centralwidget->setFixedSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX);
		setFixedSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX);

		showFullScreen();

		int width = (int) QApplication::desktop()->screenGeometry().width();
		int height = (int) QApplication::desktop()->screenGeometry().height();
		PSP_CoreParameter().pixelWidth = width;
		PSP_CoreParameter().pixelHeight = height;
		PSP_CoreParameter().outputWidth = width;
		PSP_CoreParameter().outputHeight = height;

		int antialias = 1;
		if (g_Config.SSAntiAliasing) antialias = 2;
		PSP_CoreParameter().renderWidth = width * antialias;
		PSP_CoreParameter().renderHeight = height * antialias;

		pixel_xres = width;
		pixel_yres = height;
		dp_xres = pixel_xres;
		dp_yres = pixel_yres;
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
	// TODO display about
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

void MainWindow::on_action_OptionsGamePadControls_triggered()
{
#if QT_HAS_SDL
	gamePadDlg->show();
#else
	QMessageBox::information(this,"Gamepad","You need to compile with SDL to have Gamepad support.", QMessageBox::Ok);
#endif
}

void MainWindow::on_language_changed(QAction *action)
{
	if (0 != action)
	{
		loadLanguage(action->data().toString());
	}
}

void switchTranslator(QTranslator &translator, const QString &filename)
{
	qApp->removeTranslator(&translator);

	if (translator.load(filename))
		qApp->installTranslator(&translator);
}

void MainWindow::loadLanguage(const QString& language)
{
	if (currentLanguage != language)
	{
		currentLanguage = language;
		QLocale locale = QLocale(currentLanguage);
		QLocale::setDefault(locale);
		QString languageName = QLocale::languageToString(locale.language());
		switchTranslator(translator, QString(":/languages/ppsspp_%1.qm").arg(language));
	}
}

void MainWindow::createLanguageMenu()
{
	QActionGroup *langGroup = new QActionGroup(ui->menuLanguage);
	langGroup->setExclusive(true);

	connect(langGroup, SIGNAL(triggered(QAction *)), this, SLOT(on_language_changed(QAction *)));

	QString defaultLocale = QLocale::system().name();
	defaultLocale.truncate(defaultLocale.lastIndexOf('_'));
	QStringList fileNames = QDir(":/languages").entryList(QStringList("ppsspp_*.qm"));

	if (fileNames.size() == 0)
	{
		QAction *action = new QAction(tr("No translations"), this);
		action->setCheckable(false);
		action->setDisabled(true);
		ui->menuLanguage->addAction(action);
		langGroup->addAction(action);
	}

	for (int i = 0; i < fileNames.size(); ++i)
	{
		QString locale = fileNames[i];
		locale.truncate(locale.lastIndexOf('.'));
		locale.remove(0, locale.indexOf('_') + 1);

#if QT_VERSION >= 0x040800
		QString language = QLocale(locale).nativeLanguageName();
#else
		QString language = QLocale::languageToString(QLocale(locale).language());
#endif
		QAction *action = new QAction(language, this);
		action->setCheckable(true);
		action->setData(locale);

		ui->menuLanguage->addAction(action);
		langGroup->addAction(action);

		// TODO check en as default until we save language to config
		if ("en" == locale)
		{
			action->setChecked(true);
			currentLanguage = "en";
		}
	}
}

void MainWindow::changeEvent(QEvent *event)
{
	QMainWindow::changeEvent(event);

	if (0 != event)
	{
		switch (event->type())
		{
		case QEvent::LanguageChange:
			ui->retranslateUi(this);
			break;
		case QEvent::LocaleChange:
			{
				QString locale = QLocale::system().name();
				locale.truncate(locale.lastIndexOf('_'));
				loadLanguage(locale);
			}
			break;
		default:
			break;
		}
	}
}

void MainWindow::ShowMemory(u32 addr)
{
	if(memoryWindow)
		memoryWindow->Goto(addr);
}

void MainWindow::Update()
{

}

void MainWindow::on_action_EmulationReset_triggered()
{
	if(dialogDisasm)
		dialogDisasm->Stop();

	EmuThread_LockDraw(true);
	EmuThread_LockDraw(false);

	if(dialogDisasm)
		dialogDisasm->close();
	if(memoryWindow)
		memoryWindow->close();

	EmuThread_StopGame();

	EmuThread_StartGame(GetCurrentFilename());
}

void MainWindow::on_action_DebugDumpFrame_triggered()
{
	gpu->DumpNextFrame();
}

void MainWindow::on_action_EmulationRunLoad_triggered()
{
	g_Config.bAutoRun = !g_Config.bAutoRun;
	UpdateMenus();
}

void MainWindow::on_action_OptionsVertexCache_triggered()
{
	g_Config.bVertexCache = !g_Config.bVertexCache;
	UpdateMenus();
}

void MainWindow::on_action_OptionsUseVBO_triggered()
{
	g_Config.bUseVBO = !g_Config.bUseVBO;
	UpdateMenus();
}

void MainWindow::on_action_Simple_2xAA_triggered()
{
	g_Config.SSAntiAliasing = !g_Config.SSAntiAliasing;
	UpdateMenus();
}

void MainWindow::on_action_AFOff_triggered()
{
	g_Config.iAnisotropyLevel = 0;
	UpdateMenus();
}

void MainWindow::on_action_AF2x_triggered()
{
	g_Config.iAnisotropyLevel = 2;
	UpdateMenus();
}

void MainWindow::on_action_AF4x_triggered()
{
	g_Config.iAnisotropyLevel = 4;
	UpdateMenus();
}

void MainWindow::on_action_AF8x_triggered()
{
	g_Config.iAnisotropyLevel = 8;
	UpdateMenus();
}

void MainWindow::on_action_AF16x_triggered()
{
	g_Config.iAnisotropyLevel = 16;
	UpdateMenus();
}

void MainWindow::on_action_Show_FPS_counter_triggered()
{
	g_Config.bShowFPSCounter = !g_Config.bShowFPSCounter;
	UpdateMenus();
}

void MainWindow::on_action_Stretch_to_display_triggered()
{
	g_Config.bStretchToDisplay = !g_Config.bStretchToDisplay;
	UpdateMenus();
	if (gpu)
		gpu->Resized();
}

void MainWindow::on_action_Sound_triggered()
{
	g_Config.bEnableSound = !g_Config.bEnableSound;
	UpdateMenus();
}
