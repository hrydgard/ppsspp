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
		showNormal();
		SetZoom(g_Config.iInternalResolution);
		InitPadLayout();
		if (globalUIState == UISTATE_INGAME && QApplication::overrideCursor())
			QApplication::restoreOverrideCursor();
	}
	else {
		g_Config.bFullScreen = true;

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
	openAct->setText(tr("&Open..."));
	closeAct->setText(tr("&Close"));
	qlstateAct->setText(tr("Quickload State"));
	qsstateAct->setText(tr("Quicksave State"));
	lstateAct->setText(tr("&Load State File..."));
	sstateAct->setText(tr("&Save State File..."));
	exitAct->setText(tr("E&xit"));

	emuMenu->setTitle(tr("&Emulation"));
	runAct->setText(tr("&Run"));
	pauseAct->setText(tr("&Pause"));
	resetAct->setText(tr("Re&set"));
	runonloadAct->setText(tr("Run on &load"));

	debugMenu->setTitle(tr("De&bug"));
	lmapAct->setText(tr("Load Map File..."));
	smapAct->setText(tr("Save Map File..."));
	resetTableAct->setText(tr("Reset Symbol Table"));
	dumpNextAct->setText(tr("Dump next frame to log"));
	disasmAct->setText(tr("Disassembly"));
	dpyListAct->setText(tr("Display List..."));
	consoleAct->setText(tr("Log Console"));
	memviewAct->setText(tr("Memory View"));
	memviewTexAct->setText(tr("Memory View Texture"));

	optionsMenu->setTitle(tr("&Options"));
	coreMenu->setTitle(tr("&Core"));
	dynarecAct->setText(tr("&CPU Dynarec"));
	vertexDynarecAct->setText(tr("&Vertex Decoder Dynarec"));
	fastmemAct->setText(tr("Fast &Memory (unstable)"));
	ignoreIllegalAct->setText(tr("&Ignore Illegal reads/writes"));
	videoMenu->setTitle(tr("&Video"));
	anisotropicMenu->setTitle(tr("&Anisotropic Filtering"));
	bufferRenderAct->setText(tr("&Buffered Rendering"));
	linearAct->setText(tr("&Linear Filtering"));
	screenMenu->setTitle(tr("&Screen Size"));
	stretchAct->setText(tr("&Stretch to Display"));
	transformAct->setText(tr("&Hardware Transform"));
	vertexCacheAct->setText(tr("&VertexCache"));
	frameskipAct->setText(tr("&Frameskip"));
	audioAct->setText(tr("&Audio"));
	fullscreenAct->setText(tr("&Fullscreen"));
	statsAct->setText(tr("&Show debug statistics"));
	showFPSAct->setText(tr("&Show FPS"));
	levelsMenu->setTitle(tr("Lo&g levels"));
	langMenu->setTitle(tr("&Language"));

	helpMenu->setTitle(tr("&Help"));
	websiteAct->setText(tr("&Go to official website"));
	aboutAct->setText(tr("&About PPSSPP..."));
}

void MainWindow::createMenus()
{
// In Qt5 we could just use lambdas here
#define NEW_ACTION(menu, name) \
	name = new QAction(this); \
	connect(name, SIGNAL(triggered()), this, SLOT(name ## _triggered())); \
	menu->addAction(name);

#define NEW_ACTION_CHK(menu, name) \
	NEW_ACTION(menu, name) \
	name->setCheckable(true);

#define NEW_ACTION_KEY(menu, name, key) \
	NEW_ACTION(menu, name) \
	name->setShortcut(key);

#define NEW_ACTION_KEY_CHK(menu, name, key) \
	NEW_ACTION_CHK(menu, name) \
	name->setShortcut(key);

#define NEW_GROUP(menu, group, stringlist, valuelist) \
{ \
	group = new QActionGroup(this); \
	QListIterator<int> i(valuelist); \
	foreach(QString name, stringlist) { \
		QAction *action = new QAction(name, this); \
		action->setCheckable(true); \
		action->setData(i.next()); \
		group->addAction(action); \
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
		QAction *action = new QAction(name, this); \
		action->setCheckable(true); \
		action->setData(i.next()); \
		action->setShortcut(k.next()); \
		group->addAction(action); \
	} \
	connect(group, SIGNAL(triggered(QAction *)), this, SLOT(group ## _triggered(QAction *))); \
	menu->addActions(group->actions()); \
}

	// File
	fileMenu = menuBar()->addMenu("");
	NEW_ACTION_KEY(fileMenu, openAct, QKeySequence::Open);
	NEW_ACTION_KEY(fileMenu, closeAct, QKeySequence::Close);
	fileMenu->addSeparator();
	NEW_ACTION_KEY(fileMenu, qlstateAct, Qt::Key_F4);
	NEW_ACTION_KEY(fileMenu, qsstateAct,Qt::Key_F2);
	NEW_ACTION(fileMenu, lstateAct);
	NEW_ACTION(fileMenu, sstateAct);
	fileMenu->addSeparator();
	NEW_ACTION(fileMenu, exitAct);

	// Emulation
	emuMenu = menuBar()->addMenu("");
	NEW_ACTION_KEY(emuMenu, runAct, Qt::Key_F7);
	NEW_ACTION_KEY(emuMenu, pauseAct, Qt::Key_F8);
	NEW_ACTION(emuMenu, resetAct);
	emuMenu->addSeparator();
	NEW_ACTION_CHK(emuMenu, runonloadAct);

	// Debug
	debugMenu = menuBar()->addMenu("");
	NEW_ACTION(debugMenu, lmapAct);
	NEW_ACTION(debugMenu, smapAct);
	NEW_ACTION(debugMenu, resetTableAct);
	debugMenu->addSeparator();
	NEW_ACTION(debugMenu, dumpNextAct);
	debugMenu->addSeparator();
	NEW_ACTION_KEY(debugMenu, disasmAct, Qt::CTRL + Qt::Key_D);
	NEW_ACTION(debugMenu, dpyListAct);
	NEW_ACTION(debugMenu, consoleAct);
	NEW_ACTION(debugMenu, memviewAct);
	NEW_ACTION(debugMenu, memviewTexAct);

	// Options
	optionsMenu = menuBar()->addMenu("");
	// - Core
	coreMenu = optionsMenu->addMenu("");
	NEW_ACTION_CHK(coreMenu, dynarecAct);
	NEW_ACTION_CHK(coreMenu, vertexDynarecAct);
	NEW_ACTION_CHK(coreMenu, fastmemAct);
	NEW_ACTION_CHK(coreMenu, ignoreIllegalAct);
	// - Video
	videoMenu = optionsMenu->addMenu("");
	// - Anisotropic Filtering
	anisotropicMenu = videoMenu->addMenu("");
	NEW_GROUP(anisotropicMenu, anisotropicGroup, QStringList() << "Off" << "2x" << "4x" << "8x" << "16x",
		QList<int>() << 0 << 1 << 2 << 3 << 4);
	NEW_ACTION_KEY(videoMenu, bufferRenderAct, Qt::Key_F5);
	NEW_ACTION_CHK(videoMenu, linearAct);
	videoMenu->addSeparator();
	// - Screen Size
	screenMenu = videoMenu->addMenu("");
	NEW_GROUP_KEYS(screenMenu, screenGroup, QStringList() << "1x" << "2x" << "3x" << "4x", QList<int>() << 1 << 2 << 3 << 4,
		QList<int>() << Qt::CTRL + Qt::Key_1 << Qt::CTRL + Qt::Key_2 << Qt::CTRL + Qt::Key_3 << Qt::CTRL + Qt::Key_4);
	NEW_ACTION_CHK(videoMenu, stretchAct);
	videoMenu->addSeparator();
	NEW_ACTION_KEY_CHK(videoMenu, transformAct, Qt::Key_F6);
	NEW_ACTION_CHK(videoMenu, vertexCacheAct);
	NEW_ACTION_CHK(videoMenu, frameskipAct);
	NEW_ACTION_CHK(optionsMenu, audioAct);
	optionsMenu->addSeparator();
	NEW_ACTION_KEY_CHK(optionsMenu, fullscreenAct, Qt::Key_F11);
	NEW_ACTION_CHK(optionsMenu, statsAct);
	NEW_ACTION_CHK(optionsMenu, showFPSAct);
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
		QString thisLocale = QLocale().name();
		thisLocale.truncate(thisLocale.indexOf('_'));
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
			if (thisLocale == locale) {
				action->setChecked(true);
				currentLanguage = locale;
				loadLanguage(locale, false);
			}
			else if (currentLanguage == "en" && locale == "en")
			{
				action->setChecked(true);
			}
		}
	}
	langMenu->addActions(langGroup->actions());
	
	// Help
	helpMenu = menuBar()->addMenu("");
	NEW_ACTION(helpMenu, websiteAct);
	NEW_ACTION(helpMenu, aboutAct);

	retranslateUi();
}

void MainWindow::notifyMapsLoaded()
{
	if (dialogDisasm)
		dialogDisasm->NotifyMapLoaded();
	if (memoryWindow)
		memoryWindow->NotifyMapLoaded();
}
