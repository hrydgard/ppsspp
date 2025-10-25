#include "Common/Net/Resolve.h"
#include "Common/System/Request.h"
#include "UI/UploadScreen.h"
#include "Common/StringUtils.h"
#include "Core/WebServer.h"

UploadScreen::UploadScreen(const Path &targetFolder) : targetFolder_(targetFolder) {
	net::GetLocalIP4List(localIPs_);
	WebServerSetUploadPath(targetFolder);
	StartWebServer(WebServerFlags::FILE_UPLOAD);
}

UploadScreen::~UploadScreen() {
	StopWebServer(WebServerFlags::FILE_UPLOAD);
}

void UploadScreen::CreateViews() {
	using namespace UI;
	auto co = GetI18NCategory(I18NCat::NETWORKING);
	root_ = new LinearLayout(ORIENT_VERTICAL);
	LinearLayout *topBar = new LinearLayout(ORIENT_HORIZONTAL, new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT));
	topBar->Add(new Choice(ImageID("I_NAVIGATE_BACK"), new LinearLayoutParams()))->OnClick.Handle<UIScreen>(this, &UIScreen::OnBack);

	root_->Add(topBar);

	if (prevRunning_) {
		root_->Add(new TextView(co->T("On your other device, connect to the same network, then go to this URL:")));
		for (const auto &ip : localIPs_) {
			std::string url = StringFromFormat("http://%s:%d/upload", ip.c_str(), WebServerPort());
			root_->Add(new TextView(url));
			root_->Add(new Choice(ImageID("I_FILE_COPY")))->OnClick.Add([url](UI::EventParams &) {
				System_CopyStringToClipboard(url);
			});
		}
	} else {
		root_->Add(new TextView(co->T("Connecting...")));
	}

	root_->Add(new TextView(std::string(co->T("Uploading to: ")) + targetFolder_.ToVisualString()));

	//infoText->SetTextAlignment(TEXT_ALIGN_CENTER);
	//verticalLayout->AddView(infoText);
	//AddStandardBack(verticalLayout);
	//root_->AddView(verticalLayout);
}

void UploadScreen::update() {
	UIScreen::update();
	bool running = WebServerRunning(WebServerFlags::FILE_UPLOAD);
	if (prevRunning_ != running) {
		prevRunning_ = running;
		RecreateViews();
	}
}
