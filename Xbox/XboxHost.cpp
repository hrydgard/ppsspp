// Copyright (c) 2012- PPSSPP Project.

// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, version 2.0 or later versions.

// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License 2.0 for more details.

// A copy of the GPL 2.0 should have been included with the program.
// If not, see http://www.gnu.org/licenses/

// Official git repository and contact information can be found at
// https://github.com/hrydgard/ppsspp and http://www.ppsspp.org/.

#include "XboxHost.h"
#include "XaudioSound.h"
#include "Compare.h"

#include <stdio.h>
#include <xtl.h>
#include <io.h>

#include "file/vfs.h"
#include "file/zip_read.h"

#include "Gpu/Directx9/helper/global.h"
#include "Gpu/Directx9/helper/dx_state.h"

const bool WINDOW_VISIBLE = false;
const int WINDOW_WIDTH = 480;
const int WINDOW_HEIGHT = 272;

void XboxHost::LoadNativeAssets()
{
	VFSRegister("", new DirectoryAssetReader("assets\\"));
	VFSRegister("", new DirectoryAssetReader(""));
	VFSRegister("", new DirectoryAssetReader("../"));
	VFSRegister("", new DirectoryAssetReader("game:\\assets\\"));

	// See SendDebugOutput() for how things get back on track.
}

void XboxHost::SendDebugOutput(const std::string &output)
{
	OutputDebugString(output.c_str());
}

void XboxHost::SendDebugScreenshot(const u8 *pixbuf, u32 w, u32 h)
{
	
}

void XboxHost::SetComparisonScreenshot(const std::string &filename)
{
}

bool XboxHost::InitGL(std::string *error_message)
{
	
	LoadNativeAssets();

	return ResizeGL();
}

void XboxHost::ShutdownGL()
{
}

bool XboxHost::ResizeGL()
{
	return true;
}

// scedisplay
extern void __DisplayGetFPS(float *out_vps, float *out_fps, float *out_actual_fps);


static char fpsbuf[1024];

static void ShowFPS() {
	static unsigned long lastTick = 0;
	unsigned long nowTick;
	nowTick = GetTickCount();
	if (lastTick + 1000 <= nowTick) {		
		float vps, fps, cfps;
		__DisplayGetFPS(&vps, &fps, &cfps);
		sprintf(fpsbuf, "Speed: %0.1f%%\nFPS: %0.1f\nVPS: %0.1f\n", vps / 60.0f * 100.0f, fps, vps);

		OutputDebugStringA(fpsbuf);

		lastTick = nowTick;
	}
}

void XboxHost::SwapBuffers()
{

	DX9::SwapBuffers();

	//DX9::dxstate.Restore();
#ifdef _DEBUG
	static int i = 0;
	if (i++ == 60) {
		float vps, fps, cfps;
		__DisplayGetFPS(&vps, &fps, &cfps);
		printf("vps : %f| fps: %f\r\n", vps, fps);
		i = 0;
	}
#endif

	ShowFPS();
}


void XboxHost::InitSound(PMixer *mixer) {
	XAudioInit(mixer);
}
void XboxHost::ShutdownSound() {
	XAudioShutdown();
}

void XboxHost::BeginFrame() {
	DX9::BeginFrame();
}
void XboxHost::EndFrame() {
	DX9::EndFrame();
}