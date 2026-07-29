\# An overview of how JPCSP boots the vsh. This was deduced by Claude Sonnet.



1\. The shortcut path: --vsh command-line flag



This is the simple one (MainGUI.java:3103-3129). It:

\- Directly loads flash0:/vsh/module/vshmain.prx through the normal PRX/module loader — exactly like loading any other executable, no special boot emulation.

\- Manually fixes up the root thread afterward to match what a real boot would have set up: forces it into kernel mode and sets its priority to the lowest value (0x7E) so other init threads get to run first.

\- Manually pre-loads three extra flash0 modules the emulator wouldn't otherwise bother with (PSP\_MODULE\_AV\_VAUDIO, PSP\_MODULE\_AV\_ATRAC3PLUS, PSP\_MODULE\_AV\_AVCODEC).

\- Sets the firmware version to 660 and points the game-scanning path at ms0:/PSP/GAME.



2\. The "real" path: --reboot / LLE reboot module



Also present (and preferred when available — --vsh only falls back to the shortcut if Modules.rebootModule.loadAndRun() fails): a genuine low-level emulation of the PSP's actual boot ROM (src/jpcsp/HLE/modules/reboot.java, 1500+ lines). It loads flash0:/reboot.bin, then walks through real Pre-IPL/IPL emulation, decrypting and KL4E/KL3E-decompressing the actual firmware's kd/loadexec\_XXg.prx and kd/sysmem.prx using a real crypto engine. VSH isn't hardcoded anywhere in this path — it just naturally comes out the end as the terminal step of the real firmware chain, same as a real console. This mode is opt-in (reboot.enableReboot defaults false), gated on required firmware files being present (reboot.isAvailable()), and is clearly the more experimental/fragile of the two.



What extra functionality VSH support required



\- A new sceVshBridge HLE module (\~450 lines, \~65 functions) implementing everything flash0:/kd/vshbridge.prx exports — syscalls that only VSH-privileged code calls: region checking, UMD teardown, OSD/"impose" settings, Memory Stick audio DRM, ID-storage lookup, flash formatting, and a whole family of vshKernelLoadExec\*/vshKernelExitVSH\* variants.

\- A new sceVshCommonUtil module — stubs for vsh/module/common\_util.prx's exports.

\- LoadExecForKernel extended with the real kernel-mode syscalls (sceKernelLoadExecVSHMs1..5, sceKernelLoadExecVSHDisc(Debug), sceKernelLoadExecBufferVSHUsbWlan(Debug), sceKernelExitVSHVSH/Kernel) plus a SceKernelLoadExecVSHParam struct matching the real parameter layout — these are what VSH itself calls when the user picks a game from the XMB.

\- A VSH-only module allowlist in HLEModuleManager (kd/vshbridge.prx, vsh/module/paf.prx, vsh/module/common\_gui.prx, vsh/module/common\_util.prx) loaded on top of the normal always-loaded kernel set, only when isRunningFromVsh().

\- Different UMD-insert behavior: opening a UMD while VSH is running doesn't force-boot it — it just fires a UMD switch/insert event (sceUmdUserModule.hleUmdSwitch) and lets VSH's own disc-detection logic (running inside the emulation) notice and launch it, matching real hardware instead of the emulator jumping straight to the game.

\- A dedicated VSHELL\_PARTITION\_ID memory partition for VSH's own allocations.



One thing worth flagging for relevance to PPSSPP



JPCSP ships no Sony firmware at all — the repo's own flash0/ only contains a font. vshmain.prx, reboot.bin, and everything else are expected to come from the user's own dumped PSP. PPSSPP's flash0 is currently entirely synthetic/HLE-reconstructed with no real Sony binaries required to run anything, including games. Adopting either of JPCSP's approaches as-is would mean requiring users to supply a real firmware dump just to reach VSH — a meaningfully different distribution posture than PPSSPP has today, and probably the biggest open question before this is "just an implementation task" versus "a scope/legal decision."

