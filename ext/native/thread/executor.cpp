#include "thread/executor.h"
#include "base/functional.h"

namespace threading {

void SameThreadExecutor::Run(std::function<void()> func) {
  func();
}

}  // namespace threading
