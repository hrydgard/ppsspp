#include "thread/executor.h"

#include <functional>
#include <thread>

namespace threading {

void SameThreadExecutor::Run(std::function<void()> func) {
	func();
}

void NewThreadExecutor::Run(std::function<void()> func) {
	std::thread(func).detach();
}

}  // namespace threading
