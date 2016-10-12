#include "thread/executor.h"

#include <functional>

namespace threading {

void SameThreadExecutor::Run(std::function<void()> func) {
  func();
}

}  // namespace threading
