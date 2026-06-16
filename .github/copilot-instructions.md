# PPSSPP Copilot Instructions

These rules apply to this repository by default.

Ignore the folder ai_instructions, it's old stuff from contributors.

## Priorities

1. Preserve emulator behavior and compatibility first.
2. Prefer correctness over speed when they conflict.
3. Improve performance only when behavior is unchanged and measurable.
4. Keep style changes minimal unless requested. Follow existing code patterns and conventions.

## Change Strategy

1. Make the smallest safe change that solves the issue.
2. Avoid broad refactors unless they are required for correctness.
3. Preserve platform-specific code paths and build logic.
4. Keep cross-platform parity in mind when changing shared code.

## Core Safety Checks

1. For HLE, CPU, GPU, timing, threading, and memory changes, call out regression risks explicitly.
2. Consider savestate compatibility when changing serialized state.
3. Keep lock usage and shared-state access consistent with existing patterns.
4. Prefer existing architecture and helper paths over introducing new abstractions.

## Build and Validation

To verify that things build on Linux/Mac, use ./b.sh --debug. For Windows, use the Visual Studio solution in the Windows subdirectory.

Do not run unit test (I will add instructions for how to run them later).

## Review Expectations

1. In reviews, list concrete risks and behavior changes before summaries.
2. Include file and symbol references for important findings.
3. Propose focused follow-up checks when full validation is not possible.