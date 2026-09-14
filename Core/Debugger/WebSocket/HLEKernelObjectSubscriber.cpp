// Copyright (c) 2026- PPSSPP Project.

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

// Introspection for kernel synchronization objects (event flags, mutexes, semaphores, message
// pipes, callbacks) plus a generic overview across every kernel object type at once. These read
// straight off the live KernelObject-derived classes (exposed in their respective sceKernel*.h
// headers for exactly this purpose) rather than going through a kernel-side accessor function -
// deliberately: nothing here ever mutates kernel state, only reads already-public fields, so
// there's no reason to route through the owning module. That also means "waiting threads" lists
// below are shown exactly as the kernel currently has them - a thread that's actually done
// waiting but hasn't been pruned by the next real syscall on that object yet may still appear.

#include "Core/Core.h"
#include "Core/HLE/sceKernel.h"
#include "Core/HLE/sceKernelEventFlag.h"
#include "Core/HLE/sceKernelMutex.h"
#include "Core/HLE/sceKernelSemaphore.h"
#include "Core/HLE/sceKernelMsgPipe.h"
#include "Core/HLE/sceKernelThread.h"
#include "Core/Debugger/WebSocket/HLEKernelObjectSubscriber.h"
#include "Core/Debugger/WebSocket/WebSocketUtils.h"

DebuggerSubscriber *WebSocketHLEKernelObjectInit(DebuggerEventHandlerMap &map) {
	map["hle.object.list"] = &WebSocketHLEObjectList;

	map["hle.eventflag.list"] = &WebSocketHLEEventFlagList;
	map["hle.eventflag.info"] = &WebSocketHLEEventFlagInfo;
	map["hle.mutex.list"] = &WebSocketHLEMutexList;
	map["hle.mutex.info"] = &WebSocketHLEMutexInfo;
	map["hle.semaphore.list"] = &WebSocketHLESemaphoreList;
	map["hle.semaphore.info"] = &WebSocketHLESemaphoreInfo;
	map["hle.msgpipe.list"] = &WebSocketHLEMsgPipeList;
	map["hle.msgpipe.info"] = &WebSocketHLEMsgPipeInfo;
	map["hle.callback.list"] = &WebSocketHLECallbackList;
	map["hle.callback.info"] = &WebSocketHLECallbackInfo;
	return nullptr;
}

// Resolves uid to a T*, or fails the request and returns null (mismatched type counts as not
// found, same as the HLE syscalls themselves treat it - see KernelObjectPool::Get<T>().)
template <typename T>
static T *RequireKernelObject(DebuggerRequest &req, uint32_t uid) {
	u32 error;
	T *obj = kernelObjects.Get<T>((SceUID)uid, error);
	if (!obj) {
		req.Fail("No matching object found for that uid");
		return nullptr;
	}
	return obj;
}

// Writes a plain {threadId} entry for each id in a wait list that's just SceUIDs (Mutex,
// Semaphore) - already inside an array context (ArrayScope) opened by the caller.
static void WriteThreadIdWaitList(JsonWriter &json, const std::vector<SceUID> &threadIDs) {
	for (SceUID threadID : threadIDs) {
		JsonWriter::DictScope d(json);
		json.writeInt("threadId", threadID);
	}
}

// List every live kernel object of every type (hle.object.list)
//
// A coarse overview across all kernel object kinds at once (threads, modules, semaphores, event
// flags, mutexes, msgpipes, callbacks, ...) - for the sync primitive types also covered by
// hle.eventflag.*/hle.mutex.*/etc., use those for full detail; this just gives uid/type/name/a
// one-line summary for everything, useful for figuring out what's alive before drilling in.
//
// Parameters:
//  - type: optional string, only include objects whose type name equals this (as returned in
//    each entry's own 'type' field - e.g. "EventFlag", "Mutex", "Semaphore", "MsgPipe",
//    "CallBack", "Thread", "Module", ...; case sensitive, matches KernelObject::GetTypeName().)
//
// Response (same event name):
//  - objects: array of objects, each with properties:
//     - uid: unsigned integer kernel object handle.
//     - type: string type name.
//     - name: string name the object was created with.
//     - quickInfo: string, a short type-specific one-line summary (whatever that type finds
//       most useful - not machine-parseable, for human/log-style display only.)
void WebSocketHLEObjectList(DebuggerRequest &req) {
	std::string typeFilter;
	if (!req.ParamString("type", &typeFilter, DebuggerParamType::OPTIONAL))
		return;

	// Route the actual kernel object reads to the CPU thread instead of poking at them directly
	// from this WebSocket handler thread - see Core_RunOnCPUThread() in Core.h.
	Core_RunOnCPUThread([&] {
		JsonWriter &json = req.Respond();
		JsonWriter::ArrayScope objects(json, "objects");
		kernelObjects.IterateAll([&](int uid, KernelObject *obj) -> bool {
			if (!typeFilter.empty() && typeFilter != obj->GetTypeName())
				return true;

			char quickInfo[256];
			obj->GetQuickInfo(quickInfo, sizeof(quickInfo));

			JsonWriter::DictScope d(json);
			json.writeUint("uid", uid);
			json.writeString("type", obj->GetTypeName());
			json.writeString("name", obj->GetName());
			json.writeString("quickInfo", quickInfo);
			return true;
		});
	});
}

// List all event flags (hle.eventflag.list)
//
// No parameters.
//
// Response (same event name):
//  - eventFlags: array of objects, each with properties:
//     - uid: unsigned integer kernel object handle - pass to hle.eventflag.info for detail.
//     - name: string name.
//     - currentPattern: unsigned integer, the flag's current bit pattern.
//     - numWaitThreads: integer, how many threads are currently waiting on it.
void WebSocketHLEEventFlagList(DebuggerRequest &req) {
	Core_RunOnCPUThread([&] {
		JsonWriter &json = req.Respond();
		JsonWriter::ArrayScope arr(json, "eventFlags");
		kernelObjects.Iterate<EventFlag>([&](int uid, EventFlag *e) -> bool {
			JsonWriter::DictScope d(json);
			json.writeUint("uid", uid);
			json.writeString("name", e->nef.name);
			json.writeUint("currentPattern", e->nef.currentPattern);
			json.writeInt("numWaitThreads", (int)e->waitingThreads.size());
			return true;
		});
	});
}

// Get full detail for one event flag (hle.eventflag.info)
//
// Parameters:
//  - uid: unsigned integer kernel object handle (as returned by hle.eventflag.list.)
//
// Response (same event name):
//  - name: string name.
//  - attr: unsigned integer creation attributes.
//  - initPattern: unsigned integer, the bit pattern the flag was created with.
//  - currentPattern: unsigned integer, the flag's current bit pattern.
//  - waitingThreads: array of objects, each with properties:
//     - threadId: integer thread uid.
//     - bits: unsigned integer, the bit pattern this thread is waiting to match.
//     - matchAny: boolean, true if any one bit matches (OR), false if all bits must (AND.)
//     - clear: boolean, true if matched bits get cleared from the pattern on match.
//     - clearAll: boolean, true if the whole pattern gets cleared to 0 on match.
void WebSocketHLEEventFlagInfo(DebuggerRequest &req) {
	uint32_t uid;
	if (!req.ParamU32("uid", &uid))
		return;

	Core_RunOnCPUThread([&] {
		EventFlag *e = RequireKernelObject<EventFlag>(req, uid);
		if (!e)
			return;

		JsonWriter &json = req.Respond();
		json.writeString("name", e->nef.name);
		json.writeUint("attr", e->nef.attr);
		json.writeUint("initPattern", e->nef.initPattern);
		json.writeUint("currentPattern", e->nef.currentPattern);
		{
			JsonWriter::ArrayScope waiters(json, "waitingThreads");
			for (const EventFlagTh &t : e->waitingThreads) {
				JsonWriter::DictScope d(json);
				json.writeInt("threadId", t.threadID);
				json.writeUint("bits", t.bits);
				// PSP_EVENT_WAIT* bits, see sceKernelWaitEventFlag()'s 'wait' parameter -
				// 0x01=OR, 0x10=clear-all-on-match, 0x20=clear-matched-on-match.
				json.writeBool("matchAny", (t.wait & 0x01) != 0);
				json.writeBool("clear", (t.wait & 0x20) != 0);
				json.writeBool("clearAll", (t.wait & 0x10) != 0);
			}
		}
	});
}

// List all mutexes (hle.mutex.list)
//
// No parameters.
//
// Response (same event name):
//  - mutexes: array of objects, each with properties:
//     - uid: unsigned integer kernel object handle - pass to hle.mutex.info for detail.
//     - name: string name.
//     - lockThread: integer thread uid currently holding the lock, or -1 if unlocked.
//     - lockLevel: integer recursion depth (only >1 possible if created with the recursive attr.)
//     - numWaitThreads: integer, how many threads are currently waiting to lock it.
void WebSocketHLEMutexList(DebuggerRequest &req) {
	Core_RunOnCPUThread([&] {
		JsonWriter &json = req.Respond();
		JsonWriter::ArrayScope arr(json, "mutexes");
		kernelObjects.Iterate<PSPMutex>([&](int uid, PSPMutex *m) -> bool {
			JsonWriter::DictScope d(json);
			json.writeUint("uid", uid);
			json.writeString("name", m->nm.name);
			json.writeInt("lockThread", m->nm.lockThread);
			json.writeInt("lockLevel", m->nm.lockLevel);
			json.writeInt("numWaitThreads", (int)m->waitingThreads.size());
			return true;
		});
	});
}

// Get full detail for one mutex (hle.mutex.info)
//
// Parameters:
//  - uid: unsigned integer kernel object handle (as returned by hle.mutex.list.)
//
// Response (same event name):
//  - name: string name.
//  - attr: unsigned integer creation attributes.
//  - initialCount: integer lock count the mutex was created with.
//  - lockThread: integer thread uid currently holding the lock, or -1 if unlocked.
//  - lockLevel: integer recursion depth.
//  - waitingThreads: array of objects, each with properties:
//     - threadId: integer thread uid waiting to lock this mutex.
void WebSocketHLEMutexInfo(DebuggerRequest &req) {
	uint32_t uid;
	if (!req.ParamU32("uid", &uid))
		return;

	Core_RunOnCPUThread([&] {
		PSPMutex *m = RequireKernelObject<PSPMutex>(req, uid);
		if (!m)
			return;

		JsonWriter &json = req.Respond();
		json.writeString("name", m->nm.name);
		json.writeUint("attr", m->nm.attr);
		json.writeInt("initialCount", m->nm.initialCount);
		json.writeInt("lockThread", m->nm.lockThread);
		json.writeInt("lockLevel", m->nm.lockLevel);
		{
			JsonWriter::ArrayScope waiters(json, "waitingThreads");
			WriteThreadIdWaitList(json, m->waitingThreads);
		}
	});
}

// List all semaphores (hle.semaphore.list)
//
// No parameters.
//
// Response (same event name):
//  - semaphores: array of objects, each with properties:
//     - uid: unsigned integer kernel object handle - pass to hle.semaphore.info for detail.
//     - name: string name.
//     - currentCount: integer current count.
//     - numWaitThreads: integer, how many threads are currently waiting on it.
void WebSocketHLESemaphoreList(DebuggerRequest &req) {
	Core_RunOnCPUThread([&] {
		JsonWriter &json = req.Respond();
		JsonWriter::ArrayScope arr(json, "semaphores");
		kernelObjects.Iterate<PSPSemaphore>([&](int uid, PSPSemaphore *sema) -> bool {
			JsonWriter::DictScope d(json);
			json.writeUint("uid", uid);
			json.writeString("name", sema->ns.name);
			json.writeInt("currentCount", sema->ns.currentCount);
			json.writeInt("numWaitThreads", (int)sema->waitingThreads.size());
			return true;
		});
	});
}

// Get full detail for one semaphore (hle.semaphore.info)
//
// Parameters:
//  - uid: unsigned integer kernel object handle (as returned by hle.semaphore.list.)
//
// Response (same event name):
//  - name: string name.
//  - attr: unsigned integer creation attributes.
//  - initCount: integer count the semaphore was created with.
//  - currentCount: integer current count.
//  - maxCount: integer maximum count.
//  - waitingThreads: array of objects, each with properties:
//     - threadId: integer thread uid waiting on this semaphore.
void WebSocketHLESemaphoreInfo(DebuggerRequest &req) {
	uint32_t uid;
	if (!req.ParamU32("uid", &uid))
		return;

	Core_RunOnCPUThread([&] {
		PSPSemaphore *sema = RequireKernelObject<PSPSemaphore>(req, uid);
		if (!sema)
			return;

		JsonWriter &json = req.Respond();
		json.writeString("name", sema->ns.name);
		json.writeUint("attr", sema->ns.attr);
		json.writeInt("initCount", sema->ns.initCount);
		json.writeInt("currentCount", sema->ns.currentCount);
		json.writeInt("maxCount", sema->ns.maxCount);
		{
			JsonWriter::ArrayScope waiters(json, "waitingThreads");
			WriteThreadIdWaitList(json, sema->waitingThreads);
		}
	});
}

// List all message pipes (hle.msgpipe.list)
//
// No parameters.
//
// Response (same event name):
//  - msgPipes: array of objects, each with properties:
//     - uid: unsigned integer kernel object handle - pass to hle.msgpipe.info for detail.
//     - name: string name.
//     - freeSize: integer free bytes currently in the pipe's buffer.
//     - numSendWaitThreads: integer threads currently blocked trying to send.
//     - numReceiveWaitThreads: integer threads currently blocked trying to receive.
void WebSocketHLEMsgPipeList(DebuggerRequest &req) {
	Core_RunOnCPUThread([&] {
		JsonWriter &json = req.Respond();
		JsonWriter::ArrayScope arr(json, "msgPipes");
		kernelObjects.Iterate<MsgPipe>([&](int uid, MsgPipe *m) -> bool {
			JsonWriter::DictScope d(json);
			json.writeUint("uid", uid);
			json.writeString("name", m->nmp.name);
			json.writeInt("freeSize", m->nmp.freeSize);
			json.writeInt("numSendWaitThreads", (int)m->sendWaitingThreads.size());
			json.writeInt("numReceiveWaitThreads", (int)m->receiveWaitingThreads.size());
			return true;
		});
	});
}

static void WriteMsgPipeWaitList(JsonWriter &json, const std::vector<MsgPipeWaitingThread> &threads) {
	for (const MsgPipeWaitingThread &t : threads) {
		JsonWriter::DictScope d(json);
		json.writeInt("threadId", t.threadID);
		json.writeUint("bufSize", t.bufSize);
		json.writeUint("freeSize", t.freeSize);
		json.writeInt("waitMode", t.waitMode);
	}
}

// Get full detail for one message pipe (hle.msgpipe.info)
//
// Parameters:
//  - uid: unsigned integer kernel object handle (as returned by hle.msgpipe.list.)
//
// Response (same event name):
//  - name: string name.
//  - attr: unsigned integer creation attributes.
//  - bufSize: integer total buffer size in bytes.
//  - freeSize: integer free bytes currently in the buffer.
//  - sendWaitingThreads / receiveWaitingThreads: array of objects, each with properties:
//     - threadId: integer thread uid.
//     - bufSize: unsigned integer size of this thread's own send/receive buffer.
//     - freeSize: unsigned integer bytes not yet transferred for this thread.
//     - waitMode: integer, 0 = wait for the full transfer (SCE_KERNEL_MPW_FULL), 1 = accept a
//       partial transfer (SCE_KERNEL_MPW_ASAP.)
void WebSocketHLEMsgPipeInfo(DebuggerRequest &req) {
	uint32_t uid;
	if (!req.ParamU32("uid", &uid))
		return;

	Core_RunOnCPUThread([&] {
		MsgPipe *m = RequireKernelObject<MsgPipe>(req, uid);
		if (!m)
			return;

		JsonWriter &json = req.Respond();
		json.writeString("name", m->nmp.name);
		json.writeUint("attr", m->nmp.attr);
		json.writeInt("bufSize", m->nmp.bufSize);
		json.writeInt("freeSize", m->nmp.freeSize);
		{
			JsonWriter::ArrayScope waiters(json, "sendWaitingThreads");
			WriteMsgPipeWaitList(json, m->sendWaitingThreads);
		}
		{
			JsonWriter::ArrayScope waiters(json, "receiveWaitingThreads");
			WriteMsgPipeWaitList(json, m->receiveWaitingThreads);
		}
	});
}

// List all callbacks (hle.callback.list)
//
// No parameters.
//
// Response (same event name):
//  - callbacks: array of objects, each with properties:
//     - uid: unsigned integer kernel object handle - pass to hle.callback.info for detail.
//     - name: string name.
//     - threadId: integer uid of the thread this callback belongs to.
//     - notifyCount: integer, incremented each time the callback is queued to run.
void WebSocketHLECallbackList(DebuggerRequest &req) {
	Core_RunOnCPUThread([&] {
		JsonWriter &json = req.Respond();
		JsonWriter::ArrayScope arr(json, "callbacks");
		kernelObjects.Iterate<PSPCallback>([&](int uid, PSPCallback *c) -> bool {
			JsonWriter::DictScope d(json);
			json.writeUint("uid", uid);
			json.writeString("name", c->nc.name);
			json.writeInt("threadId", c->nc.threadId);
			json.writeInt("notifyCount", c->nc.notifyCount);
			return true;
		});
	});
}

// Get full detail for one callback (hle.callback.info)
//
// Parameters:
//  - uid: unsigned integer kernel object handle (as returned by hle.callback.list.)
//
// Response (same event name):
//  - name: string name.
//  - threadId: integer uid of the thread this callback belongs to.
//  - entrypoint: unsigned integer address of the callback function.
//  - commonArgument: unsigned integer, the 'common' argument passed to every invocation.
//  - notifyCount: integer, incremented each time the callback is queued to run.
//  - notifyArg: integer, the argument from the most recent notification.
void WebSocketHLECallbackInfo(DebuggerRequest &req) {
	uint32_t uid;
	if (!req.ParamU32("uid", &uid))
		return;

	Core_RunOnCPUThread([&] {
		PSPCallback *c = RequireKernelObject<PSPCallback>(req, uid);
		if (!c)
			return;

		JsonWriter &json = req.Respond();
		json.writeString("name", c->nc.name);
		json.writeInt("threadId", c->nc.threadId);
		json.writeUint("entrypoint", c->nc.entrypoint);
		json.writeUint("commonArgument", c->nc.commonArgument);
		json.writeInt("notifyCount", c->nc.notifyCount);
		json.writeInt("notifyArg", c->nc.notifyArg);
	});
}
