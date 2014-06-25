/*
 * Copyright (c) 2012 Sacha Refshauge
 *
 */
// Qt 4.7+ / 5.0+ implementation of the framework.
// Currently supports: Symbian, Blackberry, Maemo/Meego, Linux, Windows, Mac OSX

#include <QApplication>
#include <QUrl>
#include <QDir>
#include <QDesktopWidget>
#include <QDesktopServices>
#include <QLocale>
#include <QThread>

#ifdef ANDROID
#include <QStandardPaths>
#endif

#ifdef __SYMBIAN32__
#include <e32std.h>
#include <QSystemScreenSaver>
#include <QFeedbackHapticsEffect>
#include "SymbianMediaKeys.h"
#endif
#ifdef QT_HAS_SDL
#include "SDL/SDLJoystick.h"
#include "SDL_audio.h"
#endif
#include "QtMain.h"

#include <string.h>

InputState* input_state;

#ifdef QT_HAS_SDL
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

void System_SendMessage(const char *command, const char *parameter) {
	if (!strcmp(command, "finish")) {
		qApp->exit(0);
	}
}

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
#ifdef __SYMBIAN32__
	// Set RunFast hardware mode for VFPv2.
	User::SetFloatingPointMode(EFpModeRunFast);
	// Disable screensaver
	QScopedPointer<QSystemScreenSaver> ssObject(new QSystemScreenSaver(emugl));
	ssObject->setScreenSaverInhibit();
	QScopedPointer<SymbianMediaKeys> mediakeys(new SymbianMediaKeys());
#endif

#ifdef QT_HAS_SDL
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
		if (ret_fmt.freq != 44100 || ret_fmt.format != AUDIO_S16 || ret_fmt.channels != 2 || fmt.samples != 2048) {
			ELOG("Sound buffer format does not match requested format.");
			ELOG("Output audio freq: %d (requested: %d)", ret_fmt.freq, 44100);
			ELOG("Output audio format: %d (requested: %d)", ret_fmt.format, AUDIO_S16);
			ELOG("Output audio channels: %d (requested: %d)", ret_fmt.channels, 2);
			ELOG("Output audio samples: %d (requested: %d)", ret_fmt.samples, 2048);
		}

		if (ret_fmt.freq != 44100 || ret_fmt.format != AUDIO_S16 || ret_fmt.channels != 2) {
			ELOG("Provided output format does not match requirement, turning audio off");
			SDL_CloseAudio();
		} else {
			ELOG("Provided output audio format is usable, thus using it");
		}
	}

	// Audio must be unpaused _after_ NativeInit()
	SDL_PauseAudio(0);
#else
	QScopedPointer<QThread> thread(new QThread);
	QScopedPointer<MainAudio> audio(new MainAudio());
	audio->moveToThread(thread.data());
	QObject::connect(thread.data(), SIGNAL(started()), audio.data(), SLOT(run()));
	thread->start();
#endif
	int ret = a.exec();
#ifndef QT_HAS_SDL
	thread->quit();
#endif
	return ret;
}

#ifndef QT_HAS_SDL
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
#ifdef __SYMBIAN32__
	const char *savegame_dir = "E:/PPSSPP/";
	const char *assets_dir = "E:/PPSSPP/";
#elif defined(BLACKBERRY)
	const char *savegame_dir = "/accounts/1000/shared/misc/";
	const char *assets_dir = "app/native/assets/";
#elif defined(MAEMO)
	const char *savegame_dir = "/home/user/MyDocs/PPSSPP/";
	const char *assets_dir = "/opt/PPSSPP/";
#elif defined(ANDROID)
	const char *savegame_dir = QStandardPaths::standardLocations(QStandardPaths::HomeLocation).at(0).toStdString().c_str();
	const char *assets_dir = QStandardPaths::standardLocations(QStandardPaths::GenericDataLocation).at(0).toStdString().c_str();
	setenv("QT_USE_ANDROID_NATIVE_DIALOGS", "1", 1); // Which Qt version does this need?
#else
	const char *savegame_dir = "./";
	const char *assets_dir = "./";
#endif
	NativeInit(argc, (const char **)argv, savegame_dir, assets_dir, "BADCOFFEE");

	int ret = mainInternal(a);

#ifndef MOBILE_DEVICE
	exit(0);
#endif
	NativeShutdownGraphics();
	SDL_PauseAudio(1);
	SDL_CloseAudio();
	NativeShutdown();
	net::Shutdown();
	return ret;
}

