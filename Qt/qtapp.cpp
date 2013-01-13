#include "qtapp.h"
#include "EmuThread.h"
#include "Core/Debugger/SymbolMap.h"
#include "native/base/NativeApp.h"

QtApp::QtApp(MainWindow *mainWindow_)
	: mainWindow(mainWindow_),
	  QObject(0)
{
	QObject::connect(this,SIGNAL(BootDoneSignal()),mainWindow,SLOT(Boot()));
}

void QtApp::InitGL()
{

}

void QtApp::ShutdownGL()
{
	//GL_Shutdown();
}

void QtApp::SetWindowTitle(const char *message)
{
	// Really need a better way to deal with versions.
	QString title = "PPSSPP v0.5 - ";
	title += QString::fromUtf8(message);

	mainWindow->setWindowTitle(title);
}

void QtApp::InitSound(PMixer *mixer)
{
	NativeSetMixer(mixer);
}

void QtApp::UpdateSound()
{
}

void QtApp::ShutdownSound()
{
	NativeSetMixer(0);
}

void QtApp::UpdateUI()
{
	//mainWindow->Update();
	mainWindow->UpdateMenus();
}


void QtApp::UpdateMemView()
{
	/*for (int i=0; i<numCPUs; i++)
		if (memoryWindow[i])
			memoryWindow[i]->Update();*/
}

void QtApp::UpdateDisassembly()
{
	/*for (int i=0; i<numCPUs; i++)
		if (disasmWindow[i])
			disasmWindow[i]->Update();*/
	/*if(dialogDisasm)
		dialogDisasm->Update();*/
}

void QtApp::SetDebugMode(bool mode)
{
	/*for (int i=0; i<numCPUs; i++)*/
	if(mainWindow->GetDialogDisasm())
		mainWindow->GetDialogDisasm()->SetDebugMode(mode);
}


void QtApp::BeginFrame()
{
	/*for (auto iter = this->input.begin(); iter != this->input.end(); iter++)
		if ((*iter)->UpdateState() == 0)
			break; // *iter is std::shared_ptr, **iter is InputDevice
	GL_BeginFrame();*/
}
void QtApp::EndFrame()
{
	//GL_EndFrame();
}

void QtApp::BootDone()
{
	symbolMap.SortSymbols();
	emit BootDoneSignal();
}

bool QtApp::AttemptLoadSymbolMap()
{
	char filename[256];
	strcpy(filename, GetCurrentFilename());
	int len = strlen(filename);
	int ptpos = len-1;
	while (filename[ptpos]!='.' && ptpos>len-8)
		ptpos--;
	filename[ptpos+1] = 'm';
	filename[ptpos+2] = 'a';
	filename[ptpos+3] = 'p';
	return symbolMap.LoadSymbolMap(filename);
}

void QtApp::PrepareShutdown()
{
	char filename[256];
	strcpy(filename, GetCurrentFilename());
	int len = strlen(filename);
	int ptpos = len-1;
	while (filename[ptpos]!='.' && ptpos>len-8)
		ptpos--;
	filename[ptpos+1] = 'm';
	filename[ptpos+2] = 'a';
	filename[ptpos+3] = 'p';
	symbolMap.SaveSymbolMap(filename);
}

void QtApp::AddSymbol(std::string name, u32 addr, u32 size, int type=0)
{
	symbolMap.AddSymbol(name.c_str(), addr, size, (SymbolType)type);
}

bool QtApp::IsDebuggingEnabled()
{
#ifdef _DEBUG
	return true;
#else
	return false;
#endif
}
