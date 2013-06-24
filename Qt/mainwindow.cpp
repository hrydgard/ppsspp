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

#include "QtHost.h"
#include "EmuThread.h"

// TODO: Make this class thread-aware. Can't send events to a different thread. Currently only works on X11.
// Needs to use QueuedConnection for signals/slots.
MainWindow::MainWindow(QWidget *parent) :
	QMainWindow(parent),
	ui(new Ui::MainWindow),
	nextState(CORE_POWERDOWN),
	dialogDisasm(0),
	memoryWindow(0),
	memoryTexWindow(0),
	timer(this),
	displaylistWindow(0),
	lastUIState(UISTATE_MENU)
{
	ui->setupUi(this);

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

	SetGameTitle(fileToStart);

	connect(&timer, SIGNAL(timeout()), this, SLOT(Update()));
	timer.setInterval(0);
	timer.start();

//	if (!fileToStart.isNull())
//	{
//		UpdateMenus();

//		if (stateToLoad != NULL)
//			SaveState::Load(stateToLoad);
//	}
}

MainWindow::~MainWindow()
{
	delete ui;
}

void MainWindow::ShowMemory(u32 addr)
{
	if(memoryWindow)
		memoryWindow->Goto(addr);
}

void MainWindow::Update()
{
	emugl->updateGL();

	for (int i = 0; i < controllistCount; i++)
	{
		if (pressedKeys.contains(controllist[i].key) ||
				input_state.pad_buttons_down & controllist[i].emu_id)
			__CtrlButtonDown(controllist[i].psp_id);
		else
			__CtrlButtonUp(controllist[i].psp_id);
	}
	__CtrlSetAnalog(input_state.pad_lstick_x, input_state.pad_lstick_y);

	if (lastUIState != globalUIState) {
		lastUIState = globalUIState;
		UpdateMenus();
	}
}

void MainWindow::UpdateMenus()
{
	bool enable = globalUIState == UISTATE_MENU;
	ui->action_FileLoad->setEnabled(enable);
	ui->action_FileClose->setEnabled(!enable);
	ui->action_FileSaveStateFile->setEnabled(!enable);
	ui->action_FileLoadStateFile->setEnabled(!enable);
	ui->action_FileQuickloadState->setEnabled(!enable);
	ui->action_FileQuickSaveState->setEnabled(!enable);
	ui->action_CPUDynarec->setEnabled(enable);
	ui->action_CPUInterpreter->setEnabled(enable);
	ui->action_DebugDumpFrame->setEnabled(!enable);
	ui->action_DebugDisassembly->setEnabled(!enable);
	ui->action_DebugMemoryView->setEnabled(!enable);
	ui->action_DebugMemoryViewTexture->setEnabled(!enable);
	ui->action_DebugDisplayList->setEnabled(!enable);

	ui->action_EmulationRun->setEnabled(Core_IsStepping() || globalUIState == UISTATE_PAUSEMENU);
	ui->action_EmulationPause->setEnabled(globalUIState == UISTATE_INGAME);
	ui->action_EmulationReset->setEnabled(globalUIState == UISTATE_INGAME);

	// checking
	ui->action_EmulationRunLoad->setChecked(g_Config.bAutoRun);

	ui->action_CPUInterpreter->setChecked(!g_Config.bJit);
	ui->action_CPUDynarec->setChecked(g_Config.bJit);
	ui->action_OptionsFastMemory->setChecked(g_Config.bFastMemory);
	ui->action_OptionsIgnoreIllegalReadsWrites->setChecked(g_Config.bIgnoreBadMemAccess);

	ui->action_AFOff->setChecked(g_Config.iAnisotropyLevel == 0);
	ui->action_AF2x->setChecked(g_Config.iAnisotropyLevel == 2);
	ui->action_AF4x->setChecked(g_Config.iAnisotropyLevel == 4);
	ui->action_AF8x->setChecked(g_Config.iAnisotropyLevel == 8);
	ui->action_AF16x->setChecked(g_Config.iAnisotropyLevel == 16);

	ui->action_OptionsBufferedRendering->setChecked(g_Config.bBufferedRendering);
	ui->action_OptionsLinearFiltering->setChecked(g_Config.bLinearFiltering);
	ui->action_Simple_2xAA->setChecked(g_Config.SSAntiAliasing);

	ui->action_OptionsScreen1x->setChecked(0 == (g_Config.iWindowZoom - 1));
	ui->action_OptionsScreen2x->setChecked(1 == (g_Config.iWindowZoom - 1));
	ui->action_OptionsScreen3x->setChecked(2 == (g_Config.iWindowZoom - 1));
	ui->action_OptionsScreen4x->setChecked(3 == (g_Config.iWindowZoom - 1));

	ui->action_Stretch_to_display->setChecked(g_Config.bStretchToDisplay);
	ui->action_OptionsHardwareTransform->setChecked(g_Config.bHardwareTransform);
	ui->action_OptionsUseVBO->setChecked(g_Config.bUseVBO);
	ui->action_OptionsVertexCache->setChecked(g_Config.bVertexCache);
	ui->action_OptionsDisplayRawFramebuffer->setChecked(g_Config.bDisplayFramebuffer);
	ui->actionFrameskip->setChecked(g_Config.iFrameSkip != 0);

	ui->action_Sound->setChecked(g_Config.bEnableSound);

	ui->action_OptionsShowDebugStatistics->setChecked(g_Config.bShowDebugStats);
	ui->action_Show_FPS_counter->setChecked(g_Config.iShowFPSCounter);

	ui->actionLogDefDebug->setChecked(LogManager::GetInstance()->GetLogLevel(LogTypes::COMMON) == LogTypes::LDEBUG);
	ui->actionLogDefInfo->setChecked(LogManager::GetInstance()->GetLogLevel(LogTypes::COMMON) == LogTypes::LINFO);
	ui->actionLogDefWarning->setChecked(LogManager::GetInstance()->GetLogLevel(LogTypes::COMMON) == LogTypes::LWARNING);
	ui->actionLogDefError->setChecked(LogManager::GetInstance()->GetLogLevel(LogTypes::COMMON) == LogTypes::LERROR);

	ui->actionLogG3DDebug->setChecked(LogManager::GetInstance()->GetLogLevel(LogTypes::G3D) == LogTypes::LDEBUG);
	ui->actionLogG3DInfo->setChecked(LogManager::GetInstance()->GetLogLevel(LogTypes::G3D) == LogTypes::LINFO);
	ui->actionLogG3DWarning->setChecked(LogManager::GetInstance()->GetLogLevel(LogTypes::G3D) == LogTypes::LWARNING);
	ui->actionLogG3DError->setChecked(LogManager::GetInstance()->GetLogLevel(LogTypes::G3D) == LogTypes::LERROR);

	ui->actionLogHLEDebug->setChecked(LogManager::GetInstance()->GetLogLevel(LogTypes::HLE) == LogTypes::LDEBUG);
	ui->actionLogHLEInfo->setChecked(LogManager::GetInstance()->GetLogLevel(LogTypes::HLE) == LogTypes::LINFO);
	ui->actionLogHLEWarning->setChecked(LogManager::GetInstance()->GetLogLevel(LogTypes::HLE) == LogTypes::LWARNING);
	ui->actionLogHLEError->setChecked(LogManager::GetInstance()->GetLogLevel(LogTypes::HLE) == LogTypes::LERROR);
}

void MainWindow::changeEvent(QEvent *e)
{
	if (e->type() == QEvent::LanguageChange)
		ui->retranslateUi(this);
}

void MainWindow::closeEvent(QCloseEvent *)
{
	on_action_FileExit_triggered();
}

void MainWindow::keyPressEvent(QKeyEvent *e)
{
	if(isFullScreen() && e->key() == Qt::Key_F11)
	{
		on_action_OptionsFullScreen_triggered();
		return;
	}

	pressedKeys.insert(e->key());
}

void MainWindow::keyReleaseEvent(QKeyEvent *e)
{
	pressedKeys.remove(e->key());
}

/* SLOTS */
void MainWindow::Boot()
{
	dialogDisasm = new Debugger_Disasm(currentDebugMIPS, this, this);
	if(g_Config.bShowDebuggerOnLoad)
		dialogDisasm->show();

	if(g_Config.bFullScreen != isFullScreen())
		on_action_OptionsFullScreen_triggered();

	memoryWindow = new Debugger_Memory(currentDebugMIPS, this, this);
	memoryTexWindow = new Debugger_MemoryTex(this);
	displaylistWindow = new Debugger_DisplayList(currentDebugMIPS, this, this);

	notifyMapsLoaded();

	if (nextState == CORE_RUNNING)
		on_action_EmulationRun_triggered();
	UpdateMenus();
}

void MainWindow::CoreEmitWait(bool isWaiting)
{
	// Unlock mutex while core is waiting;
	EmuThread_LockDraw(!isWaiting);
}

void MainWindow::on_action_FileLoad_triggered()
{
	QString filename = QFileDialog::getOpenFileName(NULL, "Load File", g_Config.currentDirectory.c_str(), "PSP ROMs (*.pbp *.elf *.iso *.cso *.prx)");
	if (QFile::exists(filename))
	{
		QFileInfo info(filename);
		g_Config.currentDirectory = info.absolutePath().toStdString();
		NativeMessageReceived("boot", filename.toStdString().c_str());
	}
	UpdateMenus();
}

void MainWindow::on_action_FileClose_triggered()
{
	if(dialogDisasm)
		dialogDisasm->Stop();

	if(dialogDisasm && dialogDisasm->isVisible())
		dialogDisasm->close();
	if(memoryWindow && memoryWindow->isVisible())
		memoryWindow->close();
	if(memoryTexWindow && memoryTexWindow->isVisible())
		memoryTexWindow->close();
	if(displaylistWindow && displaylistWindow->isVisible())
		displaylistWindow->close();

	NativeMessageReceived("stop", "");
	SetGameTitle("");
	UpdateMenus();
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
}

void MainWindow::on_action_FileQuickloadState_triggered()
{
	SaveState::LoadSlot(0, SaveStateActionFinished, this);
}

void MainWindow::on_action_FileQuickSaveState_triggered()
{
	SaveState::SaveSlot(0, SaveStateActionFinished, this);
}

void MainWindow::on_action_FileLoadStateFile_triggered()
{
	QFileDialog dialog(0,"Load state");
	dialog.setFileMode(QFileDialog::ExistingFile);
	QStringList filters;
	filters << "Save States (*.ppst)" << "|All files (*.*)";
	dialog.setNameFilters(filters);
	dialog.setAcceptMode(QFileDialog::AcceptOpen);
	if (dialog.exec())
	{
		QStringList fileNames = dialog.selectedFiles();
		SaveState::Load(fileNames[0].toStdString(), SaveStateActionFinished, this);
	}
}

void MainWindow::on_action_FileSaveStateFile_triggered()
{
	QFileDialog dialog(0,"Save state");
	dialog.setFileMode(QFileDialog::AnyFile);
	dialog.setAcceptMode(QFileDialog::AcceptSave);
	QStringList filters;
	filters << "Save States (*.ppst)" << "|All files (*.*)";
	dialog.setNameFilters(filters);
	if (dialog.exec())
	{
		QStringList fileNames = dialog.selectedFiles();
		SaveState::Save(fileNames[0].toStdString(), SaveStateActionFinished, this);
	}
}

void MainWindow::on_action_FileExit_triggered()
{
	on_action_FileClose_triggered();
	QApplication::exit(0);
}

void MainWindow::on_action_EmulationRun_triggered()
{
	NativeMessageReceived("run", "");
}

void MainWindow::on_action_EmulationPause_triggered()
{
	NativeMessageReceived("pause", "");
}

void MainWindow::on_action_EmulationReset_triggered()
{
	if(dialogDisasm)
		dialogDisasm->Stop();

	if(dialogDisasm)
		dialogDisasm->close();
	if(memoryWindow)
		memoryWindow->close();
	if(memoryTexWindow)
		memoryTexWindow->close();
	if(displaylistWindow)
		displaylistWindow->close();

	NativeMessageReceived("reset", "");
}

void MainWindow::on_action_EmulationRunLoad_triggered()
{
	g_Config.bAutoRun = !g_Config.bAutoRun;
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
		notifyMapsLoaded();
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
	notifyMapsLoaded();
}

void MainWindow::on_action_DebugDumpFrame_triggered()
{
	gpu->DumpNextFrame();
}

void MainWindow::on_action_DebugDisassembly_triggered()
{
	if(dialogDisasm)
		dialogDisasm->show();
}

void MainWindow::on_action_DebugDisplayList_triggered()
{
	if(displaylistWindow)
		displaylistWindow->show();
}

void MainWindow::on_action_DebugLog_triggered()
{
	LogManager::GetInstance()->GetConsoleListener()->Show(LogManager::GetInstance()->GetConsoleListener()->Hidden());
}

void MainWindow::on_action_DebugMemoryView_triggered()
{
	if (memoryWindow)
		memoryWindow->show();
}

void MainWindow::on_action_DebugMemoryViewTexture_triggered()
{
	if(memoryTexWindow)
		memoryTexWindow->show();
}

void MainWindow::on_action_CPUDynarec_triggered()
{
	g_Config.bJit = true;
	UpdateMenus();
}

void MainWindow::on_action_CPUInterpreter_triggered()
{
	g_Config.bJit = false;
	UpdateMenus();
}

void MainWindow::on_action_OptionsFastMemory_triggered()
{
	g_Config.bFastMemory = !g_Config.bFastMemory;
	UpdateMenus();
}

void MainWindow::on_action_OptionsIgnoreIllegalReadsWrites_triggered()
{
	g_Config.bIgnoreBadMemAccess = !g_Config.bIgnoreBadMemAccess;
	UpdateMenus();
}

void MainWindow::on_action_OptionsControls_triggered()
{
	controls->show();
}

void MainWindow::on_action_OptionsGamePadControls_triggered()
{
#if QT_HAS_SDL
	gamePadDlg->show();
#else
	QMessageBox::information(this,tr("Gamepad"),tr("You need to compile with SDL to have Gamepad support."), QMessageBox::Ok);
#endif
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

void MainWindow::on_action_OptionsBufferedRendering_triggered()
{
	g_Config.bBufferedRendering = !g_Config.bBufferedRendering;
	UpdateMenus();
}

void MainWindow::on_action_OptionsLinearFiltering_triggered()
{
	g_Config.bLinearFiltering = !g_Config.bLinearFiltering;
	UpdateMenus();
}

void MainWindow::on_action_Simple_2xAA_triggered()
{
	g_Config.SSAntiAliasing = !g_Config.SSAntiAliasing;
	UpdateMenus();
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

void MainWindow::on_action_Stretch_to_display_triggered()
{
	g_Config.bStretchToDisplay = !g_Config.bStretchToDisplay;
	UpdateMenus();
	if (gpu)
		gpu->Resized();
}

void MainWindow::on_action_OptionsHardwareTransform_triggered()
{
	g_Config.bHardwareTransform = !g_Config.bHardwareTransform;
	UpdateMenus();
}

void MainWindow::on_action_OptionsUseVBO_triggered()
{
	g_Config.bUseVBO = !g_Config.bUseVBO;
	UpdateMenus();
}

void MainWindow::on_action_OptionsVertexCache_triggered()
{
	g_Config.bVertexCache = !g_Config.bVertexCache;
	UpdateMenus();
}

void MainWindow::on_action_OptionsDisplayRawFramebuffer_triggered()
{
	g_Config.bDisplayFramebuffer = !g_Config.bDisplayFramebuffer;
	UpdateMenus();
}

void MainWindow::on_actionFrameskip_triggered()
{
	g_Config.iFrameSkip = !g_Config.iFrameSkip;
	UpdateMenus();
}

void MainWindow::on_action_Sound_triggered()
{
	g_Config.bEnableSound = !g_Config.bEnableSound;
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

void MainWindow::on_action_OptionsShowDebugStatistics_triggered()
{
	g_Config.bShowDebugStats = !g_Config.bShowDebugStats;
	UpdateMenus();
}

void MainWindow::on_action_Show_FPS_counter_triggered()
{
	g_Config.iShowFPSCounter = !g_Config.iShowFPSCounter;
	UpdateMenus();
}

void setDefLogLevel(LogTypes::LOG_LEVELS level)
{
	for (int i = 0; i < LogTypes::NUMBER_OF_LOGS; i++)
	{
		LogTypes::LOG_TYPE type = (LogTypes::LOG_TYPE)i;
		if(type == LogTypes::G3D || type == LogTypes::HLE) continue;
		LogManager::GetInstance()->SetLogLevel(type, level);
	}
}

void MainWindow::on_actionLogDefDebug_triggered()
{
	setDefLogLevel(LogTypes::LDEBUG);
	UpdateMenus();
}

void MainWindow::on_actionLogDefWarning_triggered()
{
	setDefLogLevel(LogTypes::LWARNING);
	UpdateMenus();
}

void MainWindow::on_actionLogDefInfo_triggered()
{
	setDefLogLevel(LogTypes::LINFO);
	UpdateMenus();
}

void MainWindow::on_actionLogDefError_triggered()
{
	setDefLogLevel(LogTypes::LERROR);
	UpdateMenus();
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

void MainWindow::on_action_HelpOpenWebsite_triggered()
{
	QDesktopServices::openUrl(QUrl("http://www.ppsspp.org/"));
}

void MainWindow::on_action_HelpAbout_triggered()
{
	// TODO display about
}

void MainWindow::on_language_changed(QAction *action)
{
	loadLanguage(action->data().toString());
}

/* Private functions */
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

void MainWindow::SetGameTitle(QString text)
{
	QString title = "PPSSPP " + QString(PPSSPP_GIT_VERSION);
	if (text != "")
		title += QString(" - %1").arg(text);

	setWindowTitle(title);
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
		QLocale::setDefault(QLocale(currentLanguage));
		switchTranslator(translator, QString(":/languages/ppsspp_%1.qm").arg(language));
	}
}

void MainWindow::createLanguageMenu()
{
	QActionGroup *langGroup = new QActionGroup(ui->menuLanguage);
	langGroup->setExclusive(true);

	connect(langGroup, SIGNAL(triggered(QAction *)), this, SLOT(on_language_changed(QAction *)));

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

void MainWindow::notifyMapsLoaded()
{
	if (dialogDisasm)
		dialogDisasm->NotifyMapLoaded();
	if (memoryWindow)
		memoryWindow->NotifyMapLoaded();
}
