/*
 * Copyright (c) 2012 Sacha Refshauge
 *
 */
// Qt implementation of the framework.
// Currently supports: Symbian, Blackberry, Linux

#include <QtGui/QApplication>
#include <QUrl>
#include <QDesktopWidget>
#include <QDesktopServices>

#ifdef __SYMBIAN32__
#include <AknAppUi.h>
#endif
#include "QtMain.h"

#ifdef LINUX
#include "mainwindow.h"
#include "ui_mainwindow.h"
#include "qtapp.h"
#ifdef Q_WS_X11
#include <X11/Xlib.h>
#endif
MainWindow* qMW;
#endif

void LaunchBrowser(const char *url)
{
	QDesktopServices::openUrl(QUrl(url));
}

void SimulateGamepad(InputState *input) {
	input->pad_lstick_x = 0;
	input->pad_lstick_y = 0;
	input->pad_rstick_x = 0;
	input->pad_rstick_y = 0;

	if (input->pad_buttons & (1<<14))
		input->pad_lstick_y=1;
	else if (input->pad_buttons & (1<<15))
		input->pad_lstick_y=-1;
	if (input->pad_buttons & (1<<16))
		input->pad_lstick_x=-1;
	else if (input->pad_buttons & (1<<17))
		input->pad_lstick_x=1;
}

float CalculateDPIScale()
{
	// Calculate DPI from TWIPS on Symbian
#ifdef __SYMBIAN32__
	TSize sTwips = CEikonEnv::Static()->ScreenDevice()->SizeInTwips();
	float dpi = sqrt((float)(pixel_xres*pixel_xres + pixel_yres*pixel_yres))
		/ (sqrt((float)(sTwips.iHeight*sTwips.iHeight + sTwips.iWidth*sTwips.iWidth)) / KTwipsPerInch);
	return dpi / 170.0f;
#else
	// Sane default for Blackberry and Meego
	return 1.2f;
#endif
}

int main(int argc, char *argv[])
{
#ifdef LINUX
#ifdef Q_WS_X11
	XInitThreads();
#endif
#endif
	QApplication a(argc, argv);
	// Lock orientation to landscape on Symbian
#ifdef __SYMBIAN32__
	QT_TRAP_THROWING(dynamic_cast<CAknAppUi*>(CEikonEnv::Static()->AppUi())->SetOrientationL(CAknAppUi::EAppUiOrientationLandscape));
#endif
#ifdef LINUX
	pixel_xres = 480;
	pixel_yres = 272;

	float dpi_scale = 1;
	dp_xres = (int)(pixel_xres * dpi_scale); dp_yres = (int)(pixel_yres * dpi_scale);
#else
	QSize res = QApplication::desktop()->screenGeometry().size();
#ifdef USING_GLES2
	if (res.width() < res.height())
		res.transpose();
	pixel_xres = res.width();
	pixel_yres = res.height();
#else
	// Set resolution to half of the monitor on desktop systems
	pixel_xres = res.width() / 2;
	pixel_yres = res.height() / 2;
#endif
	float dpi_scale = CalculateDPIScale();
	dp_xres = (int)(pixel_xres * dpi_scale); dp_yres = (int)(pixel_yres * dpi_scale);
#endif

	net::Init();

#ifdef __SYMBIAN32__
	NativeInit(argc, (const char **)argv, "E:/PPSSPP/", "E:", "BADCOFFEE");
#elif defined(BLACKBERRY)
	NativeInit(argc, (const char **)argv, "data/", "/tmp", "BADCOFFEE");
#elif !defined(LINUX)
	NativeInit(argc, (const char **)argv, "./", "/tmp", "BADCOFFEE");
#else
	MainWindow mainWindow;
	qMW = &mainWindow;
	mainWindow.show();
	mainWindow.Create(argc, (const char **)argv, "./", "/tmp", "BADCOFFEE");
#endif

#ifdef LINUX
#else
	MainUI w(dpi_scale);
	w.resize(pixel_xres, pixel_yres);
#ifdef USING_GLES2
	w.showFullScreen();
#else
	w.show();
#endif

#endif

	MainAudio *audio = new MainAudio();

	int ret = a.exec();
	delete audio;
	NativeShutdown();
	net::Shutdown();
	return ret;
}

