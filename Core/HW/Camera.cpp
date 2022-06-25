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
#include "ppsspp_config.h"
#include "Camera.h"
#include "Core/Config.h"

#ifdef USE_FFMPEG
void convert_frame(int inw, int inh, unsigned char *inData, AVPixelFormat inFormat,
					int outw, int outh, unsigned char **outData, int *outLen) {
	struct SwsContext *sws_context = sws_getContext(
				inw, inh, inFormat,
				outw, outh, AV_PIX_FMT_RGB24,
				SWS_BICUBIC, NULL, NULL, NULL);

	// resize
	uint8_t *src[4] = {0};
	uint8_t *dst[4] = {0};
	int srcStride[4], dstStride[4];

	unsigned char *rgbData = (unsigned char*)malloc(outw * outh * 4);

	av_image_fill_linesizes(srcStride, inFormat,         inw);
	av_image_fill_linesizes(dstStride, AV_PIX_FMT_RGB24, outw);

	av_image_fill_pointers(src, inFormat,         inh,  inData,  srcStride);
	av_image_fill_pointers(dst, AV_PIX_FMT_RGB24, outh, rgbData, dstStride);

	sws_scale(sws_context,
		src, srcStride, 0, inh,
		dst, dstStride);

	// compress jpeg
	*outLen = outw * outh * 2;
	*outData = (unsigned char*)malloc(*outLen);

	jpge::params params;
	params.m_quality = 60;
	params.m_subsampling = jpge::H2V2;
	params.m_two_pass_flag = false;
	jpge::compress_image_to_jpeg_file_in_memory(
		*outData, *outLen, outw, outh, 3, rgbData, params);
	free(rgbData);
}
#endif //USE_FFMPEG


void __cameraDummyImage(int width, int height, unsigned char** outData, int* outLen) {
#ifdef USE_FFMPEG
	unsigned char* rgbData = (unsigned char*)malloc(3 * width * height);
	for (int y = 0; y < height; y++) {
		for (int x = 0; x < width; x++) {
			rgbData[3 * (y * width + x) + 0] = x*255/width;
			rgbData[3 * (y * width + x) + 1] = x*255/width;
			rgbData[3 * (y * width + x) + 2] = y*255/height;
		}
	}

	*outLen = width * height * 2;
	*outData = (unsigned char*)malloc(*outLen);

	jpge::params params;
	params.m_quality = 60;
	params.m_subsampling = jpge::H2V2;
	params.m_two_pass_flag = false;
	jpge::compress_image_to_jpeg_file_in_memory(
		*outData, *outLen, width, height, 3, rgbData, params);
	free(rgbData);
#endif //USE_FFMPEG
}


#if defined(USING_QT_UI)

std::vector<std::string> __qt_getDeviceList() {
	std::vector<std::string> deviceList;
	const QList<QCameraInfo> cameras = QCameraInfo::availableCameras();
	for (const QCameraInfo &cameraInfo : cameras) {
		deviceList.push_back(cameraInfo.deviceName().toStdString()
			+ " (" + cameraInfo.description().toStdString() + ")");
	}
	return deviceList;
}

QList<QVideoFrame::PixelFormat> MyViewfinder::supportedPixelFormats(QAbstractVideoBuffer::HandleType handleType) const {
	Q_UNUSED(handleType);
	// Return the formats you will support
	return QList<QVideoFrame::PixelFormat>()
		<< QVideoFrame::Format_RGB24
		<< QVideoFrame::Format_YUYV
		;
}

bool MyViewfinder::present(const QVideoFrame &frame) {
#ifdef USE_FFMPEG
	if (frame.isValid()) {
		QVideoFrame cloneFrame(frame);
		cloneFrame.map(QAbstractVideoBuffer::ReadOnly);

		unsigned char *jpegData = nullptr;
		int jpegLen = 0;

		QVideoFrame::PixelFormat frameFormat = cloneFrame.pixelFormat();
		if (frameFormat == QVideoFrame::Format_RGB24) {
			convert_frame(cloneFrame.size().width(), cloneFrame.size().height(),
				(unsigned char*)cloneFrame.bits(), AV_PIX_FMT_RGB24,
				qtc_ideal_width, qtc_ideal_height, &jpegData, &jpegLen);

		} else if (frameFormat == QVideoFrame::Format_YUYV) {
			convert_frame(cloneFrame.size().width(), cloneFrame.size().height(),
				(unsigned char*)cloneFrame.bits(), AV_PIX_FMT_YUYV422,
				qtc_ideal_width, qtc_ideal_height, &jpegData, &jpegLen);
		}

		if (jpegData) {
			Camera::pushCameraImage(jpegLen, jpegData);
			free(jpegData);
			jpegData = nullptr;
		}

		cloneFrame.unmap();
		return true;
	}
#endif //USE_FFMPEG
	return false;
}

int __qt_startCapture(int width, int height) {
	if (qt_camera != nullptr) {
		ERROR_LOG(HLE, "camera already started");
		return -1;
	}

	char selectedCamera[80];
	sscanf(g_Config.sCameraDevice.c_str(), "%80s ", &selectedCamera[0]);

	const QList<QCameraInfo> availableCameras = QCameraInfo::availableCameras();
	if (availableCameras.size() < 1) {
		delete qt_camera;
		qt_camera = nullptr;
		ERROR_LOG(HLE, "no camera found");
		return -1;
	}
	for (const QCameraInfo &cameraInfo : availableCameras) {
		if (cameraInfo.deviceName() == selectedCamera) {
			qt_camera = new QCamera(cameraInfo);
		}
	}
	if (qt_camera == nullptr) {
		qt_camera = new QCamera();
		if (qt_camera == nullptr) {
			ERROR_LOG(HLE, "cannot open camera");
			return -1;
		}
	}

	qtc_ideal_width = width;
	qtc_ideal_height = height;

	qt_viewfinder = new MyViewfinder;

	QCameraViewfinderSettings viewfinderSettings = qt_camera->viewfinderSettings();
	viewfinderSettings.setResolution(640, 480);
	viewfinderSettings.setMinimumFrameRate(15.0);
	viewfinderSettings.setMaximumFrameRate(15.0);

	qt_camera->setViewfinderSettings(viewfinderSettings);
	qt_camera->setViewfinder(qt_viewfinder);
	qt_camera->start();

	return 0;
}

int __qt_stopCapture() {
	if (qt_camera != nullptr) {
		qt_camera->stop();
		qt_camera->unload();
		delete qt_camera;
		delete qt_viewfinder;
		qt_camera = nullptr;
	}
	return 0;
}

//endif defined(USING_QT_UI)
#elif PPSSPP_PLATFORM(LINUX) && !PPSSPP_PLATFORM(ANDROID)

std::vector<std::string> __v4l_getDeviceList() {
	std::vector<std::string> deviceList;
#ifdef USE_FFMPEG
	for (int i = 0; i < 64; i++) {
		char path[256];
		snprintf(path, sizeof(path), "/dev/video%d", i);
		if (access(path, F_OK) < 0) {
			break;
		}
		int fd = -1;
		if((fd = open(path, O_RDONLY)) < 0) {
			ERROR_LOG(HLE, "Cannot open '%s'; errno=%d(%s)", path, errno, strerror(errno));
			continue;
		}
		struct v4l2_capability video_cap;
		if(ioctl(fd, VIDIOC_QUERYCAP, &video_cap) < 0) {
			ERROR_LOG(HLE, "VIDIOC_QUERYCAP");
			goto cont;
		} else {
			char device[256];
			snprintf(device, sizeof(device), "%d:%s", i, video_cap.card);
			deviceList.push_back(device);
		}
cont:
		close(fd);
		fd = -1;
	}
#endif //USE_FFMPEG
	return deviceList;
}

void *v4l_loop(void *data) {
#ifdef USE_FFMPEG
	SetCurrentThreadName("v4l_loop");
	while (v4l_fd >= 0) {
		struct v4l2_buffer buf;
		memset(&buf, 0, sizeof(buf));
		buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		buf.memory = V4L2_MEMORY_MMAP;

		if (ioctl(v4l_fd, VIDIOC_DQBUF, &buf) == -1) {
			ERROR_LOG(HLE, "VIDIOC_DQBUF; errno=%d(%s)", errno, strerror(errno));
			switch (errno) {
			case EAGAIN:
				continue;
			default:
				return nullptr;
			}
		}

		unsigned char *jpegData = nullptr;
		int jpegLen = 0;

		if (v4l_format == V4L2_PIX_FMT_YUYV) {
			convert_frame(v4l_hw_width, v4l_hw_height, (unsigned char*)v4l_buffers[buf.index].start, AV_PIX_FMT_YUYV422,
				v4l_ideal_width, v4l_ideal_height, &jpegData, &jpegLen);
		} else if (v4l_format == V4L2_PIX_FMT_JPEG
				|| v4l_format == V4L2_PIX_FMT_MJPEG) {
			// decompress jpeg
			int width, height, req_comps;
			unsigned char *rgbData = jpgd::decompress_jpeg_image_from_memory(
				(unsigned char*)v4l_buffers[buf.index].start, buf.bytesused, &width, &height, &req_comps, 3);

			convert_frame(v4l_hw_width, v4l_hw_height, (unsigned char*)rgbData, AV_PIX_FMT_RGB24,
				v4l_ideal_width, v4l_ideal_height, &jpegData, &jpegLen);
			free(rgbData);
		}

		if (jpegData) {
			Camera::pushCameraImage(jpegLen, jpegData);
			free(jpegData);
			jpegData = nullptr;
		}

		buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		buf.memory = V4L2_MEMORY_MMAP;
		if (ioctl(v4l_fd, VIDIOC_QBUF, &buf) == -1) {
			ERROR_LOG(HLE, "VIDIOC_QBUF");
			return nullptr;
		}
	}
	return nullptr;
#endif //USE_FFMPEG
}

int __v4l_startCapture(int ideal_width, int ideal_height) {
#ifdef USE_FFMPEG
	if (v4l_fd >= 0) {
		__v4l_stopCapture();
	}
	v4l_ideal_width  = ideal_width;
	v4l_ideal_height = ideal_height;

	int dev_index = 0;
	char dev_name[64];
	sscanf(g_Config.sCameraDevice.c_str(), "%d:", &dev_index);
	snprintf(dev_name, sizeof(dev_name), "/dev/video%d", dev_index);

	if ((v4l_fd = open(dev_name, O_RDWR)) == -1) {
		ERROR_LOG(HLE, "Cannot open '%s'; errno=%d(%s)", dev_name, errno, strerror(errno));
		return -1;
	}

	struct v4l2_capability cap;
	memset(&cap, 0, sizeof(cap));
	if (ioctl(v4l_fd, VIDIOC_QUERYCAP, &cap) == -1) {
		ERROR_LOG(HLE, "VIDIOC_QUERYCAP");
		return -1;
	}
	if (!(cap.capabilities & V4L2_CAP_VIDEO_CAPTURE)) {
		ERROR_LOG(HLE, "V4L2_CAP_VIDEO_CAPTURE");
		return -1;
	}
	if (!(cap.capabilities & V4L2_CAP_STREAMING)) {
		ERROR_LOG(HLE, "V4L2_CAP_STREAMING");
		return -1;
	}

	struct v4l2_format fmt;
	memset(&fmt, 0, sizeof(fmt));
	fmt.type                = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	fmt.fmt.pix.pixelformat = 0;

	// select a pixel format
	struct v4l2_fmtdesc desc;
	memset(&desc, 0, sizeof(desc));
	desc.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	while (ioctl(v4l_fd, VIDIOC_ENUM_FMT, &desc) == 0) {
		desc.index++;
		INFO_LOG(HLE, "V4L2: pixel format supported: %s", desc.description);
		if (fmt.fmt.pix.pixelformat != 0) {
			continue;
		} else if (desc.pixelformat == V4L2_PIX_FMT_YUYV
				|| desc.pixelformat == V4L2_PIX_FMT_JPEG
				|| desc.pixelformat == V4L2_PIX_FMT_MJPEG) {
			INFO_LOG(HLE, "V4L2: %s selected", desc.description);
			fmt.fmt.pix.pixelformat = desc.pixelformat;
			v4l_format              = desc.pixelformat;
		}
	}
	if (fmt.fmt.pix.pixelformat == 0) {
		ERROR_LOG(HLE, "V4L2: No supported format found");
		return -1;
	}

	// select a frame size
	fmt.fmt.pix.width  = 0;
	fmt.fmt.pix.height = 0;
	struct v4l2_frmsizeenum frmsize;
	memset(&frmsize, 0, sizeof(frmsize));
	frmsize.pixel_format = fmt.fmt.pix.pixelformat;
	while (ioctl(v4l_fd, VIDIOC_ENUM_FRAMESIZES, &frmsize) == 0) {
		frmsize.index++;
		if (frmsize.type == V4L2_FRMSIZE_TYPE_DISCRETE) {
			INFO_LOG(HLE, "V4L2: frame size supported: %dx%d", frmsize.discrete.width, frmsize.discrete.height);
			bool matchesIdeal = frmsize.discrete.width >= ideal_width && frmsize.discrete.height >= ideal_height;
			bool zeroPix = fmt.fmt.pix.width == 0 && fmt.fmt.pix.height == 0;
			bool pixLarger = frmsize.discrete.width < fmt.fmt.pix.width && frmsize.discrete.height < fmt.fmt.pix.height;
			if (matchesIdeal && (zeroPix || pixLarger)) {
				fmt.fmt.pix.width  = frmsize.discrete.width;
				fmt.fmt.pix.height = frmsize.discrete.height;
			}
		}
	}

	if (fmt.fmt.pix.width == 0 && fmt.fmt.pix.height == 0) {
		fmt.fmt.pix.width  = ideal_width;
		fmt.fmt.pix.height = ideal_height;
	}
	INFO_LOG(HLE, "V4L2: asking for   %dx%d", fmt.fmt.pix.width, fmt.fmt.pix.height);
	if (ioctl(v4l_fd, VIDIOC_S_FMT, &fmt) == -1) {
		ERROR_LOG(HLE, "VIDIOC_S_FMT");
		return -1;
	}
	v4l_hw_width  = fmt.fmt.pix.width;
	v4l_hw_height = fmt.fmt.pix.height;
	INFO_LOG(HLE, "V4L2: will receive %dx%d", v4l_hw_width, v4l_hw_height);
	v4l_height_fixed_aspect = v4l_hw_width * ideal_height / ideal_width;
	INFO_LOG(HLE, "V4L2: will use     %dx%d", v4l_hw_width, v4l_height_fixed_aspect);

	struct v4l2_requestbuffers req;
	memset(&req, 0, sizeof(req));
	req.count  = 1;
	req.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	req.memory = V4L2_MEMORY_MMAP;
	if (ioctl(v4l_fd, VIDIOC_REQBUFS, &req) == -1) {
		ERROR_LOG(HLE, "VIDIOC_REQBUFS");
		return -1;
	}
	v4l_buffer_count = req.count;
	INFO_LOG(HLE, "V4L2: buffer count: %d", v4l_buffer_count);
	v4l_buffers = (v4l_buf_t*) calloc(v4l_buffer_count, sizeof(v4l_buf_t));

	for (int buf_id = 0; buf_id < v4l_buffer_count; buf_id++) {
		struct v4l2_buffer buf;
		memset(&buf, 0, sizeof(buf));
		buf.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		buf.memory = V4L2_MEMORY_MMAP;
		buf.index  = buf_id;
		if (ioctl(v4l_fd, VIDIOC_QUERYBUF, &buf) == -1) {
			ERROR_LOG(HLE, "VIDIOC_QUERYBUF");
			return -1;
		}

		v4l_buffers[buf_id].length = buf.length;
		v4l_buffers[buf_id].start = mmap(NULL,
				buf.length,
				PROT_READ | PROT_WRITE,
				MAP_SHARED,
				v4l_fd, buf.m.offset);
		if (v4l_buffers[buf_id].start == MAP_FAILED) {
			ERROR_LOG(HLE, "MAP_FAILED");
			return -1;
		}

		memset(&buf, 0, sizeof(buf));
		buf.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		buf.memory = V4L2_MEMORY_MMAP;
		buf.index  = buf_id;
		if (ioctl(v4l_fd, VIDIOC_QBUF, &buf) == -1) {
			ERROR_LOG(HLE, "VIDIOC_QBUF");
			return -1;
		}
	}

	enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	if (ioctl(v4l_fd, VIDIOC_STREAMON, &type) == -1) {
		ERROR_LOG(HLE, "VIDIOC_STREAMON");
		return -1;
	}

	pthread_create(&v4l_thread, NULL, v4l_loop, NULL);
#endif //USE_FFMPEG
	return 0;
}

int __v4l_stopCapture() {
	enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

	if (v4l_fd < 0) {
		goto exit;
	}

	if (ioctl(v4l_fd, VIDIOC_STREAMOFF, &type) == -1) {
		ERROR_LOG(HLE, "VIDIOC_STREAMOFF");
		goto exit;
	}

	for (int buf_id = 0; buf_id < v4l_buffer_count; buf_id++) {
		if (munmap(v4l_buffers[buf_id].start, v4l_buffers[buf_id].length) == -1) {
			ERROR_LOG(HLE, "munmap");
			goto exit;
		}
	}

	if (close(v4l_fd) == -1) {
		ERROR_LOG(HLE, "close");
		goto exit;
	}

	v4l_fd = -1;
	//pthread_join(v4l_thread, NULL);

exit:
	v4l_fd = -1;
	return 0;
}

#endif // PPSSPP_PLATFORM(LINUX) && !PPSSPP_PLATFORM(ANDROID)
