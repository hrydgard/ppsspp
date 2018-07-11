#pragma once

#include <functional>

namespace threading {

// Stuff that can execute other stuff, like threadpools, should inherit from this.
class Executor {
public:
	virtual void Run(std::function<void()> func) = 0;
};

class SameThreadExecutor : public Executor {
public:
	void Run(std::function<void()> func) override;
};

class NewThreadExecutor : public Executor {
public:
	void Run(std::function<void()> func) override;
};

}  // namespace threading
