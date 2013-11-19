#include "mainwindow.h"

#include "Core/MIPS/MIPSDebugInterface.h"
#include "Core/Debugger/SymbolMap.h"
#include "Core/SaveState.h"
#include "Core/System.h"
#include "base/display.h"
#include "base/NKCodeFromQt.h"
#include "GPU/GPUInterface.h"
#include "UI/GamepadEmu.h"

#include "QtHost.h"
#include "qtemugl.h"
#include "EmuThread.h"

// TODO: Make this class thread-aware. Can't send events to a different thread. Currently only works on X11.
// Needs to use QueuedConnection for signals/slots.
MainWindow::MainWindow(QWidget *parent) :
	QMainWindow(parent),
	timer(this),
	nextState(CORE_POWERDOWN),
	lastUIState(UISTATE_MENU),
	dialogDisasm(0),
	memoryWindow(0),
	memoryTexWindow(0),
	displaylistWindow(0)
{
	host = new QtHost(this);
	emugl = new QtEmuGL();
	setCentralWidget(emugl);
	emugl->init(&input_state);
	emugl->resize(pixel_xres, pixel_yres);
	emugl->setMinimumSize(pixel_xres, pixel_yres);
	emugl->setMaximumSize(pixel_xres, pixel_yres);
	QObject::connect(emugl, SIGNAL(doubleClick()), this, SLOT(fullscreenAct_triggered()) );

	createLanguageMenu();
	UpdateMenus();

	int zoom = g_Config.iInternalResolution;
	if (zoom < 1) zoom = 1;
	if (zoom > 4) zoom = 4;
	SetZoom(zoom);

	SetGameTitle(fileToStart);

	connect(&timer, SIGNAL(timeout()), this, SLOT(Update()));
	timer.setInterval(16); // 62.5 refreshes but close enough
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
}

void MainWindow::ShowMemory(u32 addr)
{
	if(memoryWindow)
		memoryWindow->Goto(addr);
}

inline float clamp1(float x) {
	if (x > 1.0f) return 1.0f;
	if (x < -1.0f) return -1.0f;
	return x;
}

void MainWindow::Update()
{
	emugl->updateGL();

	if (lastUIState != globalUIState) {
		lastUIState = globalUIState;
		if (lastUIState == UISTATE_INGAME && g_Config.bFullScreen && !QApplication::overrideCursor() && !g_Config.bShowTouchControls)
			QApplication::setOverrideCursor(QCursor(Qt::BlankCursor));
		if (lastUIState != UISTATE_INGAME && g_Config.bFullScreen && QApplication::overrideCursor())
			QApplication::restoreOverrideCursor();

		UpdateMenus();
	}
}

void MainWindow::UpdateMenus()
{
	bool enable = globalUIState == UISTATE_MENU;
	// File
	openAct->setEnabled(enable);
	closeAct->setEnabled(!enable);
	qlstateAct->setEnabled(!enable);
	qsstateAct->setEnabled(!enable);
	lstateAct->setEnabled(!enable);
	sstateAct->setEnabled(!enable);
	// Emulation
	runAct->setEnabled(Core_IsStepping() || globalUIState == UISTATE_PAUSEMENU);
	pauseAct->setEnabled(globalUIState == UISTATE_INGAME);
	resetAct->setEnabled(globalUIState == UISTATE_INGAME);
	runonloadAct->setChecked(g_Config.bAutoRun);
	// Debug
	lmapAct->setEnabled(!enable);
	smapAct->setEnabled(!enable);
	resetTableAct->setEnabled(!enable);
	dumpNextAct->setEnabled(!enable);
	disasmAct->setEnabled(!enable);
	dpyListAct->setEnabled(!enable);
	consoleAct->setEnabled(!enable);
	memviewAct->setEnabled(!enable);
	memviewTexAct->setEnabled(!enable);
	// Options
	dynarecAct->setChecked(g_Config.bJit);
	fastmemAct->setChecked(g_Config.bFastMemory);
	ignoreIllegalAct->setChecked(g_Config.bIgnoreBadMemAccess);

	audioAct->setChecked(g_Config.bEnableSound);

	foreach(QAction * action, anisotropicGroup->actions()) {
		if (g_Config.iAnisotropyLevel == action->data().toInt()) {
			action->setChecked(true);
			break;
		}
	}

	bufferRenderAct->setChecked(g_Config.iRenderingMode == 1);
	linearAct->setChecked(3 == g_Config.iTexFiltering);

	foreach(QAction * action, screenGroup->actions()) {
		if (g_Config.iInternalResolution == action->data().toInt()) {
			action->setChecked(true);
			break;
		}
	}

	stretchAct->setChecked(g_Config.bStretchToDisplay);
	transformAct->setChecked(g_Config.bHardwareTransform);
	vertexCacheAct->setChecked(g_Config.bVertexCache);
	frameskipAct->setChecked(g_Config.iFrameSkip != 0);

	statsAct->setChecked(g_Config.bShowDebugStats);
	showFPSAct->setChecked(g_Config.iShowFPSCounter);

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
}

void MainWindow::changeEvent(QEvent *e)
{
//	if (e->type() == QEvent::LanguageChange)
//		ui->retranslateUi(this);
}

void MainWindow::closeEvent(QCloseEvent *)
{
	exitAct_triggered();
}

void MainWindow::keyPressEvent(QKeyEvent *e)
{
	if(isFullScreen() && e->key() == Qt::Key_F11)
	{
		fullscreenAct_triggered();
		return;
	}

	NativeKey(KeyInput(DEVICE_ID_KEYBOARD, KeyMapRawQttoNative.find(e->key())->second, KEY_DOWN));
}

void MainWindow::keyReleaseEvent(QKeyEvent *e)
{
	NativeKey(KeyInput(DEVICE_ID_KEYBOARD, KeyMapRawQttoNative.find(e->key())->second, KEY_UP));
}

/* SLOTS */
void MainWindow::Boot()
{
	dialogDisasm = new Debugger_Disasm(currentDebugMIPS, this, this);
	if(g_Config.bShowDebuggerOnLoad)
		dialogDisasm->show();

	if(g_Config.bFullScreen != isFullScreen())
		fullscreenAct_triggered();

	memoryWindow = new Debugger_Memory(currentDebugMIPS, this, this);
	memoryTexWindow = new Debugger_MemoryTex(this);
	displaylistWindow = new Debugger_DisplayList(currentDebugMIPS, this, this);

	notifyMapsLoaded();

	if (nextState == CORE_RUNNING)
		runAct_triggered();
	UpdateMenus();
}

void MainWindow::CoreEmitWait(bool isWaiting)
{
	// Unlock mutex while core is waiting;
	EmuThread_LockDraw(!isWaiting);
}

void MainWindow::openAct_triggered()
{
	QString filename = QFileDialog::getOpenFileName(NULL, "Load File", g_Config.currentDirectory.c_str(), "PSP ROMs (*.pbp *.elf *.iso *.cso *.prx)");
	if (QFile::exists(filename))
	{
		QFileInfo info(filename);
		g_Config.currentDirectory = info.absolutePath().toStdString();
		NativeMessageReceived("boot", filename.toStdString().c_str());
	}
}

void MainWindow::closeAct_triggered()
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

void MainWindow::qlstateAct_triggered()
{
	SaveState::LoadSlot(0, SaveStateActionFinished, this);
}

void MainWindow::qsstateAct_triggered()
{
	SaveState::SaveSlot(0, SaveStateActionFinished, this);
}

void MainWindow::lstateAct_triggered()
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

void MainWindow::sstateAct_triggered()
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

void MainWindow::exitAct_triggered()
{
	closeAct_triggered();
	QApplication::exit(0);
}

void MainWindow::runAct_triggered()
{
	NativeMessageReceived("run", "");
}

void MainWindow::pauseAct_triggered()
{
	NativeMessageReceived("pause", "");
}

void MainWindow::resetAct_triggered()
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

void MainWindow::runonloadAct_triggered()
{
	runonloadAct->setChecked(g_Config.bAutoRun = !g_Config.bAutoRun);
}

void MainWindow::lmapAct_triggered()
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

void MainWindow::smapAct_triggered()
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

void MainWindow::resetTableAct_triggered()
{
	symbolMap.Clear();
	notifyMapsLoaded();
}

void MainWindow::dumpNextAct_triggered()
{
	gpu->DumpNextFrame();
}

void MainWindow::disasmAct_triggered()
{
	if(dialogDisasm)
		dialogDisasm->show();
}

void MainWindow::dpyListAct_triggered()
{
	if(displaylistWindow)
		displaylistWindow->show();
}

void MainWindow::consoleAct_triggered()
{
	LogManager::GetInstance()->GetConsoleListener()->Show(LogManager::GetInstance()->GetConsoleListener()->Hidden());
}

void MainWindow::memviewAct_triggered()
{
	if (memoryWindow)
		memoryWindow->show();
}

void MainWindow::memviewTexAct_triggered()
{
	if(memoryTexWindow)
		memoryTexWindow->show();
}

void MainWindow::stretchAct_triggered()
{
	g_Config.bStretchToDisplay = !g_Config.bStretchToDisplay;
	if (gpu)
		gpu->Resized();
}

void MainWindow::fullscreenAct_triggered()
{
	if(isFullScreen()) {
		g_Config.bFullScreen = false;
		menuBar()->setVisible(true);
		showNormal();
		SetZoom(g_Config.iInternalResolution);
		InitPadLayout();
		if (globalUIState == UISTATE_INGAME && QApplication::overrideCursor())
			QApplication::restoreOverrideCursor();

	}
	else {
		g_Config.bFullScreen = true;
		menuBar()->setVisible(false);

		// Remove constraint
		emugl->setMinimumSize(0, 0);
		emugl->setMaximumSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX);
		//centralwidget->setFixedSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX);
		setFixedSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX);

		showFullScreen();

		int width = (int) QApplication::desktop()->screenGeometry().width();
		int height = (int) QApplication::desktop()->screenGeometry().height();
		PSP_CoreParameter().pixelWidth = width;
		PSP_CoreParameter().pixelHeight = height;
		PSP_CoreParameter().outputWidth = width;
		PSP_CoreParameter().outputHeight = height;
		PSP_CoreParameter().renderWidth = width;
		PSP_CoreParameter().renderHeight = height;

		pixel_xres = width;
		pixel_yres = height;
		dp_xres = pixel_xres;
		dp_yres = pixel_yres;
		if (gpu)
			gpu->Resized();
		InitPadLayout();
		if (globalUIState == UISTATE_INGAME && !g_Config.bShowTouchControls)
			QApplication::setOverrideCursor(QCursor(Qt::BlankCursor));

	}
	fullscreenAct->setChecked(g_Config.bFullScreen);
}

void MainWindow::websiteAct_triggered()
{
	QDesktopServices::openUrl(QUrl("http://www.ppsspp.org/"));
}

void MainWindow::aboutAct_triggered()
{
	// TODO display about
}

/* Private functions */
void MainWindow::SetZoom(int zoom) {
	g_Config.iInternalResolution = zoom;

	pixel_xres = 480 * zoom;
	pixel_yres = 272 * zoom;
	dp_xres = pixel_xres;
	dp_yres = pixel_yres;

	emugl->resize(pixel_xres, pixel_yres);
	emugl->setMinimumSize(pixel_xres, pixel_yres);
	emugl->setMaximumSize(pixel_xres, pixel_yres);

	setFixedSize(sizeHint());
	resize(sizeHint());

	PSP_CoreParameter().pixelWidth = pixel_xres;
	PSP_CoreParameter().pixelHeight = pixel_yres;
	PSP_CoreParameter().outputWidth = pixel_xres;
	PSP_CoreParameter().outputHeight = pixel_yres;

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
// In Qt5 we could just use lambdas here
#define NEW_ACTION(menu, name, text, slot) \
	name = new QAction(tr(text), this); \
	connect(name, SIGNAL(triggered()), this, SLOT(slot())); \
	menu->addAction(name);

#define NEW_ACTION_CHK(menu, name, text, slot) \
	NEW_ACTION(menu, name, text, slot) \
	name->setCheckable(true);

#define NEW_ACTION_KEY(menu, name, text, slot, key) \
	NEW_ACTION(menu, name, text, slot) \
	name->setShortcut(key);

#define NEW_ACTION_KEY_CHK(menu, name, text, slot, key) \
	NEW_ACTION_CHK(menu, name, text, slot) \
	name->setShortcut(key);

#define NEW_GROUP(menu, group, stringlist, valuelist, slot) \
{ \
	group = new QActionGroup(this); \
	QListIterator<int> i(valuelist); \
	foreach(QString name, stringlist) { \
		QAction *action = new QAction(name, this); \
		action->setCheckable(true); \
		action->setData(i.next()); \
		group->addAction(action); \
	} \
	connect(group, SIGNAL(triggered(QAction *)), this, SLOT(slot(QAction *))); \
	menu->addActions(group->actions()); \
}

#define NEW_GROUP_KEYS(menu, group, stringlist, valuelist, keylist, slot) \
{ \
	group = new QActionGroup(this); \
	QListIterator<int> i(valuelist); \
	QListIterator<int> k(keylist); \
	foreach(QString name, stringlist) { \
		QAction *action = new QAction(name, this); \
		action->setCheckable(true); \
		action->setData(i.next()); \
		action->setShortcut(k.next()); \
		group->addAction(action); \
	} \
	connect(group, SIGNAL(triggered(QAction *)), this, SLOT(slot(QAction *))); \
	menu->addActions(group->actions()); \
}

	// File
    QMenu* fileMenu = menuBar()->addMenu(tr("&File"));
	NEW_ACTION_KEY(fileMenu, openAct, "&Open...", openAct_triggered, QKeySequence::Open);
	NEW_ACTION_KEY(fileMenu, closeAct, "&Close", closeAct_triggered, QKeySequence::Close);
    fileMenu->addSeparator();
	NEW_ACTION_KEY(fileMenu, qlstateAct, "Quickload State", qlstateAct_triggered, Qt::Key_F4);
	NEW_ACTION_KEY(fileMenu, qsstateAct, "Quicksave State", qsstateAct_triggered, Qt::Key_F2);
	NEW_ACTION(fileMenu, lstateAct, "&Load State File...", lstateAct_triggered);
	NEW_ACTION(fileMenu, sstateAct, "&Save State File...", sstateAct_triggered);
    fileMenu->addSeparator();
	NEW_ACTION(fileMenu, exitAct, "E&xit", exitAct_triggered);

	// Emulation
    QMenu* emuMenu = menuBar()->addMenu(tr("&Emulation"));
	NEW_ACTION_KEY(emuMenu, runAct, "&Run", runAct_triggered, Qt::Key_F7);
	NEW_ACTION_KEY(emuMenu, pauseAct, "&Pause", pauseAct_triggered, Qt::Key_F8);
	NEW_ACTION(emuMenu, resetAct, "Re&set", resetAct_triggered);
    emuMenu->addSeparator();
	NEW_ACTION_CHK(emuMenu, runonloadAct, "Run on loa&d", runonloadAct_triggered);

	// Debug
    QMenu* debugMenu = menuBar()->addMenu(tr("De&bug"));
	NEW_ACTION(debugMenu, lmapAct, "Load Map File...", lmapAct_triggered);
	NEW_ACTION(debugMenu, smapAct, "Save Map File...", smapAct_triggered);
	NEW_ACTION(debugMenu, resetTableAct, "Reset Symbol Table", resetTableAct_triggered);
    debugMenu->addSeparator();
	NEW_ACTION(debugMenu, dumpNextAct, "Dump next frame to log", dumpNextAct_triggered);
    debugMenu->addSeparator();
	NEW_ACTION_KEY(debugMenu, disasmAct, "Disassembly", disasmAct_triggered, Qt::CTRL + Qt::Key_D);
	NEW_ACTION(debugMenu, dpyListAct, "Display List...", dpyListAct_triggered);
	NEW_ACTION(debugMenu, consoleAct, "Log Console", consoleAct_triggered);
	NEW_ACTION(debugMenu, memviewAct, "Memory View", memviewAct_triggered);
	NEW_ACTION(debugMenu, memviewTexAct, "Memory View Texture", memviewTexAct_triggered);

	// Options
    QMenu* optionsMenu = menuBar()->addMenu(tr("&Options"));
	// - Core
	QMenu* coreMenu = optionsMenu->addMenu(tr("&Core"));
	NEW_ACTION_CHK(coreMenu, dynarecAct, "&Dynarec", dynarecAct_triggered);
	NEW_ACTION_CHK(coreMenu, fastmemAct, "Fast &Memory (unstable)", fastmemAct_triggered);
	NEW_ACTION_CHK(coreMenu, ignoreIllegalAct, "&Ignore illegal reads/writes", ignoreIllegalAct_triggered);
	// - Video
	QMenu* videoMenu = optionsMenu->addMenu(tr("&Video"));
	// - Anisotropic Filtering
	QMenu* anisotropicMenu = videoMenu->addMenu(tr("&Anisotropic Filtering"));
	NEW_GROUP(anisotropicMenu, anisotropicGroup, QStringList() << "Off" << "2x" << "4x" << "8x" << "16x",
		QList<int>() << 0 << 1 << 2 << 3 << 4, anisotropic_triggered);
	NEW_ACTION_KEY(videoMenu, bufferRenderAct, "&Buffered Rendering", bufferRenderAct_triggered, Qt::Key_F5);
	NEW_ACTION_CHK(videoMenu, linearAct, "&Linear Filtering", linearAct_triggered);
	videoMenu->addSeparator();
	// - Screen Size
	QMenu* screenMenu = videoMenu->addMenu(tr("&Screen Size"));
	NEW_GROUP_KEYS(screenMenu, screenGroup, QStringList() << "1x" << "2x" << "3x" << "4x", QList<int>() << 1 << 2 << 3 << 4,
		QList<int>() << Qt::CTRL + Qt::Key_1 << Qt::CTRL + Qt::Key_2 << Qt::CTRL + Qt::Key_3 << Qt::CTRL + Qt::Key_4, screen_triggered);
	NEW_ACTION_CHK(videoMenu, stretchAct, "&Stretch to Display", stretchAct_triggered);
	videoMenu->addSeparator();
	NEW_ACTION_KEY_CHK(videoMenu, transformAct, "&Hardware Transform", transformAct_triggered, Qt::Key_F6);
	NEW_ACTION_CHK(videoMenu, vertexCacheAct, "&VertexCache", vertexCacheAct_triggered);
	NEW_ACTION_CHK(videoMenu, frameskipAct, "&Frameskip", frameskipAct_triggered);
	
	NEW_ACTION_CHK(optionsMenu, audioAct, "&Audio", audioAct_triggered);
	optionsMenu->addSeparator();
	NEW_ACTION_KEY_CHK(optionsMenu, fullscreenAct, "&Fullscreen", fullscreenAct_triggered, Qt::Key_F11);
	NEW_ACTION_CHK(optionsMenu, statsAct, "&Show debug statistics", statsAct_triggered);
	NEW_ACTION_CHK(optionsMenu, showFPSAct, "&Show FPS", showFPSAct_triggered);
	optionsMenu->addSeparator();
	// - Log Levels
	QMenu* levelsMenu = optionsMenu->addMenu(tr("Lo&g levels"));
	QMenu* defaultLogMenu = levelsMenu->addMenu(tr("Default"));
	NEW_GROUP(defaultLogMenu, defaultLogGroup, QStringList() << tr("Debug") << tr("Warning") << tr("Info") << tr("Error"),
		QList<int>() << LogTypes::LDEBUG << LogTypes::LWARNING << LogTypes::LINFO << LogTypes::LERROR, defaultLog_triggered);
	QMenu* g3dLogMenu = levelsMenu->addMenu(tr("G3D"));
	NEW_GROUP(g3dLogMenu, g3dLogGroup, QStringList() << tr("Debug") << tr("Warning") << tr("Info") << tr("Error"),
		QList<int>() << LogTypes::LDEBUG << LogTypes::LWARNING << LogTypes::LINFO << LogTypes::LERROR, hleLog_triggered);
	QMenu* hleLogMenu = levelsMenu->addMenu(tr("HLE"));
	NEW_GROUP(hleLogMenu, hleLogGroup, QStringList() << tr("Debug") << tr("Warning") << tr("Info") << tr("Error"),
		QList<int>() << LogTypes::LDEBUG << LogTypes::LWARNING << LogTypes::LINFO << LogTypes::LERROR, g3dLog_triggered);
	optionsMenu->addSeparator();
	// - Language
	QMenu* langMenu = optionsMenu->addMenu(tr("&Language"));
	langGroup = new QActionGroup(this);
	QStringList fileNames = QDir(":/languages").entryList(QStringList("ppsspp_*.qm"));

	if (fileNames.size() == 0)
	{
		QAction *action = new QAction(tr("No translations"), this);
		action->setDisabled(true);
		langGroup->addAction(action);
	} else {
		connect(langGroup, SIGNAL(triggered(QAction *)), this, SLOT(langChanged(QAction *)));
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

			langGroup->addAction(action);

			// TODO check en as default until we save language to config
			if ("en" == locale)
			{
				action->setChecked(true);
				currentLanguage = "en";
			}
		}
	}
	langMenu->addActions(langGroup->actions());
	
	// Help
    QMenu* helpMenu = menuBar()->addMenu(tr("&Help"));
	NEW_ACTION(helpMenu, websiteAct, "&Go to official website", websiteAct_triggered);
	NEW_ACTION(helpMenu, aboutAct, "&About PPSSPP...", aboutAct_triggered);
}

void MainWindow::notifyMapsLoaded()
{
	if (dialogDisasm)
		dialogDisasm->NotifyMapLoaded();
	if (memoryWindow)
		memoryWindow->NotifyMapLoaded();
}
