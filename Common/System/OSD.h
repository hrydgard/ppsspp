#pragma once

#include <string>
#include <string_view>
#include <vector>
#include <functional>

#include "Common/Common.h"

#include <queues/mpsc_stack.h>

// Shows a visible message to the user.
// The default implementation in NativeApp.cpp uses our "osm" system (on screen messaging).
enum class OSDType {
	MESSAGE_INFO,
	MESSAGE_SUCCESS,
	MESSAGE_WARNING,
	MESSAGE_ERROR,
	MESSAGE_ERROR_DUMP,  // displays lots of text (after the first line), small size
	MESSAGE_FILE_LINK,
	MESSAGE_CENTERED_WARNING,
	MESSAGE_CENTERED_ERROR,

	PROGRESS_BAR,

	STATUS_ICON,  // Use icons from the atlas

	VALUE_COUNT,
};

#undef None  // X11, sigh.

enum class OSDMessageFlags {
	None = 0,
	SpinLeft = 1,
	SpinRight = 2,
	Transparent = 4,
};
ENUM_CLASS_BITOPS(OSDMessageFlags);

// Data holder for on-screen messages.
// Show/Set/Cancel may be called from any thread: they queue a command. Update(),
// Entries(), IsEmpty(), ClickEntry() and the ingame nudge belong to the one thread
// that drives the display, which applies the queued commands in Update().
class OnScreenDisplay {
public:
	OnScreenDisplay();
	~OnScreenDisplay();

	// If you specify 0.0f as duration, a duration will be chosen automatically depending on type.
	void Show(OSDType type, std::string_view text, float duration_s = 0.0f, const char *id = nullptr) {
		Show(type, text, "", duration_s, id);
	}
	void Show(OSDType type, std::string_view text, std::string_view text2, float duration_s = 0.0f, const char *id = nullptr) {
		Show(type, text, text2, "", duration_s, id);
	}
	void Show(OSDType type, std::string_view text, std::string_view text2, std::string_view icon, float duration_s = 0.0f, const char *id = nullptr);

	void ShowOnOff(std::string_view message, bool on, float duration_s = 0.0f);
	void CancelById(std::string_view id);

	bool IsEmpty() const { return entries_.empty(); }  // Shortcut to skip rendering.

	// Call this every frame, cleans up old entries.
	void Update();

	// Progress bar controls
	// Set is both create and update. If you set maxValue <= minValue, you'll create an "indeterminate" progress
	// bar that doesn't show a specific amount of progress.
	void SetProgressBar(std::string_view id, std::string_view message, float minValue, float maxValue, float progress, float delay_s);
	void RemoveProgressBar(std::string_view id, bool success, float delay_s);

	// Call every frame to keep the sidebar visible. Otherwise it'll fade out.
	void NudgeIngameNotifications();
	float IngameAlpha() const;

	// Can't add an infinite number of "Show" functions, so starting to offer post-modification.
	void SetFlags(std::string_view id, OSDMessageFlags flag);
	void SetClickCallback(std::string_view id, std::function<void()> callback);

	struct Entry {
		OSDType type;
		std::string text;
		std::string text2;
		std::string iconName;
		std::string id;
		OSDMessageFlags flags;

		std::function<void()> clickCallback;

		int instanceID;
		double startTime;
		double endTime;

		// Progress-bar-only data:
		float minValue;
		float maxValue;
		float progress;
	};

	std::vector<Entry> Entries();

	// TODO: Use something more stable than the index.
	void ClickEntry(size_t index, double now);

	static float FadeinTime() { return 0.1f; }
	static float FadeoutTime() { return 0.25f; }

private:
	void Post(std::function<void(OnScreenDisplay &)> fn);

	void ShowNow(OSDType type, std::string_view text, std::string_view text2, std::string_view icon, float duration_s, const char *id);
	void CancelByIdNow(std::string_view id);
	void SetProgressBarNow(std::string_view id, std::string_view message, float minValue, float maxValue, float progress, float delay_s);
	void RemoveProgressBarNow(std::string_view id, bool success, float delay_s);
	void SetClickCallbackNow(std::string_view id, std::function<void()> callback);
	void SetFlagsNow(std::string_view id, OSDMessageFlags flags);

	std::vector<Entry> entries_;  // display thread only
	mpsc_stack_t pending_;

	double sideBarShowTime_ = 0.0;
};

extern OnScreenDisplay g_OSD;
