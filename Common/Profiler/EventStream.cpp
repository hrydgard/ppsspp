#include <map>
#include <memory>

#include "EventStream.h"

// Impl to avoid including <map> (or similar) in the header.
struct EventStreamManagerImpl {
	EventStream *Get(const char *identifier);

	std::vector<std::unique_ptr<EventStream>> streams;
};

EventStreamManager::EventStreamManager() : impl_(new EventStreamManagerImpl) {}
EventStreamManager::~EventStreamManager() { delete impl_; }

EventStreamManager g_eventStreamManager;

void EventStreamManager::GarbageCollect(Instant before) {
	for (auto &iter : impl_->streams) {
		iter->GarbageCollect(before);
	}
}

EventStream *EventStreamManager::Get(const char *identifier) {
	for (auto &iter : impl_->streams) {
		if (!strcmp(identifier, iter->Identifier()))
			return iter.get();
	}

	EventStream *stream = new EventStream(identifier);
	impl_->streams.push_back(std::unique_ptr<EventStream>(stream));
	return stream;
}

EventStream *EventStreamManager::GetByIndex(size_t index) {
	if (index < 0 || index >= impl_->streams.size()) {
		return nullptr;
	} else {
		return impl_->streams[index].get();
	}
}

size_t EventStreamManager::NumStreams() {
	return impl_->streams.size();
}

// Matches the GetAll order.
size_t NumStreams();

std::vector<EventStream *> EventStreamManager::GetAll() {
	std::vector<EventStream *> vec;
	for (auto &iter : impl_->streams) {
		vec.push_back(iter.get());
	}
	return vec;
}

std::vector<EventStream::Event> EventStream::GetEventRange(Instant startTime, Instant endTime, int64_t *firstEventId) {
	// Search backwards to the start. Since we keep history and are usually looking somewhere
	// near the end, this will usually be fast enough, no need for acceleration structures.
	std::lock_guard<std::mutex> guard(lock_);
	int i;
	for (i = (int)events_.size() - 1; i >= 0; i--) {
		if (events_[i].endTime < startTime) {
			break;
		}
	}

	if (i < 0) {
		return std::vector<EventStream::Event>();
	}

	*firstEventId = i;

	std::vector<Event> outEvents;
	for (int j = i; j < (int)events_.size(); j++) {
		if (events_[j].startTime > endTime) {
			// Reached the end.
			break;
		}
		outEvents.push_back(events_[j]);
	}

	return outEvents;
}

int64_t EventStream::GetEventIdAt(Instant time) {
	std::lock_guard<std::mutex> guard(lock_);
	int i;
	for (i = (int)events_.size() - 1; i >= 0; i--) {
		if (time >= events_[i].startTime && time < events_[i].endTime) {
			return i;
		}
	}
	return -1;
}

void EventStream::GarbageCollect(Instant before) {
	std::lock_guard<std::mutex> guard(lock_);

	size_t i;
	for (i = 0; i < events_.size(); i++) {
		if (events_[i].startTime > before) {
			break;
		}
	}

	if (i == 0) {
		return;
	}

	events_.erase(events_.begin(), events_.begin() + i);
	offset_ += i;
}
