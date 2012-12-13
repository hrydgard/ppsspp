/*
 * Copyright (c) 2012 Sacha Refshauge
 *
 */
// Qt implementation of the framework.
// Currently supports: Symbian

#include <QtGui/QApplication>
#include <QUrl>
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

int main(int argc, char *argv[])
{
	QApplication a(argc, argv);
	// Lock orientation to landscape on Symbian
#ifdef __SYMBIAN32__
	QT_TRAP_THROWING(dynamic_cast<CAknAppUi*>(CEikonEnv::Static()->AppUi())->SetOrientationL(CAknAppUi::EAppUiOrientationLandscape));
#endif
	net::Init();
	QSize res = QApplication::desktop()->screenGeometry().size();
	if (res.width() < res.height())
		res.transpose();
	pixel_xres = res.width();
	pixel_yres = res.height();
	// Calculate DPI from TWIPS on Symbian
#ifdef __SYMBIAN32__
	TSize sizeTwips = CEikonEnv::Static()->ScreenDevice()->SizeInTwips();
	float dpi = sqrt((float)(pixel_xres*pixel_xres + pixel_yres*pixel_yres))
		/ (sqrt((float)(sizeTwips.iHeight*sizeTwips.iHeight + sizeTwips.iWidth*sizeTwips.iWidth)) / KTwipsPerInch);
	float dpi_scale = dpi / 170.0f;
#endif
	dp_xres = (int)(pixel_xres * dpi_scale); dp_yres = (int)(pixel_yres * dpi_scale);
	NativeInit(argc, (const char **)argv,
#ifdef __SYMBIAN32__
			   "E:/PPSSPP/", "E:", "BADCOFFEE");
#else
			   "./","/tmp","BADCOFFEE");
#endif
	MainUI w;
	w.resize(pixel_xres, pixel_yres);
	w.showFullScreen();
	int ret = a.exec();
	NativeShutdown();
	net::Shutdown();
	return ret;
}

