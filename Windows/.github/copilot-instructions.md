# Copilot Instructions

## Project Guidelines
- When working in this PPSSPP workspace, the workspace root is C:\dev\ppsspp\Windows\, so files outside the Windows directory (like GPU/Common/, Core/, etc.)
  must be accessed with the ".." prefix. For example, use "..\GPU\Common\DrawEngineCommon.cpp" instead of "GPU\Common\DrawEngineCommon.cpp" when calling file editing tools.

## LSX documentation

- When writing code for LSX, the SIMD instruction set for Loongarch64, you can refer to:
	- http://ppsspp.org/unofficial/lsx/LSX_LASX_REFERENCE.md
	- http://ppsspp.org/unofficial/lsx/LSX_LASX_DETAILED.md
	- They might also be available at D:\Data\LSX.
	- These AI-friendly text files were generated from https://jia.je/unofficial-loongarch-intrinsics-guide/.
	- Note, verifying by compiling doesn't work, MSVC doesn't support LoongArch so the code is all ifdeff'ed out.
