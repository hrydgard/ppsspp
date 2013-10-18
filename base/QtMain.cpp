/*
 * Copyright (c) 2012 Sacha Refshauge
 *
 */
// Qt 4.7 implementation of the framework.
// Currently supports: Symbian, Blackberry, Meego, Linux, Windows

#include <QApplication>
#include <QUrl>
#include <QDir>
#include <QDesktopWidget>
#include <QDesktopServices>
#include <QLocale>

#ifdef __SYMBIAN32__
#include <e32std.h>
#include <QSystemScreenSaver>
#include <hwrmvibra.h>
#endif
#include "QtMain.h"

#if defined(Q_WS_X11) && !defined(MEEGO_EDITION_HARMATTAN) && !defined(__SYMBIAN32__) && !defined(BLACKBERRY)
#define X11LINUXDESKTOP
#endif

InputState* input_state;

std::string System_GetProperty(SystemProperty prop) {
	switch (prop) {
	case SYSPROP_NAME:
#ifdef __SYMBIAN32__
		return "Qt:Symbian";
#elif defined(BLACKBERRY)
		return "Qt:Blackberry10";
#elif defined(MEEGO_EDITION_HARMATTAN)
		return "Qt:Meego";
#elif defined(Q_WS_X11)
		return "Qt:Linux";
#elif defined(_WIN32)
		return "Qt:Windows";
#else
		return "Qt";
#endif
	case SYSPROP_LANGREGION:
		return QLocale::system().name().toStdString();
	default:
		return "";
	}
}

#ifdef __SYMBIAN32__
CHWRMVibra* vibra;
#endif

void Vibrate(int length_ms) {
	if (length_ms == -1 || length_ms == -3)
		length_ms = 50;
	else if (length_ms == -2)
		length_ms = 25;
	// Qt 4.8 does not have any cross-platform Vibrate. Symbian-only for now.
#ifdef __SYMBIAN32__
	CHWRMVibra::TVibraModeState iState = vibra->VibraSettings();
	CHWRMVibra::TVibraStatus iStatus = vibra->VibraStatus();
	// User has not enabled vibration in settings.
	if(iState != CHWRMVibra::EVibraModeON)
		return;
	if(iStatus != CHWRMVibra::EVibraStatusStopped)
		vibra->StopVibraL();
#endif

#ifdef __SYMBIAN32__
	vibra->StartVibraL(length_ms, 20);
#endif
}

void LaunchBrowser(const char *url)
{
	QDesktopServices::openUrl(QUrl(url));
}

void SimulateGamepad(InputState *input) {
	input->pad_lstick_x = 0;
	input->pad_lstick_y = 0;
	input->pad_rstick_x = 0;
	input->pad_rstick_y = 0;

	if (input->pad_buttons & PAD_BUTTON_JOY_UP)
		input->pad_lstick_y=1;
	else if (input->pad_buttons & PAD_BUTTON_JOY_DOWN)
		input->pad_lstick_y=-1;
	if (input->pad_buttons & PAD_BUTTON_JOY_LEFT)
		input->pad_lstick_x=-1;
	else if (input->pad_buttons & PAD_BUTTON_JOY_RIGHT)
		input->pad_lstick_x=1;
}

float CalculateDPIScale()
{
	// Sane default rather than check DPI
#ifdef __SYMBIAN32__
	return 1.4f;
#else
	return 1.2f;
#endif
}

Q_DECL_EXPORT int main(int argc, char *argv[])
{
#ifdef Q_WS_X11
	QApplication::setAttribute(Qt::AA_X11InitThreads, true);
#endif
	QApplication a(argc, argv);
	QSize res = QApplication::desktop()->screenGeometry().size();
	if (res.width() < res.height())
		res.transpose();
	pixel_xres = res.width();
	pixel_yres = res.height();
#ifdef X11LINUXDESKTOP
	g_dpi_scale = 1.0f;
#else
	g_dpi_scale = CalculateDPIScale();
#endif
	dp_xres = (int)(pixel_xres * g_dpi_scale); dp_yres = (int)(pixel_yres * g_dpi_scale);
	net::Init();
#ifdef __SYMBIAN32__
	char* savegame_dir = "E:/PPSSPP/";
	char* assets_dir = "E:/PPSSPP/";
#elif defined(BLACKBERRY)
	char* savegame_dir = "/accounts/1000/shared/misc/";
	char* assets_dir = "app/native/assets/";
#elif defined(MEEGO_EDITION_HARMATTAN)
	char* savegame_dir = "/home/user/MyDocs/PPSSPP/";
	QDir myDocs("/home/user/MyDocs/");
	if (!myDocs.exists("PPSSPP"))
		myDocs.mkdir("PPSSPP");
	char* assets_dir = "/opt/PPSSPP/";
#else
	char* savegame_dir = "./";
	char* assets_dir = "./";
#endif
	NativeInit(argc, (const char **)argv, savegame_dir, assets_dir, "BADCOFFEE");

#if !defined(Q_WS_X11) || defined(ARM)
	MainUI w;
	w.resize(pixel_xres, pixel_yres);
	w.showFullScreen();
#endif
#ifdef __SYMBIAN32__
	// Set RunFast hardware mode for VFPv2.
	User::SetFloatingPointMode(EFpModeRunFast);
	// Disable screensaver
	QSystemScreenSaver *ssObject = new QSystemScreenSaver(&w);
	ssObject->setScreenSaverInhibit();
	// Start vibration service
	vibra = CHWRMVibra::NewL();
#endif

	MainAudio *audio = new MainAudio();

	int ret = a.exec();
	delete audio;
#ifdef __SYMBIAN32__
	delete vibra;
#endif
	NativeShutdown();
	net::Shutdown();
	return ret;
}
