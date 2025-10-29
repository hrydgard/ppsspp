#include <cmath>
#include "Common/Net/Resolve.h"
#include "Common/System/Request.h"
#include "Common/File/DiskFree.h"
#include "Common/StringUtils.h"
#include "Common/Data/Text/Parsers.h"
#include "Common/Data/Text/I18n.h"
#include "Core/WebServer.h"
#include "UI/UploadScreen.h"
#include "UI/MiscViews.h"

UploadScreen::UploadScreen(const Path &targetFolder) : targetFolder_(targetFolder) {
	std::vector<std::string> ips;
	net::GetLocalIP4List(ips);
	localIPs_.clear();
	for (const auto &ip : ips) {
		if (!ip.empty() && !startsWith(ip, "127.") && !startsWith(ip, "169.254.")) {
			localIPs_.push_back(ip);
		}
	}
	WebServerSetUploadPath(targetFolder);
	StartWebServer(WebServerFlags::FILE_UPLOAD);
}

UploadScreen::~UploadScreen() {
	StopWebServer(WebServerFlags::FILE_UPLOAD);
}

void UploadScreen::CreateDialogViews(UI::ViewGroup *root) {
	using namespace UI;
	auto n = GetI18NCategory(I18NCat::NETWORKING);

	LinearLayout *container = new LinearLayout(ORIENT_VERTICAL, new LinearLayoutParams(500, FILL_PARENT, 0.0f, UI::Gravity::G_HCENTER, Margins(10)));
	root->Add(container);

	container->Add(new TextWithImage(ImageID("I_FOLDER_UPLOAD"), targetFolder_.ToVisualString()));
	container->Add(new Spacer(20.0f));

	if (prevRunning_) {
		container->Add(new TextWithImage(ImageID("I_WIFI"), n->T("With a web browser on the same network, go to:")));
		for (const auto &ip : localIPs_) {
			std::string url = StringFromFormat("http://%s:%d/upload", ip.c_str(), WebServerPort());
			container->Add(new CopyableText(ImageID("I_WEB_BROWSER"), url));
		}
	}

	statusContainer_ = new LinearLayout(ORIENT_VERTICAL);
	container->Add(statusContainer_);
}

void UploadScreen::RecreateStatus() {
	if (!statusContainer_) {
		return;
	}

	using namespace UI;
	statusContainer_->Clear();
	// Show information about current upload streams (there can be multiple, but normally it's just one
	// where files are uploaded sequentially).
	std::vector<UploadProgress> uploads = GetUploadsInProgress();
	for (const auto &upload : uploads) {
		int percent = upload.totalBytes == 0 ? 0 : (int)(ceil(100.0 * (double)upload.uploadedBytes / (double)upload.totalBytes));
		std::string uploadText = StringFromFormat("%s: %s/%s (%d%%)", upload.filename.c_str(),
			NiceSizeFormat(upload.uploadedBytes).c_str(), NiceSizeFormat(upload.totalBytes).c_str(), percent);
		statusContainer_->Add(new TextWithImage(ImageID("I_FILE"), uploadText));
	}
}

void UploadScreen::update() {
	UIScreen::update();
	bool running = WebServerRunning(WebServerFlags::FILE_UPLOAD);
	if (prevRunning_ != running) {
		prevRunning_ = running;
		RecreateViews();
	}

	// Update in-progress uploads approximately every quarter of a second.
	if (lastUpdate_.ElapsedSeconds() > 0.25) {
		RecreateStatus();
		lastUpdate_ = Instant::Now();
	}
}
