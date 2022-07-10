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
#include "Common/System/System.h"
#include "Common/GPU/OpenGL/GLFeatures.h"
#include "Common/Math/math_util.h"
#include "Common/Profiler/Profiler.h"

#include "QtMain.h"
#include "Common/Data/Text/I18n.h"
#include "Common/Thread/ThreadUtil.h"
#include "Common/Data/Encoding/Utf8.h"
#include "Common/StringUtils.h"
#include "Core/Config.h"
#include "Core/ConfigValues.h"
#include "Core/HW/Camera.h"

#include <signal.h>
#include <string.h>

MainUI *emugl = nullptr;
static float refreshRate = 60.f;
static int browseFileEvent = -1;
static int browseFolderEvent = -1;
QTCamera *qtcamera = nullptr;

#ifdef SDL
SDL_AudioSpec g_retFmt;

static SDL_AudioDeviceID audioDev = 0;

extern void mixaudio(void *userdata, Uint8 *stream, int len) {
	NativeMix((short *)stream, len / 4);
}

static void InitSDLAudioDevice() {
	SDL_AudioSpec fmt;
	memset(&fmt, 0, sizeof(fmt));
	fmt.freq = 44100;
	fmt.format = AUDIO_S16;
	fmt.channels = 2;
	fmt.samples = 1024;
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
	case SYSPROP_HAS_FILE_BROWSER:
	case SYSPROP_HAS_FOLDER_BROWSER:
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

void System_SendMessage(const char *command, const char *parameter) {
	if (!strcmp(command, "finish")) {
		qApp->exit(0);
	} else if (!strcmp(command, "browse_file")) {
		QCoreApplication::postEvent(emugl, new QEvent((QEvent::Type)browseFileEvent));
	} else if (!strcmp(command, "browse_folder")) {
		QCoreApplication::postEvent(emugl, new QEvent((QEvent::Type)browseFolderEvent));
	} else if (!strcmp(command, "graphics_restart")) {
		// Should find a way to properly restart the app.
		qApp->exit(0);
	} else if (!strcmp(command, "camera_command")) {
		if (!strncmp(parameter, "startVideo", 10)) {
			int width = 0, height = 0;
			sscanf(parameter, "startVideo_%dx%d", &width, &height);
			emit(qtcamera->onStartCamera(width, height));
		} else if (!strcmp(parameter, "stopVideo")) {
			emit(qtcamera->onStopCamera());
		}
	} else if (!strcmp(command, "setclipboardtext")) {
		QApplication::clipboard()->setText(parameter);
#if defined(SDL)
	} else if (!strcmp(command, "audio_resetDevice")) {
		StopSDLAudioDevice();
		InitSDLAudioDevice();
#endif
	}
}
void System_Toast(const char *text) {}

void System_AskForPermission(SystemPermission permission) {}
PermissionStatus System_GetPermissionStatus(SystemPermission permission) { return PERMISSION_STATUS_GRANTED; }

void System_InputBoxGetString(const std::string &title, const std::string &defaultValue, std::function<void(bool, const std::string &)> cb) {
	QString text = emugl->InputBoxGetQString(QString::fromStdString(title), QString::fromStdString(defaultValue));
	if (text.isEmpty()) {
		NativeInputBoxReceived(cb, false, "");
	} else {
		NativeInputBoxReceived(cb, true, text.toStdString());
	}
}

void Vibrate(int length_ms) {
	if (length_ms == -1 || length_ms == -3)
		length_ms = 50;
	else if (length_ms == -2)
		length_ms = 25;
}

void OpenDirectory(const char *path) {
	QDesktopServices::openUrl(QUrl::fromLocalFile(QString::fromUtf8(path)));
}

void LaunchBrowser(const char *url)
{
	QDesktopServices::openUrl(QUrl(url));
}

static int mainInternal(QApplication &a) {
#ifdef MOBILE_DEVICE
	emugl = new MainUI();
	emugl->resize(pixel_xres, pixel_yres);
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
		UpdateRunLoop();
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
		NativeMessageReceived("gpu_resized", "");
	}
	xscale = w / this->width();
	yscale = h / this->height();

	PSP_CoreParameter().pixelWidth = pixel_xres;
	PSP_CoreParameter().pixelHeight = pixel_yres;
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
				input.x = touchPoint.pos().x() * g_dpi_scale_x * xscale;
				input.y = touchPoint.pos().y() * g_dpi_scale_y * yscale;
				input.flags = (touchPoint.state() == Qt::TouchPointPressed) ? TOUCH_DOWN : TOUCH_UP;
				input.id = touchPoint.id();
				NativeTouch(input);
				break;
			case Qt::TouchPointMoved:
				input.x = touchPoint.pos().x() * g_dpi_scale_x * xscale;
				input.y = touchPoint.pos().y() * g_dpi_scale_y * yscale;
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
			input.x = ((QMouseEvent*)e)->pos().x() * g_dpi_scale_x * xscale;
			input.y = ((QMouseEvent*)e)->pos().y() * g_dpi_scale_y * yscale;
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
		input.x = ((QMouseEvent*)e)->pos().x() * g_dpi_scale_x * xscale;
		input.y = ((QMouseEvent*)e)->pos().y() * g_dpi_scale_y * yscale;
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
			int nativeKeycode = 0;
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
					int code = u8_nextchar(str.c_str(), &pos);
					NativeKey(KeyInput(DEVICE_ID_KEYBOARD, code, KEY_CHAR));
				}
				break;
			}
		}
		break;
	case QEvent::KeyRelease:
		NativeKey(KeyInput(DEVICE_ID_KEYBOARD, KeyMapRawQttoNative.find(((QKeyEvent*)e)->key())->second, KEY_UP));
		break;

	default:
		if (e->type() == browseFileEvent) {
			QString fileName = QFileDialog::getOpenFileName(nullptr, "Load ROM", g_Config.currentDirectory.c_str(), "PSP ROMs (*.iso *.cso *.pbp *.elf *.zip *.ppdmp)");
			if (QFile::exists(fileName)) {
				QDir newPath;
				g_Config.currentDirectory = Path(newPath.filePath(fileName).toStdString());
				g_Config.Save("browseFileEvent");

				NativeMessageReceived("boot", fileName.toStdString().c_str());
			}
			break;
		} else if (e->type() == browseFolderEvent) {
			auto mm = GetI18NCategory("MainMenu");
			QString fileName = QFileDialog::getExistingDirectory(nullptr, mm->T("Choose folder"), g_Config.currentDirectory.c_str());
			if (QDir(fileName).exists()) {
				NativeMessageReceived("browse_folderSelect", fileName.toStdString().c_str());
			}
		} else {
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
		UpdateRunLoop();
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
		AxisInput axis;
		axis.deviceId = DEVICE_ID_ACCELEROMETER;
		axis.flags = 0;

		axis.axisId = JOYSTICK_AXIS_ACCELEROMETER_X;
		axis.value = reading->x();
		NativeAxis(axis);

		axis.axisId = JOYSTICK_AXIS_ACCELEROMETER_Y;
		axis.value = reading->y();
		NativeAxis(axis);

		axis.axisId = JOYSTICK_AXIS_ACCELEROMETER_Z;
		axis.value = reading->z();
		NativeAxis(axis);
	}
#endif
}

#ifndef SDL
// Audio
#define AUDIO_FREQ 44100
#define AUDIO_CHANNELS 2
#define AUDIO_SAMPLES 2048
#define AUDIO_SAMPLESIZE 16
#define AUDIO_BUFFERS 5

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
	size_t frames = NativeMix((short *)mixbuf, AUDIO_BUFFERS*AUDIO_SAMPLES);
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
	pixel_xres = res.width();
	pixel_yres = res.height();

	g_dpi_scale_x = screen->logicalDotsPerInchX() / screen->physicalDotsPerInchX();
	g_dpi_scale_y = screen->logicalDotsPerInchY() / screen->physicalDotsPerInchY();
	g_dpi_scale_real_x = g_dpi_scale_x;
	g_dpi_scale_real_y = g_dpi_scale_y;
	dp_xres = (int)(pixel_xres * g_dpi_scale_x);
	dp_yres = (int)(pixel_yres * g_dpi_scale_y);

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
