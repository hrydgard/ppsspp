#include <functional>
#include <string_view>
#include <cstring>
#include <algorithm>
// for std::min

#include "Common/System/OSD.h"
#include "Common/TimeUtil.h"
#include "Common/Log.h"
#include "Common/Math/math_util.h"

OnScreenDisplay g_OSD;

// Effectively forever.

namespace {
struct OSDCommand;
struct OSDLink {
	mpsc_stack_node_t node;  // first, so a node pointer is a link pointer
	OSDCommand *self;
};
struct OSDCommand {
	OSDLink link;
	std::function<void(OnScreenDisplay &)> fn;
};
}

OnScreenDisplay::OnScreenDisplay() {
	mpsc_stack_init(&pending_);
}

OnScreenDisplay::~OnScreenDisplay() {
	mpsc_stack_node_t *node = mpsc_stack_drain(&pending_);
	while (node) {
		mpsc_stack_node_t *next = node->next;
		delete ((OSDLink *)node)->self;
		node = next;
	}
}

void OnScreenDisplay::Post(std::function<void(OnScreenDisplay &)> fn) {
	OSDCommand *cmd = new OSDCommand{ {}, std::move(fn) };
	cmd->link.self = cmd;
	mpsc_stack_push(&pending_, &cmd->link.node);
}

void OnScreenDisplay::Update() {
	// Apply commands oldest first.
	mpsc_stack_node_t *node = mpsc_stack_reverse(mpsc_stack_drain(&pending_));
	while (node) {
		mpsc_stack_node_t *next = node->next;
		OSDCommand *cmd = ((OSDLink *)node)->self;
		cmd->fn(*this);
		delete cmd;
		node = next;
	}

	double now = time_now_d();
	for (auto iter = entries_.begin(); iter != entries_.end(); ) {
		if (now >= iter->endTime) {
			iter = entries_.erase(iter);
		} else {
			iter++;
		}
	}
}

std::vector<OnScreenDisplay::Entry> OnScreenDisplay::Entries() {
	return entries_;  // makes a copy.
}

void OnScreenDisplay::Show(OSDType type, std::string_view text, std::string_view text2, std::string_view icon, float duration_s, const char *id) {
	if (text.empty() && type != OSDType::STATUS_ICON) {
		// The user hacked the translation files to get rid of the message. Let's reward the dedication
		// by skipping it entirely.
		return;
	}

	INFO_LOG(Log::UI, "OSD: %.*s (%.*s)", STR_VIEW(text), STR_VIEW(text2));

	const bool hasId = id != nullptr;
	Post([type, t = std::string(text), t2 = std::string(text2), ic = std::string(icon), duration_s, hasId, i = std::string(hasId ? id : "")](OnScreenDisplay &osd) {
		osd.ShowNow(type, t, t2, ic, duration_s, hasId ? i.c_str() : nullptr);
	});
}

void OnScreenDisplay::CancelById(std::string_view id) {
	Post([i = std::string(id)](OnScreenDisplay &osd) { osd.CancelByIdNow(i); });
}

void OnScreenDisplay::SetProgressBar(std::string_view id, std::string_view message, float minValue, float maxValue, float progress, float delay_s) {
	Post([=, i = std::string(id), m = std::string(message)](OnScreenDisplay &osd) { osd.SetProgressBarNow(i, m, minValue, maxValue, progress, delay_s); });
}

void OnScreenDisplay::RemoveProgressBar(std::string_view id, bool success, float delay_s) {
	Post([=, i = std::string(id)](OnScreenDisplay &osd) { osd.RemoveProgressBarNow(i, success, delay_s); });
}

void OnScreenDisplay::SetClickCallback(std::string_view id, std::function<void()> callback) {
	Post([i = std::string(id), cb = std::move(callback)](OnScreenDisplay &osd) { osd.SetClickCallbackNow(i, cb); });
}

void OnScreenDisplay::SetFlags(std::string_view id, OSDMessageFlags flags) {
	Post([=, i = std::string(id)](OnScreenDisplay &osd) { osd.SetFlagsNow(i, flags); });
}

void OnScreenDisplay::NudgeIngameNotifications() {
	sideBarShowTime_ = time_now_d();
}

float OnScreenDisplay::IngameAlpha() const {
	double timeSinceNudge = time_now_d() - sideBarShowTime_;

	// Fade out in 1/4 second, 0.1s after the last nudge.
	return saturatef(1.0f - ((float)timeSinceNudge - 0.1f) * 4.0f);
}

void OnScreenDisplay::ClickEntry(size_t index, double now) {
	if (index < entries_.size()) {
		if (entries_[index].clickCallback) {
			entries_[index].clickCallback();
		} else {
			entries_[index].endTime = std::min(now + FadeoutTime(), entries_[index].endTime);
		}
	}
}

void OnScreenDisplay::ShowNow(OSDType type, std::string_view text, std::string_view text2, std::string_view icon, float duration_s, const char *id) {
	// Automatic duration based on type.
	if (duration_s <= 0.0f) {
		switch (type) {
		case OSDType::MESSAGE_ERROR:
		case OSDType::MESSAGE_WARNING:
			duration_s = 4.0f;
			break;
		case OSDType::MESSAGE_FILE_LINK:
			duration_s = 5.0f;
			break;
		case OSDType::MESSAGE_INFO:
			duration_s = 3.0f;
			break;
		case OSDType::MESSAGE_SUCCESS:
			duration_s = 2.0f;
			break;
		default:
			duration_s = 1.5f;
			break;
		}
	}

	double now = time_now_d();
	if (id) {
		for (auto iter = entries_.begin(); iter != entries_.end(); ++iter) {
			if (iter->id == id) {
				Entry msg = *iter;
				msg.endTime = now + duration_s;
				msg.text = text;
				msg.text2 = text2;
				msg.type = type;
				msg.iconName = icon;
				// Move to top (should we? maybe not?)
				entries_.erase(iter);
				entries_.insert(entries_.begin(), msg);
				return;
			}
		}
	}

	Entry msg{};
	msg.text = text;
	msg.text2 = text2;
	msg.iconName = icon;
	msg.startTime = now;
	msg.endTime = now + duration_s;
	msg.type = type;
	if (id) {
		msg.id = id;
	}
	entries_.insert(entries_.begin(), msg);
}

void OnScreenDisplay::CancelByIdNow(std::string_view id) {
	for (auto iter = entries_.begin(); iter != entries_.end();) {
		if (iter->id == id) {
			iter = entries_.erase(iter);
		} else {
			iter++;
		}
	}
}

void OnScreenDisplay::ShowOnOff(std::string_view message, bool on, float duration_s) {
	std::string msg(message);
	msg += ": ";
	msg += on ? "on" : "off";
	// TODO: translate "on" and "off"? Or just get rid of this whole thing?
	Show(OSDType::MESSAGE_INFO, msg, duration_s);
}

void OnScreenDisplay::SetProgressBarNow(std::string_view id, std::string_view message, float minValue, float maxValue, float progress, float delay) {
	_dbg_assert_(!my_isnanorinf(progress));
	_dbg_assert_(!my_isnanorinf(minValue));
	_dbg_assert_(!my_isnanorinf(maxValue));

	double now = time_now_d();
	bool found = false;

	for (auto &bar : entries_) {
		if (bar.type == OSDType::PROGRESS_BAR && bar.id == id) {
			bar.minValue = minValue;
			bar.maxValue = maxValue;
			bar.progress = progress;
			bar.text = message;
			bar.endTime = now + 60.0;  // Nudge the progress bar to keep it shown.
			return;
		}
	}

	Entry bar{};
	bar.id = id;
	bar.type = OSDType::PROGRESS_BAR;
	bar.text = message;
	bar.minValue = minValue;
	bar.maxValue = maxValue;
	bar.progress = progress;
	bar.startTime = now + delay;
	bar.endTime = now + 60.0;  // Show the progress bar for 60 seconds, then fade it out.
	entries_.push_back(bar);
}

void OnScreenDisplay::RemoveProgressBarNow(std::string_view id, bool success, float delay_s) {
	for (auto &ent : entries_) {
		if (ent.type == OSDType::PROGRESS_BAR && ent.id == id) {
			if (success) {
				// Quickly shoot up to max, if we weren't there.
				if (ent.maxValue != 0.0f) {
					ent.progress = ent.maxValue;
				} else {
					// Fake a full progress
					ent.minValue = 0;
					ent.maxValue = 1;
					ent.progress = 1;
				}
			}
			ent.endTime = time_now_d() + delay_s + FadeoutTime();
			break;
		}
	}
}

// Fades out everything related to achievements. Should be used on game shutdown.
void OnScreenDisplay::SetClickCallbackNow(std::string_view id, std::function<void()> callback) {
	_dbg_assert_(callback != nullptr);
	for (auto &ent : entries_) {
		// protect against dupes.
		if (ent.id == id) {
			ent.clickCallback = callback;
		}
	}
}

void OnScreenDisplay::SetFlagsNow(std::string_view id, OSDMessageFlags flags) {
	for (auto &ent : entries_) {
		// protect against dupes.
		if (ent.id == id) {
			ent.flags = flags;
		}
	}
}
