// Qt Desktop UI: works on Linux, Windows and Mac OSX
#include "mainwindow.h"

#include <QApplication>
#include <QDesktopServices>
#include <QDesktopWidget>
#include <QFileDialog>
#include <QMessageBox>

#include "base/display.h"
#include "base/NativeApp.h"
#include "Core/MIPS/MIPSDebugInterface.h"
#include "Core/Debugger/SymbolMap.h"
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
	QDesktopWidget *desktop = QApplication::desktop();
	int screenNum = QProcessEnvironment::systemEnvironment().value("SDL_VIDEO_FULLSCREEN_HEAD", "0").toInt();
	
	// Move window to top left coordinate of selected screen
	QRect rect = desktop->screenGeometry(screenNum);
	move(rect.topLeft());

	SetGameTitle("");
	emugl = new MainUI(this);

	setCentralWidget(emugl);
	createMenus();
	updateMenus();

	SetWindowScale(-1);
	
	if(fullscreen)
	  fullscrAct();

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
		if (lastUIState == UISTATE_INGAME && g_Config.bFullScreen && !QApplication::overrideCursor() && !g_Config.bShowTouchControls)
			QApplication::setOverrideCursor(QCursor(Qt::BlankCursor));
		if (lastUIState != UISTATE_INGAME && g_Config.bFullScreen && QApplication::overrideCursor())
			QApplication::restoreOverrideCursor();

		updateMenus();
	}

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

void MainWindow::updateMenus()
{
	foreach(QAction * action, anisotropicGroup->actions()) {
		if (g_Config.iAnisotropyLevel == action->data().toInt()) {
			action->setChecked(true);
			break;
		}
	}

	foreach(QAction * action, screenGroup->actions()) {
		int width = (g_Config.IsPortrait() ? 272 : 480) * action->data().toInt();
		int height = (g_Config.IsPortrait() ? 480 : 272) * action->data().toInt();
		if (g_Config.iWindowWidth == width && g_Config.iWindowHeight == height) {
			action->setChecked(true);
			break;
		}
	}

	foreach(QAction * action, displayLayoutGroup->actions()) {
		if (g_Config.iSmallDisplayZoomType == action->data().toInt()) {

			NativeMessageReceived("gpu_resized", "");

			action->setChecked(true);
			break;
		}
	}

	int defaultLevel = LogManager::GetInstance()->GetLogLevel(LogTypes::COMMON);
	foreach(QAction * action, defaultLogGroup->actions()) {
		if (defaultLevel == action->data().toInt()) {
			action->setChecked(true);
			break;
		}
	}

	int g3dLevel = LogManager::GetInstance()->GetLogLevel(LogTypes::G3D);
	foreach(QAction * action, g3dLogGroup->actions()) {
		if (g3dLevel == action->data().toInt()) {
			action->setChecked(true);
			break;
		}
	}

	int hleLevel = LogManager::GetInstance()->GetLogLevel(LogTypes::HLE);
	foreach(QAction * action, hleLogGroup->actions()) {
		if (hleLevel == action->data().toInt()) {
			action->setChecked(true);
			break;
		}
	}
	emit updateMenu();
}

void MainWindow::bootDone()
{
	if(g_Config.bFullScreen != isFullScreen())
		fullscrAct();

	if (nextState == CORE_RUNNING)
		runAct();
	updateMenus();
}

/* SIGNALS */
void MainWindow::openAct()
{
	QString filename = QFileDialog::getOpenFileName(NULL, "Load File", g_Config.currentDirectory.c_str(), "PSP ROMs (*.pbp *.elf *.iso *.cso *.prx)");
	if (QFile::exists(filename))
	{
		QFileInfo info(filename);
		g_Config.currentDirectory = info.absolutePath().toStdString();
		NativeMessageReceived("boot", filename.toStdString().c_str());
	}
}

void MainWindow::closeAct()
{
	updateMenus();

	NativeMessageReceived("stop", "");
	SetGameTitle("");
}

void SaveStateActionFinished(bool result, const std::string &message, void *userdata)
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

void MainWindow::qlstateAct()
{
	std::string gamePath = PSP_CoreParameter().fileToStart;
	SaveState::LoadSlot(gamePath, 0, SaveStateActionFinished, this);
}

void MainWindow::qsstateAct()
{
	std::string gamePath = PSP_CoreParameter().fileToStart;
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
		SaveState::Load(fileNames[0].toStdString(), SaveStateActionFinished, this);
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
		SaveState::Save(fileNames[0].toStdString(), SaveStateActionFinished, this);
	}
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

void MainWindow::resetAct()
{
	updateMenus();

	NativeMessageReceived("reset", "");
}

void MainWindow::runonloadAct()
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
		g_symbolMap->LoadSymbolMap(fileName.toStdString().c_str());
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
		g_symbolMap->SaveSymbolMap(fileNames[0].toStdString().c_str());
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

void MainWindow::fullscrAct()
{
	if(isFullScreen()) {
		g_Config.bFullScreen = false;
		menuBar()->show();
		updateMenus();

		showNormal();
		SetWindowScale(-1);
		InitPadLayout(dp_xres, dp_yres);
		if (GetUIState() == UISTATE_INGAME && QApplication::overrideCursor())
			QApplication::restoreOverrideCursor();
	}
	else {
		g_Config.bFullScreen = true;
		menuBar()->hide();

		emugl->setFixedSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX);
		setMaximumSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX);
		setFixedSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX);

		showFullScreen();

		NativeMessageReceived("gpu_resized", "");
		InitPadLayout(dp_xres, dp_yres);
		if (GetUIState() == UISTATE_INGAME && !g_Config.bShowTouchControls)
			QApplication::setOverrideCursor(QCursor(Qt::BlankCursor));

	}
	
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

void MainWindow::gitAct() 
{
	QDesktopServices::openUrl(QUrl("https://github.com/hrydgard/ppsspp/"));
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
		if (zoom > 4)
			zoom = 4;

		width = (g_Config.IsPortrait() ? 272 : 480) * zoom;
		height = (g_Config.IsPortrait() ? 480 : 272) * zoom;
	}

	g_Config.iWindowWidth = width;
	g_Config.iWindowHeight = height;

	emugl->setFixedSize(g_Config.iWindowWidth, g_Config.iWindowHeight);
	setFixedSize(sizeHint());
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
	fileMenu->add(new MenuAction(this, SLOT(openAct()),       QT_TR_NOOP("&Open..."), QKeySequence::Open))
		->addEnableState(UISTATE_MENU);
	fileMenu->add(new MenuAction(this, SLOT(closeAct()),      QT_TR_NOOP("&Close"), QKeySequence::Close))
		->addDisableState(UISTATE_MENU);
	fileMenu->addSeparator();
	fileMenu->add(new MenuAction(this, SLOT(qlstateAct()),    QT_TR_NOOP("Quickload State"), Qt::Key_F4))
		->addDisableState(UISTATE_MENU);
	fileMenu->add(new MenuAction(this, SLOT(qsstateAct()),    QT_TR_NOOP("Quicksave State"), Qt::Key_F2))
		->addDisableState(UISTATE_MENU);
	fileMenu->add(new MenuAction(this, SLOT(lstateAct()),     QT_TR_NOOP("&Load State File...")))
		->addDisableState(UISTATE_MENU);
	fileMenu->add(new MenuAction(this, SLOT(sstateAct()),     QT_TR_NOOP("&Save State File...")))
		->addDisableState(UISTATE_MENU);
	fileMenu->addSeparator();
	fileMenu->add(new MenuAction(this, SLOT(exitAct()),       QT_TR_NOOP("E&xit"), QKeySequence::Quit));

	// Emulation
	MenuTree* emuMenu = new MenuTree(this, menuBar(),     QT_TR_NOOP("&Emulation"));
	emuMenu->add(new MenuAction(this, SLOT(runAct()),         QT_TR_NOOP("&Run"), Qt::Key_F7))
		->addEnableStepping()->addEnableState(UISTATE_PAUSEMENU);
	emuMenu->add(new MenuAction(this, SLOT(pauseAct()),       QT_TR_NOOP("&Pause"), Qt::Key_F8))
		->addEnableState(UISTATE_INGAME);
	emuMenu->add(new MenuAction(this, SLOT(resetAct()),       QT_TR_NOOP("Re&set")))
		->addEnableState(UISTATE_INGAME);
	emuMenu->addSeparator();
	emuMenu->add(new MenuAction(this, SLOT(runonloadAct()),   QT_TR_NOOP("Run on &load")))
		->addEventChecked(&g_Config.bAutoRun);

	// Debug
	MenuTree* debugMenu = new MenuTree(this, menuBar(),   QT_TR_NOOP("De&bug"));
	debugMenu->add(new MenuAction(this, SLOT(lmapAct()),      QT_TR_NOOP("Load Map File...")))
		->addDisableState(UISTATE_MENU);
	debugMenu->add(new MenuAction(this, SLOT(smapAct()),      QT_TR_NOOP("Save Map File...")))
		->addDisableState(UISTATE_MENU);
	debugMenu->add(new MenuAction(this, SLOT(resetTableAct()),QT_TR_NOOP("Reset Symbol Table")))
		->addDisableState(UISTATE_MENU);
	debugMenu->addSeparator();
	debugMenu->add(new MenuAction(this, SLOT(dumpNextAct()),  QT_TR_NOOP("Dump next frame to log")))
		->addDisableState(UISTATE_MENU);
	debugMenu->add(new MenuAction(this, SLOT(takeScreen()),  QT_TR_NOOP("Take Screenshot"), Qt::Key_F12))
		->addDisableState(UISTATE_MENU);
	debugMenu->addSeparator();
	debugMenu->add(new MenuAction(this, SLOT(consoleAct()),   QT_TR_NOOP("Log Console")))
		->addDisableState(UISTATE_MENU);

	// Options
	MenuTree* optionsMenu = new MenuTree(this, menuBar(), QT_TR_NOOP("&Options"));
	// - Core
	MenuTree* coreMenu = new MenuTree(this, optionsMenu,      QT_TR_NOOP("&Core"));
	coreMenu->add(new MenuAction(this, SLOT(vertexDynarecAct()),  QT_TR_NOOP("&Vertex Decoder Dynarec")))
		->addEventChecked(&g_Config.bVertexDecoderJit);
	coreMenu->add(new MenuAction(this, SLOT(fastmemAct()),        QT_TR_NOOP("Fast &Memory (unstable)")))
		->addEventChecked(&g_Config.bFastMemory);
	coreMenu->add(new MenuAction(this, SLOT(ignoreIllegalAct()),  QT_TR_NOOP("&Ignore Illegal reads/writes")))
		->addEventChecked(&g_Config.bIgnoreBadMemAccess);
	// - Video
	MenuTree* videoMenu = new MenuTree(this, optionsMenu,     QT_TR_NOOP("&Video"));
	// - Anisotropic Filtering
	MenuTree* anisotropicMenu = new MenuTree(this, videoMenu,     QT_TR_NOOP("&Anisotropic Filtering"));
	anisotropicGroup = new MenuActionGroup(this, anisotropicMenu, SLOT(anisotropicGroup_triggered(QAction *)),
		QStringList() << "Off" << "2x" << "4x" << "8x" << "16x",
		QList<int>()  << 0     << 1    << 2    << 3    << 4);
	videoMenu->add(new MenuAction(this, SLOT(bufferRenderAct()),  QT_TR_NOOP("&Buffered Rendering"), Qt::Key_F5))
		->addEventChecked(&g_Config.iRenderingMode);
	videoMenu->add(new MenuAction(this, SLOT(linearAct()),        QT_TR_NOOP("&Linear Filtering")))
		->addEventChecked(&g_Config.iTexFiltering);
	videoMenu->addSeparator();
	// - Screen Size
	MenuTree* screenMenu = new MenuTree(this, videoMenu,          QT_TR_NOOP("&Screen Size"));
	screenGroup = new MenuActionGroup(this, screenMenu, SLOT(screenGroup_triggered(QAction *)),
		QStringList() << "1x" << "2x" << "3x" << "4x",
		QList<int>()  << 1    << 2    << 3    << 4,
		QList<int>() << Qt::CTRL + Qt::Key_1 << Qt::CTRL + Qt::Key_2 << Qt::CTRL + Qt::Key_3 << Qt::CTRL + Qt::Key_4);

	MenuTree* displayLayoutMenu = new MenuTree(this, videoMenu, QT_TR_NOOP("&Display Layout Options"));
	displayLayoutGroup = new MenuActionGroup(this, displayLayoutMenu, SLOT(displayLayoutGroup_triggered(QAction *)),
		QStringList() << "Stretched" << "Partialy stretched" << "Auto Scaling" << "Manual Scaling",
		QList<int>() << 0 << 1 << 2 << 3);
	videoMenu->addSeparator();
	videoMenu->add(new MenuAction(this, SLOT(transformAct()),     QT_TR_NOOP("&Hardware Transform"), Qt::Key_F6))
		->addEventChecked(&g_Config.bHardwareTransform);
	videoMenu->add(new MenuAction(this, SLOT(vertexCacheAct()),   QT_TR_NOOP("&Vertex Cache")))
		->addEventChecked(&g_Config.bVertexCache);
	videoMenu->add(new MenuAction(this, SLOT(frameskipAct()),     QT_TR_NOOP("&Frameskip")))
		->addEventChecked(&g_Config.iFrameSkip);
	optionsMenu->add(new MenuAction(this, SLOT(audioAct()),   QT_TR_NOOP("&Audio")))
		->addEventChecked(&g_Config.bEnableSound);
	optionsMenu->addSeparator();
#if QT_VERSION < QT_VERSION_CHECK(5, 0, 0)
	optionsMenu->add(new MenuAction(this, SLOT(fullscrAct()), QT_TR_NOOP("&Fullscreen"), Qt::Key_F11))
#else
	optionsMenu->add(new MenuAction(this, SLOT(fullscrAct()), QT_TR_NOOP("&Fullscreen"), QKeySequence::FullScreen))
#endif
		->addEventChecked(&g_Config.bFullScreen);
	optionsMenu->add(new MenuAction(this, SLOT(statsAct()),   QT_TR_NOOP("&Show debug statistics")))
		->addEventChecked(&g_Config.bShowDebugStats);
	optionsMenu->add(new MenuAction(this, SLOT(showFPSAct()), QT_TR_NOOP("&Show FPS")))
		->addEventChecked(&g_Config.iShowFPSCounter);
	optionsMenu->addSeparator();
	// - Log Levels
	MenuTree* levelsMenu = new MenuTree(this, optionsMenu,    QT_TR_NOOP("Lo&g levels"));
	QMenu* defaultLogMenu = levelsMenu->addMenu("Default");
	defaultLogGroup = new MenuActionGroup(this, defaultLogMenu, SLOT(defaultLogGroup_triggered(QAction *)),
		QStringList() << "Debug"          << "Warning"          << "Info"          << "Error",
		QList<int>()  << LogTypes::LDEBUG << LogTypes::LWARNING << LogTypes::LINFO << LogTypes::LERROR);
	QMenu* g3dLogMenu = levelsMenu->addMenu("G3D");
	g3dLogGroup = new MenuActionGroup(this, g3dLogMenu, SLOT(g3dLogGroup_triggered(QAction *)),
		QStringList() << "Debug"          << "Warning"          << "Info"          << "Error",
		QList<int>()  << LogTypes::LDEBUG << LogTypes::LWARNING << LogTypes::LINFO << LogTypes::LERROR);
	QMenu* hleLogMenu = levelsMenu->addMenu("HLE");
	hleLogGroup = new MenuActionGroup(this, hleLogMenu, SLOT(hleLogGroup_triggered(QAction *)),
		QStringList() << "Debug"          << "Warning"          << "Info"          << "Error",
		QList<int>()  << LogTypes::LDEBUG << LogTypes::LWARNING << LogTypes::LINFO << LogTypes::LERROR);
	optionsMenu->addSeparator();
	// - Language
	MenuTree* langMenu = new MenuTree(this, optionsMenu,      QT_TR_NOOP("&Language"));
	QActionGroup* langGroup = new QActionGroup(this);
	QStringList fileNames = QDir(":/languages").entryList(QStringList("ppsspp_*.qm"));

	if (fileNames.size() == 0)
	{
		QAction *action = new QAction("No translations", this);
		action->setDisabled(true);
		langGroup->addAction(action);
	} else {
		connect(langGroup, SIGNAL(triggered(QAction *)), this, SLOT(langChanged(QAction *)));
		bool found = false;
		QString currentLocale = g_Config.sLanguageIni.c_str();
		QString currentLang = currentLocale.split('_').first();
		for (int i = 0; i < fileNames.size(); ++i)
		{
			QString locale = fileNames[i];
			locale.truncate(locale.lastIndexOf('.'));
			locale.remove(0, locale.indexOf('_') + 1);

#if QT_VERSION >= QT_VERSION_CHECK(4, 8, 0)
			QString language = QLocale(locale).nativeLanguageName();
#else
			QString language = QLocale::languageToString(QLocale(locale).language());
#endif
			QAction *action = new MenuAction(this, langGroup, locale, language);
			std::string testLang = g_Config.sLanguageIni;
			if (currentLocale == locale || currentLang == locale) {
				action->setChecked(true);
				loadLanguage(locale, false);
				found = true;
			}

			if (!found && locale == "en") {
				action->setChecked(true);
			}
		}
	}
	langMenu->addActions(langGroup->actions());
	
	// Help
	MenuTree* helpMenu = new MenuTree(this, menuBar(),    QT_TR_NOOP("&Help"));
	helpMenu->add(new MenuAction(this, SLOT(websiteAct()),    QT_TR_NOOP("Official &website"), QKeySequence::HelpContents));
	helpMenu->add(new MenuAction(this, SLOT(forumAct()),      QT_TR_NOOP("Official &forum")));
	helpMenu->add(new MenuAction(this, SLOT(gitAct()),        QT_TR_NOOP("&GitHub")));
	helpMenu->add(new MenuAction(this, SLOT(aboutAct()),      QT_TR_NOOP("&About PPSSPP..."), QKeySequence::WhatsThis));

	retranslate();
}
