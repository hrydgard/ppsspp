/*
 * Copyright (c) 2012 Sacha Refshauge
 *
 */
// Qt 4.7+ / 5.0+ implementation of the framework.
// Currently supports: Android, Linux, Windows, Mac OSX

#include "ppsspp_config.h"
#include <QApplication>
#include <QClipboard>
#include <QDesktopWidget>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QLocale>
#include <QScreen>
#include <QThread>
#include <QUrl>

#include "ext/glslang/glslang/Public/ShaderLang.h"

#if QT_VERSION > QT_VERSION_CHECK(5, 0, 0)
#include <QStandardPaths>
#ifdef QT_HAS_SYSTEMINFO
#include <QScreenSaver>
#endif
#endif

#ifdef SDL
#include "SDL/SDLJoystick.h"
#include "SDL_audio.h"
#include "SDL_keyboard.h"
#endif

#include "Common/System/NativeApp.h"
#include "Common/System/Request.h"
#include "Common/GPU/OpenGL/GLFeatures.h"
#include "Common/Math/math_util.h"
#include "Common/Profiler/Profiler.h"

#include "QtMain.h"
#include "Qt/mainwindow.h"
#include "Common/Data/Text/I18n.h"
#include "Common/Thread/ThreadUtil.h"
#include "Common/Data/Encoding/Utf8.h"
#include "Common/StringUtils.h"
#include "Core/Config.h"
#include "Core/ConfigValues.h"
#include "Core/HW/Camera.h"
#include "Core/Debugger/SymbolMap.h"

#include <signal.h>
#include <string.h>

// Audio
#define AUDIO_FREQ 44100
#define AUDIO_CHANNELS 2
#define AUDIO_SAMPLES 2048
#define AUDIO_SAMPLESIZE 16
#define AUDIO_BUFFERS 5

MainUI *emugl = nullptr;
static float refreshRate = 60.f;
static int browseFileEvent = -1;
static int browseFolderEvent = -1;
static int inputBoxEvent = -1;

QTCamera *qtcamera = nullptr;
MainWindow *g_mainWindow;

#ifdef SDL
SDL_AudioSpec g_retFmt;

static SDL_AudioDeviceID audioDev = 0;

extern void mixaudio(void *userdata, Uint8 *stream, int len) {
	NativeMix((short *)stream, len / 4, AUDIO_FREQ);
}

static void InitSDLAudioDevice() {
	SDL_AudioSpec fmt;
	memset(&fmt, 0, sizeof(fmt));
	fmt.freq = 44100;
	fmt.format = AUDIO_S16;
	fmt.channels = 2;
	fmt.samples = 256;
	fmt.callback = &mixaudio;
	fmt.userdata = nullptr;

	audioDev = 0;
	if (!g_Config.sAudioDevice.empty()) {
		audioDev = SDL_OpenAudioDevice(g_Config.sAudioDevice.c_str(), 0, &fmt, &g_retFmt, SDL_AUDIO_ALLOW_FREQUENCY_CHANGE);
		if (audioDev <= 0) {
			WARN_LOG(AUDIO, "Failed to open preferred audio device %s", g_Config.sAudioDevice.c_str());
		}
	}
	if (audioDev <= 0) {
		audioDev = SDL_OpenAudioDevice(nullptr, 0, &fmt, &g_retFmt, SDL_AUDIO_ALLOW_FREQUENCY_CHANGE);
	}
	if (audioDev <= 0) {
		ERROR_LOG(AUDIO, "Failed to open audio: %s", SDL_GetError());
	} else {
		if (g_retFmt.samples != fmt.samples) // Notify, but still use it
			ERROR_LOG(AUDIO, "Output audio samples: %d (requested: %d)", g_retFmt.samples, fmt.samples);
		if (g_retFmt.format != fmt.format || g_retFmt.channels != fmt.channels) {
			ERROR_LOG(AUDIO, "Sound buffer format does not match requested format.");
			ERROR_LOG(AUDIO, "Output audio freq: %d (requested: %d)", g_retFmt.freq, fmt.freq);
			ERROR_LOG(AUDIO, "Output audio format: %d (requested: %d)", g_retFmt.format, fmt.format);
			ERROR_LOG(AUDIO, "Output audio channels: %d (requested: %d)", g_retFmt.channels, fmt.channels);
			ERROR_LOG(AUDIO, "Provided output format does not match requirement, turning audio off");
			SDL_CloseAudioDevice(audioDev);
		}
		SDL_PauseAudioDevice(audioDev, 0);
	}
}

static void StopSDLAudioDevice() {
	if (audioDev > 0) {
		SDL_PauseAudioDevice(audioDev, 1);
		SDL_CloseAudioDevice(audioDev);
	}
}
#endif

std::string System_GetProperty(SystemProperty prop) {
	switch (prop) {
	case SYSPROP_NAME:
#if defined(__ANDROID__)
		return "Qt:Android";
#elif defined(Q_OS_LINUX)
		return "Qt:Linux";
#elif defined(_WIN32)
		return "Qt:Windows";
#elif defined(Q_OS_MAC)
		return "Qt:macOS";
#else
		return "Qt";
#endif
	case SYSPROP_LANGREGION:
		return QLocale::system().name().toStdString();
	case SYSPROP_CLIPBOARD_TEXT:
		return QApplication::clipboard()->text().toStdString();
#if defined(SDL)
	case SYSPROP_AUDIO_DEVICE_LIST:
		{
			std::string result;
			for (int i = 0; i < SDL_GetNumAudioDevices(0); ++i) {
				const char *name = SDL_GetAudioDeviceName(i, 0);
				if (!name) {
					continue;
				}

				if (i == 0) {
					result = name;
				} else {
					result.append(1, '\0');
					result.append(name);
				}
			}
			return result;
		}
#endif
	case SYSPROP_BUILD_VERSION:
		return PPSSPP_GIT_VERSION;
	default:
		return "";
	}
}

std::vector<std::string> System_GetPropertyStringVec(SystemProperty prop) {
	std::vector<std::string> result;
	switch (prop) {
	case SYSPROP_TEMP_DIRS:
		if (getenv("TMPDIR") && strlen(getenv("TMPDIR")) != 0)
			result.push_back(getenv("TMPDIR"));
		if (getenv("TMP") && strlen(getenv("TMP")) != 0)
			result.push_back(getenv("TMP"));
		if (getenv("TEMP") && strlen(getenv("TEMP")) != 0)
			result.push_back(getenv("TEMP"));
		return result;

	default:
		return result;
	}
}

int System_GetPropertyInt(SystemProperty prop) {
	switch (prop) {
#if defined(SDL)
	case SYSPROP_AUDIO_SAMPLE_RATE:
		return g_retFmt.freq;
	case SYSPROP_AUDIO_FRAMES_PER_BUFFER:
		return g_retFmt.samples;
	case SYSPROP_KEYBOARD_LAYOUT:
	{
		// TODO: Use Qt APIs for detecting this
		char q, w, y;
		q = SDL_GetKeyFromScancode(SDL_SCANCODE_Q);
		w = SDL_GetKeyFromScancode(SDL_SCANCODE_W);
		y = SDL_GetKeyFromScancode(SDL_SCANCODE_Y);
		if (q == 'a' && w == 'z' && y == 'y')
			return KEYBOARD_LAYOUT_AZERTY;
		else if (q == 'q' && w == 'w' && y == 'z')
			return KEYBOARD_LAYOUT_QWERTZ;
		return KEYBOARD_LAYOUT_QWERTY;
	}
#endif
	case SYSPROP_DEVICE_TYPE:
#if defined(__ANDROID__)
		return DEVICE_TYPE_MOBILE;
#elif defined(Q_OS_LINUX)
		return DEVICE_TYPE_DESKTOP;
#elif defined(_WIN32)
		return DEVICE_TYPE_DESKTOP;
#elif defined(Q_OS_MAC)
		return DEVICE_TYPE_DESKTOP;
#else
		return DEVICE_TYPE_DESKTOP;
#endif
	case SYSPROP_DISPLAY_COUNT:
		return QApplication::screens().size();
	default:
		return -1;
	}
}

float System_GetPropertyFloat(SystemProperty prop) {
	switch (prop) {
	case SYSPROP_DISPLAY_REFRESH_RATE:
		return refreshRate;
	case SYSPROP_DISPLAY_LOGICAL_DPI:
		return QApplication::primaryScreen()->logicalDotsPerInch();
	case SYSPROP_DISPLAY_DPI:
		return QApplication::primaryScreen()->physicalDotsPerInch();
	case SYSPROP_DISPLAY_SAFE_INSET_LEFT:
	case SYSPROP_DISPLAY_SAFE_INSET_RIGHT:
	case SYSPROP_DISPLAY_SAFE_INSET_TOP:
	case SYSPROP_DISPLAY_SAFE_INSET_BOTTOM:
		return 0.0f;
	default:
		return -1;
	}
}

bool System_GetPropertyBool(SystemProperty prop) {
	switch (prop) {
	case SYSPROP_HAS_BACK_BUTTON:
		return true;
	case SYSPROP_HAS_IMAGE_BROWSER:
	case SYSPROP_HAS_FILE_BROWSER:
	case SYSPROP_HAS_FOLDER_BROWSER:
	case SYSPROP_HAS_OPEN_DIRECTORY:
	case SYSPROP_HAS_TEXT_INPUT_DIALOG:
	case SYSPROP_CAN_SHOW_FILE:
		return true;
	case SYSPROP_SUPPORTS_OPEN_FILE_IN_EDITOR:
		return true;  // FileUtil.cpp: OpenFileInEditor
	case SYSPROP_APP_GOLD:
#ifdef GOLD
		return true;
#else
		return false;
#endif
	case SYSPROP_CAN_JIT:
		return true;
	case SYSPROP_HAS_KEYBOARD:
		return true;
	default:
		return false;
	}
}

void System_Notify(SystemNotification notification) {
	switch (notification) {
	case SystemNotification::BOOT_DONE:
		g_symbolMap->SortSymbols();
		g_mainWindow->Notify(MainWindowMsg::BOOT_DONE);
		break;
	case SystemNotification::SYMBOL_MAP_UPDATED:
		if (g_symbolMap)
			g_symbolMap->SortSymbols();
		break;
	case SystemNotification::AUDIO_RESET_DEVICE:
#ifdef SDL
		StopSDLAudioDevice();
		InitSDLAudioDevice();
#endif
		break;
	default:
		break;
	}
}

// TODO: Find a better version to pass parameters to HandleCustomEvent.
static std::string g_param1;
static std::string g_param2;
static int g_param3;
static int g_requestId;

bool MainUI::HandleCustomEvent(QEvent *e) {
	if (e->type() == browseFileEvent) {
		BrowseFileType fileType = (BrowseFileType)g_param3;
		const char *filter = "All files (*.*)";
		switch (fileType) {
		case BrowseFileType::BOOTABLE:
			filter = "PSP ROMs (*.iso *.cso *.chd *.pbp *.elf *.zip *.ppdmp)";
			break;
		case BrowseFileType::IMAGE:
			filter = "Pictures (*.jpg *.png)";
			break;
		case BrowseFileType::INI:
			filter = "INI files (*.ini)";
			break;
		case BrowseFileType::DB:
			filter = "DB files (*.db)";
			break;
		case BrowseFileType::SOUND_EFFECT:
			filter = "WAVE files (*.wav)";
			break;
		case BrowseFileType::ZIP:
			filter = "ZIP files (*.zip)";
			break;
		case BrowseFileType::ANY:
			break;
		}

		QString fileName = QFileDialog::getOpenFileName(nullptr, g_param1.c_str(), g_Config.currentDirectory.c_str(), filter);
		if (QFile::exists(fileName)) {
			g_requestManager.PostSystemSuccess(g_requestId, fileName.toStdString().c_str());
		} else {
			g_requestManager.PostSystemFailure(g_requestId);
		}
	} else if (e->type() == browseFolderEvent) {
		QString title = QString::fromStdString(g_param1);
		QString fileName = QFileDialog::getExistingDirectory(nullptr, title, g_Config.currentDirectory.c_str());
		if (QDir(fileName).exists()) {
			g_requestManager.PostSystemSuccess(g_requestId, fileName.toStdString().c_str());
		} else {
			g_requestManager.PostSystemFailure(g_requestId);
		}
	} else if (e->type() == inputBoxEvent) {
	    QString title = QString::fromStdString(g_param1);
	    QString defaultValue = QString::fromStdString(g_param2);
	    QString text = emugl->InputBoxGetQString(title, defaultValue);
	    if (text.isEmpty()) {
	        g_requestManager.PostSystemFailure(g_requestId);
	    } else {
	        g_requestManager.PostSystemSuccess(g_requestId, text.toStdString().c_str());
	    }
	} else {
		return false;
	}
	return true;
}

bool System_MakeRequest(SystemRequestType type, int requestId, const std::string &param1, const std::string &param2, int param3) {
	switch (type) {
	case SystemRequestType::EXIT_APP:
		qApp->exit(0);
		return true;
	case SystemRequestType::RESTART_APP:
		// Should find a way to properly restart the app.
		qApp->exit(0);
		return true;
	case SystemRequestType::COPY_TO_CLIPBOARD:
		QApplication::clipboard()->setText(param1.c_str());
		return true;
	case SystemRequestType::SET_WINDOW_TITLE:
	{
		std::string title = std::string("PPSSPP ") + PPSSPP_GIT_VERSION;
		if (!param1.empty())
			title += std::string(" - ") + param1;
#ifdef _DEBUG
		title += " (debug)";
#endif
		g_mainWindow->SetWindowTitleAsync(title);
		return true;
	}
	case SystemRequestType::INPUT_TEXT_MODAL:
	{
		g_requestId = requestId;
		g_param1 = param1;
		g_param2 = param2;
		g_param3 = param3;
		QCoreApplication::postEvent(emugl, new QEvent((QEvent::Type)inputBoxEvent));
		return true;
	}
	case SystemRequestType::BROWSE_FOR_IMAGE:
		// Fall back to file browser.
		return System_MakeRequest(SystemRequestType::BROWSE_FOR_FILE, requestId, param1, param2, (int)BrowseFileType::IMAGE);
	case SystemRequestType::BROWSE_FOR_FILE:
		g_requestId = requestId;
		g_param1 = param1;
		g_param2 = param2;
		g_param3 = param3;
		QCoreApplication::postEvent(emugl, new QEvent((QEvent::Type)browseFileEvent));
		return true;
	case SystemRequestType::BROWSE_FOR_FOLDER:
		g_requestId = requestId;
		g_param1 = param1;
		g_param2 = param2;
		QCoreApplication::postEvent(emugl, new QEvent((QEvent::Type)browseFolderEvent));
		return true;
	case SystemRequestType::CAMERA_COMMAND:
		if (!strncmp(param1.c_str(), "startVideo", 10)) {
			int width = 0, height = 0;
			sscanf(param1.c_str(), "startVideo_%dx%d", &width, &height);
			emit(qtcamera->onStartCamera(width, height));
		} else if (param1 == "stopVideo") {
			emit(qtcamera->onStopCamera());
		}
		return true;
	case SystemRequestType::SHOW_FILE_IN_FOLDER:
		QDesktopServices::openUrl(QUrl::fromLocalFile(QString::fromUtf8(param1.c_str())));
		return true;
	default:
		return false;
	}
}

void System_Toast(const char *text) {}

void System_AskForPermission(SystemPermission permission) {}
PermissionStatus System_GetPermissionStatus(SystemPermission permission) { return PERMISSION_STATUS_GRANTED; }

void System_Vibrate(int length_ms) {
	if (length_ms == -1 || length_ms == -3)
		length_ms = 50;
	else if (length_ms == -2)
		length_ms = 25;
}

void System_LaunchUrl(LaunchUrlType urlType, const char *url)
{
	QDesktopServices::openUrl(QUrl(url));
}

static int mainInternal(QApplication &a) {
#ifdef MOBILE_DEVICE
	emugl = new MainUI();
	emugl->resize(g_display.pixel_xres, g_display.pixel_yres);
	emugl->showFullScreen();
#endif
	EnableFZ();
	// Disable screensaver
#if defined(QT_HAS_SYSTEMINFO)
	QScreenSaver ssObject(emugl);
	ssObject.setScreenSaverEnabled(false);
#endif

#ifdef SDL
	SDLJoystick joy(true);
	joy.registerEventHandler();
	SDL_Init(SDL_INIT_AUDIO);
	InitSDLAudioDevice();
#else
	QScopedPointer<MainAudio> audio(new MainAudio());
	audio->run();
#endif

	browseFileEvent = QEvent::registerEventType();
	browseFolderEvent = QEvent::registerEventType();
	inputBoxEvent = QEvent::registerEventType();

	int retval = a.exec();
	delete emugl;
	return retval;
}

void MainUI::EmuThreadFunc() {
	SetCurrentThreadName("Emu");

	// There's no real requirement that NativeInit happen on this thread, though it can't hurt...
	// We just call the update/render loop here. NativeInitGraphics should be here though.
	NativeInitGraphics(graphicsContext);

	emuThreadState = (int)EmuThreadState::RUNNING;
	while (emuThreadState != (int)EmuThreadState::QUIT_REQUESTED) {
		updateAccelerometer();
		UpdateRunLoop(graphicsContext);
	}
	emuThreadState = (int)EmuThreadState::STOPPED;

	NativeShutdownGraphics();
	graphicsContext->StopThread();
}

void MainUI::EmuThreadStart() {
	emuThreadState = (int)EmuThreadState::START_REQUESTED;
	emuThread = std::thread([&]() { this->EmuThreadFunc(); } );
}

void MainUI::EmuThreadStop() {
	emuThreadState = (int)EmuThreadState::QUIT_REQUESTED;
}

void MainUI::EmuThreadJoin() {
	emuThread.join();
	emuThread = std::thread();
}

MainUI::MainUI(QWidget *parent)
	: QGLWidget(parent) {
	emuThreadState = (int)EmuThreadState::DISABLED;
	setAttribute(Qt::WA_AcceptTouchEvents);
#if QT_VERSION < QT_VERSION_CHECK(5, 0, 0)
	setAttribute(Qt::WA_LockLandscapeOrientation);
#endif
#if defined(MOBILE_DEVICE)
	acc = new QAccelerometer(this);
	acc->start();
#endif
	setFocus();
	setFocusPolicy(Qt::StrongFocus);
	startTimer(16);
}

MainUI::~MainUI() {
	INFO_LOG(SYSTEM, "MainUI::Destructor");
	if (emuThreadState != (int)EmuThreadState::DISABLED) {
		INFO_LOG(SYSTEM, "EmuThreadStop");
		EmuThreadStop();
		while (graphicsContext->ThreadFrame()) {
			// Need to keep eating frames to allow the EmuThread to exit correctly.
			continue;
		}
		EmuThreadJoin();
	}
#if defined(MOBILE_DEVICE)
	delete acc;
#endif
	graphicsContext->Shutdown();
	delete graphicsContext;
	graphicsContext = nullptr;
}

QString MainUI::InputBoxGetQString(QString title, QString defaultValue) {
	bool ok;
	QString text = QInputDialog::getText(this, title, title, QLineEdit::Normal, defaultValue, &ok);
	if (!ok)
		text = QString();
	return text;
}

void MainUI::resizeGL(int w, int h) {
	if (UpdateScreenScale(w, h)) {
		System_PostUIMessage(UIMessage::GPU_RENDER_RESIZED);
	}
	xscale = w / this->width();
	yscale = h / this->height();

	PSP_CoreParameter().pixelWidth = g_display.pixel_xres;
	PSP_CoreParameter().pixelHeight = g_display.pixel_yres;
}

void MainUI::timerEvent(QTimerEvent *) {
	updateGL();
	emit newFrame();
}

void MainUI::changeEvent(QEvent *e) {
	QGLWidget::changeEvent(e);
	if (e->type() == QEvent::WindowStateChange)
		Core_NotifyWindowHidden(isMinimized());
}

bool MainUI::event(QEvent *e) {
	TouchInput input;
	QList<QTouchEvent::TouchPoint> touchPoints;

	switch (e->type()) {
	case QEvent::TouchBegin:
	case QEvent::TouchUpdate:
	case QEvent::TouchEnd:
		touchPoints = static_cast<QTouchEvent *>(e)->touchPoints();
		foreach (const QTouchEvent::TouchPoint &touchPoint, touchPoints) {
			switch (touchPoint.state()) {
			case Qt::TouchPointStationary:
				break;
			case Qt::TouchPointPressed:
			case Qt::TouchPointReleased:
				input.x = touchPoint.pos().x() * g_display.dpi_scale_x * xscale;
				input.y = touchPoint.pos().y() * g_display.dpi_scale_y * yscale;
				input.flags = (touchPoint.state() == Qt::TouchPointPressed) ? TOUCH_DOWN : TOUCH_UP;
				input.id = touchPoint.id();
				NativeTouch(input);
				break;
			case Qt::TouchPointMoved:
				input.x = touchPoint.pos().x() * g_display.dpi_scale_x * xscale;
				input.y = touchPoint.pos().y() * g_display.dpi_scale_y * yscale;
				input.flags = TOUCH_MOVE;
				input.id = touchPoint.id();
				NativeTouch(input);
				break;
			default:
				break;
			}
		}
		break;
	case QEvent::MouseButtonDblClick:
		if (!g_Config.bShowTouchControls || GetUIState() != UISTATE_INGAME)
			emit doubleClick();
		break;
	case QEvent::MouseButtonPress:
	case QEvent::MouseButtonRelease:
		switch(((QMouseEvent*)e)->button()) {
		case Qt::LeftButton:
			input.x = ((QMouseEvent*)e)->pos().x() * g_display.dpi_scale_x * xscale;
			input.y = ((QMouseEvent*)e)->pos().y() * g_display.dpi_scale_y * yscale;
			input.flags = (e->type() == QEvent::MouseButtonPress) ? TOUCH_DOWN : TOUCH_UP;
			input.id = 0;
			NativeTouch(input);
			break;
		case Qt::RightButton:
			NativeKey(KeyInput(DEVICE_ID_MOUSE, NKCODE_EXT_MOUSEBUTTON_2, (e->type() == QEvent::MouseButtonPress) ? KEY_DOWN : KEY_UP));
			break;
		case Qt::MiddleButton:
			NativeKey(KeyInput(DEVICE_ID_MOUSE, NKCODE_EXT_MOUSEBUTTON_3, (e->type() == QEvent::MouseButtonPress) ? KEY_DOWN : KEY_UP));
			break;
		case Qt::ExtraButton1:
			NativeKey(KeyInput(DEVICE_ID_MOUSE, NKCODE_EXT_MOUSEBUTTON_4, (e->type() == QEvent::MouseButtonPress) ? KEY_DOWN : KEY_UP));
			break;
		case Qt::ExtraButton2:
			NativeKey(KeyInput(DEVICE_ID_MOUSE, NKCODE_EXT_MOUSEBUTTON_5, (e->type() == QEvent::MouseButtonPress) ? KEY_DOWN : KEY_UP));
			break;
		default:
			break;
		}
		break;
	case QEvent::MouseMove:
		input.x = ((QMouseEvent*)e)->pos().x() * g_display.dpi_scale_x * xscale;
		input.y = ((QMouseEvent*)e)->pos().y() * g_display.dpi_scale_y * yscale;
		input.flags = TOUCH_MOVE;
		input.id = 0;
		NativeTouch(input);
		break;
	case QEvent::Wheel:
		NativeKey(KeyInput(DEVICE_ID_MOUSE, ((QWheelEvent*)e)->delta()<0 ? NKCODE_EXT_MOUSEWHEEL_DOWN : NKCODE_EXT_MOUSEWHEEL_UP, KEY_DOWN));
		break;
	case QEvent::KeyPress:
		{
			auto qtKeycode = ((QKeyEvent*)e)->key();
			auto iter = KeyMapRawQttoNative.find(qtKeycode);
			InputKeyCode nativeKeycode = NKCODE_UNKNOWN;
			if (iter != KeyMapRawQttoNative.end()) {
				nativeKeycode = iter->second;
				NativeKey(KeyInput(DEVICE_ID_KEYBOARD, nativeKeycode, KEY_DOWN));
			}

			// Also get the unicode value.
			QString text = ((QKeyEvent*)e)->text();
			std::string str = text.toStdString();
			// Now, we don't want CHAR events for non-printable characters. Not quite sure how we'll best
			// do that, but here's one attempt....
			switch (nativeKeycode) {
			case NKCODE_DEL:
			case NKCODE_FORWARD_DEL:
			case NKCODE_TAB:
				break;
			default:
				if (str.size()) {
					int pos = 0;
					int unicode = u8_nextchar(str.c_str(), &pos, str.size());
					NativeKey(KeyInput(DEVICE_ID_KEYBOARD, unicode));
				}
				break;
			}
		}
		break;
	case QEvent::KeyRelease:
		NativeKey(KeyInput(DEVICE_ID_KEYBOARD, KeyMapRawQttoNative.find(((QKeyEvent*)e)->key())->second, KEY_UP));
		break;

	default:
		// Can't switch on dynamic event types.
		if (!HandleCustomEvent(e)) {
			return QWidget::event(e);
		}
	}
	e->accept();
	return true;
}

void MainUI::initializeGL() {
	if (g_Config.iGPUBackend != (int)GPUBackend::OPENGL) {
		INFO_LOG(SYSTEM, "Only GL supported under Qt - switching.");
		g_Config.iGPUBackend = (int)GPUBackend::OPENGL;
	}

	bool useCoreContext = format().profile() == QGLFormat::CoreProfile;

	SetGLCoreContext(useCoreContext);

#ifndef USING_GLES2
	// Some core profile drivers elide certain extensions from GL_EXTENSIONS/etc.
	// glewExperimental allows us to force GLEW to search for the pointers anyway.
	if (useCoreContext) {
		glewExperimental = true;
	}
	glewInit();
	// Unfortunately, glew will generate an invalid enum error, ignore.
	if (useCoreContext) {
		glGetError();
	}
#endif
	if (g_Config.iGPUBackend == (int)GPUBackend::OPENGL) {
		// OpenGL uses a background thread to do the main processing and only renders on the gl thread.
		INFO_LOG(SYSTEM, "Initializing GL graphics context");
		graphicsContext = new QtGLGraphicsContext();
		INFO_LOG(SYSTEM, "Using thread, starting emu thread");
		EmuThreadStart();
	} else {
		INFO_LOG(SYSTEM, "Not using thread, backend=%d", (int)g_Config.iGPUBackend);
	}
	graphicsContext->ThreadStart();
}

void MainUI::paintGL() {
#ifdef SDL
	SDL_PumpEvents();
#endif
	updateAccelerometer();
	if (emuThreadState == (int)EmuThreadState::DISABLED) {
		UpdateRunLoop(graphicsContext);
	} else {
		graphicsContext->ThreadFrame();
		// Do the rest in EmuThreadFunc
	}
}

void MainUI::updateAccelerometer() {
#if defined(MOBILE_DEVICE)
	// TODO: Toggle it depending on whether it is enabled
	QAccelerometerReading *reading = acc->reading();
	if (reading) {
		NativeAccelerometer(reading->x(), reading->y(), reading->z());
	}
#endif
}

#ifndef SDL

MainAudio::~MainAudio() {
	if (feed != nullptr) {
		killTimer(timer);
		feed->close();
	}
	if (output) {
		output->stop();
		delete output;
	}
	if (mixbuf)
		free(mixbuf);
}

void MainAudio::run() {
	QAudioFormat fmt;
	fmt.setSampleRate(AUDIO_FREQ);
	fmt.setCodec("audio/pcm");
	fmt.setChannelCount(AUDIO_CHANNELS);
	fmt.setSampleSize(AUDIO_SAMPLESIZE);
	fmt.setByteOrder(QAudioFormat::LittleEndian);
	fmt.setSampleType(QAudioFormat::SignedInt);
	mixlen = sizeof(short)*AUDIO_BUFFERS*AUDIO_CHANNELS*AUDIO_SAMPLES;
	mixbuf = (char*)malloc(mixlen);
	output = new QAudioOutput(fmt);
	output->setBufferSize(mixlen);
	feed = output->start();
	if (feed != nullptr) {
		// buffering has already done in the internal mixed buffer
		// use a small interval to copy mixed audio stream from
		// internal buffer to audio output buffer as soon as possible
		// use 1 instead of 0 to prevent CPU exhausting
		timer = startTimer(1);
	}
}

void MainAudio::timerEvent(QTimerEvent *) {
	memset(mixbuf, 0, mixlen);
	size_t frames = NativeMix((short *)mixbuf, AUDIO_BUFFERS*AUDIO_SAMPLES, AUDIO_FREQ);
	if (frames > 0)
		feed->write(mixbuf, sizeof(short) * AUDIO_CHANNELS * frames);
}

#endif


void QTCamera::startCamera(int width, int height) {
	__qt_startCapture(width, height);
}

void QTCamera::stopCamera() {
	__qt_stopCapture();
}

#ifndef SDL
Q_DECL_EXPORT
#endif
int main(int argc, char *argv[])
{
	for (int i = 1; i < argc; i++) {
		if (!strcmp(argv[i], "--version")) {
			printf("%s\n", PPSSPP_GIT_VERSION);
			return 0;
		}
	}

	// Ignore sigpipe.
	if (signal(SIGPIPE, SIG_IGN) == SIG_ERR) {
		perror("Unable to ignore SIGPIPE");
	}

	PROFILE_INIT();
	glslang::InitializeProcess();
#if defined(Q_OS_LINUX)
	QApplication::setAttribute(Qt::AA_X11InitThreads, true);
#endif

	// Qt would otherwise default to a 3.0 compatibility profile
	// except on Nvidia, where Nvidia gives us the highest supported anyway
	QGLFormat format;
	format.setVersion(4, 6);
	format.setProfile(QGLFormat::CoreProfile);
	QGLFormat::setDefaultFormat(format);

	QApplication a(argc, argv);
	QScreen* screen = a.primaryScreen();
	QSizeF res = screen->physicalSize();

	if (res.width() < res.height())
		res.transpose();
	g_display.pixel_xres = res.width();
	g_display.pixel_yres = res.height();

	g_display.dpi_scale_x = screen->logicalDotsPerInchX() / screen->physicalDotsPerInchX();
	g_display.dpi_scale_y = screen->logicalDotsPerInchY() / screen->physicalDotsPerInchY();
	g_display.dpi_scale_real_x = g_display.dpi_scale_x;
	g_display.dpi_scale_real_y = g_display.dpi_scale_y;
	g_display.dp_xres = (int)(g_display.pixel_xres * g_display.dpi_scale_x);
	g_display.dp_yres = (int)(g_display.pixel_yres * g_display.dpi_scale_y);

	refreshRate = screen->refreshRate();

	qtcamera = new QTCamera;
	QObject::connect(qtcamera, SIGNAL(onStartCamera(int, int)), qtcamera, SLOT(startCamera(int, int)));
	QObject::connect(qtcamera, SIGNAL(onStopCamera()),  qtcamera, SLOT(stopCamera()));

	std::string savegame_dir = ".";
	std::string external_dir = ".";
#if QT_VERSION > QT_VERSION_CHECK(5, 0, 0)
	savegame_dir = QStandardPaths::writableLocation(QStandardPaths::HomeLocation).toStdString();
	external_dir = QStandardPaths::writableLocation(QStandardPaths::DataLocation).toStdString();
#endif
	savegame_dir += "/";
	external_dir += "/";

	NativeInit(argc, (const char **)argv, savegame_dir.c_str(), external_dir.c_str(), nullptr);

	g_mainWindow = new MainWindow(nullptr, g_Config.UseFullScreen());
	g_mainWindow->show();

	// TODO: Support other backends than GL, like Vulkan, in the Qt backend.
	g_Config.iGPUBackend = (int)GPUBackend::OPENGL;

	int ret = mainInternal(a);
	INFO_LOG(SYSTEM, "Left mainInternal here.");

#ifdef SDL
	if (audioDev > 0) {
		SDL_PauseAudioDevice(audioDev, 1);
		SDL_CloseAudioDevice(audioDev);
	}
#endif
	NativeShutdown();
	glslang::FinalizeProcess();
	return ret;
}
