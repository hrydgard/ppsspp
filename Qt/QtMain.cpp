/*
 * Copyright (c) 2012 Sacha Refshauge
 *
 */
// Qt 4.7+ / 5.0+ implementation of the framework.
// Currently supports: Android, Linux, Windows, Mac OSX

#include <QApplication>
#include <QUrl>
#include <QDir>
#include <QDesktopWidget>
#include <QDesktopServices>
#include <QLocale>
#include <QThread>

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
#endif
#include "QtMain.h"
#include "gfx_es2/gpu_features.h"
#include "math/math_util.h"
#include "thread/threadutil.h"

#include <string.h>

MainUI *emugl = NULL;

#ifdef SDL
extern void mixaudio(void *userdata, Uint8 *stream, int len) {
	NativeMix((short *)stream, len / 4);
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
		return "Qt:Mac";
#else
		return "Qt";
#endif
	case SYSPROP_LANGREGION:
		return QLocale::system().name().toStdString();
	default:
		return "";
	}
}

int System_GetPropertyInt(SystemProperty prop) {
	switch (prop) {
	case SYSPROP_AUDIO_SAMPLE_RATE:
		return 44100;
	case SYSPROP_DISPLAY_REFRESH_RATE:
		return 60000;
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
	default:
		return -1;
	}
}

bool System_GetPropertyBool(SystemProperty prop) {
	switch (prop) {
	case SYSPROP_HAS_BACK_BUTTON:
		return true;
	case SYSPROP_APP_GOLD:
#ifdef GOLD
		return true;
#else
		return false;
#endif
	default:
		return false;
	}
}

void System_SendMessage(const char *command, const char *parameter) {
	if (!strcmp(command, "finish")) {
		qApp->exit(0);
	}
}

void System_AskForPermission(SystemPermission permission) {}
PermissionStatus System_GetPermissionStatus(SystemPermission permission) { return PERMISSION_STATUS_GRANTED; }

bool System_InputBoxGetString(const char *title, const char *defaultValue, char *outValue, size_t outLength)
{
	QString text = emugl->InputBoxGetQString(QString(title), QString(defaultValue));
	if (text.isEmpty())
		return false;
	strcpy(outValue, text.toStdString().c_str());
	return true;
}

void Vibrate(int length_ms) {
	if (length_ms == -1 || length_ms == -3)
		length_ms = 50;
	else if (length_ms == -2)
		length_ms = 25;
}

void LaunchBrowser(const char *url)
{
	QDesktopServices::openUrl(QUrl(url));
}

float CalculateDPIScale()
{
	// Sane default rather than check DPI
#if defined(USING_GLES2)
	return 1.2f;
#else
	return 1.0f;
#endif
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
	SDL_AudioSpec fmt, ret_fmt;
	memset(&fmt, 0, sizeof(fmt));
	fmt.freq = 44100;
	fmt.format = AUDIO_S16;
	fmt.channels = 2;
	fmt.samples = 2048;
	fmt.callback = &mixaudio;
	fmt.userdata = (void *)0;

	if (SDL_OpenAudio(&fmt, &ret_fmt) < 0) {
		ELOG("Failed to open audio: %s", SDL_GetError());
	} else {
		if (ret_fmt.samples != fmt.samples) // Notify, but still use it
			ELOG("Output audio samples: %d (requested: %d)", ret_fmt.samples, fmt.samples);
		if (ret_fmt.freq != fmt.freq || ret_fmt.format != fmt.format || ret_fmt.channels != fmt.channels) {
			ELOG("Sound buffer format does not match requested format.");
			ELOG("Output audio freq: %d (requested: %d)", ret_fmt.freq, fmt.freq);
			ELOG("Output audio format: %d (requested: %d)", ret_fmt.format, fmt.format);
			ELOG("Output audio channels: %d (requested: %d)", ret_fmt.channels, fmt.channels);
			ELOG("Provided output format does not match requirement, turning audio off");
			SDL_CloseAudio();
		}
	}
	SDL_PauseAudio(0);
#else
	QScopedPointer<MainAudio> audio(new MainAudio());
	audio->run();
#endif
	int retval = a.exec();
	delete emugl;
	return retval;
}

void MainUI::EmuThreadFunc() {
	setCurrentThreadName("Emu");

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

MainUI::MainUI(QWidget *parent):
	QGLWidget(parent)
{
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

MainUI::~MainUI()
{
	ILOG("MainUI::Destructor");
	if (emuThreadState != (int)EmuThreadState::DISABLED) {
		ILOG("EmuThreadStop");
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

QString MainUI::InputBoxGetQString(QString title, QString defaultValue)
{
    bool ok;
    QString text = QInputDialog::getText(this, title, title, QLineEdit::Normal, defaultValue, &ok);
    if (!ok)
        text = QString();
    return text;
}

void MainUI::resizeGL(int w, int h)
{
    if (UpdateScreenScale(w, h)) {
        NativeMessageReceived("gpu_resized", "");
    }
    xscale = w / this->width();
    yscale = h / this->height();

    PSP_CoreParameter().pixelWidth = pixel_xres;
    PSP_CoreParameter().pixelHeight = pixel_yres;
}

void MainUI::timerEvent(QTimerEvent *)
{
    updateGL();
    emit newFrame();
}

void MainUI::changeEvent(QEvent *e)
{
    QGLWidget::changeEvent(e);
    if(e->type() == QEvent::WindowStateChange)
        Core_NotifyWindowHidden(isMinimized());
}

bool MainUI::event(QEvent *e)
{
    TouchInput input;
    QList<QTouchEvent::TouchPoint> touchPoints;
    switch(e->type())
    {
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
        input.x = ((QMouseEvent*)e)->pos().x() * g_dpi_scale_x * xscale;
        input.y = ((QMouseEvent*)e)->pos().y() * g_dpi_scale_y * yscale;
        input.flags = (e->type() == QEvent::MouseButtonPress) ? TOUCH_DOWN : TOUCH_UP;
        input.id = 0;
        NativeTouch(input);
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
        NativeKey(KeyInput(DEVICE_ID_KEYBOARD, KeyMapRawQttoNative.find(((QKeyEvent*)e)->key())->second, KEY_DOWN));
        break;
    case QEvent::KeyRelease:
        NativeKey(KeyInput(DEVICE_ID_KEYBOARD, KeyMapRawQttoNative.find(((QKeyEvent*)e)->key())->second, KEY_UP));
        break;
    default:
        return QWidget::event(e);
    }
    e->accept();
    return true;
}

void MainUI::initializeGL() {
	if (g_Config.iGPUBackend != (int)GPUBackend::OPENGL) {
		ILOG("Only GL supported under Qt - switching.");
		g_Config.iGPUBackend = (int)GPUBackend::OPENGL;
	}

#ifndef USING_GLES2
	// Some core profile drivers elide certain extensions from GL_EXTENSIONS/etc.
	// glewExperimental allows us to force GLEW to search for the pointers anyway.
	if (gl_extensions.IsCoreContext) {
		glewExperimental = true;
	}
	glewInit();
	// Unfortunately, glew will generate an invalid enum error, ignore.
	if (gl_extensions.IsCoreContext) {
		glGetError();
	}
#endif
	if (g_Config.iGPUBackend == (int)GPUBackend::OPENGL) {
		// OpenGL uses a background thread to do the main processing and only renders on the gl thread.
		ILOG("Initializing GL graphics context");
		graphicsContext = new QtGLGraphicsContext();
		ILOG("Using thread, starting emu thread");
		EmuThreadStart();
	} else {
		ILOG("Not using thread, backend=%d", (int)g_Config.iGPUBackend);
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

void MainUI::updateAccelerometer()
{
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

MainAudio::~MainAudio()
{
    if (feed != NULL) {
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

void MainAudio::run()
{
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
    if (feed != NULL) {
        // buffering has already done in the internal mixed buffer
        // use a small interval to copy mixed audio stream from
        // internal buffer to audio output buffer as soon as possible
        // use 1 instead of 0 to prevent CPU exhausting
        timer = startTimer(1);
    }
}

void MainAudio::timerEvent(QTimerEvent *)
{
    memset(mixbuf, 0, mixlen);
    size_t frames = NativeMix((short *)mixbuf, AUDIO_BUFFERS*AUDIO_SAMPLES);
    if (frames > 0)
        feed->write(mixbuf, sizeof(short) * AUDIO_CHANNELS * frames);
}

#endif


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

	glslang::InitializeProcess();
#if defined(Q_OS_LINUX)
	QApplication::setAttribute(Qt::AA_X11InitThreads, true);
#endif
	QApplication a(argc, argv);
	QSize res = QApplication::desktop()->screenGeometry().size();
	if (res.width() < res.height())
		res.transpose();
	pixel_xres = res.width();
	pixel_yres = res.height();
	g_dpi_scale_x = CalculateDPIScale();
	g_dpi_scale_y = CalculateDPIScale();
	g_dpi_scale_real_x = g_dpi_scale_x;
	g_dpi_scale_real_y = g_dpi_scale_y;
	dp_xres = (int)(pixel_xres * g_dpi_scale_x);
	dp_yres = (int)(pixel_yres * g_dpi_scale_y);
	std::string savegame_dir = ".";
	std::string external_dir = ".";
#if QT_VERSION > QT_VERSION_CHECK(5, 0, 0)
	savegame_dir = QStandardPaths::writableLocation(QStandardPaths::HomeLocation).toStdString();
	external_dir = QStandardPaths::writableLocation(QStandardPaths::DataLocation).toStdString();
#endif
	savegame_dir += "/";
	external_dir += "/";

	bool fullscreenCLI=false;
	for (int i = 1; i < argc; i++) {
		if (!strcmp(argv[i],"--fullscreen"))
			fullscreenCLI=true;
	}
	NativeInit(argc, (const char **)argv, savegame_dir.c_str(), external_dir.c_str(), nullptr, fullscreenCLI);

	// TODO: Support other backends than GL, like Vulkan, in the Qt backend.
	g_Config.iGPUBackend = (int)GPUBackend::OPENGL;

	int ret = mainInternal(a);
	ILOG("Left mainInternal here.");

#ifdef SDL
	SDL_PauseAudio(1);
	SDL_CloseAudio();
#endif
	NativeShutdown();
	glslang::FinalizeProcess();
	return ret;
}

