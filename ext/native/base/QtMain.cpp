/*
 * Copyright (c) 2012 Sacha Refshauge
 *
 */
// Qt 4.7+ / 5.0+ implementation of the framework.
// Currently supports: Android, Symbian, Blackberry, Maemo/Meego, Linux, Windows, Mac OSX

#include <QApplication>
#include <QUrl>
#include <QDir>
#include <QDesktopWidget>
#include <QDesktopServices>
#include <QLocale>
#include <QThread>

#if QT_VERSION > QT_VERSION_CHECK(5, 0, 0)
#include <QStandardPaths>
#ifdef QT_HAS_SYSTEMINFO
#include <QScreenSaver>
#endif
#endif

#ifdef __SYMBIAN32__
#include <QSystemScreenSaver>
#include <QFeedbackHapticsEffect>
#include "SymbianMediaKeys.h"
#endif
#ifdef SDL
#include "SDL/SDLJoystick.h"
#include "SDL_audio.h"
#endif
#include "QtMain.h"
#include "gfx_es2/gpu_features.h"
#include "math/math_util.h"

#include <string.h>

InputState input_state;
MainUI *emugl = NULL;

#ifdef SDL
extern void mixaudio(void *userdata, Uint8 *stream, int len) {
	NativeMix((short *)stream, len / 4);
}
#endif

std::string System_GetProperty(SystemProperty prop) {
	switch (prop) {
	case SYSPROP_NAME:
#ifdef __SYMBIAN32__
		return "Qt:Symbian";
#elif defined(BLACKBERRY)
		return "Qt:Blackberry";
#elif defined(MAEMO)
		return "Qt:Maemo";
#elif defined(ANDROID)
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
#ifdef __SYMBIAN32__
		return DEVICE_TYPE_MOBILE;
#elif defined(BLACKBERRY)
		return DEVICE_TYPE_MOBILE;
#elif defined(MAEMO)
		return DEVICE_TYPE_MOBILE;
#elif defined(ANDROID)
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
	// Symbian only for now
#if defined(__SYMBIAN32__)
	QFeedbackHapticsEffect effect;
	effect.setIntensity(0.8);
	effect.setDuration(length_ms);
	effect.start();
#endif
}

void LaunchBrowser(const char *url)
{
	QDesktopServices::openUrl(QUrl(url));
}

float CalculateDPIScale()
{
	// Sane default rather than check DPI
#ifdef __SYMBIAN32__
	return 1.4f;
#elif defined(USING_GLES2)
	return 1.2f;
#else
	return 1.0f;
#endif
}

static int mainInternal(QApplication &a)
{
#ifdef MOBILE_DEVICE
	emugl = new MainUI();
	emugl->resize(pixel_xres, pixel_yres);
	emugl->showFullScreen();
#endif
	EnableFZ();
	// Disable screensaver
#ifdef __SYMBIAN32__
	QSystemScreenSaver ssObject(emugl);
	ssObject.setScreenSaverInhibit();
	QScopedPointer<SymbianMediaKeys> mediakeys(new SymbianMediaKeys());
#elif defined(QT_HAS_SYSTEMINFO)
	QScreenSaver ssObject(emugl);
	ssObject.setScreenSaverEnabled(false);
#endif

#ifdef SDL
	SDLJoystick joy(true);
	joy.startEventLoop();
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
	return a.exec();
}

MainUI::MainUI(QWidget *parent):
    QGLWidget(parent)
{
    setAttribute(Qt::WA_AcceptTouchEvents);
#if QT_VERSION < QT_VERSION_CHECK(5, 0, 0)
    setAttribute(Qt::WA_LockLandscapeOrientation);
#endif
#if defined(MOBILE_DEVICE) && !defined(MAEMO)
    acc = new QAccelerometer(this);
    acc->start();
#endif
    setFocus();
    setFocusPolicy(Qt::StrongFocus);
    startTimer(16);
}

MainUI::~MainUI()
{
#if defined(MOBILE_DEVICE) && !defined(MAEMO)
        delete acc;
#endif
        NativeShutdownGraphics();
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
    bool smallWindow = g_Config.IsPortrait() ? (h < 480 + 80) : (w < 480 + 80);
    if (UpdateScreenScale(w, h, smallWindow)) {
        NativeMessageReceived("gpu resized", "");
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
                input_state.pointer_down[touchPoint.id()] = (touchPoint.state() == Qt::TouchPointPressed);
                input_state.pointer_x[touchPoint.id()] = touchPoint.pos().x() * g_dpi_scale * xscale;
                input_state.pointer_y[touchPoint.id()] = touchPoint.pos().y() * g_dpi_scale * yscale;

                input.x = touchPoint.pos().x() * g_dpi_scale * xscale;
                input.y = touchPoint.pos().y() * g_dpi_scale * yscale;
                input.flags = (touchPoint.state() == Qt::TouchPointPressed) ? TOUCH_DOWN : TOUCH_UP;
                input.id = touchPoint.id();
                NativeTouch(input);
                break;
            case Qt::TouchPointMoved:
                input_state.pointer_x[touchPoint.id()] = touchPoint.pos().x() * g_dpi_scale * xscale;
                input_state.pointer_y[touchPoint.id()] = touchPoint.pos().y() * g_dpi_scale * yscale;

                input.x = touchPoint.pos().x() * g_dpi_scale * xscale;
                input.y = touchPoint.pos().y() * g_dpi_scale * yscale;
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
        input_state.pointer_down[0] = (e->type() == QEvent::MouseButtonPress);
        input_state.pointer_x[0] = ((QMouseEvent*)e)->pos().x() * g_dpi_scale * xscale;
        input_state.pointer_y[0] = ((QMouseEvent*)e)->pos().y() * g_dpi_scale * yscale;

        input.x = ((QMouseEvent*)e)->pos().x() * g_dpi_scale * xscale;
        input.y = ((QMouseEvent*)e)->pos().y() * g_dpi_scale * yscale;
        input.flags = (e->type() == QEvent::MouseButtonPress) ? TOUCH_DOWN : TOUCH_UP;
        input.id = 0;
        NativeTouch(input);
        break;
    case QEvent::MouseMove:
        input_state.pointer_x[0] = ((QMouseEvent*)e)->pos().x() * g_dpi_scale * xscale;
        input_state.pointer_y[0] = ((QMouseEvent*)e)->pos().y() * g_dpi_scale * yscale;

        input.x = ((QMouseEvent*)e)->pos().x() * g_dpi_scale * xscale;
        input.y = ((QMouseEvent*)e)->pos().y() * g_dpi_scale * yscale;
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

void MainUI::initializeGL()
{
#ifndef USING_GLES2
	// Some core profile drivers elide certain extensions from GL_EXTENSIONS/etc.
	// glewExperimental allows us to force GLEW to search for the pointers anyway.
	if (gl_extensions.IsCoreContext)
		glewExperimental = true;
	glewInit();
	// Unfortunately, glew will generate an invalid enum error, ignore.
	if (gl_extensions.IsCoreContext)
		glGetError();
#endif
	graphicsContext = new QtDummyGraphicsContext();
	NativeInitGraphics(graphicsContext);
}

void MainUI::paintGL()
{
    updateAccelerometer();
    UpdateInputState(&input_state);
    time_update();
    UpdateRunLoop(&input_state);
}

void MainUI::updateAccelerometer()
{
#if defined(MOBILE_DEVICE) && !defined(MAEMO)
        // TODO: Toggle it depending on whether it is enabled
        QAccelerometerReading *reading = acc->reading();
        if (reading) {
            input_state.acc.x = reading->x();
            input_state.acc.y = reading->y();
            input_state.acc.z = reading->z();
            AxisInput axis;
            axis.deviceId = DEVICE_ID_ACCELEROMETER;
            axis.flags = 0;

            axis.axisId = JOYSTICK_AXIS_ACCELEROMETER_X;
            axis.value = input_state.acc.x;
            NativeAxis(axis);

            axis.axisId = JOYSTICK_AXIS_ACCELEROMETER_Y;
            axis.value = input_state.acc.y;
            NativeAxis(axis);

            axis.axisId = JOYSTICK_AXIS_ACCELEROMETER_Z;
            axis.value = input_state.acc.z;
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
#if defined(Q_OS_LINUX) && !defined(MAEMO)
	QApplication::setAttribute(Qt::AA_X11InitThreads, true);
#endif
	QApplication a(argc, argv);
	QSize res = QApplication::desktop()->screenGeometry().size();
	if (res.width() < res.height())
		res.transpose();
	pixel_xres = res.width();
	pixel_yres = res.height();
	g_dpi_scale = CalculateDPIScale();
	dp_xres = (int)(pixel_xres * g_dpi_scale); dp_yres = (int)(pixel_yres * g_dpi_scale);
	net::Init();
	std::string savegame_dir = ".";
	std::string assets_dir = ".";
#if QT_VERSION > QT_VERSION_CHECK(5, 0, 0)
	savegame_dir = QStandardPaths::writableLocation(QStandardPaths::HomeLocation).toStdString();
	assets_dir = QStandardPaths::writableLocation(QStandardPaths::DataLocation).toStdString();
#elif defined(__SYMBIAN32__)
	savegame_dir = "E:/PPSSPP";
	assets_dir = "E:/PPSSPP";
#elif defined(BLACKBERRY)
	savegame_dir = "/accounts/1000/shared/misc";
	assets_dir = "app/native/assets";
#elif defined(MAEMO)
	savegame_dir = "/home/user/MyDocs/PPSSPP";
	assets_dir = "/opt/PPSSPP";
#endif
	savegame_dir += "/";
	assets_dir += "/";
	
	bool fullscreenCLI=false;
	for (int i = 1; i < argc; i++) 
	{
		if (!strcmp(argv[i],"--fullscreen"))
			fullscreenCLI=true;
	}
	NativeInit(argc, (const char **)argv, savegame_dir.c_str(), assets_dir.c_str(), nullptr, fullscreenCLI);
	
	int ret = mainInternal(a);

	NativeShutdownGraphics();
#ifdef SDL
	SDL_PauseAudio(1);
	SDL_CloseAudio();
#endif
	NativeShutdown();
	net::Shutdown();
	return ret;
}

