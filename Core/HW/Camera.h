// Copyright (c) 2020- PPSSPP Project.

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
#pragma once

#include "ppsspp_config.h"
#include "Core/HLE/sceUsbCam.h"

#include "ext/jpge/jpgd.h"
#include "ext/jpge/jpge.h"

extern "C" {
#ifdef USE_FFMPEG
#include "libswscale/swscale.h"
#include "libavutil/imgutils.h"
#endif //USE_FFMPEG
}

void __cameraDummyImage(int width, int height, unsigned char** outData, int* outLen);

#if defined(USING_QT_UI)
#include <QAbstractVideoSurface>
#include <QCameraInfo>

	class MyViewfinder : public QAbstractVideoSurface {
		Q_OBJECT
	public:
		QList<QVideoFrame::PixelFormat> supportedPixelFormats(QAbstractVideoBuffer::HandleType handleType) const;
		bool present(const QVideoFrame &frame);
	};

	static int        qtc_ideal_width;
	static int        qtc_ideal_height;
	static QCamera *qt_camera;
	static QAbstractVideoSurface *qt_viewfinder;

	std::vector<std::string> __qt_getDeviceList();
	int __qt_startCapture(int width, int height);
	int __qt_stopCapture();

#elif PPSSPP_PLATFORM(LINUX) && !PPSSPP_PLATFORM(ANDROID)
#include <fcntl.h>
#include <linux/videodev2.h>
#include <sys/ioctl.h>
#include <sys/mman.h>

#include "Common/Thread/ThreadUtil.h"

	typedef struct {
		void         *start;
		int           length;
	} v4l_buf_t;

	static int        v4l_fd = -1;
	static uint32_t   v4l_format;
	static int        v4l_hw_width;
	static int        v4l_hw_height;
	static int        v4l_height_fixed_aspect;
	static int        v4l_ideal_width;
	static int        v4l_ideal_height;

	static pthread_t  v4l_thread;
	static int        v4l_buffer_count;
	static v4l_buf_t *v4l_buffers;

	std::vector<std::string> __v4l_getDeviceList();
	int __v4l_startCapture(int width, int height);
	int __v4l_stopCapture();
#endif