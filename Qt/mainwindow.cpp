// Qt Desktop UI: works on Linux, Windows and Mac OSX
#include "mainwindow.h"

#include <QApplication>
#include <QDesktopServices>
#include <QDesktopWidget>
#include <QFileDialog>
#include <QKeyEvent>
#include <QMessageBox>

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

MainWindow::MainWindow(QWidget *parent) :
	QMainWindow(parent),
	currentLanguage("en"),
	nextState(CORE_POWERDOWN),
	lastUIState(UISTATE_MENU),
	dialogDisasm(0),
	memoryWindow(0),
	memoryTexWindow(0),
	displaylistWindow(0)
{
	host = new QtHost(this);
	emugl = new QtEmuGL();
	showNormal();
	setCentralWidget(emugl);
	emugl->init(&input_state);
	int zoom = g_Config.iInternalResolution;
	if (zoom < 1) zoom = 1;
	if (zoom > 4) zoom = 4;
	SetZoom(zoom);

	createMenus();
	updateMenus();

	SetGameTitle(fileToStart);

	startTimer(16);

//	if (!fileToStart.isNull())
//	{
//		if (stateToLoad != NULL)
//			SaveState::Load(stateToLoad);
//	}

	QObject::connect(emugl, SIGNAL(doubleClick()), this, SLOT(fullscreenAct_triggered()) );
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

void MainWindow::timerEvent(QTimerEvent *)
{
	emugl->updateGL();

	if (lastUIState != globalUIState) {
		lastUIState = globalUIState;
		if (lastUIState == UISTATE_INGAME && g_Config.bFullScreen && !QApplication::overrideCursor() && !g_Config.bShowTouchControls)
			QApplication::setOverrideCursor(QCursor(Qt::BlankCursor));
		if (lastUIState != UISTATE_INGAME && g_Config.bFullScreen && QApplication::overrideCursor())
			QApplication::restoreOverrideCursor();

		updateMenus();
	}
}

void MainWindow::updateMenus()
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
	vertexDynarecAct->setChecked(g_Config.bVertexDecoderJit);
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
	linearAct->setChecked(g_Config.iTexFiltering > 0);

	foreach(QAction * action, screenGroup->actions()) {
		if (g_Config.iInternalResolution == action->data().toInt()) {
			action->setChecked(true);
			break;
		}
	}

	stretchAct->setChecked(g_Config.bStretchToDisplay);
	transformAct->setChecked(g_Config.bHardwareTransform);
	vertexCacheAct->setChecked(g_Config.bVertexCache);
	frameskipAct->setChecked(g_Config.iFrameSkip > 0);

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

void MainWindow::closeEvent(QCloseEvent *)
{
	exitAct_triggered();
}

void MainWindow::keyPressEvent(QKeyEvent *e)
{
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
	updateMenus();
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
		menuBar()->show();

		showNormal();
		SetZoom(g_Config.iInternalResolution);
		InitPadLayout();
		if (globalUIState == UISTATE_INGAME && QApplication::overrideCursor())
			QApplication::restoreOverrideCursor();
	}
	else {
		g_Config.bFullScreen = true;
		menuBar()->hide();

		emugl->setFixedSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX);
		setMaximumSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX);

		showFullScreen();

		int width = (int) QApplication::desktop()->screenGeometry().width();
		int height = (int) QApplication::desktop()->screenGeometry().height();
		PSP_CoreParameter().pixelWidth = width;
		PSP_CoreParameter().pixelHeight = height;
		PSP_CoreParameter().outputWidth = width;
		PSP_CoreParameter().outputHeight = height;

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
	QMessageBox::about(this, "PPSSPP Qt", QString::fromUtf8("Created by Henrik Rydg\xc3\xa5rd"));
}

/* Private functions */
void MainWindow::SetZoom(int zoom) {
	if (isFullScreen())
		fullscreenAct_triggered();
	g_Config.iInternalResolution = zoom;

	pixel_xres = 480 * zoom;
	pixel_yres = 272 * zoom;
	dp_xres = pixel_xres;
	dp_yres = pixel_yres;

	emugl->setFixedSize(pixel_xres, pixel_yres);
	setFixedSize(sizeHint());

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

void MainWindow::loadLanguage(const QString& language, bool retranslate)
{
	if (currentLanguage != language)
	{
		currentLanguage = language;
		QLocale::setDefault(QLocale(currentLanguage));
		QApplication::removeTranslator(&translator);
		if (translator.load(QString(":/languages/ppsspp_%1.qm").arg(language))) {
			QApplication::installTranslator(&translator);
			if (retranslate)
				retranslateUi();
		}
	}
}

void MainWindow::retranslateUi() {
	fileMenu->setTitle(tr("&File"));
	emuMenu->setTitle(tr("&Emulation"));
	debugMenu->setTitle(tr("De&bug"));
	optionsMenu->setTitle(tr("&Options"));
	coreMenu->setTitle(tr("&Core"));
	videoMenu->setTitle(tr("&Video"));
	anisotropicMenu->setTitle(tr("&Anisotropic Filtering"));
	screenMenu->setTitle(tr("&Screen Size"));
	levelsMenu->setTitle(tr("Lo&g levels"));
	langMenu->setTitle(tr("&Language"));
	helpMenu->setTitle(tr("&Help"));
	emit retranslate();
}

void MainWindow::createMenus()
{

#define NEW_GROUP(menu, group, stringlist, valuelist) \
{ \
	group = new QActionGroup(this); \
	QListIterator<int> i(valuelist); \
	foreach(QString name, stringlist) { \
		QAction *action = new MenuAction(this, group, i.next(), name); \
	} \
	connect(group, SIGNAL(triggered(QAction *)), this, SLOT(group ## _triggered(QAction *))); \
	menu->addActions(group->actions()); \
}

#define NEW_GROUP_KEYS(menu, group, stringlist, valuelist, keylist) \
{ \
	group = new QActionGroup(this); \
	QListIterator<int> i(valuelist); \
	QListIterator<int> k(keylist); \
	foreach(QString name, stringlist) { \
		QAction *action = new MenuAction(this, group, i.next(), name, k.next()); \
	} \
	connect(group, SIGNAL(triggered(QAction *)), this, SLOT(group ## _triggered(QAction *))); \
	menu->addActions(group->actions()); \
}

	// File
	fileMenu = menuBar()->addMenu("");
	openAct = new MenuAction(this, fileMenu, SLOT(openAct_triggered()), QT_TR_NOOP("&Open..."), false, QKeySequence::Open);
	closeAct = new MenuAction(this, fileMenu, SLOT(closeAct_triggered()), QT_TR_NOOP("&Close"), false, QKeySequence::Close);
	fileMenu->addSeparator();
	qlstateAct = new MenuAction(this, fileMenu, SLOT(qlstateAct_triggered()), QT_TR_NOOP("Quickload State"), false, Qt::Key_F4);
	qsstateAct = new MenuAction(this, fileMenu, SLOT(qsstateAct_triggered()), QT_TR_NOOP("Quicksave State"), false, Qt::Key_F2);
	lstateAct = new MenuAction(this, fileMenu, SLOT(lstateAct_triggered()), QT_TR_NOOP("&Load State File..."), false);
	sstateAct = new MenuAction(this, fileMenu, SLOT(sstateAct_triggered()), QT_TR_NOOP("&Save State File..."), false);
	fileMenu->addSeparator();
	exitAct = new MenuAction(this, fileMenu, SLOT(exitAct_triggered()), QT_TR_NOOP("E&xit"), false);

	// Emulation
	emuMenu = menuBar()->addMenu("");
	runAct = new MenuAction(this, emuMenu, SLOT(runAct_triggered()), QT_TR_NOOP("&Run"), false, Qt::Key_F7);
	pauseAct = new MenuAction(this, emuMenu, SLOT(pauseAct_triggered()), QT_TR_NOOP("&Pause"), false, Qt::Key_F8);
	resetAct = new MenuAction(this, emuMenu, SLOT(resetAct_triggered()), QT_TR_NOOP("Re&set"), false);
	emuMenu->addSeparator();
	runonloadAct = new MenuAction(this, emuMenu, SLOT(runonloadAct_triggered()), QT_TR_NOOP("Run on &load"), true);

	// Debug
	debugMenu = menuBar()->addMenu("");
	lmapAct = new MenuAction(this, debugMenu, SLOT(lmapAct_triggered()), QT_TR_NOOP("Load Map File..."), false);
	smapAct = new MenuAction(this, debugMenu, SLOT(smapAct_triggered()), QT_TR_NOOP("Save Map File..."), false);
	resetTableAct = new MenuAction(this, debugMenu, SLOT(resetTableAct_triggered()), QT_TR_NOOP("Reset Symbol Table"), false);
	debugMenu->addSeparator();
	dumpNextAct = new MenuAction(this, debugMenu, SLOT(dumpNextAct_triggered()), QT_TR_NOOP("Dump next frame to log"), false);
	debugMenu->addSeparator();
	disasmAct = new MenuAction(this, debugMenu, SLOT(disasmAct_triggered()), QT_TR_NOOP("Disassembly"), false, Qt::CTRL + Qt::Key_D);
	dpyListAct = new MenuAction(this, debugMenu, SLOT(dpyListAct_triggered()), QT_TR_NOOP("Display List..."), false);
	consoleAct = new MenuAction(this, debugMenu, SLOT(consoleAct_triggered()), QT_TR_NOOP("Log Console"), false);
	memviewAct = new MenuAction(this, debugMenu, SLOT(memviewAct_triggered()), QT_TR_NOOP("Memory View"), false);
	memviewTexAct = new MenuAction(this, debugMenu, SLOT(memviewTexAct_triggered()), QT_TR_NOOP("Memory View Texture"), false);

	// Options
	optionsMenu = menuBar()->addMenu("");
	// - Core
	coreMenu = optionsMenu->addMenu("");
	dynarecAct = new MenuAction(this, coreMenu, SLOT(dynarecAct_triggered()), QT_TR_NOOP("&CPU Dynarec"), true);
	vertexDynarecAct = new MenuAction(this, coreMenu, SLOT(vertexDynarecAct_triggered()), QT_TR_NOOP("&Vertex Decoder Dynarec"), true);
	fastmemAct = new MenuAction(this, coreMenu, SLOT(fastmemAct_triggered()), QT_TR_NOOP("Fast &Memory (unstable)"), true);
	ignoreIllegalAct = new MenuAction(this, coreMenu, SLOT(ignoreIllegalAct_triggered()), QT_TR_NOOP("&Ignore Illegal reads/writes"), true);
	// - Video
	videoMenu = optionsMenu->addMenu("");
	// - Anisotropic Filtering
	anisotropicMenu = videoMenu->addMenu("");
	NEW_GROUP(anisotropicMenu, anisotropicGroup, QStringList() << "Off" << "2x" << "4x" << "8x" << "16x",
		QList<int>() << 0 << 1 << 2 << 3 << 4);
	bufferRenderAct = new MenuAction(this, videoMenu, SLOT(bufferRenderAct_triggered()), QT_TR_NOOP("&Buffered Rendering"), false, Qt::Key_F5);
	linearAct = new MenuAction(this, videoMenu, SLOT(linearAct_triggered()), QT_TR_NOOP("&Linear Filtering"), true);
	videoMenu->addSeparator();
	// - Screen Size
	screenMenu = videoMenu->addMenu("");
	NEW_GROUP_KEYS(screenMenu, screenGroup, QStringList() << "1x" << "2x" << "3x" << "4x", QList<int>() << 1 << 2 << 3 << 4,
		QList<int>() << Qt::CTRL + Qt::Key_1 << Qt::CTRL + Qt::Key_2 << Qt::CTRL + Qt::Key_3 << Qt::CTRL + Qt::Key_4);
	stretchAct = new MenuAction(this, videoMenu, SLOT(stretchAct_triggered()), QT_TR_NOOP("&Stretch to Display"), true);
	videoMenu->addSeparator();
	transformAct = new MenuAction(this, videoMenu, SLOT(transformAct_triggered()), QT_TR_NOOP("&Hardware Transform"), true, Qt::Key_F6);
	vertexCacheAct = new MenuAction(this, videoMenu, SLOT(vertexCacheAct_triggered()), QT_TR_NOOP("&Vertex Cache"), true);
	frameskipAct = new MenuAction(this, videoMenu, SLOT(frameskipAct_triggered()), QT_TR_NOOP("&Frameskip"), true);
	audioAct = new MenuAction(this, videoMenu, SLOT(audioAct_triggered()), QT_TR_NOOP("&Audio"), true);
	optionsMenu->addSeparator();
	fullscreenAct = new MenuAction(this, optionsMenu, SLOT(fullscreenAct_triggered()), QT_TR_NOOP("&Fullscreen"), true, Qt::Key_F11);
	statsAct = new MenuAction(this, optionsMenu, SLOT(statsAct_triggered()), QT_TR_NOOP("&Show debug statistics"), true);
	showFPSAct = new MenuAction(this, optionsMenu, SLOT(showFPSAct_triggered()), QT_TR_NOOP("&Show FPS"), true);
	optionsMenu->addSeparator();
	// - Log Levels
	levelsMenu = optionsMenu->addMenu("");
	QMenu* defaultLogMenu = levelsMenu->addMenu("Default");
	NEW_GROUP(defaultLogMenu, defaultLogGroup, QStringList() << "Debug" << "Warning" << "Info" << "Error",
		QList<int>() << LogTypes::LDEBUG << LogTypes::LWARNING << LogTypes::LINFO << LogTypes::LERROR);
	QMenu* g3dLogMenu = levelsMenu->addMenu("G3D");
	NEW_GROUP(g3dLogMenu, g3dLogGroup, QStringList() << "Debug" << "Warning" << "Info" << "Error",
		QList<int>() << LogTypes::LDEBUG << LogTypes::LWARNING << LogTypes::LINFO << LogTypes::LERROR);
	QMenu* hleLogMenu = levelsMenu->addMenu("HLE");
	NEW_GROUP(hleLogMenu, hleLogGroup, QStringList() << "Debug" << "Warning" << "Info" << "Error",
		QList<int>() << LogTypes::LDEBUG << LogTypes::LWARNING << LogTypes::LINFO << LogTypes::LERROR);
	optionsMenu->addSeparator();
	// - Language
	langMenu = optionsMenu->addMenu("");
	langGroup = new QActionGroup(this);
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

#if QT_VERSION >= 0x040800
			QString language = QLocale(locale).nativeLanguageName();
#else
			QString language = QLocale::languageToString(QLocale(locale).language());
#endif
			QAction *action = new MenuAction(this, langGroup, locale, language);
			std::string testLang = g_Config.sLanguageIni;
			if (currentLocale == locale || currentLang == locale) {
				action->setChecked(true);
				currentLanguage = locale;
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
	helpMenu = menuBar()->addMenu("");
	websiteAct = new MenuAction(this, helpMenu, SLOT(websiteAct_triggered()), QT_TR_NOOP("&Go to official website"), false);
	aboutAct = new MenuAction(this, helpMenu, SLOT(aboutAct_triggered()), QT_TR_NOOP("&About PPSSPP..."), false);

	retranslateUi();
}

void MainWindow::notifyMapsLoaded()
{
	if (dialogDisasm)
		dialogDisasm->NotifyMapLoaded();
	if (memoryWindow)
		memoryWindow->NotifyMapLoaded();
}
