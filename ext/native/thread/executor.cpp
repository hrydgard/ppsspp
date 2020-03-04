#include "thread/executor.h"

#include <functional>
#include <thread>

namespace threading {

void SameThreadExecutor::Run(std::function<void()> func) {
	func();
}

void NewThreadExecutor::Run(std::function<void()> func) {
	thread_ = std::thread(func);
}

NewThreadExecutor::~NewThreadExecutor() {
	// If Run was ever called...
	if (thread_.joinable())
		thread_.join();
}

}  // namespace threading
