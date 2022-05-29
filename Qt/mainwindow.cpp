// Qt Desktop UI: works on Linux, Windows and Mac OSX
#include "ppsspp_config.h"
#include "mainwindow.h"

#include <QApplication>
#include <QDesktopServices>
#include <QDesktopWidget>
#include <QFileDialog>
#include <QMessageBox>

#include "Common/System/Display.h"
#include "Common/System/NativeApp.h"
#include "Common/System/System.h"
#include "Common/File/Path.h"
#include "Core/MIPS/MIPSDebugInterface.h"
#include "Core/Debugger/SymbolMap.h"
#include "Core/HLE/sceUmd.h"
#include "Core/SaveState.h"
#include "Core/System.h"
#include "GPU/GPUInterface.h"
#include "UI/GamepadEmu.h"

MainWindow::MainWindow(QWidget *parent, bool fullscreen) :
	QMainWindow(parent),
	currentLanguage("en"),
	nextState(CORE_POWERDOWN),
	lastUIState(UISTATE_MENU)
{
	setWindowIcon(QIcon(qApp->applicationDirPath() + "/assets/icon_regular_72.png"));

	SetGameTitle("");
	emugl = new MainUI(this);

	setCentralWidget(emugl);
	createMenus();
	updateMenus();

	SetFullScreen(fullscreen);

	QObject::connect(emugl, SIGNAL(doubleClick()), this, SLOT(fullscrAct()));
	QObject::connect(emugl, SIGNAL(newFrame()), this, SLOT(newFrame()));
}

inline float clamp1(float x) {
	if (x > 1.0f) return 1.0f;
	if (x < -1.0f) return -1.0f;
	return x;
}

void MainWindow::newFrame()
{
	if (lastUIState != GetUIState()) {
		lastUIState = GetUIState();
		if (lastUIState == UISTATE_INGAME && g_Config.UseFullScreen() && !QApplication::overrideCursor() && !g_Config.bShowTouchControls)
			QApplication::setOverrideCursor(QCursor(Qt::BlankCursor));
		if (lastUIState != UISTATE_INGAME && g_Config.UseFullScreen() && QApplication::overrideCursor())
			QApplication::restoreOverrideCursor();

		updateMenus();
	}

	if (g_Config.UseFullScreen() != isFullScreen())
		SetFullScreen(g_Config.UseFullScreen());

	std::unique_lock<std::mutex> lock(msgMutex_);
	while (!msgQueue_.empty()) {
		MainWindowMsg msg = msgQueue_.front();
		msgQueue_.pop();
		switch (msg) {
		case MainWindowMsg::BOOT_DONE:
			bootDone();
			break;
		case MainWindowMsg::WINDOW_TITLE_CHANGED:
			std::unique_lock<std::mutex> lock(titleMutex_);
			setWindowTitle(QString::fromUtf8(newWindowTitle_.c_str()));
			break;
		}
	}
}

void MainWindow::updateMenuGroupInt(QActionGroup *group, int value) {
	foreach (QAction *action, group->actions()) {
		action->setChecked(action->data().toInt() == value);
	}
}

void MainWindow::updateMenus()
{
	updateMenuGroupInt(saveStateGroup, g_Config.iCurrentStateSlot);
	updateMenuGroupInt(displayRotationGroup, g_Config.iInternalScreenRotation);
	updateMenuGroupInt(renderingResolutionGroup, g_Config.iInternalResolution);
	updateMenuGroupInt(renderingModeGroup, g_Config.iRenderingMode);
	updateMenuGroupInt(frameSkippingGroup, g_Config.iFrameSkip);
	updateMenuGroupInt(frameSkippingTypeGroup, g_Config.iFrameSkipType);
	updateMenuGroupInt(textureFilteringGroup, g_Config.iTexFiltering);
	updateMenuGroupInt(screenScalingFilterGroup, g_Config.iBufFilter);
	updateMenuGroupInt(textureScalingLevelGroup, g_Config.iTexScalingLevel);
	updateMenuGroupInt(textureScalingTypeGroup, g_Config.iTexScalingType);

	foreach(QAction * action, windowGroup->actions()) {
		int width = (g_Config.IsPortrait() ? 272 : 480) * action->data().toInt();
		int height = (g_Config.IsPortrait() ? 480 : 272) * action->data().toInt();
		if (g_Config.iWindowWidth == width && g_Config.iWindowHeight == height) {
			action->setChecked(true);
			break;
		}
	}
	emit updateMenu();
}

void MainWindow::bootDone()
{
	if (nextState == CORE_RUNNING)
		runAct();
	updateMenus();
}

/* SIGNALS */
void MainWindow::loadAct()
{
	QString filename = QFileDialog::getOpenFileName(NULL, "Load File", g_Config.currentDirectory.c_str(), "PSP ROMs (*.pbp *.elf *.iso *.cso *.prx)");
	if (QFile::exists(filename))
	{
		QFileInfo info(filename);
		g_Config.currentDirectory = Path(info.absolutePath().toStdString());
		NativeMessageReceived("boot", filename.toStdString().c_str());
	}
}

void MainWindow::closeAct()
{
	updateMenus();

	NativeMessageReceived("stop", "");
	SetGameTitle("");
}

void MainWindow::openmsAct()
{
	QString confighome = getenv("XDG_CONFIG_HOME");
	QString memorystick = confighome + "/ppsspp";
	QDesktopServices::openUrl(QUrl(memorystick));
}

void SaveStateActionFinished(SaveState::Status status, const std::string &message, void *userdata)
{
	// TODO: Improve messaging?
	if (status == SaveState::Status::FAILURE)
	{
		QMessageBox msgBox;
		msgBox.setWindowTitle("Load Save State");
		msgBox.setText("Savestate failure. Please try again later");
		msgBox.exec();
		return;
	}
}

void MainWindow::qlstateAct()
{
	Path gamePath = PSP_CoreParameter().fileToStart;
	SaveState::LoadSlot(gamePath, 0, SaveStateActionFinished, this);
}

void MainWindow::qsstateAct()
{
	Path gamePath = PSP_CoreParameter().fileToStart;
	SaveState::SaveSlot(gamePath, 0, SaveStateActionFinished, this);
}

void MainWindow::lstateAct()
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
		SaveState::Load(Path(fileNames[0].toStdString()), -1, SaveStateActionFinished, this);
	}
}

void MainWindow::sstateAct()
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
		SaveState::Save(Path(fileNames[0].toStdString()), -1, SaveStateActionFinished, this);
	}
}

void MainWindow::recordDisplayAct()
{
	g_Config.bDumpFrames = !g_Config.bDumpFrames;
}

void MainWindow::useLosslessVideoCodecAct()
{
	g_Config.bUseFFV1 = !g_Config.bUseFFV1;
}

void MainWindow::useOutputBufferAct()
{
	g_Config.bDumpVideoOutput = !g_Config.bDumpVideoOutput;
}

void MainWindow::recordAudioAct()
{
	g_Config.bDumpAudio = !g_Config.bDumpAudio;
}

void MainWindow::exitAct()
{
	closeAct();
	QApplication::exit(0);
}

void MainWindow::runAct()
{
	NativeMessageReceived("run", "");
}

void MainWindow::pauseAct()
{
	NativeMessageReceived("pause", "");
}

void MainWindow::stopAct()
{
	Core_Stop();
	NativeMessageReceived("stop", "");
}

void MainWindow::resetAct()
{
	updateMenus();

	NativeMessageReceived("reset", "");
}

void MainWindow::switchUMDAct()
{
	QString filename = QFileDialog::getOpenFileName(NULL, "Switch UMD", g_Config.currentDirectory.c_str(), "PSP ROMs (*.pbp *.elf *.iso *.cso *.prx)");
	if (QFile::exists(filename))
	{
		QFileInfo info(filename);
		g_Config.currentDirectory = Path(info.absolutePath().toStdString());
		__UmdReplace(Path(filename.toStdString()));
	}
}

void MainWindow::breakonloadAct()
{
	g_Config.bAutoRun = !g_Config.bAutoRun;
}

void MainWindow::lmapAct()
{
	QFileDialog dialog(0,"Load .MAP");
	dialog.setFileMode(QFileDialog::ExistingFile);
	QStringList filters;
	filters << "Maps (*.map)" << "|All files (*.*)";
	dialog.setNameFilters(filters);
	dialog.setAcceptMode(QFileDialog::AcceptOpen);
	QStringList fileNames;
	if (dialog.exec())
		fileNames = dialog.selectedFiles();

	if (fileNames.count() > 0)
	{
		QString fileName = QFileInfo(fileNames[0]).absoluteFilePath();
		g_symbolMap->LoadSymbolMap(Path(fileName.toStdString()));
	}
}

void MainWindow::smapAct()
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
		g_symbolMap->SaveSymbolMap(Path(fileNames[0].toStdString()));
	}
}

void MainWindow::lsymAct()
{
	QFileDialog dialog(0,"Load .SYM");
	dialog.setFileMode(QFileDialog::ExistingFile);
	QStringList filters;
	filters << "Symbols (*.sym)" << "|All files (*.*)";
	dialog.setNameFilters(filters);
	dialog.setAcceptMode(QFileDialog::AcceptOpen);
	QStringList fileNames;
	if (dialog.exec())
		fileNames = dialog.selectedFiles();

	if (fileNames.count() > 0)
	{
		QString fileName = QFileInfo(fileNames[0]).absoluteFilePath();
		g_symbolMap->LoadNocashSym(Path(fileName.toStdString()));
	}
}

void MainWindow::ssymAct()
{
	QFileDialog dialog(0,"Save .SYM");
	dialog.setFileMode(QFileDialog::AnyFile);
	dialog.setAcceptMode(QFileDialog::AcceptSave);
	QStringList filters;
	filters << "Save .SYM (*.sym)" << "|All files (*.*)";
	dialog.setNameFilters(filters);
	QStringList fileNames;
	if (dialog.exec())
	{
		fileNames = dialog.selectedFiles();
		g_symbolMap->SaveNocashSym(Path(fileNames[0].toStdString()));
	}
}

void MainWindow::resetTableAct()
{
	g_symbolMap->Clear();
}

void MainWindow::dumpNextAct()
{
	gpu->DumpNextFrame();
}

void MainWindow::consoleAct()
{
	LogManager::GetInstance()->GetConsoleListener()->Show(LogManager::GetInstance()->GetConsoleListener()->Hidden());
}

void MainWindow::raiseTopMost()
{
	setWindowState( (windowState() & ~Qt::WindowMinimized) | Qt::WindowActive);
	raise();
	activateWindow();
}

void MainWindow::SetFullScreen(bool fullscreen) {
	if (fullscreen) {
#if !PPSSPP_PLATFORM(MAC)
		menuBar()->hide();

		emugl->setFixedSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX);
		// TODO: Shouldn't this be physicalSize()?
		emugl->resizeGL(emugl->size().width(), emugl->size().height());
		// TODO: Won't showFullScreen do this for us?
		setMaximumSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX);
		setFixedSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX);
#endif

		showFullScreen();
		InitPadLayout(dp_xres, dp_yres);

		if (GetUIState() == UISTATE_INGAME && !g_Config.bShowTouchControls)
			QApplication::setOverrideCursor(QCursor(Qt::BlankCursor));
	} else {
#if !PPSSPP_PLATFORM(MAC)
		menuBar()->show();
		updateMenus();
#endif

		showNormal();
		SetWindowScale(-1);
		InitPadLayout(dp_xres, dp_yres);

		if (GetUIState() == UISTATE_INGAME && QApplication::overrideCursor())
			QApplication::restoreOverrideCursor();

		QDesktopWidget *desktop = QApplication::desktop();
		int screenNum = QProcessEnvironment::systemEnvironment().value("SDL_VIDEO_FULLSCREEN_HEAD", "0").toInt();

		// Move window to the center of selected screen
		QRect rect = desktop->screenGeometry(screenNum);
		move((rect.width() - frameGeometry().width()) / 4, (rect.height() - frameGeometry().height()) / 4);
	}
}

void MainWindow::fullscrAct()
{
	// Toggle the current state.
	g_Config.bFullScreen = !isFullScreen();
	g_Config.iForceFullScreen = -1;
	SetFullScreen(g_Config.bFullScreen);

	QTimer::singleShot(1000, this, SLOT(raiseTopMost()));
}

void MainWindow::websiteAct()
{
	QDesktopServices::openUrl(QUrl("https://www.ppsspp.org/"));
}

void MainWindow::forumAct()
{
	QDesktopServices::openUrl(QUrl("https://forums.ppsspp.org/"));
}

void MainWindow::goldAct()
{
	QDesktopServices::openUrl(QUrl("https://central.ppsspp.org/buygold"));
}

void MainWindow::gitAct()
{
	QDesktopServices::openUrl(QUrl("https://github.com/hrydgard/ppsspp/"));
}

void MainWindow::discordAct()
{
	QDesktopServices::openUrl(QUrl("https://discord.gg/5NJB6dD"));
}

void MainWindow::aboutAct()
{
	QMessageBox::about(this, "About", QString("PPSSPP Qt %1\n\n"
	                                                    "PSP emulator and debugger\n\n"
	                                                    "Copyright (c) by Henrik Rydg\xc3\xa5rd and the PPSSPP Project 2012-\n"
	                                                    "Qt port maintained by xSacha\n\n"
	                                                    "Additional credits:\n"
	                                                    "    PSPSDK by #pspdev (freenode)\n"
	                                                    "    CISO decompression code by BOOSTER\n"
	                                                    "    zlib by Jean-loup Gailly (compression) and Mark Adler (decompression)\n"
	                                                    "    Qt project by Digia\n\n"
	                                                    "All trademarks are property of their respective owners.\n"
	                                                    "The emulator is for educational and development purposes only and it may not be used to play games you do not legally own.").arg(PPSSPP_GIT_VERSION));
}

/* Private functions */
void MainWindow::SetWindowScale(int zoom) {
	if (isFullScreen())
		fullscrAct();

	int width, height;
	if (zoom == -1 && (g_Config.iWindowWidth <= 0 || g_Config.iWindowHeight <= 0)) {
		// Default to zoom level 2.
		zoom = 2;
	}
	if (zoom == -1) {
		// Take the last setting.
		width = g_Config.iWindowWidth;
		height = g_Config.iWindowHeight;
	} else {
		// Update to the specified factor.  Let's clamp first.
		if (zoom < 1)
			zoom = 1;
		if (zoom > 10)
			zoom = 10;

		width = (g_Config.IsPortrait() ? 272 : 480) * zoom;
		height = (g_Config.IsPortrait() ? 480 : 272) * zoom;
	}

	g_Config.iWindowWidth = width;
	g_Config.iWindowHeight = height;

#if !PPSSPP_PLATFORM(MAC)
	emugl->setFixedSize(g_Config.iWindowWidth, g_Config.iWindowHeight);
	// TODO: Shouldn't this be scaled size?
	emugl->resizeGL(g_Config.iWindowWidth, g_Config.iWindowHeight);
	setFixedSize(sizeHint());
#else
	resize(g_Config.iWindowWidth, g_Config.iWindowHeight);
#endif
	updateMenus();
}

void MainWindow::SetGameTitle(QString text)
{
	QString title = QString("PPSSPP %1").arg(PPSSPP_GIT_VERSION);
	if (text != "")
		title += QString(" - %1").arg(text);

	setWindowTitle(title);
}

void MainWindow::loadLanguage(const QString& language, bool translate)
{
	if (currentLanguage != language)
	{
		QLocale::setDefault(QLocale(language));
		QApplication::removeTranslator(&translator);

		currentLanguage = language;
		if (translator.load(QString(":/languages/ppsspp_%1.qm").arg(language))) {
			QApplication::installTranslator(&translator);
		}
		if (translate)
			emit retranslate();
	}
}

void MainWindow::createMenus()
{
	// File
	MenuTree* fileMenu = new MenuTree(this, menuBar(),    QT_TR_NOOP("&File"));
	fileMenu->add(new MenuAction(this, SLOT(loadAct()),       QT_TR_NOOP("&Load..."), QKeySequence::Open))
		->addEnableState(UISTATE_MENU);
	fileMenu->addSeparator();
	fileMenu->add(new MenuAction(this, SLOT(openmsAct()),       QT_TR_NOOP("Open &Memory Stick")))
		->addEnableState(UISTATE_MENU);
	fileMenu->addSeparator();
	MenuTree* savestateMenu = new MenuTree(this, fileMenu, QT_TR_NOOP("Saves&tate slot"));
	saveStateGroup = new MenuActionGroup(this, savestateMenu, SLOT(saveStateGroup_triggered(QAction *)),
		QStringList() << "1" << "2" << "3" << "4" << "5",
		QList<int>() << 0 << 1 << 2 << 3 << 4);
	fileMenu->add(new MenuAction(this, SLOT(qlstateAct()),    QT_TR_NOOP("L&oad state"), Qt::Key_F4))
		->addDisableState(UISTATE_MENU);
	fileMenu->add(new MenuAction(this, SLOT(qsstateAct()),    QT_TR_NOOP("S&ave state"), Qt::Key_F2))
		->addDisableState(UISTATE_MENU);
	fileMenu->add(new MenuAction(this, SLOT(lstateAct()),     QT_TR_NOOP("&Load state file...")))
		->addDisableState(UISTATE_MENU);
	fileMenu->add(new MenuAction(this, SLOT(sstateAct()),     QT_TR_NOOP("&Save state file...")))
		->addDisableState(UISTATE_MENU);
	MenuTree* recordMenu = new MenuTree(this, fileMenu, QT_TR_NOOP("&Record"));
	recordMenu->add(new MenuAction(this, SLOT(recordDisplayAct()),         QT_TR_NOOP("Record &display")))
		->addEventChecked(&g_Config.bDumpFrames);
	recordMenu->add(new MenuAction(this, SLOT(useLosslessVideoCodecAct()), QT_TR_NOOP("&Use lossless video codec (FFV1)")))
		->addEventChecked(&g_Config.bUseFFV1);
	recordMenu->add(new MenuAction(this, SLOT(useOutputBufferAct()),       QT_TR_NOOP("Use output buffer for video")))
		->addEventChecked(&g_Config.bDumpVideoOutput);
	recordMenu->addSeparator();
	recordMenu->add(new MenuAction(this, SLOT(recordAudioAct()),        QT_TR_NOOP("Record &audio")))
		->addEventChecked(&g_Config.bDumpAudio);
	fileMenu->addSeparator();
	fileMenu->add(new MenuAction(this, SLOT(exitAct()),       QT_TR_NOOP("E&xit"), QKeySequence::Quit));

	// Emulation
	MenuTree* emuMenu = new MenuTree(this, menuBar(),     QT_TR_NOOP("&Emulation"));
	emuMenu->add(new MenuAction(this, SLOT(pauseAct()),       QT_TR_NOOP("&Pause")))
		->addEnableState(UISTATE_INGAME);
	emuMenu->add(new MenuAction(this, SLOT(stopAct()),       QT_TR_NOOP("&Stop"), Qt::CTRL + Qt::Key_W))
		->addEnableState(UISTATE_INGAME);
	emuMenu->add(new MenuAction(this, SLOT(resetAct()),       QT_TR_NOOP("R&eset"), Qt::CTRL + Qt::Key_B))
		->addEnableState(UISTATE_INGAME);
	emuMenu->add(new MenuAction(this, SLOT(switchUMDAct()),       QT_TR_NOOP("Switch UMD"), Qt::CTRL + Qt::Key_U))
		->addEnableState(UISTATE_INGAME);
	MenuTree* displayRotationMenu = new MenuTree(this, emuMenu, QT_TR_NOOP("Display rotation"));
	displayRotationGroup = new MenuActionGroup(this, displayRotationMenu, SLOT(displayRotationGroup_triggered(QAction *)),
		QStringList() << "Landscape" << "Portrait" << "Landscape reversed" << "Portrait reversed",
		QList<int>() << 1 << 2 << 3 << 4);

	// Debug
	MenuTree* debugMenu = new MenuTree(this, menuBar(),   QT_TR_NOOP("&Debug"));
	debugMenu->add(new MenuAction(this, SLOT(breakonloadAct()),   QT_TR_NOOP("Break on load")))
		->addEventUnchecked(&g_Config.bAutoRun);
	debugMenu->add(new MenuAction(this, SLOT(ignoreIllegalAct()),  QT_TR_NOOP("&Ignore illegal reads/writes")))
		->addEventChecked(&g_Config.bIgnoreBadMemAccess);
	debugMenu->addSeparator();
	debugMenu->add(new MenuAction(this, SLOT(lmapAct()),      QT_TR_NOOP("&Load MAP file...")))
		->addDisableState(UISTATE_MENU);
	debugMenu->add(new MenuAction(this, SLOT(smapAct()),      QT_TR_NOOP("&Save MAP file...")))
		->addDisableState(UISTATE_MENU);
	debugMenu->add(new MenuAction(this, SLOT(lsymAct()),      QT_TR_NOOP("Lo&ad SYM file...")))
		->addDisableState(UISTATE_MENU);
	debugMenu->add(new MenuAction(this, SLOT(ssymAct()),      QT_TR_NOOP("Sav&e SYM file...")))
		->addDisableState(UISTATE_MENU);
	debugMenu->add(new MenuAction(this, SLOT(resetTableAct()),QT_TR_NOOP("Reset s&ymbol table")))
		->addDisableState(UISTATE_MENU);
	debugMenu->addSeparator();
	debugMenu->add(new MenuAction(this, SLOT(takeScreen()),  QT_TR_NOOP("&Take screenshot"), Qt::Key_F12))
		->addDisableState(UISTATE_MENU);
	debugMenu->add(new MenuAction(this, SLOT(dumpNextAct()),  QT_TR_NOOP("D&ump next frame to log")))
		->addDisableState(UISTATE_MENU);
	debugMenu->add(new MenuAction(this, SLOT(statsAct()),   QT_TR_NOOP("Show debu&g statistics")))
		->addEventChecked(&g_Config.bShowDebugStats);
	debugMenu->addSeparator();
	debugMenu->add(new MenuAction(this, SLOT(consoleAct()),   QT_TR_NOOP("&Log console"), Qt::CTRL + Qt::Key_L))
		->addDisableState(UISTATE_MENU);

	// Game settings
	MenuTree* gameSettingsMenu = new MenuTree(this, menuBar(), QT_TR_NOOP("&Game settings"));
	gameSettingsMenu->add(new MenuAction(this, SLOT(languageAct()),        QT_TR_NOOP("La&nguage...")));
	gameSettingsMenu->add(new MenuAction(this, SLOT(controlMappingAct()),        QT_TR_NOOP("C&ontrol mapping...")));
	gameSettingsMenu->add(new MenuAction(this, SLOT(displayLayoutEditorAct()),        QT_TR_NOOP("Display layout editor...")));
	gameSettingsMenu->add(new MenuAction(this, SLOT(moreSettingsAct()),        QT_TR_NOOP("&More settings...")));
	gameSettingsMenu->addSeparator();
#if QT_VERSION < QT_VERSION_CHECK(5, 0, 0)
	gameSettingsMenu->add(new MenuAction(this, SLOT(fullscrAct()), QT_TR_NOOP("&Fullscreen"), Qt::Key_F11))
#else
	gameSettingsMenu->add(new MenuAction(this, SLOT(fullscrAct()), QT_TR_NOOP("Fu&llscreen"), QKeySequence::FullScreen))
#endif
		->addEventChecked(&g_Config.bFullScreen);
	MenuTree* renderingResolutionMenu = new MenuTree(this, gameSettingsMenu, QT_TR_NOOP("&Rendering resolution"));
	renderingResolutionGroup = new MenuActionGroup(this, renderingResolutionMenu, SLOT(renderingResolutionGroup_triggered(QAction *)),
		QStringList() << "&Auto" << "&1x" << "&2x" << "&3x" << "&4x" << "&5x" << "&6x" << "&7x" << "&8x" << "&9x" << "1&0x",
		QList<int>() << 0 << 1 << 2 << 3 << 4 << 5 << 6 << 7 << 8 << 9 << 10);
	// - Window Size
	MenuTree* windowMenu = new MenuTree(this, gameSettingsMenu, QT_TR_NOOP("&Window size"));
	windowGroup = new MenuActionGroup(this, windowMenu, SLOT(windowGroup_triggered(QAction *)),
		QStringList() << "&1x" << "&2x" << "&3x" << "&4x" << "&5x" << "&6x" << "&7x" << "&8x" << "&9x" << "1&0x",
		QList<int>() << 1 << 2 << 3 << 4 << 5 << 6 << 7 << 8 << 9 << 10);

	MenuTree* renderingModeMenu = new MenuTree(this, gameSettingsMenu, QT_TR_NOOP("Rendering m&ode"));
	renderingModeGroup = new MenuActionGroup(this, renderingModeMenu, SLOT(renderingModeGroup_triggered(QAction *)),
		QStringList() << "&Skip buffered effects (non-buffered, faster)" << "&Buffered rendering",
		QList<int>() << 0 << 1);
	MenuTree* frameSkippingMenu = new MenuTree(this, gameSettingsMenu, QT_TR_NOOP("&Frame skipping"));
	frameSkippingMenu->add(new MenuAction(this, SLOT(autoframeskipAct()),        QT_TR_NOOP("&Auto")))
		->addEventChecked(&g_Config.bAutoFrameSkip);
	frameSkippingMenu->addSeparator();
	frameSkippingGroup = new MenuActionGroup(this, frameSkippingMenu, SLOT(frameSkippinGroup_triggered(QAction *)),
		QStringList() << "&Off" << "&1" << "&2" << "&3" << "&4" << "&5" << "&6" << "&7" << "&8",
		QList<int>() << 0 << 1 << 2 << 3 << 4 << 5 << 6 << 7 << 8);
	MenuTree* frameSkippingTypeMenu = new MenuTree(this, gameSettingsMenu, QT_TR_NOOP("Frame skipping type"));
	frameSkippingTypeGroup = new MenuActionGroup(this, frameSkippingTypeMenu, SLOT(frameSkippingTypeGroup_triggered(QAction *)),
		QStringList() << "Skip number of frames" << "Skip percent of FPS",
		QList<int>() << 0 << 1);
	MenuTree* textureFilteringMenu = new MenuTree(this, gameSettingsMenu, QT_TR_NOOP("Te&xture filtering"));
	textureFilteringGroup = new MenuActionGroup(this, textureFilteringMenu, SLOT(textureFilteringGroup_triggered(QAction *)),
		QStringList() << "&Auto" << "&Nearest" << "&Linear" << "Auto Max &Quality",
		QList<int>() << 1 << 2 << 3 << 4);
	MenuTree* screenScalingFilterMenu = new MenuTree(this, gameSettingsMenu, QT_TR_NOOP("Scr&een scaling filter"));
	screenScalingFilterGroup = new MenuActionGroup(this, screenScalingFilterMenu, SLOT(screenScalingFilterGroup_triggered(QAction *)),
		QStringList() << "&Linear" << "&Nearest",
		QList<int>() << 0 << 1);

	MenuTree* textureScalingMenu = new MenuTree(this, gameSettingsMenu, QT_TR_NOOP("&Texture scaling"));
	textureScalingLevelGroup = new MenuActionGroup(this, textureScalingMenu, SLOT(textureScalingLevelGroup_triggered(QAction *)),
		QStringList() << "&Off" << "&2x" << "&3x" << "&4x" << "&5x",
		QList<int>() << 1 << 2 << 3 << 4 << 5);
	textureScalingMenu->addSeparator();
	textureScalingTypeGroup = new MenuActionGroup(this, textureScalingMenu, SLOT(textureScalingTypeGroup_triggered(QAction *)),
		QStringList() << "&xBRZ" << "&Hybrid" << "&Bicubic" << "H&ybrid + bicubic",
		QList<int>() << 0 << 1 << 2 << 3);
	textureScalingMenu->addSeparator();
	textureScalingMenu->add(new MenuAction(this, SLOT(deposterizeAct()),        QT_TR_NOOP("&Deposterize")))
		->addEventChecked(&g_Config.bTexDeposterize);

	gameSettingsMenu->add(new MenuAction(this, SLOT(transformAct()),     QT_TR_NOOP("&Hardware transform")))
		->addEventChecked(&g_Config.bHardwareTransform);
	gameSettingsMenu->add(new MenuAction(this, SLOT(vertexCacheAct()),   QT_TR_NOOP("&Vertex cache")))
		->addEventChecked(&g_Config.bVertexCache);
	gameSettingsMenu->add(new MenuAction(this, SLOT(showFPSAct()), QT_TR_NOOP("&Show FPS counter")))
		->addEventChecked(&g_Config.iShowFPSCounter);
	gameSettingsMenu->addSeparator();
	gameSettingsMenu->add(new MenuAction(this, SLOT(audioAct()),   QT_TR_NOOP("Enable s&ound")))
		->addEventChecked(&g_Config.bEnableSound);
	gameSettingsMenu->addSeparator();
	gameSettingsMenu->add(new MenuAction(this, SLOT(cheatsAct()),   QT_TR_NOOP("Enable &cheats"), Qt::CTRL + Qt::Key_T))
		->addEventChecked(&g_Config.bEnableCheats);
	gameSettingsMenu->addSeparator();
	gameSettingsMenu->add(new MenuAction(this, SLOT(chatAct()),   QT_TR_NOOP("Open chat"), Qt::CTRL + Qt::Key_C))
		->SetEnabledFunc([=]() {
			return g_Config.bEnableNetworkChat && GetUIState() == UISTATE_INGAME;
		});

	// Help
	MenuTree* helpMenu = new MenuTree(this, menuBar(),    QT_TR_NOOP("&Help"));
	helpMenu->add(new MenuAction(this, SLOT(websiteAct()),    QT_TR_NOOP("Visit www.&ppsspp.org")));
	helpMenu->add(new MenuAction(this, SLOT(forumAct()),      QT_TR_NOOP("PPSSPP &forums")));
	helpMenu->add(new MenuAction(this, SLOT(goldAct()),       QT_TR_NOOP("Buy &Gold")));
	helpMenu->add(new MenuAction(this, SLOT(gitAct()),        QT_TR_NOOP("Git&Hub")));
	helpMenu->add(new MenuAction(this, SLOT(discordAct()),      QT_TR_NOOP("Discord")));
	helpMenu->addSeparator();
	helpMenu->add(new MenuAction(this, SLOT(aboutAct()),      QT_TR_NOOP("&About PPSSPP...")));

	retranslate();
}
