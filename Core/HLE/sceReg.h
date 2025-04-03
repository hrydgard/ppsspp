#pragma once

class PointerWrap;

void Register_sceReg();

void __RegInit();
void __RegShutdown();
void __RegDoState(PointerWrap &p);
