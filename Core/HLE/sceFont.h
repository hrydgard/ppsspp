#pragma once

class PointerWrap;

void Register_sceFont();
void Register_sceLibFttt();

void __FontInit();
void __FontShutdown();
void __FontDoState(PointerWrap &p);