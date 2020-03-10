#include "thread/executor.h"

#include <functional>
#include <thread>

namespace threading {

void SameThreadExecutor::Run(std::function<void()> func) {
	func();
}

void NewThreadExecutor::Run(std::function<void()> func) {
	threads_.push_back(std::thread(func));
}

NewThreadExecutor::~NewThreadExecutor() {
	// If Run was ever called...
	for (auto &thread : threads_)
		thread.join();
	threads_.clear();
}

}  // namespace threading
