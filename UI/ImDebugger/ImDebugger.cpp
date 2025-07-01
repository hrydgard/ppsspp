#include <algorithm>


#include "ext/imgui/imgui_internal.h"
#include "ext/imgui/imgui_extras.h"

#include "Common/StringUtils.h"
#include "Common/File/FileUtil.h"
#include "Common/Data/Format/IniFile.h"
#include "Common/Data/Text/Parsers.h"
#include "Common/Log/LogManager.h"
#include "Common/TimeUtil.h"
#include "Core/Config.h"
#include "Core/System.h"
#include "Core/SaveState.h"
#include "Core/Debugger/MemBlockInfo.h"
#include "Core/RetroAchievements.h"
#include "Core/Core.h"
#include "Core/Debugger/DebugInterface.h"
#include "Core/Debugger/DisassemblyManager.h"
#include "Core/Debugger/Breakpoints.h"
#include "Core/MIPS/MIPSDebugInterface.h"
#include "Core/MIPS/MIPSTables.h"
#include "Core/HW/SimpleAudioDec.h"
#include "Core/FileSystems/MetaFileSystem.h"
#include "Core/Debugger/SymbolMap.h"
#include "Core/MemMap.h"
#include "Core/HLE/HLE.h"
#include "Core/HLE/SocketManager.h"
#include "Core/HLE/NetInetConstants.h"
#include "Core/HLE/sceKernelModule.h"
#include "Core/HLE/sceMpeg.h"
#include "Core/HLE/sceNp.h"
#include "Core/HLE/sceNet.h"
#include "Core/HLE/sceNetApctl.h"
#include "Core/HLE/sceNetAdhoc.h"
#include "Core/HLE/proAdhoc.h"
#include "Core/HLE/sceNetAdhocMatching.h"
#include "Core/HLE/NetAdhocCommon.h"
#include "Common/System/Request.h"

#include "Core/Util/AtracTrack.h"
#include "Core/HLE/sceAtrac.h"
#include "Core/HLE/sceAudio.h"
#include "Core/HLE/sceAudiocodec.h"
#include "Core/HLE/sceMp3.h"
#include "Core/HLE/AtracCtx.h"
#include "Core/HLE/sceSas.h"
#include "Core/HW/SasAudio.h"
#include "Core/HW/Display.h"
#include "Core/Dialog/PSPSaveDialog.h"

#include "Core/CoreTiming.h"
// Threads window
#include "Core/HLE/sceKernelThread.h"

// Callstack window
#include "Core/MIPS/MIPSStackWalk.h"

// GPU things
#include "GPU/Common/GPUDebugInterface.h"
#include "GPU/Debugger/Stepping.h"

#include "UI/ImDebugger/ImDebugger.h"
#include "UI/ImDebugger/ImGe.h"

extern bool g_TakeScreenshot;
static ImVec4 g_normalTextColor;

void ShowInMemoryViewerMenuItem(uint32_t addr, ImControl &control) {
	if (ImGui::BeginMenu("Show in memory viewer")) {
		for (int i = 0; i < 4; i++) {
			if (ImGui::MenuItem(ImMemWindow::Title(i))) {
				control.command = { ImCmd::SHOW_IN_MEMORY_VIEWER, addr, (u32)i };
			}
		}
		ImGui::EndMenu();
	}
}

void ShowInMemoryDumperMenuItem(uint32_t addr, uint32_t size, MemDumpMode mode, ImControl &control) {
	if (ImGui::MenuItem(mode == MemDumpMode::Raw ? "Dump bytes to file..." : "Disassemble to file...")) {
		control.command = { ImCmd::SHOW_IN_MEMORY_DUMPER, addr, size, (u32)mode};
	}
}

void ShowInWindowMenuItems(uint32_t addr, ImControl &control) {
	// Enable when we implement the memory viewer
	ShowInMemoryViewerMenuItem(addr, control);
	if (ImGui::MenuItem("Show in CPU debugger")) {
		control.command = { ImCmd::SHOW_IN_CPU_DISASM, addr };
	}
	if (ImGui::MenuItem("Show in GE debugger")) {
		control.command = { ImCmd::SHOW_IN_GE_DISASM, addr };
	}
}

void StatusBar(std::string_view status) {
	if (!status.size()) {
		return;
	}
	ImGui::TextUnformatted(status.data(), status.data() + status.length());
	ImGui::SameLine();
	if (ImGui::SmallButton("Copy")) {
		System_CopyStringToClipboard(status);
	}
}

// TODO: Style it more
// Left click performs the preferred action, if any. Right click opens a menu for more.
void ImClickableValue(const char *id, uint32_t value, ImControl &control, ImCmd cmd) {
	ImGui::PushID(id);

	bool validAddr = Memory::IsValidAddress(value);

	char temp[32];
	snprintf(temp, sizeof(temp), "%08x", value);
	if (ImGui::Selectable(temp) && validAddr) {
		control.command = { cmd, value };
	}

	// Create a right-click popup menu. Restore the color while it's up. NOTE: can't query the theme, pushcolor modifies it!
	ImGui::PushStyleColor(ImGuiCol_Text, g_normalTextColor);
	if (ImGui::BeginPopupContextItem(temp)) {
		if (ImGui::MenuItem(validAddr ? "Copy address to clipboard" : "Copy value to clipboard")) {
			System_CopyStringToClipboard(temp);
		}
		if (validAddr) {
			ImGui::Separator();
			ShowInWindowMenuItems(value, control);
		}
		ImGui::EndPopup();
	}
	ImGui::PopStyleColor();
	ImGui::PopID();
}

// Left click performs the preferred action, if any. Right click opens a menu for more.
void ImClickableValueFloat(const char *id, float value) {
	ImGui::PushID(id);

	char temp[32];
	snprintf(temp, sizeof(temp), "%0.7f", value);
	if (ImGui::Selectable(temp)) {}

	// Create a right-click popup menu. Restore the color while it's up. NOTE: can't query the theme, pushcolor modifies it!
	ImGui::PushStyleColor(ImGuiCol_Text, g_normalTextColor);
	if (ImGui::BeginPopupContextItem(temp)) {
		if (ImGui::MenuItem("Copy value to clipboard")) {
			System_CopyStringToClipboard(temp);
		}
		ImGui::EndPopup();
	}
	ImGui::PopStyleColor();
	ImGui::PopID();
}

void DrawTimeView(ImConfig &cfg) {
	ImGui::SetNextWindowSize(ImVec2(420, 300), ImGuiCond_FirstUseEver);
	if (!ImGui::Begin("Time", &cfg.timeOpen)) {
		ImGui::End();
		return;
	}

	// Display timing
	if (ImGui::CollapsingHeader("Display Timing", ImGuiTreeNodeFlags_DefaultOpen)) {
		ImGui::Text("Num VBlanks: %d", __DisplayGetNumVblanks());
		ImGui::Text("FlipCount: %d", __DisplayGetFlipCount());
		ImGui::Text("VCount: %d", __DisplayGetVCount());
		ImGui::Text("HCount cur: %d accum: %d", __DisplayGetCurrentHcount(), __DisplayGetAccumulatedHcount());
		ImGui::Text("IsVblank: %d", DisplayIsVblank());
	}

	// RTC
	if (ImGui::CollapsingHeader("RTC", ImGuiTreeNodeFlags_DefaultOpen)) {
		PSPTimeval tv;
		__RtcTimeOfDay(&tv);
		ImGui::Text("RtcTimeOfDay: %d.%06d", tv.tv_sec, tv.tv_usec);
		ImGui::Text("RtcGetCurrentTick: %lld", (long long)__RtcGetCurrentTick());
	}

	ImGui::End();
}

void DrawSchedulerView(ImConfig &cfg) {
	ImGui::SetNextWindowSize(ImVec2(420, 300), ImGuiCond_FirstUseEver);
	if (!ImGui::Begin("Event Scheduler", &cfg.schedulerOpen)) {
		ImGui::End();
		return;
	}
	s64 ticks = CoreTiming::GetTicks();
	if (ImGui::BeginChild("event_list", ImVec2(300.0f, 0.0))) {
		const CoreTiming::Event *event = CoreTiming::GetFirstEvent();
		while (event) {
			ImGui::Text("%s (%lld): %d", CoreTiming::GetEventTypes()[event->type].name, event->time - ticks, (int)event->userdata);
			event = event->next;
		}
		ImGui::EndChild();
	}
	ImGui::SameLine();
	if (ImGui::BeginChild("general")) {
		ImGui::Text("CoreState: %s", CoreStateToString(coreState));
		ImGui::Text("downcount: %d", currentMIPS->downcount);
		ImGui::Text("slicelength: %d", CoreTiming::slicelength);
		ImGui::Text("Ticks: %lld", ticks);
		ImGui::Text("Clock (MHz): %0.1f", (float)CoreTiming::GetClockFrequencyHz() / 1000000.0f);
		ImGui::Text("Global time (us): %lld", CoreTiming::GetGlobalTimeUs());
		ImGui::EndChild();
	}
	ImGui::End();
}

static void DrawGPRs(ImConfig &config, ImControl &control, const MIPSDebugInterface *mipsDebug, const ImSnapshotState &prev) {
	ImGui::SetNextWindowSize(ImVec2(320, 600), ImGuiCond_FirstUseEver);
	if (!ImGui::Begin("GPRs", &config.gprOpen)) {
		ImGui::End();
		return;
	}

	bool noDiff = coreState == CORE_RUNNING_CPU || coreState == CORE_STEPPING_GE;

	if (ImGui::Button("Copy all to clipboard")) {
		char *buffer = new char[20000];
		StringWriter w(buffer, 20000);
		for (int i = 0; i < 32; i++) {
			u32 value = mipsDebug->GetGPR32Value(i);
			w.F("%s: %08x (%d)", mipsDebug->GetRegName(0, i).c_str(), value, value).endl();
		}
		w.F("hi: %08x", mipsDebug->GetHi()).endl();
		w.F("lo: %08x", mipsDebug->GetLo()).endl();
		w.F("pc: %08x", mipsDebug->GetPC()).endl();
		System_CopyStringToClipboard(buffer);
		delete[] buffer;
	}

	if (ImGui::BeginTable("gpr", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersH)) {
		ImGui::TableSetupColumn("Reg", ImGuiTableColumnFlags_WidthFixed);
		ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthFixed);
		ImGui::TableSetupColumn("Decimal", ImGuiTableColumnFlags_WidthStretch);

		ImGui::TableHeadersRow();

		auto gprLine = [&](int index, const char *regname, int value, int prevValue) {
			bool diff = value != prevValue && !noDiff;
			bool disabled = value == 0xdeadbeef;

			ImGui::TableNextColumn();
			ImGui::TextUnformatted(regname);
			ImGui::TableNextColumn();
			if (diff) {
				ImGui::PushStyleColor(ImGuiCol_Text, !disabled ? ImDebuggerColor_Diff : ImDebuggerColor_DiffAlpha);
			} else if (disabled) {
				ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(255, 255, 255, 128));
			}
			// TODO: Check if the address is in the code segment to decide default action.
			ImClickableValue(regname, value, control, index == MIPS_REG_RA ? ImCmd::SHOW_IN_CPU_DISASM : ImCmd::SHOW_IN_MEMORY_VIEWER);
			ImGui::TableNextColumn();
			if (value >= -1000000 && value <= 1000000) {
				ImGui::Text("%d", value);
			}
			if (diff || disabled) {
				ImGui::PopStyleColor();
			}
		};
		for (int i = 0; i < 32; i++) {
			ImGui::TableNextRow();
			gprLine(i, mipsDebug->GetRegName(0, i).c_str(), mipsDebug->GetGPR32Value(i), prev.gpr[i]);
		}
		ImGui::TableNextRow();
		gprLine(32, "hi", mipsDebug->GetHi(), prev.hi);
		ImGui::TableNextRow();
		gprLine(33, "lo", mipsDebug->GetLo(), prev.lo);
		ImGui::TableNextRow();
		gprLine(34, "pc", mipsDebug->GetPC(), prev.pc);
		ImGui::TableNextRow();
		gprLine(35, "ll", mipsDebug->GetLLBit(), prev.ll);
		ImGui::EndTable();
	}
	ImGui::End();
}

static void DrawFPRs(ImConfig &config, ImControl &control, const MIPSDebugInterface *mipsDebug, const ImSnapshotState &prev) {
	ImGui::SetNextWindowSize(ImVec2(320, 600), ImGuiCond_FirstUseEver);
	if (!ImGui::Begin("FPRs", &config.fprOpen)) {
		ImGui::End();
		return;
	}

	bool noDiff = coreState == CORE_RUNNING_CPU || coreState == CORE_STEPPING_GE;

	if (ImGui::BeginTable("fpr", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersH)) {
		ImGui::TableSetupColumn("Reg", ImGuiTableColumnFlags_WidthFixed);
		ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthFixed);
		ImGui::TableSetupColumn("Hex", ImGuiTableColumnFlags_WidthStretch);

		ImGui::TableHeadersRow();

		// fpcond
		ImGui::TableNextRow();
		ImGui::TableNextColumn();
		ImGui::TextUnformatted("fpcond");
		ImGui::TableNextColumn();
		ImGui::Text("%08x", mipsDebug->GetFPCond());

		for (int i = 0; i < 32; i++) {
			float fvalue = mipsDebug->GetFPR32Value(i);
			float prevValue = prev.fpr[i];

			// NOTE: Using memcmp to avoid NaN problems.
			bool diff = memcmp(&fvalue, &prevValue, 4) != 0 && !noDiff;

			u32 fivalue;
			memcpy(&fivalue, &fvalue, sizeof(fivalue));
			ImGui::TableNextRow();
			ImGui::TableNextColumn();
			if (diff) {
				ImGui::PushStyleColor(ImGuiCol_Text, ImDebuggerColor_Diff);
			}
			ImGui::TextUnformatted(mipsDebug->GetRegName(1, i).c_str());
			ImGui::TableNextColumn();
			ImClickableValueFloat(mipsDebug->GetRegName(1, i).c_str(), fvalue);
			ImGui::TableNextColumn();
			ImGui::Text("%08x", fivalue);
			if (diff) {
				ImGui::PopStyleColor();
			}
		}

		ImGui::EndTable();
	}
	ImGui::End();
}

static void DrawVFPU(ImConfig &config, ImControl &control, const MIPSDebugInterface *mipsDebug, const ImSnapshotState &prev) {
	ImGui::SetNextWindowSize(ImVec2(320, 600), ImGuiCond_FirstUseEver);
	if (!ImGui::Begin("VFPU", &config.vfpuOpen)) {
		ImGui::End();
		return;
	}
	ImGui::Text("TODO");
	ImGui::End();
}

void WaitIDToString(WaitType waitType, SceUID waitID, char *buffer, size_t bufSize) {
	switch (waitType) {
	case WAITTYPE_AUDIOCHANNEL:
		snprintf(buffer, bufSize, "chan %d", (int)waitID);
		return;
	case WAITTYPE_IO:
		// TODO: More detail
		snprintf(buffer, bufSize, "fd: %d", (int)waitID);
		return;
	case WAITTYPE_ASYNCIO:
		snprintf(buffer, bufSize, "id: %d", (int)waitID);
		return;
	case WAITTYPE_THREADEND:
	case WAITTYPE_MUTEX:
	case WAITTYPE_LWMUTEX:
	case WAITTYPE_MODULE:
	case WAITTYPE_MSGPIPE:
	case WAITTYPE_FPL:
	case WAITTYPE_VPL:
	case WAITTYPE_MBX:
	case WAITTYPE_EVENTFLAG:
	case WAITTYPE_SEMA:
		// Get the name of the thread
		if (kernelObjects.IsValid(waitID)) {
			auto obj = kernelObjects.GetFast<KernelObject>(waitID);
			if (obj && obj->GetName()) {
				truncate_cpy(buffer, bufSize, obj->GetName());
				return;
			}
		}
		break;
	case WAITTYPE_DELAY:
	case WAITTYPE_SLEEP:
	case WAITTYPE_HLEDELAY:
	case WAITTYPE_UMD:
	case WAITTYPE_NONE:
	case WAITTYPE_VBLANK:
	case WAITTYPE_MICINPUT:
		truncate_cpy(buffer, bufSize, "-");
		return;
	default:
		truncate_cpy(buffer, bufSize, "(unimpl)");
		return;
	}

}

void DrawThreadView(ImConfig &cfg, ImControl &control) {
	ImGui::SetNextWindowSize(ImVec2(420, 300), ImGuiCond_FirstUseEver);
	if (!ImGui::Begin("Threads", &cfg.threadsOpen)) {
		ImGui::End();
		return;
	}

	std::vector<DebugThreadInfo> info = GetThreadsInfo();
	if (ImGui::BeginTable("threads", 8, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersH)) {
		ImGui::TableSetupColumn("Id", ImGuiTableColumnFlags_WidthFixed);
		ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthFixed);
		ImGui::TableSetupColumn("PC", ImGuiTableColumnFlags_WidthFixed);
		ImGui::TableSetupColumn("Entry", ImGuiTableColumnFlags_WidthFixed);
		ImGui::TableSetupColumn("Priority", ImGuiTableColumnFlags_WidthFixed);
		ImGui::TableSetupColumn("State", ImGuiTableColumnFlags_WidthFixed);
		ImGui::TableSetupColumn("Wait Type", ImGuiTableColumnFlags_WidthStretch);
		ImGui::TableSetupColumn("Wait ID", ImGuiTableColumnFlags_WidthStretch);
		// .initialStack, .stackSize, etc
		ImGui::TableHeadersRow();

		for (int i = 0; i < (int)info.size(); i++) {
			const DebugThreadInfo &thread = info[i];
			ImGui::TableNextRow();
			ImGui::PushID(i);
			ImGui::TableNextColumn();
			ImGui::Text("%d", thread.id);
			ImGui::TableNextColumn();
			if (ImGui::Selectable(thread.name, cfg.selectedThread == i, ImGuiSelectableFlags_AllowDoubleClick | ImGuiSelectableFlags_SpanAllColumns)) {
				cfg.selectedThread = i;
			}
			if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
				cfg.selectedThread = i;
				ImGui::OpenPopup("threadPopup");
			}
			ImGui::TableNextColumn();
			ImClickableValue("curpc", thread.curPC, control, ImCmd::SHOW_IN_CPU_DISASM);
			ImGui::TableNextColumn();
			ImClickableValue("entry", thread.entrypoint, control, ImCmd::SHOW_IN_CPU_DISASM);
			ImGui::TableNextColumn();
			ImGui::Text("%d", thread.priority);
			ImGui::TableNextColumn();
			ImGui::TextUnformatted(ThreadStatusToString(thread.status));
			ImGui::TableNextColumn();
			ImGui::TextUnformatted(WaitTypeToString(thread.waitType));
			ImGui::TableNextColumn();
			char temp[64];
			WaitIDToString(thread.waitType, thread.waitID, temp, sizeof(temp));
			ImGui::TextUnformatted(temp);
			if (ImGui::BeginPopup("threadPopup")) {
				DebugThreadInfo &thread = info[i];
				ImGui::Text("Thread: %s", thread.name);
				if (ImGui::MenuItem("Copy entry to clipboard")) {
					char temp[64];
					snprintf(temp, sizeof(temp), "%08x", thread.entrypoint);
					System_CopyStringToClipboard(temp);
				}
				if (ImGui::MenuItem("Copy thread PC to clipboard")) {
					char temp[64];
					snprintf(temp, sizeof(temp), "%08x", thread.curPC);
					System_CopyStringToClipboard(temp);
				}
				if (ImGui::MenuItem("Kill thread")) {
					// Dangerous!
					sceKernelTerminateThread(thread.id);
				}
				if (thread.status == THREADSTATUS_WAIT) {
					if (ImGui::MenuItem("Force run now")) {
						__KernelResumeThreadFromWait(thread.id, 0);
					}
				}
				ImGui::EndPopup();
			}
			ImGui::PopID();
		}

		ImGui::EndTable();
	}
	ImGui::End();
}

// TODO: Add popup menu, export file, export dir, etc...
static void RecurseFileSystem(IFileSystem *fs, std::string path, RequesterToken token) {
	std::vector<PSPFileInfo> fileInfo = fs->GetDirListing(path);
	for (auto &file : fileInfo) {
		if (file.type == FileType::FILETYPE_DIRECTORY) {
			if (file.name != "." && file.name != ".." && ImGui::TreeNode(file.name.c_str())) {
				std::string fpath = path + "/" + file.name;
				RecurseFileSystem(fs, fpath, token);
				ImGui::TreePop();
			}
		} else {
			ImGui::Selectable(file.name.c_str());
			if (ImGui::BeginPopupContextItem()) {
				if (ImGui::MenuItem("Copy Path")) {
					System_CopyStringToClipboard(path + "/" + file.name);
				}
				if (ImGui::MenuItem("Save file...")) {
					std::string fullPath = path + "/" + file.name;
					int size = file.size;
					// save dialog
					System_BrowseForFileSave(token, "Save file", file.name, BrowseFileType::ANY, [fullPath, fs, size](const char *responseString, int) {
						int fd = fs->OpenFile(fullPath, FILEACCESS_READ);
						if (fd >= 0) {
							std::string data;
							data.resize(size);
							fs->ReadFile(fd, (u8 *)data.data(), size);
							fs->CloseFile(fd);
							Path dest(responseString);
							File::WriteDataToFile(false, data.data(), data.size(), dest);
						}
					});
				}
				// your popup code
				ImGui::EndPopup();
			}
		}
	}
}

static void DrawFilesystemBrowser(ImConfig &cfg) {
	ImGui::SetNextWindowSize(ImVec2(420, 500), ImGuiCond_FirstUseEver);
	if (!ImGui::Begin("File System", &cfg.filesystemBrowserOpen)) {
		ImGui::End();
		return;
	}

	for (auto &fs : pspFileSystem.GetMounts()) {
		std::string path;
		char desc[256];
		fs.system->Describe(desc, sizeof(desc));
		char fsTitle[512];
		snprintf(fsTitle, sizeof(fsTitle), "%s - %s", fs.prefix.c_str(), desc);
		if (ImGui::TreeNode(fsTitle)) {
			auto system = fs.system;
			RecurseFileSystem(system.get(), path, cfg.requesterToken);
			ImGui::TreePop();
		}
	}

	ImGui::End();
}

static void DrawKernelObjects(ImConfig &cfg) {
	if (!ImGui::Begin("Kernel Objects", &cfg.kernelObjectsOpen)) {
		ImGui::End();
		return;
	}
	if (ImGui::BeginTable("kos", 4, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersH | ImGuiTableFlags_Resizable)) {
		ImGui::TableSetupColumn("ID", ImGuiTableColumnFlags_WidthFixed);
		ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed);
		ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthFixed);
		ImGui::TableSetupColumn("Summary", ImGuiTableColumnFlags_WidthStretch);

		ImGui::TableHeadersRow();

		for (int i = 0; i < (int)KernelObjectPool::maxCount; i++) {
			int id = i + KernelObjectPool::handleOffset;
			if (!kernelObjects.IsValid(id)) {
				continue;
			}
			KernelObject *obj = kernelObjects.GetFast<KernelObject>(id);
			ImGui::TableNextRow();
			ImGui::TableNextColumn();
			ImGui::PushID(i);
			if (ImGui::Selectable("", cfg.selectedKernelObject == i, ImGuiSelectableFlags_AllowDoubleClick | ImGuiSelectableFlags_SpanAllColumns)) {
				cfg.selectedKernelObject = id;
			}
			ImGui::SameLine();
			/*
			if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
				cfg.selectedThread = i;
				ImGui::OpenPopup("kernelObjectPopup");
			}
			*/
			ImGui::Text("%04x", id);
			ImGui::TableNextColumn();
			ImGui::TextUnformatted(obj->GetTypeName());
			ImGui::TableNextColumn();
			ImGui::TextUnformatted(obj->GetName());
			ImGui::TableNextColumn();
			char qi[128];
			obj->GetQuickInfo(qi, sizeof(qi));
			ImGui::TextUnformatted(qi);
			ImGui::PopID();
		}

		ImGui::EndTable();
	}

	if (kernelObjects.IsValid(cfg.selectedKernelObject)) {
		const int id = cfg.selectedKernelObject;
		KernelObject *obj = kernelObjects.GetFast<KernelObject>(id);
		if (obj) {
			ImGui::Text("%08x: %s", id, obj->GetTypeName());
			char longInfo[4096];
			obj->GetLongInfo(longInfo, sizeof(longInfo));
			ImGui::TextUnformatted(longInfo);
		}

		// TODO: Show details
	}
	ImGui::End();
}

static void DrawNp(ImConfig &cfg) {
	if (!ImGui::Begin("NP", &cfg.npOpen)) {
		ImGui::End();
		return;
	}

	ImGui::Text("Signed in: %d", npSigninState);
	ImGui::Text("Title ID: %s", npTitleId.data);

	SceNpId id{};
	NpGetNpId(&id);
	ImGui::Text("User Handle: %s", id.handle.data);


	ImGui::End();
}

static void DrawApctl(ImConfig &cfg) {
	if (!ImGui::Begin("Apctl", &cfg.apctlOpen)) {
		ImGui::End();
		return;
	}

	ImGui::Text("State: %s", ApctlStateToString(netApctlState));
	if (netApctlState != PSP_NET_APCTL_STATE_DISCONNECTED && ImGui::CollapsingHeader("ApCtl Details")) {
		ImGui::Text("Name: %s", netApctlInfo.name);
		ImGui::Text("IP: %s", netApctlInfo.ip);
		ImGui::Text("SubnetMask: %s", netApctlInfo.ip);
		ImGui::Text("SSID: %.*s", netApctlInfo.ssidLength, netApctlInfo.ssid);
		ImGui::Text("SSID: %.*s", netApctlInfo.ssidLength, netApctlInfo.ssid);
		ImGui::Text("Gateway: %s", netApctlInfo.gateway);
		ImGui::Text("PrimaryDNS: %s", netApctlInfo.primaryDns);
		ImGui::Text("SecondaryDNS: %s", netApctlInfo.secondaryDns);
	}

	if (g_Config.bInfrastructureAutoDNS) {
		const InfraDNSConfig &dnsConfig = GetInfraDNSConfig();
		if (dnsConfig.loaded) {
			if (!dnsConfig.gameName.empty()) {
				ImGui::Text("Known game: %s", dnsConfig.gameName.c_str());
			}
			ImGui::Text("connectAdhocForGrouping: %s", BoolStr(dnsConfig.connectAdHocForGrouping));
			ImGui::Text("DNS: %s", dnsConfig.dns.c_str());
			if (!dnsConfig.dyn_dns.empty()) {
				ImGui::Text("DynDNS: %s", dnsConfig.dyn_dns.c_str());
			}
			if (!dnsConfig.fixedDNS.empty()) {
				ImGui::TextUnformatted("Fixed DNS");
				for (auto iter : dnsConfig.fixedDNS) {
					ImGui::Text("%s -> %s", iter.first.c_str(), iter.second.c_str());
				}
			}
		} else {
			ImGui::TextUnformatted("(InfraDNSConfig not loaded)");
		}
	}

	ImGui::End();
}

static void DrawInternals(ImConfig &cfg) {
	if (!ImGui::Begin("PPSSPP Internals", &cfg.internalsOpen)) {
		ImGui::End();
		return;
	}

	struct entry {
		PSPDirectories dir;
		const char *name;
	};

	static const entry dirs[] = {
		{DIRECTORY_PSP, "PSP"},
		{DIRECTORY_CHEATS, "CHEATS"},
		{DIRECTORY_SCREENSHOT, "SCREENSHOT"},
		{DIRECTORY_SYSTEM, "SYSTEM"},
		{DIRECTORY_GAME, "GAME"},
		{DIRECTORY_SAVEDATA, "SAVEDATA"},
		{DIRECTORY_PAUTH, "PAUTH"},
		{DIRECTORY_DUMP, "DUMP"},
		{DIRECTORY_SAVESTATE, "SAVESTATE"},
		{DIRECTORY_CACHE, "CACHE"},
		{DIRECTORY_TEXTURES, "TEXTURES"},
		{DIRECTORY_PLUGINS, "PLUGINS"},
		{DIRECTORY_APP_CACHE, "APP_CACHE"},
		{DIRECTORY_VIDEO, "VIDEO"},
		{DIRECTORY_AUDIO, "AUDIO"},
		{DIRECTORY_MEMSTICK_ROOT, "MEMSTICK_ROOT"},
		{DIRECTORY_EXDATA, "EXDATA"},
		{DIRECTORY_CUSTOM_SHADERS, "CUSTOM_SHADERS"},
		{DIRECTORY_CUSTOM_THEMES, "CUSTOM_THEMES"},
	};

	if (ImGui::CollapsingHeader("GetSysDirectory")) {
		for (auto &dir : dirs) {
			ImGui::Text("%s: %s", dir.name, GetSysDirectory(dir.dir).c_str());
		}
	}

	if (ImGui::CollapsingHeader("Memory")) {
		ImGui::Text("Base pointer: %p", Memory::base);
		ImGui::Text("Main memory size: %08x", Memory::g_MemorySize);
		if (ImGui::Button("Copy to clipboard")) {
			System_CopyStringToClipboard(StringFromFormat("0x%p", Memory::base));
		}
	}

	if (ImGui::CollapsingHeader("ImGui state")) {
		const auto &io = ImGui::GetIO();
		ImGui::Text("WantCaptureMouse: %s", BoolStr(io.WantCaptureMouse));
		ImGui::Text("WantCaptureKeyboard: %s", BoolStr(io.WantCaptureKeyboard));
		ImGui::Text("WantCaptureMouseUnlessPopupClose: %s", BoolStr(io.WantCaptureMouseUnlessPopupClose));
		ImGui::Text("WantTextInput: %s", BoolStr(io.WantTextInput));
	}

	if (ImGui::CollapsingHeader("Save detection")) {
		ImGui::Text("Last in-game save/load: %0.1f seconds ago", SecondsSinceLastGameSave());
		ImGui::Text("Last save/load state: %0.1f seconds ago", SaveState::SecondsSinceLastSavestate());
	}

	ImGui::End();
}

static void DrawAdhoc(ImConfig &cfg) {
	if (!ImGui::Begin("AdHoc", &cfg.adhocOpen)) {
		ImGui::End();
		return;
	}

	const char *discoverStatusStr = "N/A";
	switch (netAdhocDiscoverStatus) {
	case 0: discoverStatusStr = "NONE"; break;
	case 1: discoverStatusStr = "IN_PROGRESS"; break;
	case 2: discoverStatusStr = "COMPLETED"; break;
	default: break;
	}

	ImGui::Text("sceNetAdhoc inited: %s", BoolStr(netAdhocInited));
	ImGui::Text("sceNetAdhocctl inited: %s", BoolStr(netAdhocctlInited));
	ImGui::Text("sceNetAdhocctl state: %s", AdhocCtlStateToString(NetAdhocctl_GetState()));
	ImGui::Text("sceNetAdhocMatching inited: %s", BoolStr(netAdhocctlInited));
	ImGui::Text("GameMode entered: %s", BoolStr(netAdhocGameModeEntered));
	ImGui::Text("FriendFinder running: %s", BoolStr(g_adhocServerConnected));
	ImGui::Text("sceNetAdhocDiscover status: %s", discoverStatusStr);

	if (ImGui::BeginTable("sock", 5, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersH | ImGuiTableFlags_Resizable)) {
		ImGui::TableSetupColumn("ID", ImGuiTableColumnFlags_WidthFixed);
		ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed);
		ImGui::TableSetupColumn("Non-blocking", ImGuiTableColumnFlags_WidthFixed);
		ImGui::TableSetupColumn("BufSize", ImGuiTableColumnFlags_WidthFixed);
		ImGui::TableSetupColumn("IsClient", ImGuiTableColumnFlags_WidthFixed);

		ImGui::TableHeadersRow();

		for (int i = 0; i < MAX_SOCKET; i++) {
			const AdhocSocket *socket = adhocSockets[i];
			if (!socket) {
				continue;
			}

			ImGui::TableNextRow();
			ImGui::TableNextColumn();
			ImGui::Text("%d", i + 1);
			ImGui::TableNextColumn();
			switch (socket->type) {
			case SOCK_PDP: ImGui::TextUnformatted("PDP"); break;
			case SOCK_PTP: ImGui::TextUnformatted("PTP"); break;
			default: ImGui::Text("(%d)", socket->type); break;
			}
			ImGui::TableNextColumn();
			ImGui::TextUnformatted(socket->nonblocking ? "Non-blocking" : "Blocking");
			ImGui::TableNextColumn();
			ImGui::Text("%d", socket->buffer_size);
			ImGui::TableNextColumn();
			ImGui::TextUnformatted(BoolStr(socket->isClient));
		}
		ImGui::EndTable();
	}

	ImGui::End();
}

static void DrawSockets(ImConfig &cfg) {
	if (!ImGui::Begin("Sockets", &cfg.socketsOpen)) {
		ImGui::End();
		return;
	}
	if (ImGui::BeginTable("sock", 9, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersH | ImGuiTableFlags_Resizable)) {
		ImGui::TableSetupColumn("ID", ImGuiTableColumnFlags_WidthFixed);
		ImGui::TableSetupColumn("Port", ImGuiTableColumnFlags_WidthFixed);
		ImGui::TableSetupColumn("IP address", ImGuiTableColumnFlags_WidthFixed);
		ImGui::TableSetupColumn("Non-blocking", ImGuiTableColumnFlags_WidthFixed);
		ImGui::TableSetupColumn("Created by", ImGuiTableColumnFlags_WidthFixed);
		ImGui::TableSetupColumn("Domain", ImGuiTableColumnFlags_WidthFixed);
		ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed);
		ImGui::TableSetupColumn("Protocol", ImGuiTableColumnFlags_WidthStretch);
		ImGui::TableSetupColumn("Host handle", ImGuiTableColumnFlags_WidthFixed);

		ImGui::TableHeadersRow();

		for (int i = SocketManager::MIN_VALID_INET_SOCKET; i < SocketManager::VALID_INET_SOCKET_COUNT; i++) {
			InetSocket *inetSocket;
			if (!g_socketManager.GetInetSocket(i, &inetSocket)) {
				continue;
			}

			ImGui::TableNextRow();
			ImGui::TableNextColumn();
			ImGui::Text("%d", i);
			ImGui::TableNextColumn();
			ImGui::Text("%d", inetSocket->port);
			ImGui::TableNextColumn();
			ImGui::TextUnformatted(inetSocket->addr.c_str());
			ImGui::TableNextColumn();
			ImGui::TextUnformatted(inetSocket->nonblocking ? "Non-blocking" : "Blocking");
			ImGui::TableNextColumn();
			ImGui::TextUnformatted(SocketStateToString(inetSocket->state));
			ImGui::TableNextColumn();
			std::string str = inetSocketDomain2str(inetSocket->domain);
			ImGui::TextUnformatted(str.c_str());
			ImGui::TableNextColumn();
			str = inetSocketType2str(inetSocket->type);
			ImGui::TextUnformatted(str.c_str());
			ImGui::TableNextColumn();
			str = inetSocketProto2str(inetSocket->protocol);
			ImGui::TextUnformatted(str.c_str());
			ImGui::TableNextColumn();
			ImGui::Text("%d", (int)inetSocket->sock);
		}

		ImGui::EndTable();
	}
	ImGui::End();
}

static const char *MemCheckConditionToString(MemCheckCondition cond) {
	// (int) casting to avoid "case not in enum" warnings
	switch ((int)cond) {
	case (int)MEMCHECK_READ: return "Read";
	case (int)MEMCHECK_WRITE: return "Write";
	case (int)MEMCHECK_READWRITE: return "Read/Write";
	case (int)(MEMCHECK_WRITE | MEMCHECK_WRITE_ONCHANGE): return "Write Change";
	case (int)(MEMCHECK_READWRITE | MEMCHECK_WRITE_ONCHANGE): return "Read/Write Change";
	default:
		return "(bad!)";
	}
}

static void DrawBreakpointsView(MIPSDebugInterface *mipsDebug, ImConfig &cfg) {
	if (!ImGui::Begin("Breakpoints", &cfg.breakpointsOpen)) {
		ImGui::End();
		return;
	}
	if (ImGui::BeginTable("bp_window", 2, ImGuiTableFlags_Resizable | ImGuiTableFlags_BordersInnerV)) {
		ImGui::TableSetupColumn("left", ImGuiTableColumnFlags_WidthStretch);
		ImGui::TableSetupColumn("right", ImGuiTableColumnFlags_WidthFixed, 150.0f);
		ImGui::TableNextRow();
		ImGui::TableNextColumn();
		auto &bps = g_breakpoints.GetBreakpointRefs();
		auto &mcs = g_breakpoints.GetMemCheckRefs();

		if (cfg.selectedBreakpoint >= bps.size()) {
			cfg.selectedBreakpoint = -1;
		}
		if (cfg.selectedMemCheck >= mcs.size()) {
			cfg.selectedMemCheck = -1;
		}

		if (ImGui::Button("Add Breakpoint")) {
			cfg.selectedBreakpoint = g_breakpoints.AddBreakPoint(0);
			cfg.selectedMemCheck = -1;
		}
		ImGui::SameLine();
		if (ImGui::Button("Add MemCheck")) {
			cfg.selectedMemCheck = g_breakpoints.AddMemCheck(0, 0, MemCheckCondition::MEMCHECK_WRITE, BreakAction::BREAK_ACTION_PAUSE);
			cfg.selectedBreakpoint = -1;
		}

		if (ImGui::BeginTable("breakpoints", 8, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersH)) {
			ImGui::TableSetupColumn("Enabled", ImGuiTableColumnFlags_WidthFixed);
			ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed);
			ImGui::TableSetupColumn("Address", ImGuiTableColumnFlags_WidthFixed);
			ImGui::TableSetupColumn("Size/Label", ImGuiTableColumnFlags_WidthFixed);
			ImGui::TableSetupColumn("OpCode", ImGuiTableColumnFlags_WidthFixed);
			ImGui::TableSetupColumn("Cond", ImGuiTableColumnFlags_WidthFixed);
			ImGui::TableSetupColumn("Hits", ImGuiTableColumnFlags_WidthStretch);
			ImGui::TableHeadersRow();

			for (int i = 0; i < (int)bps.size(); i++) {
				auto &bp = bps[i];
				bool temp = bp.temporary;
				if (temp) {
					continue;
				}

				ImGui::TableNextRow();
				ImGui::TableNextColumn();
				ImGui::PushID(i);
				// DONE: This clashes with the checkbox!
				// TODO: Test to make sure this works properly
				if (ImGui::Selectable("", cfg.selectedBreakpoint == i, ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowOverlap) && !bp.temporary) {
					cfg.selectedBreakpoint = i;
					cfg.selectedMemCheck = -1;
				}
				ImGui::SameLine();
				ImGui::CheckboxFlags("##enabled", (int *)&bp.result, (int)BREAK_ACTION_PAUSE);
				ImGui::TableNextColumn();
				ImGui::TextUnformatted("Exec");
				ImGui::TableNextColumn();
				ImGui::Text("%08x", bp.addr);
				ImGui::TableNextColumn();
				const std::string sym = g_symbolMap->GetLabelString(bp.addr);
				if (!sym.empty()) {
					ImGui::TextUnformatted(sym.c_str());  // size/label
				} else {
					ImGui::TextUnformatted("-");  // size/label
				}
				ImGui::TableNextColumn();
				// disasm->getOpcodeText(displayedBreakPoints_[index].addr, temp, sizeof(temp));
				ImGui::TextUnformatted("-");  // opcode
				ImGui::TableNextColumn();
				if (bp.hasCond) {
					ImGui::TextUnformatted(bp.cond.expressionString.c_str());
				} else {
					ImGui::TextUnformatted("-");  // condition
				}
				ImGui::TableNextColumn();
				ImGui::Text("-");  // hits not available on exec bps yet
				ImGui::PopID();
			}

			// OK, now list the memchecks.
			for (int i = 0; i < (int)mcs.size(); i++) {
				auto &mc = mcs[i];
				ImGui::TableNextRow();
				ImGui::TableNextColumn();
				ImGui::PushID(i + 10000);
				if (ImGui::Selectable("##memcheck", cfg.selectedMemCheck == i, ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowOverlap)) {
					cfg.selectedBreakpoint = -1;
					cfg.selectedMemCheck = i;
				}
				ImGui::SameLine();
				ImGui::CheckboxFlags("", (int *)&mc.result, BREAK_ACTION_PAUSE);
				ImGui::TableNextColumn();
				ImGui::TextUnformatted(MemCheckConditionToString(mc.cond));
				ImGui::TableNextColumn();
				ImGui::Text("%08x", mc.start);
				ImGui::TableNextColumn();
				ImGui::Text("%08x", mc.end ? (mc.end - mc.start) : 1);
				ImGui::TableNextColumn();
				ImGui::TextUnformatted("-");  // opcode
				ImGui::TableNextColumn();
				ImGui::TextUnformatted("-");  // cond
				ImGui::TableNextColumn();
				ImGui::Text("%d", mc.numHits);
				ImGui::PopID();
			}

			ImGui::EndTable();
		}

		ImGui::TableNextColumn();

		if (cfg.selectedBreakpoint >= 0) {
			// Add edit form for breakpoints
			if (ImGui::BeginChild("bp_edit")) {
				auto &bp = bps[cfg.selectedBreakpoint];
				ImGui::TextUnformatted("Edit breakpoint");
				ImGui::CheckboxFlags("Enabled", (int *)&bp.result, (int)BREAK_ACTION_PAUSE);
				ImGui::InputScalar("Address", ImGuiDataType_U32, &bp.addr, nullptr, nullptr, "%08x", ImGuiInputTextFlags_CharsHexadecimal);
				if (ImGui::Button("Delete")) {
					g_breakpoints.RemoveBreakPoint(bp.addr);
				}
				ImGui::EndChild();
			}
		}

		if (cfg.selectedMemCheck >= 0) {
			// Add edit form for memchecks
			if (ImGui::BeginChild("mc_edit")) {
				auto &mc = mcs[cfg.selectedMemCheck];
				ImGui::TextUnformatted("Edit memcheck");
				if (ImGui::BeginCombo("Condition", MemCheckConditionToString(mc.cond))) {
					if (ImGui::Selectable("Read", mc.cond == MemCheckCondition::MEMCHECK_READ)) {
						mc.cond = MemCheckCondition::MEMCHECK_READ;
					}
					if (ImGui::Selectable("Write", mc.cond == MemCheckCondition::MEMCHECK_WRITE)) {
						mc.cond = MemCheckCondition::MEMCHECK_WRITE;
					}
					if (ImGui::Selectable("Read / Write", mc.cond == MemCheckCondition::MEMCHECK_READWRITE)) {
						mc.cond = MemCheckCondition::MEMCHECK_READWRITE;
					}
					if (ImGui::Selectable("Write On Change", mc.cond == MemCheckCondition::MEMCHECK_WRITE_ONCHANGE)) {
						mc.cond = MemCheckCondition::MEMCHECK_WRITE_ONCHANGE;
					}
					ImGui::EndCombo();
				}
				ImGui::CheckboxFlags("Enabled", (int *)&mc.result, (int)BREAK_ACTION_PAUSE);
				ImGui::InputScalar("Start", ImGuiDataType_U32, &mc.start, NULL, NULL, "%08x", ImGuiInputTextFlags_CharsHexadecimal);
				ImGui::InputScalar("End", ImGuiDataType_U32, &mc.end, NULL, NULL, "%08x", ImGuiInputTextFlags_CharsHexadecimal);
				if (ImGui::Button("Delete")) {
					g_breakpoints.RemoveMemCheck(mcs[cfg.selectedMemCheck].start, mcs[cfg.selectedMemCheck].end);
				}
				ImGui::EndChild();
			}
		}
		ImGui::EndTable();
	}

	ImGui::End();
}

void DrawMediaDecodersView(ImConfig &cfg, ImControl &control) {
	if (!ImGui::Begin("Media decoding contexts", &cfg.mediaDecodersOpen)) {
		ImGui::End();
		return;
	}

	const std::map<u32, MpegContext *> &mpegCtxs = __MpegGetContexts();
	if (ImGui::CollapsingHeaderWithCount("sceMpeg", (int)mpegCtxs.size())) {
		if (ImGui::BeginTable("mpegs", 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersH)) {
			ImGui::TableSetupColumn("Addr", ImGuiTableColumnFlags_WidthFixed);
			ImGui::TableSetupColumn("VFrames", ImGuiTableColumnFlags_WidthFixed);

			ImGui::TableHeadersRow();
			for (auto iter : mpegCtxs) {
				ImGui::TableNextRow();
				ImGui::TableNextColumn();
				ImGui::PushID(iter.first);
				ImGui::SetNextItemAllowOverlap();
				char temp[16];
				snprintf(temp, sizeof(temp), "%08x", iter.first);
				if (ImGui::Selectable(temp, iter.first == cfg.selectedMpegCtx, ImGuiSelectableFlags_SpanAllColumns)) {
					cfg.selectedMpegCtx = iter.first;
				}
				ImGui::TableNextColumn();
				const MpegContext *ctx = iter.second;
				if (!ctx) {
					ImGui::TextUnformatted("N/A");
					ImGui::PopID();
					continue;
				}
				ImGui::Text("%d", ctx->videoFrameCount);
				ImGui::PopID();
			}
			ImGui::EndTable();
		}

		auto iter = mpegCtxs.find(cfg.selectedMpegCtx);
		if (iter != mpegCtxs.end()) {
			const MpegContext *ctx = iter->second;
			char temp[28];
			snprintf(temp, sizeof(temp), "sceMpeg context at %08x", iter->first);
			if (ctx && ImGui::CollapsingHeader(temp, ImGuiTreeNodeFlags_DefaultOpen)) {
				// ImGui::ProgressBar((float)sas->CurPos() / (float)info.fileDataEnd, ImVec2(200.0f, 0.0f));
				ImGui::Text("Mpeg version: %d raw: %08x", ctx->mpegVersion, ctx->mpegRawVersion);
				ImGui::Text("Frame counts: Audio %d, video %d", ctx->audioFrameCount, ctx->videoFrameCount);
				ImGui::Text("Video pixel mode: %d", ctx->videoPixelMode);
				ImGui::Text("AVC status=%d width=%d height=%d result=%d", ctx->avc.avcFrameStatus, ctx->avc.avcDetailFrameWidth, ctx->avc.avcDetailFrameHeight, ctx->avc.avcDecodeResult);
				ImGui::Text("Stream size: %d", ctx->mpegStreamSize);
			}
		}
	}

	// Count the active atrac contexts so we can display it.
	const int maxAtracContexts = __AtracMaxContexts();
	int atracCount = 0;
	for (int i = 0; i < maxAtracContexts; i++) {
		u32 type;
		if (__AtracGetCtx(i, &type)) {
			atracCount++;
		}
	}

	if (ImGui::CollapsingHeaderWithCount("sceAtrac", atracCount, ImGuiTreeNodeFlags_DefaultOpen)) {
		ImGui::Checkbox("Force FFMPEG", &g_Config.bForceFfmpegForAudioDec);
		if (ImGui::BeginTable("atracs", 8, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersH)) {
			ImGui::TableSetupColumn("Index", ImGuiTableColumnFlags_WidthFixed);
			ImGui::TableSetupColumn("Mute", ImGuiTableColumnFlags_WidthFixed);
			ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed);
			ImGui::TableSetupColumn("Status", ImGuiTableColumnFlags_WidthFixed);
			ImGui::TableSetupColumn("Channels", ImGuiTableColumnFlags_WidthFixed);
			ImGui::TableSetupColumn("CurSample", ImGuiTableColumnFlags_WidthFixed);
			ImGui::TableSetupColumn("RemFrames", ImGuiTableColumnFlags_WidthFixed);
			ImGui::TableSetupColumn("Impl", ImGuiTableColumnFlags_WidthFixed);

			ImGui::TableHeadersRow();
			for (int i = 0; i < maxAtracContexts; i++) {
				u32 codecType = 0;

				ImGui::TableNextRow();
				ImGui::TableNextColumn();
				ImGui::PushID(i);
				ImGui::SetNextItemAllowOverlap();
				char temp[16];
				snprintf(temp, sizeof(temp), "%d", i);
				if (ImGui::Selectable(temp, i == cfg.selectedAtracCtx, ImGuiSelectableFlags_SpanAllColumns)) {
					cfg.selectedAtracCtx = i;
				}
				ImGui::TableNextColumn();
				bool *mutePtr = __AtracMuteFlag(i);
				if (mutePtr) {
					ImGui::Checkbox("", mutePtr);
				}
				ImGui::TableNextColumn();

				const AtracBase *ctx = __AtracGetCtx(i, &codecType);
				if (!ctx) {
					// Nothing more we can display about uninitialized contexts.
					ImGui::PopID();
					continue;
				}

				switch (codecType) {
				case 0:
					ImGui::TextUnformatted("-");  // Uninitialized
					break;
				case PSP_CODEC_AT3PLUS:
					ImGui::TextUnformatted("Atrac3+");
					break;
				case PSP_CODEC_AT3:
					ImGui::TextUnformatted("Atrac3");
					break;
				default:
					ImGui::Text("%04x", codecType);
					break;
				}

				ImGui::TableNextColumn();
				ImGui::TextUnformatted(AtracStatusToString(ctx->BufferState()));
				ImGui::TableNextColumn();
				ImGui::Text("in:%d out:%d", ctx->Channels(), ctx->GetOutputChannels());
				ImGui::TableNextColumn();
				if (AtracStatusIsNormal(ctx->BufferState())) {
					int pos;
					ctx->GetNextDecodePosition(&pos);
					ImGui::Text("%d", pos);
				} else {
					ImGui::TextUnformatted("N/A");
				}
				ImGui::TableNextColumn();
				if (AtracStatusIsNormal(ctx->BufferState())) {
					ImGui::Text("%d", ctx->RemainingFrames());
				} else {
					ImGui::TextUnformatted("N/A");
				}
				ImGui::TableNextColumn();
				ImGui::TextUnformatted(ctx->GetContextVersion() >= 2 ? "NewImpl" : "Legacy");
				ImGui::PopID();
			}

			ImGui::EndTable();
		}

		if (cfg.selectedAtracCtx >= 0 && cfg.selectedAtracCtx < PSP_MAX_ATRAC_IDS) {
			u32 type = 0;
			const AtracBase *ctx = __AtracGetCtx(cfg.selectedAtracCtx, &type);
			// Show details about the selected atrac context here.
			char header[32];
			snprintf(header, sizeof(header), "Atrac context %d", cfg.selectedAtracCtx);
			if (ctx && ImGui::CollapsingHeader(header, ImGuiTreeNodeFlags_DefaultOpen)) {
				bool isNormal = AtracStatusIsNormal(ctx->BufferState());
				if (isNormal) {
					int pos;
					ctx->GetNextDecodePosition(&pos);
					int endSample, loopStart, loopEnd;
					ctx->GetSoundSample(&endSample, &loopStart, &loopEnd);
					ImGui::ProgressBar((float)pos / (float)endSample, ImVec2(200.0f, 0.0f));
					ImGui::Text("Status: %s", AtracStatusToString(ctx->BufferState()));
					ImGui::Text("cur/end sample: %d/%d", pos, endSample);
				}
				if (ctx->context_.IsValid()) {
					ImGui::Text("ctx addr: ");
					ImGui::SameLine();
					ImClickableValue("ctx", ctx->context_.ptr, control, ImCmd::SHOW_IN_MEMORY_VIEWER);
				}
				if (ctx->context_.IsValid() && ctx->GetContextVersion() >= 2) {
					const auto &info = ctx->context_->info;
					if (isNormal) {
						ImGui::Text("Buffer: (size: %d / %08x) Frame: %d", info.bufferByte, info.bufferByte, info.sampleSize);
						ImGui::SameLine();
						ImClickableValue("buffer", info.buffer, control, ImCmd::SHOW_IN_MEMORY_VIEWER);
						if (info.secondBuffer || info.secondBufferByte) {
							ImGui::Text("Second: (size: %d / %08x)", info.secondBufferByte, info.secondBufferByte);
							ImGui::SameLine();
							ImClickableValue("second", info.secondBuffer, control, ImCmd::SHOW_IN_MEMORY_VIEWER);
						}
						ImGui::Text("Data: %d/%d", info.dataOff, info.fileDataEnd);
						if (info.state != ATRAC_STATUS_STREAMED_WITHOUT_LOOP) {
							ImGui::Text("LoopNum: %d (%d-%d)", info.loopNum, info.loopStart, info.loopEnd);
						}
						ImGui::Text("DecodePos: %d EndSample: %d", info.decodePos, info.fileDataEnd);
						if (AtracStatusIsStreaming(info.state)) {
							ImGui::Text("Stream: offset %d, streamDataBytes: %d", info.streamOff, info.streamDataByte);
						}
						ImGui::Text("numFrame: %d curBuffer: %d streamOff2: %d", info.numSkipFrames, info.curBuffer, info.secondStreamOff);
					} else if (ctx->BufferState() == ATRAC_STATUS_FOR_SCESAS) {
						// A different set of state!
						const AtracSasStreamState *sas = ctx->StreamStateForSas();
						if (sas) {
							ImGui::ProgressBar((float)sas->CurPos() / (float)info.fileDataEnd, ImVec2(200.0f, 0.0f));
							ImGui::ProgressBar((float)sas->streamOffset / (float)sas->bufSize[sas->curBuffer], ImVec2(200.0f, 0.0f));
							ImGui::Text("Cur pos: %08x File offset: %08x File end: %08x%s", sas->CurPos(), sas->fileOffset, info.fileDataEnd, sas->fileOffset >= info.fileDataEnd ? " (END)" : "");
							ImGui::Text("Second (next buffer): %08x (sz: %08x)", info.secondBuffer, info.secondBufferByte);
							ImGui::Text("Cur buffer: %d (%08x, sz: %08x)", sas->curBuffer, sas->bufPtr[sas->curBuffer], sas->bufSize[sas->curBuffer]);
							ImGui::Text("2nd buffer: %d (%08x, sz: %08x)", sas->curBuffer ^ 1, sas->bufPtr[sas->curBuffer ^ 1], sas->bufSize[sas->curBuffer ^ 1]);
							ImGui::Text("Loop points: %08x, %08x", info.loopStart, info.loopEnd);
							ImGui::TextUnformatted(sas->isStreaming ? "Streaming mode!" : "Non-streaming mode");
						} else {
							ImGui::Text("can't access sas state");
						}
					}

					if (ctx->BufferState() == ATRAC_STATUS_ALL_DATA_LOADED) {
						if (ImGui::Button("Save to disk...")) {
							System_BrowseForFileSave(cfg.requesterToken, "Save AT3 file", "song.at3", BrowseFileType::ATRAC3, [=](const std::string &filename, int) {
								const u8 *data = Memory::GetPointerRange(info.buffer, info.bufferByte);
								if (!data) {
									return;
								}
								FILE *file = File::OpenCFile(Path(filename), "wb");
								if (!file) {
									return;
								}
								fwrite(data, 1, info.bufferByte, file);
								fclose(file);
							});
						}
					}
				} else  {
					ImGui::Text("loop: %d", ctx->LoopNum());
				}
			}
		}
	}

	if (ImGui::CollapsingHeaderWithCount("sceMp3", (int)mp3Map.size(), ImGuiTreeNodeFlags_DefaultOpen)) {
		if (ImGui::BeginTable("mp3", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersH)) {
			ImGui::TableSetupColumn("Handle", ImGuiTableColumnFlags_WidthFixed);
			ImGui::TableSetupColumn("Channels", ImGuiTableColumnFlags_WidthFixed);
			ImGui::TableSetupColumn("ReadPos", ImGuiTableColumnFlags_WidthFixed);

			for (auto &iter : mp3Map) {
				ImGui::TableNextRow();
				ImGui::TableNextColumn();
				ImGui::PushID(iter.first);
				ImGui::SetNextItemAllowOverlap();
				char temp[16];
				snprintf(temp, sizeof(temp), "%d", iter.first);
				if (ImGui::Selectable(temp, iter.first == cfg.selectedMp3Ctx, ImGuiSelectableFlags_SpanAllColumns)) {
					cfg.selectedMp3Ctx = iter.first;
				}
				if (!iter.second) {
					continue;
				}
				ImGui::TableNextColumn();
				ImGui::Text("%d", iter.second->Channels);
				ImGui::TableNextColumn();
				ImGui::Text("%d", (int)iter.second->ReadPos());
				ImGui::PopID();
			}
			ImGui::EndTable();
		}

		auto iter = mp3Map.find(cfg.selectedMp3Ctx);
		if (iter != mp3Map.end() && ImGui::CollapsingHeader("MP3 %d", iter->first)) {
			ImGui::Text("MP3 Context %d", iter->first);
			if (iter->second) {
				AuCtx *ctx = iter->second;
				ImGui::Text("%d Hz, %d channels", ctx->SamplingRate, ctx->Channels);
				ImGui::Text("AUBuf: %08x AUSize: %08x", ctx->AuBuf, ctx->AuBufSize);
				ImGui::Text("PCMBuf: %08x PCMSize: %08x", ctx->PCMBuf, ctx->PCMBufSize);
				ImGui::Text("Pos: %d (%d -> %d)", ctx->ReadPos(), (int)ctx->startPos, (int)ctx->endPos);
			}
		}
	}

	if (ImGui::CollapsingHeaderWithCount("sceAudiocodec", (int)g_audioDecoderContexts.size(), ImGuiTreeNodeFlags_DefaultOpen)) {
		if (ImGui::BeginTable("codecs", 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersH)) {
			ImGui::TableSetupColumn("CtxAddr", ImGuiTableColumnFlags_WidthFixed);
			ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed);

			for (auto &iter : g_audioDecoderContexts) {
				ImGui::TableNextRow();
				ImGui::TableNextColumn();
				ImGui::Text("%08x", iter.first);
				ImGui::TableNextColumn();
				switch (iter.second->GetAudioType()) {
				case PSP_CODEC_AAC: ImGui::TextUnformatted("AAC"); break;
				case PSP_CODEC_MP3: ImGui::TextUnformatted("MP3"); break;
				case PSP_CODEC_AT3PLUS: ImGui::TextUnformatted("Atrac3+"); break;
				case PSP_CODEC_AT3: ImGui::TextUnformatted("Atrac3"); break;
				default: ImGui::Text("%08x", iter.second->GetAudioType()); break;
				}
			}
			ImGui::EndTable();
		}
	}

	ImGui::End();
}

void DrawAudioChannels(ImConfig &cfg, ImControl &control) {
	if (!ImGui::Begin("Raw audio channels", &cfg.audioChannelsOpen)) {
		ImGui::End();
		return;
	}

	if (ImGui::BeginTable("audios", 7, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersH)) {
		ImGui::TableSetupColumn("Index", ImGuiTableColumnFlags_WidthFixed);
		ImGui::TableSetupColumn("Mute", ImGuiTableColumnFlags_WidthFixed);
		ImGui::TableSetupColumn("SampleAddr", ImGuiTableColumnFlags_WidthFixed);
		ImGui::TableSetupColumn("SampleCount", ImGuiTableColumnFlags_WidthFixed);
		ImGui::TableSetupColumn("Volume", ImGuiTableColumnFlags_WidthFixed);
		ImGui::TableSetupColumn("Format", ImGuiTableColumnFlags_WidthFixed);
		ImGui::TableSetupColumn("Waiting Thread", ImGuiTableColumnFlags_WidthFixed);

		ImGui::TableHeadersRow();

		// vaudio / output2 uses channel 8.
		for (int i = 0; i < PSP_AUDIO_CHANNEL_MAX + 1; i++) {
			if (!g_audioChans[i].reserved) {
				continue;
			}
			ImGui::TableNextRow();
			ImGui::TableNextColumn();
			ImGui::PushID(i);
			if (i == 8) {
				ImGui::TextUnformatted("audio2");
			} else {
				ImGui::Text("%d", i);
			}
			ImGui::TableNextColumn();
			ImGui::Checkbox("", &g_audioChans[i].mute);
			ImGui::TableNextColumn();
			char id[2]{};
			id[0] = i + 1;
			ImClickableValue(id, g_audioChans[i].sampleAddress, control, ImCmd::SHOW_IN_MEMORY_VIEWER);
			ImGui::TableNextColumn();
			ImGui::Text("%08x", g_audioChans[i].sampleCount);
			ImGui::TableNextColumn();
			ImGui::Text("%d | %d", g_audioChans[i].leftVolume, g_audioChans[i].rightVolume);
			ImGui::TableNextColumn();
			switch (g_audioChans[i].format) {
			case PSP_AUDIO_FORMAT_STEREO:
				ImGui::TextUnformatted("Stereo");
				break;
			case PSP_AUDIO_FORMAT_MONO:
				ImGui::TextUnformatted("Mono");
				break;
			default:
				ImGui::TextUnformatted("UNK: %04x");
				break;
			}
			ImGui::TableNextColumn();
			for (auto t : g_audioChans[i].waitingThreads) {
				KernelObject *thread = kernelObjects.GetFast<KernelObject>(t.threadID);
				if (thread) {
					ImGui::Text("%s: %d", thread->GetName(), t.numSamples);
				}
			}
			ImGui::PopID();
		}

		ImGui::EndTable();
	}
	ImGui::End();
}

void DrawLogConfig(ImConfig &cfg) {
	if (!ImGui::Begin("Logs", &cfg.logConfigOpen)) {
		ImGui::End();
		return;
	}

	static const char *logLevels[] = {
		"N/A",
		"Notice",  // starts at 1 for some reason
		"Error",
		"Warn",
		"Info",
		"Debug",
		"Verb."
	};
	_dbg_assert_(ARRAY_SIZE(logLevels) == (int)LogLevel::LVERBOSE + 1);

	if (ImGui::BeginTable("logs", 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersH)) {
		ImGui::TableSetupColumn("Log", ImGuiTableColumnFlags_WidthFixed);
		ImGui::TableSetupColumn("Level", ImGuiTableColumnFlags_WidthFixed, 150.0f);
		ImGui::TableHeadersRow();
		for (int i = 0; i < (int)Log::NUMBER_OF_LOGS; i++) {
			LogChannel *chan = g_logManager.GetLogChannel((Log)i);
			ImGui::TableNextRow();
			ImGui::TableNextColumn();

			const char *logName = LogManager::GetLogTypeName((Log)i);

			ImGui::PushID(logName);

			ImGui::Checkbox(logName, &chan->enabled);
			ImGui::TableNextColumn();

			if (ImGui::BeginCombo("-", logLevels[(int)chan->level])) {
				for (int i = 1; i < ARRAY_SIZE(logLevels); ++i) {
					LogLevel current = static_cast<LogLevel>(i);
					bool isSelected = (chan->level == current);
					if (ImGui::Selectable(logLevels[(int)current], isSelected)) {
						chan->level = current;
					}
					if (isSelected) {
						ImGui::SetItemDefaultFocus();
					}
				}
				ImGui::EndCombo();
			}
			ImGui::PopID();
		}
		ImGui::EndTable();
	}

	ImGui::End();
}

static const char *VoiceTypeToString(VoiceType type) {
	switch (type) {
	case VOICETYPE_OFF: return "OFF";
	case VOICETYPE_VAG: return "VAG";  // default
	case VOICETYPE_NOISE: return "NOISE";
	case VOICETYPE_TRIWAVE: return "TRIWAVE";  // are these used? there are functions for them (sceSetTriangularWave)
	case VOICETYPE_PULSEWAVE: return "PULSEWAVE";
	case VOICETYPE_PCM: return "PCM";
	case VOICETYPE_ATRAC3: return "ATRAC3";
	default: return "(unknown!)";
	}
}

void DrawSasAudio(ImConfig &cfg) {
	if (!ImGui::Begin("sasAudio", &cfg.sasAudioOpen)) {
		ImGui::End();
		return;
	}

	const SasInstance *sas = GetSasInstance();
	if (!sas) {
		ImGui::Text("Sas instance not available");
		ImGui::End();
		return;
	}

	ImGui::Checkbox("Mute", __SasGetGlobalMuteFlag());
	ImGui::SameLine();
	ImGui::Checkbox("Show all voices", &cfg.sasShowAllVoices);

	if (ImGui::BeginTable("saschannels", 9, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersH)) {
		ImGui::TableSetupColumn("Index", ImGuiTableColumnFlags_WidthFixed);
		ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed);
		ImGui::TableSetupColumn("On", ImGuiTableColumnFlags_WidthFixed);
		ImGui::TableSetupColumn("Loop", ImGuiTableColumnFlags_WidthFixed);
		ImGui::TableSetupColumn("Pause", ImGuiTableColumnFlags_WidthFixed);
		ImGui::TableSetupColumn("Cur", ImGuiTableColumnFlags_WidthFixed);
		ImGui::TableSetupColumn("Addr", ImGuiTableColumnFlags_WidthFixed);
		ImGui::TableSetupColumn("Size", ImGuiTableColumnFlags_WidthFixed);
		ImGui::TableSetupColumn("Volume", ImGuiTableColumnFlags_WidthFixed);
		ImGui::TableHeadersRow();

		for (int i = 0; i < PSP_SAS_VOICES_MAX; i++) {
			const SasVoice &voice = sas->voices[i];
			if (!voice.on && !cfg.sasShowAllVoices) {
				continue;
			}
			if (!voice.on) {
				ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(255, 255, 255, 128));
			}
			ImGui::TableNextRow();
			ImGui::TableNextColumn();
			ImGui::Text("%d", i);
			ImGui::TableNextColumn();
			ImGui::TextUnformatted(VoiceTypeToString(voice.type));
			ImGui::TableNextColumn();
			ImGui::TextUnformatted(BoolStr(voice.on));
			ImGui::TableNextColumn();
			ImGui::TextUnformatted(BoolStr(voice.loop));
			ImGui::TableNextColumn();
			ImGui::TextUnformatted(BoolStr(voice.paused));
			ImGui::TableNextColumn();
			switch (voice.type) {
			case VOICETYPE_OFF: ImGui::TextUnformatted("(off)"); break;
			case VOICETYPE_VAG: ImGui::Text("%08x", voice.vag.GetReadPtr()); break;
			case VOICETYPE_PCM: ImGui::Text("%08x", voice.pcmAddr + voice.pcmIndex * 2); break;
			default:
				ImGui::TextUnformatted("N/A");
				break;
			}
			ImGui::TableNextColumn();
			switch (voice.type) {
			case VOICETYPE_OFF: ImGui::TextUnformatted("(off)"); break;
			case VOICETYPE_VAG: ImGui::Text("%08x", voice.vagAddr); break;
			case VOICETYPE_PCM: ImGui::Text("%08x", voice.pcmAddr); break;
			case VOICETYPE_ATRAC3: ImGui::Text("atrac: %d", voice.atrac3.AtracID()); break;
			default:
				ImGui::TextUnformatted("N/A");
				break;
			}
			ImGui::TableNextColumn();
			switch (voice.type) {
			case VOICETYPE_OFF: ImGui::TextUnformatted("(off)"); break;
			case VOICETYPE_VAG: ImGui::Text("%08x", voice.vagSize); break;
			case VOICETYPE_PCM: ImGui::Text("%08x", voice.pcmSize); break;
			case VOICETYPE_ATRAC3: ImGui::Text("atrac: n/a"); break;
			default:
				ImGui::TextUnformatted("N/A");
				break;
			}
			ImGui::TableNextColumn();
			ImGui::Text("%d | %d", voice.volumeLeft, voice.volumeRight);
			if (!voice.on) {
				ImGui::PopStyleColor();
			}
		}

		ImGui::EndTable();
	}
	ImGui::End();
}

static void DrawCallStacks(const MIPSDebugInterface *debug, ImConfig &config, ImControl &control) {
	if (!ImGui::Begin("Callstacks", &config.callstackOpen)) {
		ImGui::End();
		return;
	}

	std::vector<DebugThreadInfo> info = GetThreadsInfo();
	// TODO: Add dropdown for thread choice, so you can check the callstacks of other threads.
	u32 entry = 0;
	u32 stackTop = 0;
	for (auto &thread : info) {
		if (thread.isCurrent) {
			entry = thread.entrypoint;
			stackTop = thread.initialStack;
			break;
		}
	}

	if (entry != 0 && ImGui::BeginTable("frames", 6, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersH)) {
		ImGui::TableSetupColumn("Entry", ImGuiTableColumnFlags_WidthFixed);
		ImGui::TableSetupColumn("EntryAddr", ImGuiTableColumnFlags_WidthFixed);
		ImGui::TableSetupColumn("CurPC", ImGuiTableColumnFlags_WidthFixed);
		ImGui::TableSetupColumn("CurOpCode", ImGuiTableColumnFlags_WidthFixed);
		ImGui::TableSetupColumn("CurSP", ImGuiTableColumnFlags_WidthFixed);
		ImGui::TableSetupColumn("Size", ImGuiTableColumnFlags_WidthStretch);

		ImGui::TableHeadersRow();

		std::vector<MIPSStackWalk::StackFrame> frames = MIPSStackWalk::Walk(debug->GetPC(), debug->GetRegValue(0, 31), debug->GetRegValue(0, 29), entry, stackTop);

		// TODO: Add context menu and clickability
		int i = 0;
		for (auto &frame : frames) {
			const std::string entrySym = g_symbolMap->GetLabelString(frame.entry);
			ImGui::PushID(i);
			ImGui::TableNextRow();
			ImGui::TableSetColumnIndex(0);
			ImGui::TextUnformatted(entrySym.c_str());
			ImGui::TableSetColumnIndex(1);
			ImClickableValue("frameentry", frame.entry, control, ImCmd::SHOW_IN_CPU_DISASM);
			ImGui::TableSetColumnIndex(2);
			ImClickableValue("framepc", frame.pc, control, ImCmd::SHOW_IN_CPU_DISASM);
			ImGui::TableSetColumnIndex(3);
			ImGui::TextUnformatted("N/A");  // opcode, see the old debugger
			ImGui::TableSetColumnIndex(4);
			ImClickableValue("framepc", frame.sp, control, ImCmd::SHOW_IN_MEMORY_VIEWER);
			ImGui::TableSetColumnIndex(5);
			ImGui::Text("%d", frame.stackSize);
			// TODO: More fields?
			ImGui::PopID();
			i++;
		}
		ImGui::EndTable();
	}
	ImGui::End();
}

static void DrawUtilityModules(ImConfig &cfg, ImControl &control) {
	if (!ImGui::Begin("Utility Modules", &cfg.utilityModulesOpen) || !g_symbolMap) {
		ImGui::End();
		return;
	}

	ImGui::TextUnformatted(
		"These are fake module representations loaded by sceUtilityLoadModule\n"
		"On a real PSP, these would be loaded from the BIOS.\n");

	const std::map<int, u32> &modules = __UtilityGetLoadedModules();
	if (ImGui::BeginTable("modules", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersH)) {
		ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthFixed);
		ImGui::TableSetupColumn("Load Address", ImGuiTableColumnFlags_WidthFixed);
		ImGui::TableSetupColumn("Load Size", ImGuiTableColumnFlags_WidthFixed);

		ImGui::TableHeadersRow();

		// TODO: Add context menu and clickability
		int i = 0;
		for (const auto &iter : modules) {
			u32 loadedAddr = iter.second;
			const ModuleLoadInfo *info = __UtilityModuleInfo(iter.first);

			ImGui::PushID(i);
			ImGui::TableNextRow();
			ImGui::TableNextColumn();
			if (ImGui::Selectable(info->name, cfg.selectedUtilityModule == i, ImGuiSelectableFlags_SpanAllColumns)) {
				cfg.selectedUtilityModule = i;
			}
			ImGui::TableNextColumn();
			if (loadedAddr) {
				ImClickableValue("addr", loadedAddr, control, ImCmd::SHOW_IN_MEMORY_VIEWER);
			} else {
				ImGui::TextUnformatted("-");
			}
			ImGui::TableNextColumn();
			ImGui::Text("%08x", info->size);
			ImGui::PopID();
			i++;
		}

		ImGui::EndTable();
	}
	ImGui::End();
}

static void DrawModules(const MIPSDebugInterface *debug, ImConfig &cfg, ImControl &control) {
	if (!ImGui::Begin("Modules", &cfg.modulesOpen) || !g_symbolMap) {
		ImGui::End();
		return;
	}

	ImGui::TextUnformatted("This shows modules that have been loaded by the game (not plain HLE)");

	if (ImGui::BeginChild("module_list", ImVec2(170.0f, 0.0), ImGuiChildFlags_ResizeX)) {
		if (ImGui::BeginTable("modules", 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersH)) {
			ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthFixed);
			ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed);
			ImGui::TableHeadersRow();

			// TODO: Add context menu and clickability
			kernelObjects.Iterate<PSPModule>([&cfg](int id, PSPModule *module) -> bool {
				ImGui::PushID(id);
				ImGui::TableNextRow();
				ImGui::TableNextColumn();
				if (ImGui::Selectable(module->GetName(), cfg.selectedModuleId == id, ImGuiSelectableFlags_SpanAllColumns)) {
					cfg.selectedModuleId = id;
				}
				ImGui::TableNextColumn();
				ImGui::TextUnformatted(module->isFake ? "FAKE/HLE" : "normal");
				ImGui::PopID();
				return true;
			});

			ImGui::EndTable();
		}
		ImGui::EndChild();
	}
	ImGui::SameLine();

	if (ImGui::BeginChild("info")) {
		if (kernelObjects.Is<PSPModule>(cfg.selectedModuleId)) {
			PSPModule *mod = kernelObjects.GetFast<PSPModule>(cfg.selectedModuleId);
			if (mod) {
				if (mod->isFake) {
					ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(255, 255, 255, 170));
				}
				ImGui::Text("%s %d.%d (%s)\n", mod->GetName(), mod->nm.version[1], mod->nm.version[0], mod->isFake ? "FAKE/HLE" : "normal");
				ImGui::Text("Attr: %08x (%s)\n", mod->nm.attribute, (mod->nm.attribute & 0x1000) ? "Kernel" : "User");
				char buf[512];
				mod->GetLongInfo(buf, sizeof(buf));
				ImGui::TextUnformatted(buf);
				if (mod->isFake) {
					ImGui::PopStyleColor();
				}
				if (!mod->impModuleNames.empty() && ImGui::CollapsingHeader("Imported modules")) {
					for (auto &name : mod->impModuleNames) {
						ImGui::TextUnformatted(name);
					}
				}
				if (!mod->expModuleNames.empty() && ImGui::CollapsingHeader("Exported modules")) {
					for (auto &name : mod->expModuleNames) {
						ImGui::TextUnformatted(name);
					}
				}
				if (!mod->importedFuncs.empty() || !mod->importedVars.empty()) {
					if (ImGui::CollapsingHeader("Imports")) {
						if (!mod->importedVars.empty() && ImGui::CollapsingHeader("Vars")) {
							for (auto &var : mod->importedVars) {
								ImGui::TextUnformatted("(some var)");  // TODO
							}
						}
						for (auto &import : mod->importedFuncs) {
							// Look the name up in our HLE database.
							const HLEFunction *func = GetHLEFunc(import.moduleName, import.nid);
							ImGui::TextUnformatted(import.moduleName);
							if (func) {
								ImGui::SameLine();
								ImGui::TextUnformatted(func->name);
							}
							ImGui::SameLine(); ImClickableValue("addr", import.stubAddr, control, ImCmd::SHOW_IN_CPU_DISASM);
						}
					}
				}
				if (!mod->exportedFuncs.empty() || !mod->exportedVars.empty()) {
					if (ImGui::CollapsingHeader("Exports")) {
						if (!mod->exportedVars.empty() && ImGui::CollapsingHeader("Vars")) {
							for (auto &var : mod->importedVars) {
								ImGui::TextUnformatted("(some var)");  // TODO
							}
						}
						for (auto &exportFunc : mod->exportedFuncs) {
							// Look the name up in our HLE database.
							const HLEFunction *func = GetHLEFunc(exportFunc.moduleName, exportFunc.nid);
							ImGui::TextUnformatted(exportFunc.moduleName);
							if (func) {
								ImGui::SameLine();
								ImGui::TextUnformatted(func->name);
							}
							ImGui::SameLine(); ImClickableValue("addr", exportFunc.symAddr, control, ImCmd::SHOW_IN_CPU_DISASM);
						}
					}
				}
			}
		} else {
			ImGui::TextUnformatted("(no module selected)");
		}
		ImGui::EndChild();
	}
	ImGui::End();
}

// Started as a module browser but really only draws from the symbols database, so let's
// evolve it to that.
static void DrawSymbols(const MIPSDebugInterface *debug, ImConfig &cfg, ImControl &control) {
	if (!ImGui::Begin("Symbols", &cfg.symbolsOpen) || !g_symbolMap) {
		ImGui::End();
		return;
	}

	// Reads from the symbol map.
	std::vector<LoadedModuleInfo> modules = g_symbolMap->getAllModules();
	if (ImGui::BeginTable("modules", 4, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersH)) {
		ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthFixed);
		ImGui::TableSetupColumn("Address", ImGuiTableColumnFlags_WidthFixed);
		ImGui::TableSetupColumn("Size", ImGuiTableColumnFlags_WidthFixed);
		ImGui::TableSetupColumn("Active", ImGuiTableColumnFlags_WidthFixed);

		ImGui::TableHeadersRow();

		// TODO: Add context menu and clickability
		for (int i = 0; i < (int)modules.size(); i++) {
			auto &module = modules[i];
			ImGui::PushID(i);
			ImGui::TableNextRow();
			ImGui::TableNextColumn();
			if (ImGui::Selectable(module.name.c_str(), cfg.selectedSymbolModule == i, ImGuiSelectableFlags_SpanAllColumns)) {
				cfg.selectedSymbolModule = i;
			}
			ImGui::TableNextColumn();
			ImClickableValue("addr", module.address, control, ImCmd::SHOW_IN_MEMORY_VIEWER);
			ImGui::TableNextColumn();
			ImGui::Text("%08x", module.size);
			ImGui::TableNextColumn();
			ImGui::TextUnformatted(module.active ? "yes" : "no");
			ImGui::PopID();
		}

		ImGui::EndTable();
	}

	if (cfg.selectedModuleId >= 0 && cfg.selectedModuleId < (int)modules.size()) {
		// TODO: Show details
	}
	ImGui::End();
}

void ImAtracToolWindow::Load() {
	if (File::ReadBinaryFileToString(Path(atracPath_), &data_)) {
		track_.reset(new Track());
		AnalyzeAtracTrack((const u8 *)data_.data(), (u32)data_.size(), track_.get(), &error_);
	} else {
		error_ = "Failed to read file from disk. Bad path?";
	}
}

void ImAtracToolWindow::Draw(ImConfig &cfg) {
	if (!ImGui::Begin("Atrac Tool", &cfg.atracToolOpen) || !g_symbolMap) {
		ImGui::End();
		return;
	}

	ImGui::InputText("File", atracPath_, sizeof(atracPath_));
	ImGui::SameLine();
	if (ImGui::Button("Choose...")) {
		System_BrowseForFile(cfg.requesterToken, "Choose AT3 file", BrowseFileType::ATRAC3, [this](const std::string &filename, int) {
			truncate_cpy(atracPath_, filename);
			Load();
		}, nullptr);
	}

	if (strlen(atracPath_) > 0) {
		if (ImGui::Button("Load")) {
			Load();
		}
	}

	if (track_.get() != 0) {
		ImGui::Text("Codec: %s", track_->codecType != PSP_CODEC_AT3 ? "at3+" : "at3");
		ImGui::Text("Bitrate: %d kbps Channels: %d", track_->Bitrate(), track_->channels);
		ImGui::Text("Frame size in bytes: %d (%04x) Output frame in samples: %d", track_->BytesPerFrame(), track_->BytesPerFrame(), track_->SamplesPerFrame());
		ImGui::Text("First valid sample: %08x", track_->FirstSampleOffsetFull());
		ImGui::Text("EndSample: %08x", track_->endSample);
	}

	if (data_.size()) {
		if (ImGui::Button("Dump 64 raw frames")) {
			std::string firstFrames = data_.substr(track_->dataByteOffset, track_->bytesPerFrame * 64);
			System_BrowseForFileSave(cfg.requesterToken, "Save .at3raw", "at3.raw", BrowseFileType::ANY, [firstFrames](const std::string &filename, int) {
				FILE *f = File::OpenCFile(Path(filename), "wb");
				if (f) {
					fwrite(firstFrames.data(), 1, firstFrames.size(), f);
					fclose(f);
				}
			});
		}

		if (ImGui::Button("Unload")) {
			data_.clear();
			track_.reset(nullptr);
		}
	}

	if (!error_.empty()) {
		ImGui::TextUnformatted(error_.c_str());
	}

	ImGui::End();
}

void DrawHLEModules(ImConfig &config) {
	if (!ImGui::Begin("HLE Modules", &config.hleModulesOpen)) {
		ImGui::End();
		return;
	}

	const int moduleCount = GetNumRegisteredHLEModules();
	std::vector<const HLEModule *> modules;
	modules.reserve(moduleCount);
	for (int i = 0; i < moduleCount; i++) {
		modules.push_back(GetHLEModuleByIndex(i));
	}

	std::sort(modules.begin(), modules.end(), [](const HLEModule* a, const HLEModule* b) {
		return a->name < b->name;
	});
	std::string label;
	for (auto mod : modules) {
		label = mod->name;
		if (ImGui::TreeNode(label.c_str())) {
			for (int j = 0; j < mod->numFunctions; j++) {
				auto &func = mod->funcTable[j];
				ImGui::Text("%s(%s)", func.name, func.argmask);
			}
			ImGui::TreePop();
		}
		if (ImGui::BeginPopupContextItem(label.c_str())) {
			if (ImGui::MenuItem("Copy as JpscpTrace")) {
				char *buffer = new char[1000000];
				StringWriter w(buffer, 1000000);

				for (int j = 0; j < mod->numFunctions; j++) {
					auto &func = mod->funcTable[j];
					// Translate the argmask to fit jpscptrace
					std::string amask = func.argmask;
					for (int i = 0; i < amask.size(); i++) {
						switch (amask[i]) {
						case 'i':
						case 'I': amask[i] = 'd'; break;
						case 'f':
						case 'F': amask[i] = 'x'; break;
						default:
							// others go straight through (p)
							break;
						}
					}
					w.F("%s 0x%08x %d %s", func.name, func.ID, strlen(func.argmask), amask.c_str()).endl();
				}
				System_CopyStringToClipboard(w.as_view());
				delete[] buffer;
			}
			if (ImGui::MenuItem("Copy as imports.S")) {
				char *buffer = new char[100000];
				StringWriter w(buffer, 100000);

				w.C(".set noreorder\n\n#include \"pspimport.s\"\n\n");
				w.F("IMPORT_START \"%.*s\",0x00090011\n", (int)mod->name.size(), mod->name.data());
				for (int j = 0; j < mod->numFunctions; j++) {
					auto &func = mod->funcTable[j];
					w.F("IMPORT_FUNC  \"%.*s\",0x%08X,%s\n", (int)mod->name.size(), mod->name.data(), func.ID, func.name);
				}
				w.endl();
				System_CopyStringToClipboard(w.as_view());
				delete[] buffer;
			}
			ImGui::EndPopup();
		}
	}

	ImGui::End();
}

ImDebugger::ImDebugger() {
	reqToken_ = g_requestManager.GenerateRequesterToken();
	cfg_.LoadConfig(ConfigPath());
	g_normalTextColor = ImGui::GetStyleColorVec4(ImGuiCol_Text);
}

ImDebugger::~ImDebugger() {
	cfg_.SaveConfig(ConfigPath());
}

void ImDebugger::Frame(MIPSDebugInterface *mipsDebug, GPUDebugInterface *gpuDebug, Draw::DrawContext *draw) {
	// Snapshot the coreState to avoid inconsistency.
	const CoreState coreState = ::coreState;

	if (Achievements::HardcoreModeActive()) {
		ImGui::Begin("RetroAchievements hardcore mode");
		ImGui::Text("The debugger may not be used when the\nRetroAchievements hardcore mode is enabled.");
		ImGui::Text("To use the debugger, go into Settings / Tools / RetroAchievements and disable them.");
		ImGui::End();
		return;
	}

	// TODO: Pass mipsDebug in where needed instead.
	g_disassemblyManager.setCpu(mipsDebug);
	disasm_.View().setDebugger(mipsDebug);
	for (int i = 0; i < 4; i++) {
		mem_[i].View().setDebugger(mipsDebug);
	}

	// Watch the step counters to figure out when to update things.

	if (lastCpuStepCount_ != Core_GetSteppingCounter()) {
		lastCpuStepCount_ = Core_GetSteppingCounter();
		snapshot_ = newSnapshot_;  // Compare against the previous snapshot.
		Snapshot(currentMIPS);
		disasm_.NotifyStep();
	}

	if (lastGpuStepCount_ != GPUStepping::GetSteppingCounter()) {
		// A GPU step has happened since last time. This means that we should re-center the cursor.
		// Snapshot();
		lastGpuStepCount_ = GPUStepping::GetSteppingCounter();
		SnapshotGPU(gpuDebug);
		geDebugger_.NotifyStep();
	}

	ImControl control{};

	if (ImGui::BeginMainMenuBar()) {
		if (ImGui::BeginMenu("Debug")) {
			switch (coreState) {
			case CoreState::CORE_STEPPING_CPU:
				if (ImGui::MenuItem("Run")) {
					Core_Resume();
				}
				break;
			case CoreState::CORE_RUNNING_CPU:
				if (ImGui::MenuItem("Break")) {
					Core_Break(BreakReason::DebugBreak);
				}
				break;
			default:
				break;
			}
			ImGui::Separator();
			ImGui::MenuItem("Ignore bad memory accesses", nullptr, &g_Config.bIgnoreBadMemAccess);
			ImGui::MenuItem("Break on frame timeout", nullptr, &g_Config.bBreakOnFrameTimeout);
			ImGui::MenuItem("Don't break on start", nullptr, &g_Config.bAutoRun);  // should really invert this bool!
			ImGui::MenuItem("Fast memory", nullptr, &g_Config.bFastMemory);
			ImGui::Separator();
			if (ImGui::MenuItem("Take screenshot")) {
				g_TakeScreenshot = true;
			}
			ImGui::MenuItem("Save screenshot as .png", nullptr, &g_Config.bScreenshotsAsPNG);
			if (ImGui::MenuItem("Restart graphics")) {
				System_PostUIMessage(UIMessage::RESTART_GRAPHICS);
			}
			if (System_GetPropertyBool(SYSPROP_HAS_TEXT_CLIPBOARD)) {
				if (ImGui::MenuItem("Copy memory base to clipboard")) {
					System_CopyStringToClipboard(StringFromFormat("%016llx", (uint64_t)(uintptr_t)Memory::base));
				}
			}
			ImGui::Separator();
			if (ImGui::MenuItem("Close")) {
				g_Config.bShowImDebugger = false;
			}
			ImGui::EndMenu();
		}
		if (ImGui::BeginMenu("Core")) {
			ImGui::MenuItem("Scheduler", nullptr, &cfg_.schedulerOpen);
			ImGui::MenuItem("Time", nullptr, &cfg_.timeOpen);
			ImGui::EndMenu();
		}
		if (ImGui::BeginMenu("CPU")) {
			ImGui::MenuItem("CPU debugger", nullptr, &cfg_.disasmOpen);
			ImGui::MenuItem("GPR regs", nullptr, &cfg_.gprOpen);
			ImGui::MenuItem("FPR regs", nullptr, &cfg_.fprOpen);
			ImGui::MenuItem("VFPU regs", nullptr, &cfg_.vfpuOpen);
			ImGui::MenuItem("Callstacks", nullptr, &cfg_.callstackOpen);
			ImGui::MenuItem("Breakpoints", nullptr, &cfg_.breakpointsOpen);
			ImGui::EndMenu();
		}
		if (ImGui::BeginMenu("Symbols")) {
			ImGui::MenuItem("Symbol browser", nullptr, &cfg_.symbolsOpen);
			ImGui::Separator();

			if (ImGui::MenuItem("Load .ppmap...")) {
				System_BrowseForFile(reqToken_, "Load PPSSPP symbol map", BrowseFileType::SYMBOL_MAP, [&](const char *responseString, int) {
					Path path(responseString);
					if (!g_symbolMap->LoadSymbolMap(path)) {
						ERROR_LOG(Log::Common, "Failed to load symbol map");
					}
					disasm_.DirtySymbolMap();
				});
			}
			if (ImGui::MenuItem("Save .ppmap...")) {
				System_BrowseForFileSave(reqToken_, "Save PPSSPP symbol map", "symbols.ppmap", BrowseFileType::SYMBOL_MAP, [](const char *responseString, int) {
					Path path(responseString);
					if (!g_symbolMap->SaveSymbolMap(path)) {
						ERROR_LOG(Log::Common, "Failed to save symbol map");
					}
				});
			}
			if (ImGui::MenuItem("Load No$ .sym...")) {
				System_BrowseForFile(reqToken_, "Load No$ symbol map", BrowseFileType::SYMBOL_MAP, [&](const char *responseString, int) {
					Path path(responseString);
					if (!g_symbolMap->LoadNocashSym(path)) {
						ERROR_LOG(Log::Common, "Failed to load No$ symbol map");
					}
					disasm_.DirtySymbolMap();
				});
			}
			if (ImGui::MenuItem("Save No$ .sym...")) {
				System_BrowseForFileSave(reqToken_, "Save No$ symbol map", "symbols.sym", BrowseFileType::SYMBOL_MAP, [](const char *responseString, int) {
					Path path(responseString);
					if (!g_symbolMap->SaveNocashSym(path)) {
						ERROR_LOG(Log::Common, "Failed to save No$ symbol map");
					}
				});
			}
			ImGui::Separator();
			ImGui::MenuItem("Compress .ppmap files", nullptr, &g_Config.bCompressSymbols);
			if (ImGui::MenuItem("Reset symbol map")) {
				g_symbolMap->Clear();
				disasm_.DirtySymbolMap();
				// NotifyDebuggerMapLoaded();
			}
			ImGui::EndMenu();
		}
		if (ImGui::BeginMenu("Memory")) {
			for (int i = 0; i < 4; i++) {
				char title[64];
				snprintf(title, sizeof(title), "Memory %d", i + 1);
				ImGui::MenuItem(title, nullptr, &cfg_.memViewOpen[i]);
			}
			ImGui::MenuItem("Memory Dumper", nullptr, &cfg_.memDumpOpen);
			ImGui::EndMenu();
		}
		if (ImGui::BeginMenu("OS HLE")) {
			ImGui::MenuItem("HLE module browser", nullptr, &cfg_.hleModulesOpen);
			ImGui::MenuItem("File System Browser", nullptr, &cfg_.filesystemBrowserOpen);
			ImGui::MenuItem("Kernel Objects", nullptr, &cfg_.kernelObjectsOpen);
			ImGui::MenuItem("Threads", nullptr, &cfg_.threadsOpen);
			ImGui::MenuItem("Modules", nullptr, &cfg_.modulesOpen);
			ImGui::MenuItem("Utility Modules",nullptr, &cfg_.utilityModulesOpen);
			ImGui::EndMenu();
		}
		if (ImGui::BeginMenu("Graphics")) {
			ImGui::MenuItem("GE Debugger", nullptr, &cfg_.geDebuggerOpen);
			ImGui::MenuItem("GE State", nullptr, &cfg_.geStateOpen);
			ImGui::MenuItem("GE Vertices", nullptr, &cfg_.geVertsOpen);
			ImGui::MenuItem("Display Output", nullptr, &cfg_.displayOpen);
			ImGui::MenuItem("Textures", nullptr, &cfg_.texturesOpen);
			ImGui::MenuItem("Framebuffers", nullptr, &cfg_.framebuffersOpen);
			ImGui::MenuItem("Pixel Viewer", nullptr, &cfg_.pixelViewerOpen);
			// More to come here...
			ImGui::EndMenu();
		}
		if (ImGui::BeginMenu("Audio/Video")) {
			ImGui::MenuItem("SasAudio mixer", nullptr, &cfg_.sasAudioOpen);
			ImGui::MenuItem("Raw audio channels", nullptr, &cfg_.audioChannelsOpen);
			ImGui::MenuItem("AV Decoder contexts", nullptr, &cfg_.mediaDecodersOpen);
			ImGui::EndMenu();
		}
		if (ImGui::BeginMenu("Network")) {
			ImGui::MenuItem("ApCtl", nullptr, &cfg_.apctlOpen);
			ImGui::MenuItem("Sockets", nullptr, &cfg_.socketsOpen);
			ImGui::MenuItem("NP", nullptr, &cfg_.npOpen);
			ImGui::MenuItem("AdHoc", nullptr, &cfg_.adhocOpen);
			ImGui::EndMenu();
		}
		if (ImGui::BeginMenu("Tools")) {
			ImGui::MenuItem("Lua Console", nullptr, &cfg_.luaConsoleOpen);
			ImGui::MenuItem("Debug stats", nullptr, &cfg_.debugStatsOpen);
			ImGui::MenuItem("Struct viewer", nullptr, &cfg_.structViewerOpen);
			ImGui::MenuItem("Log channels", nullptr, &cfg_.logConfigOpen);
			ImGui::MenuItem("Atrac Tool", nullptr, &cfg_.atracToolOpen);
			ImGui::EndMenu();
		}
		if (ImGui::BeginMenu("Misc")) {
			ImGui::MenuItem("PPSSPP Internals", nullptr, &cfg_.internalsOpen);
			ImGui::MenuItem("Dear ImGui Demo", nullptr, &cfg_.demoOpen);
			ImGui::MenuItem("Dear ImGui Style editor", nullptr, &cfg_.styleEditorOpen);
			ImGui::EndMenu();
		}
		if (ImGui::MenuItem("Close")) {
			g_Config.bShowImDebugger = false;
		}
		switch (coreState) {
		case CoreState::CORE_STEPPING_CPU:
			if (ImGui::MenuItem(">> Run")) {
				Core_Resume();
			}
			break;
		case CoreState::CORE_RUNNING_CPU:
			if (ImGui::MenuItem("|| Break")) {
				Core_Break(BreakReason::DebugBreak);
			}
			break;
		default:
			break;
		}
		ImGui::EndMainMenuBar();
	}

	if (cfg_.demoOpen) {
		ImGui::ShowDemoWindow(&cfg_.demoOpen);
	}

	if (cfg_.styleEditorOpen) {
		ImGui::ShowStyleEditor();
	}

	if (cfg_.disasmOpen) {
		disasm_.Draw(mipsDebug, cfg_, control, coreState);
	}

	if (cfg_.gprOpen) {
		DrawGPRs(cfg_, control, mipsDebug, snapshot_);
	}

	if (cfg_.fprOpen) {
		DrawFPRs(cfg_, control, mipsDebug, snapshot_);
	}

	if (cfg_.vfpuOpen) {
		DrawVFPU(cfg_, control, mipsDebug, snapshot_);
	}

	if (cfg_.breakpointsOpen) {
		DrawBreakpointsView(mipsDebug, cfg_);
	}

	if (cfg_.filesystemBrowserOpen) {
		DrawFilesystemBrowser(cfg_);
	}

	if (cfg_.audioChannelsOpen) {
		DrawAudioChannels(cfg_, control);
	}

	if (cfg_.sasAudioOpen) {
		DrawSasAudio(cfg_);
	}

	if (cfg_.kernelObjectsOpen) {
		DrawKernelObjects(cfg_);
	}

	if (cfg_.threadsOpen) {
		DrawThreadView(cfg_, control);
	}

	if (cfg_.callstackOpen) {
		DrawCallStacks(mipsDebug, cfg_, control);
	}

	if (cfg_.modulesOpen) {
		DrawModules(mipsDebug, cfg_, control);
	}

	if (cfg_.symbolsOpen) {
		DrawSymbols(mipsDebug, cfg_, control);
	}

	if (cfg_.utilityModulesOpen) {
		DrawUtilityModules(cfg_, control);
	}

	if (cfg_.mediaDecodersOpen) {
		DrawMediaDecodersView(cfg_, control);
	}

	if (cfg_.hleModulesOpen) {
		DrawHLEModules(cfg_);
	}

	if (cfg_.atracToolOpen) {
		atracToolWindow_.Draw(cfg_);
	}

	if (cfg_.framebuffersOpen) {
		DrawFramebuffersWindow(cfg_, gpuDebug->GetFramebufferManagerCommon());
	}

	if (cfg_.texturesOpen) {
		DrawTexturesWindow(cfg_, gpuDebug->GetTextureCacheCommon());
	}

	if (cfg_.logConfigOpen) {
		DrawLogConfig(cfg_);
	}

	if (cfg_.displayOpen) {
		DrawDisplayWindow(cfg_, gpuDebug->GetFramebufferManagerCommon());
	}

	if (cfg_.debugStatsOpen) {
		DrawDebugStatsWindow(cfg_);
	}

	if (cfg_.structViewerOpen) {
		structViewer_.Draw(cfg_, control, mipsDebug);
	}

	if (cfg_.geDebuggerOpen) {
		geDebugger_.Draw(cfg_, control, gpuDebug, draw);
	}

	if (cfg_.geStateOpen) {
		geStateWindow_.Draw(cfg_, control, gpuDebug);
	}

	if (cfg_.geVertsOpen) {
		DrawImGeVertsWindow(cfg_, control, gpuDebug);
	}

	if (cfg_.schedulerOpen) {
		DrawSchedulerView(cfg_);
	}

	if (cfg_.timeOpen) {
		DrawTimeView(cfg_);
	}

	if (cfg_.pixelViewerOpen) {
		pixelViewer_.Draw(cfg_, control, gpuDebug, draw);
	}

	if (cfg_.memDumpOpen) {
		memDumpWindow_.Draw(cfg_, mipsDebug);
	}

	for (int i = 0; i < 4; i++) {
		if (cfg_.memViewOpen[i]) {
			mem_[i].Draw(mipsDebug, cfg_, control, i);
		}
	}

	if (cfg_.socketsOpen) {
		DrawSockets(cfg_);
	}

	if (cfg_.npOpen) {
		DrawNp(cfg_);
	}

	if (cfg_.adhocOpen) {
		DrawAdhoc(cfg_);
	}

	if (cfg_.apctlOpen) {
		DrawApctl(cfg_);
	}

	if (cfg_.internalsOpen) {
		DrawInternals(cfg_);
	}

	if (externalCommand_.cmd != ImCmd::NONE) {
		control.command = externalCommand_;
		externalCommand_.cmd = ImCmd::NONE;
	}

	// Process UI commands
	switch (control.command.cmd) {
	case ImCmd::SHOW_IN_CPU_DISASM:
		disasm_.View().gotoAddr(control.command.param);
		cfg_.disasmOpen = true;
		ImGui::SetWindowFocus(disasm_.Title());
		break;
	case ImCmd::SHOW_IN_GE_DISASM:
		geDebugger_.View().GotoAddr(control.command.param);
		cfg_.geDebuggerOpen = true;
		ImGui::SetWindowFocus(geDebugger_.Title());
		break;
	case ImCmd::SHOW_IN_MEMORY_VIEWER:
	{
		u32 index = control.command.param2;
		_dbg_assert_(index < 4);
		mem_[index].GotoAddr(control.command.param);
		cfg_.memViewOpen[index] = true;
		ImGui::SetWindowFocus(ImMemWindow::Title(index));
		break;
	}
	case ImCmd::SHOW_IN_MEMORY_DUMPER:
	{
		cfg_.memDumpOpen = true;
		memDumpWindow_.SetRange(control.command.param, control.command.param2, (MemDumpMode)control.command.param3);
		ImGui::SetWindowFocus(memDumpWindow_.Title());
		break;
	}
	case ImCmd::TRIGGER_FIND_POPUP:
		// TODO
		break;
	case ImCmd::NONE:
		break;
	case ImCmd::SHOW_IN_PIXEL_VIEWER:
		break;
	}
	if (cfg_.luaConsoleOpen) {
		luaConsole_.Draw(cfg_);
	}
}

void ImDebugger::Snapshot(MIPSState *mips) {
	memcpy(newSnapshot_.gpr, mips->r, sizeof(newSnapshot_.gpr));
	memcpy(newSnapshot_.fpr, mips->fs, sizeof(newSnapshot_.fpr));
	memcpy(newSnapshot_.vpr, mips->v, sizeof(newSnapshot_.vpr));
	newSnapshot_.pc = mips->pc;
	newSnapshot_.lo = mips->lo;
	newSnapshot_.hi = mips->hi;
	newSnapshot_.ll = mips->llBit;
	pixelViewer_.Snapshot();
}

void ImDebugger::SnapshotGPU(GPUDebugInterface *gpuDebug) {
	pixelViewer_.Snapshot();
}

void ImDebugger::DeviceLost() {
	pixelViewer_.DeviceLost();
	geDebugger_.DeviceLost();
}

Path ImDebugger::ConfigPath() {
	return GetSysDirectory(DIRECTORY_SYSTEM) / "imdebugger.ini";
}

// TODO: Move this into the main config at some point.
// But, I don't really want Core to know about the ImDebugger..

void ImConfig::LoadConfig(const Path &iniFile) {
	requesterToken = g_requestManager.GenerateRequesterToken();

	IniFile ini;
	ini.Load(iniFile);  // Ignore return value, might not exist yet. In that case we'll end up loading defaults.
	SyncConfig(&ini, false);
}

void ImConfig::SaveConfig(const Path &iniFile) {
	IniFile ini;
	ini.Load(iniFile);  // ignore return value, might not exist yet
	SyncConfig(&ini, true);
	ini.Save(iniFile);
}

class Syncer {
public:
	explicit Syncer(bool save) : save_(save) {}
	void SetSection(Section *section) { section_ = section; }
	template<class T>
	void Sync(std::string_view key, T *value, T defaultValue) {
		if (save_) {
			section_->Set(key, *value);
		} else {
			section_->Get(key, value, defaultValue);
		}
	}
private:
	Section *section_ = nullptr;
	bool save_;
};

void ImConfig::SyncConfig(IniFile *ini, bool save) {
	Syncer sync(save);
	sync.SetSection(ini->GetOrCreateSection("Windows"));
	sync.Sync("disasmOpen", &disasmOpen, true);
	sync.Sync("demoOpen ", &demoOpen, false);
	sync.Sync("gprOpen", &gprOpen, false);
	sync.Sync("fprOpen", &fprOpen, false);
	sync.Sync("vfpuOpen", &vfpuOpen, false);
	sync.Sync("threadsOpen", &threadsOpen, false);
	sync.Sync("callstackOpen", &callstackOpen, false);
	sync.Sync("breakpointsOpen", &breakpointsOpen, false);
	sync.Sync("symbolsOpen", &symbolsOpen, false);
	sync.Sync("modulesOpen", &modulesOpen, false);
	sync.Sync("hleModulesOpen", &hleModulesOpen, false);
	sync.Sync("mediaDecodersOpen", &mediaDecodersOpen, false);
	sync.Sync("structViewerOpen", &structViewerOpen, false);
	sync.Sync("framebuffersOpen", &framebuffersOpen, false);
	sync.Sync("displayOpen", &displayOpen, true);
	sync.Sync("styleEditorOpen", &styleEditorOpen, false);
	sync.Sync("filesystemBrowserOpen", &filesystemBrowserOpen, false);
	sync.Sync("kernelObjectsOpen", &kernelObjectsOpen, false);
	sync.Sync("audioChannelsOpen", &audioChannelsOpen, false);
	sync.Sync("texturesOpen", &texturesOpen, false);
	sync.Sync("debugStatsOpen", &debugStatsOpen, false);
	sync.Sync("geDebuggerOpen", &geDebuggerOpen, false);
	sync.Sync("geStateOpen", &geStateOpen, false);
	sync.Sync("geVertsOpen", &geVertsOpen, false);
	sync.Sync("schedulerOpen", &schedulerOpen, false);
	sync.Sync("timeOpen", &timeOpen, false);
	sync.Sync("socketsOpen", &socketsOpen, false);
	sync.Sync("npOpen", &npOpen, false);
	sync.Sync("adhocOpen", &adhocOpen, false);
	sync.Sync("apctlOpen", &apctlOpen, false);
	sync.Sync("pixelViewerOpen", &pixelViewerOpen, false);
	sync.Sync("internalsOpen", &internalsOpen, false);
	sync.Sync("sasAudioOpen", &sasAudioOpen, false);
	sync.Sync("logConfigOpen", &logConfigOpen, false);
	sync.Sync("luaConsoleOpen", &luaConsoleOpen, false);
	sync.Sync("utilityModulesOpen", &utilityModulesOpen, false);
	sync.Sync("atracToolOpen", &atracToolOpen, false);
	for (int i = 0; i < 4; i++) {
		char name[64];
		snprintf(name, sizeof(name), "memory%dOpen", i + 1);
		sync.Sync(name, &memViewOpen[i], false);
	}

	sync.SetSection(ini->GetOrCreateSection("Settings"));
	sync.Sync("displayLatched", &displayLatched, false);
	sync.Sync("realtimePixelPreview", &realtimePixelPreview, false);
}
