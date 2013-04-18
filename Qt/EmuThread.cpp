#include "EmuThread.h"
#include "Core/System.h"

#include <QString>

void EmuThread_LockDraw(bool value)
{
	// left there just to avoid compilation problems, this is called a lot
	// in debuggers
}

QString GetCurrentFilename()
{
	return QString::fromStdString(PSP_CoreParameter().fileToStart);
}
