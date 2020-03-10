#pragma once

#include <functional>
#include <thread>
#include <vector>

namespace threading {

// Stuff that can execute other stuff, like threadpools, should inherit from this.
class Executor {
public:
	virtual void Run(std::function<void()> func) = 0;
	virtual ~Executor() {}
};

class SameThreadExecutor : public Executor {
public:
	void Run(std::function<void()> func) override;
};

class NewThreadExecutor : public Executor {
public:
	~NewThreadExecutor() override;
	void Run(std::function<void()> func) override;

private:
	std::vector<std::thread> threads_;
};

}  // namespace threading
