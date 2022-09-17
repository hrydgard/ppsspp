#pragma once

// Event streams.
//
// Used to visualize things over time. Will be a building block for the new built-in profiler, too.
// Does not yet support hiearchies of events properly, but will.

#include <vector>
#include <mutex>

#include "Common/TimeUtil.h"

struct EventStreamManagerImpl;
class EventStream;

class EventStreamManager {
public:
	EventStreamManager();
	~EventStreamManager();

	EventStream *Get(const char *identifier);
	EventStream *GetByIndex(size_t index);  // Matches the GetAll order.
	size_t NumStreams();

	void GarbageCollect(Instant before);

	void SetRunning(bool running) { running_ = running; }
	bool IsRunning() const { return running_; }

	// For listing, visualization.
	std::vector<EventStream *> GetAll();

private:
	EventStreamManagerImpl *impl_;
#ifdef _DEBUG
	bool running_ = true;
#else
	bool running_ = false;
#endif

	friend class EventStream;
};

extern EventStreamManager g_eventStreamManager;
class EventStream {
public:
	enum class EventType : uint8_t {
		BLOCK,
		COMPUTE,
	};

	struct Event {
		const char *name;
		int frameId;
		EventType type;
		Instant startTime;
		Instant endTime;
	};

	EventStream(const char *id) : identifier_(id) {}

	void SetFrameId(int frameId) {
		frameId_ = frameId;
	}

	int64_t Begin(const char *name, EventType type) {
		if (!g_eventStreamManager.running_)
			return -1;

		std::lock_guard<std::mutex> guard(lock_);
		Instant now = Instant::Now();
		events_.push_back(Event{ name, frameId_, type, now, now });
		return (int64_t)(offset_ + events_.size() - 1);
	}

	void End(int64_t eventId) {
		if (!g_eventStreamManager.running_ || eventId < 0)
			return;

		std::lock_guard<std::mutex> guard(lock_);
		if ((size_t)eventId >= offset_) {
			events_[(size_t)eventId - offset_].endTime = Instant::Now();
		}
	}

	// Erases stored data older than the specified instant. Call from time to time.
	// TODO: Go by frame ID instead?
	void GarbageCollect(Instant before);

	// This always returns a linear sequence, so you can deduce the id of each event by adding the index to firstEventId.
	std::vector<Event> GetEventRange(Instant start, Instant end, int64_t *firstEventId);

	int64_t GetEventIdAt(Instant time);

	const char *Identifier() const { return identifier_; }

private:
	const char *identifier_;
	size_t offset_ = 0;
	std::vector<Event> events_;
	int frameId_ = 0;

	// TODO: Figure out how we can do this all without a mutex.
	std::mutex lock_;
};

class EventScope {
public:
	EventScope(EventStream *stream, const char *name, EventStream::EventType type) : stream_(stream) {
		id_ = stream->Begin(name, type);
	}
	~EventScope() {
		stream_->End(id_);
	}

private:
	EventStream *stream_;
	size_t id_;
};
