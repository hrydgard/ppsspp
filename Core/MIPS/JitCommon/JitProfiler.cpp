// Copyright (c) 2012- PPSSPP Project.

// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, version 2.0 or later versions.

// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License 2.0 for more details.

// A copy of the GPL 2.0 should have been included with the program.
// If not, see http://www.gnu.org/licenses/

// Official git repository and contact information can be found at
// https://github.com/hrydgard/ppsspp and http://www.ppsspp.org/.

#if defined(__linux__) && !defined(_GNU_SOURCE)
// For REG_RIP and friends.
#define _GNU_SOURCE
#endif

#include "ppsspp_config.h"

#include <atomic>
#include <chrono>
#include <thread>

#if PPSSPP_PLATFORM(WINDOWS)
#include "Common/CommonWindows.h"
#else
#include <csignal>
#include <cstdlib>
#include <cxxabi.h>
#include <dlfcn.h>
#include <pthread.h>
#include <sys/ucontext.h>
#endif

#include "Core/MIPS/JitCommon/JitProfiler.h"

namespace JitProfiler {

// Written by one producer (the signal handler on the sampled thread, or the sampler thread on Windows),
// read by one consumer.
static constexpr uint32_t BUFFER_SIZE = 1 << 17;
static Sample g_buffer[BUFFER_SIZE];
static std::atomic<uint32_t> g_write{ 0 };
static std::atomic<uint32_t> g_read{ 0 };
static std::atomic<uint64_t> g_dropped{ 0 };

static std::atomic<bool> g_running{ false };
static std::thread g_thread;
static int g_periodMicros = 0;

static inline void Record(uintptr_t pc, uintptr_t caller) {
	uint32_t w = g_write.load(std::memory_order_relaxed);
	if (w - g_read.load(std::memory_order_acquire) >= BUFFER_SIZE) {
		g_dropped.fetch_add(1, std::memory_order_relaxed);
		return;
	}
	g_buffer[w & (BUFFER_SIZE - 1)] = Sample{ pc, caller };
	g_write.store(w + 1, std::memory_order_release);
}

#if PPSSPP_PLATFORM(WINDOWS) && !PPSSPP_PLATFORM(UWP) && (PPSSPP_ARCH(AMD64) || PPSSPP_ARCH(ARM64))

static HANDLE g_target = nullptr;

bool Supported() {
	return true;
}

bool Start(int periodMicros) {
	if (g_running)
		return false;
	g_target = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION, FALSE, GetCurrentThreadId());
	if (!g_target)
		return false;
	g_periodMicros = periodMicros;
	g_running = true;
	g_thread = std::thread([] {
		auto next = std::chrono::steady_clock::now();
		while (g_running) {
			// Sleep() only does milliseconds, so spin.
			next += std::chrono::microseconds(g_periodMicros);
			while (std::chrono::steady_clock::now() < next)
				std::this_thread::yield();
			if (SuspendThread(g_target) == (DWORD)-1)
				continue;
			CONTEXT ctx{};
			ctx.ContextFlags = CONTEXT_CONTROL;
			if (GetThreadContext(g_target, &ctx)) {
#if PPSSPP_ARCH(AMD64)
				Record((uintptr_t)ctx.Rip, *(const uintptr_t *)ctx.Rsp);
#else
				Record((uintptr_t)ctx.Pc, (uintptr_t)ctx.Lr);
#endif
			}
			ResumeThread(g_target);
		}
	});
	return true;
}

void Stop() {
	if (!g_running)
		return;
	g_running = false;
	g_thread.join();
	CloseHandle(g_target);
	g_target = nullptr;
}

std::string DescribeHostAddress(uintptr_t pc, uintptr_t *symbolStart) {
	*symbolStart = pc;
	return "";
}

#elif !PPSSPP_PLATFORM(WINDOWS) && (((PPSSPP_ARCH(AMD64) || PPSSPP_ARCH(ARM64) || PPSSPP_ARCH(ARM)) && (defined(__APPLE__) || defined(__linux__))) || ((PPSSPP_ARCH(RISCV64) || PPSSPP_ARCH(LOONGARCH64)) && defined(__linux__)))

static pthread_t g_target;
static struct sigaction g_oldAction;

static void SignalHandler(int, siginfo_t *, void *context) {
	const ucontext_t *uc = (const ucontext_t *)context;
	uintptr_t pc = 0, caller = 0;
#if defined(__APPLE__) && PPSSPP_ARCH(ARM64)
	pc = (uintptr_t)__darwin_arm_thread_state64_get_pc(uc->uc_mcontext->__ss);
	caller = (uintptr_t)__darwin_arm_thread_state64_get_lr(uc->uc_mcontext->__ss);
#elif defined(__APPLE__) && PPSSPP_ARCH(AMD64)
	pc = (uintptr_t)uc->uc_mcontext->__ss.__rip;
	caller = *(const uintptr_t *)uc->uc_mcontext->__ss.__rsp;
#elif PPSSPP_ARCH(ARM64)
	pc = (uintptr_t)uc->uc_mcontext.pc;
	caller = (uintptr_t)uc->uc_mcontext.regs[30];
#elif PPSSPP_ARCH(AMD64)
	pc = (uintptr_t)uc->uc_mcontext.gregs[REG_RIP];
	caller = *(const uintptr_t *)uc->uc_mcontext.gregs[REG_RSP];
#elif PPSSPP_ARCH(ARM)
	pc = (uintptr_t)uc->uc_mcontext.arm_pc;
	caller = (uintptr_t)uc->uc_mcontext.arm_lr;
#elif PPSSPP_ARCH(RISCV64)
	pc = (uintptr_t)uc->uc_mcontext.__gregs[REG_PC];
	caller = (uintptr_t)uc->uc_mcontext.__gregs[REG_RA];
#elif PPSSPP_ARCH(LOONGARCH64)
	pc = (uintptr_t)uc->uc_mcontext.__pc;
	caller = (uintptr_t)uc->uc_mcontext.__gregs[1];  // ra
#endif
	Record(pc, caller);
}

bool Supported() {
	return true;
}

bool Start(int periodMicros) {
	if (g_running)
		return false;
	struct sigaction action{};
	action.sa_sigaction = &SignalHandler;
	action.sa_flags = SA_SIGINFO | SA_RESTART;
	sigemptyset(&action.sa_mask);
	if (sigaction(SIGPROF, &action, &g_oldAction) != 0)
		return false;
	g_target = pthread_self();
	g_periodMicros = periodMicros;
	g_running = true;
	g_thread = std::thread([] {
		while (g_running) {
			std::this_thread::sleep_for(std::chrono::microseconds(g_periodMicros));
			pthread_kill(g_target, SIGPROF);
		}
	});
	return true;
}

void Stop() {
	if (!g_running)
		return;
	g_running = false;
	g_thread.join();
	sigaction(SIGPROF, &g_oldAction, nullptr);
}

std::string DescribeHostAddress(uintptr_t pc, uintptr_t *symbolStart) {
	Dl_info info{};
	*symbolStart = pc;
	if (!dladdr((const void *)pc, &info) || !info.dli_sname)
		return "";
	*symbolStart = (uintptr_t)info.dli_saddr;
	int status = 0;
	char *demangled = abi::__cxa_demangle(info.dli_sname, nullptr, nullptr, &status);
	std::string name = status == 0 && demangled ? demangled : info.dli_sname;
	free(demangled);
	return name;
}

#else

bool Supported() {
	return false;
}

bool Start(int periodMicros) {
	return false;
}

void Stop() {
}

std::string DescribeHostAddress(uintptr_t pc, uintptr_t *symbolStart) {
	*symbolStart = pc;
	return "";
}

#endif

bool IsRunning() {
	return g_running;
}

int PeriodMicros() {
	return g_periodMicros;
}

void Drain(std::vector<Sample> &out) {
	uint32_t r = g_read.load(std::memory_order_relaxed);
	const uint32_t w = g_write.load(std::memory_order_acquire);
	for (; r != w; ++r)
		out.push_back(g_buffer[r & (BUFFER_SIZE - 1)]);
	g_read.store(r, std::memory_order_release);
}

uint64_t Dropped() {
	return g_dropped;
}

}  // namespace JitProfiler
