/*
 * Copyright (c) 2012 Sacha Refshauge
 *
 */
// Qt implementation of the framework.
// Currently supports: Symbian, Blackberry, Linux

#include <QtGui/QApplication>
#include <QUrl>
#include <QDir>
#include <QDesktopWidget>
#include <QDesktopServices>

#ifdef __SYMBIAN32__
#include <AknAppUi.h>
#endif
#include "QtMain.h"

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
	// Sane default for Symbian, Blackberry and Meego
#ifdef __SYMBIAN32__
	return 1.3f;
#else
	return 1.2f;
#endif
}

int main(int argc, char *argv[])
{
#ifdef Q_WS_X11
	QApplication::setAttribute(Qt::AA_X11InitThreads, true);
#endif
	QApplication a(argc, argv);
#ifdef __SYMBIAN32__
	// Lock orientation to landscape on Symbian
	QT_TRAP_THROWING(dynamic_cast<CAknAppUi*>(CEikonEnv::Static()->AppUi())->SetOrientationL(CAknAppUi::EAppUiOrientationLandscape));
	// Set RunFast hardware mode for VFPv2. Denormalised values are treated as 0. NaN used for all NaN situations.
	User::SetFloatingPointMode(EFpModeRunFast);
#endif
	QSize res = QApplication::desktop()->screenGeometry().size();
	if (res.width() < res.height())
		res.transpose();
	pixel_xres = res.width();
	pixel_yres = res.height();

	float dpi_scale = CalculateDPIScale();
	dp_xres = (int)(pixel_xres * dpi_scale); dp_yres = (int)(pixel_yres * dpi_scale);
	net::Init();
#ifdef __SYMBIAN32__
	char* savegame_dir = "E:/PPSSPP/";
#elif defined(BLACKBERRY)
	char* savegame_dir = "data/";
#else
	char* savegame_dir = "./";
#endif
	NativeInit(argc, (const char **)argv, savegame_dir, QDir::tempPath().toStdString().c_str(), "BADCOFFEE");

#if defined(USING_GLES2)
	MainUI w(dpi_scale);
	w.resize(pixel_xres, pixel_yres);
	w.showFullScreen();
#endif

	MainAudio *audio = new MainAudio();

	int ret = a.exec();
	delete audio;
	NativeShutdown();
	net::Shutdown();
	return ret;
}
