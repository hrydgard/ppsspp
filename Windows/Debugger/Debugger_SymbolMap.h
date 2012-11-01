#pragma once

#include "../../Globals.h"


bool Debugger_LoadSymbolMap(const char *filename);
void Debugger_SaveSymbolMap(const char *filename);
void Debugger_AddSymbol(const char *symbolname, unsigned int vaddress, size_t size, SymbolType symbol);
void Debugger_ResetSymbolMap();
void Debugger_AnalyzeBackwards();
int Debugger_GetSymbolNum(unsigned int address, SymbolType symmask=ST_FUNCTION);
char *Debugger_GetDescription(unsigned int address);
void Debugger_FillSymbolListBox(HWND listbox, SymbolType symmask=ST_FUNCTION);
void Debugger_FillSymbolComboBox(HWND listbox,SymbolType symmask=ST_FUNCTION);
void Debugger_FillListBoxBLinks(HWND listbox, int num);
int Debugger_GetNumSymbols();
char *Debugger_GetSymbolName(int i);
void Debugger_SetSymbolName(int i, const char *newname);
size_t Debugger_GetSymbolSize(int i);
u32 Debugger_GetSymbolAddr(int i);
int Debugger_FindSymbol(const char *name);
DWORD Debugger_GetAddress(int num);
void Debugger_IncreaseRunCount(int num);
unsigned int Debugger_GetRunCount(int num);
void Debugger_SortSymbols();

void Debugger_UseFuncSignaturesFile(const char *filename, u32 maxAddress);
void Debugger_CompileFuncSignaturesFile(const char *filename);
