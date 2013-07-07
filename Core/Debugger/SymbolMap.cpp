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

#ifdef _WIN32
#include <windows.h>
#include <WindowsX.h>
#else
#include <unistd.h>
#endif

#include <algorithm>

#include "../../Globals.h"
#include "../../Core/MemMap.h"
#include "SymbolMap.h"

#include <map>

SymbolMap symbolMap;

//need improvement
static u32 hasher(u32 last, u32 value)
{
	return __rotl(last,3) ^ value;
}

//#define BWLINKS

// TODO: This should ignore immediates of many instructions, in order to be less sensitive. If it did,
// this could work okay.
static u32 ComputeHash(u32 start, u32 size)
{
	u32 hash=0;
	for (unsigned int i=start; i<start+size; i+=4)
		hash = hasher(hash, Memory::Read_Instruction(i));			
	return hash;
}

void SymbolMap::SortSymbols()
{
  std::sort(entries.begin(), entries.end());
}

void SymbolMap::AnalyzeBackwards()
{
#ifndef BWLINKS
	return;
#else
	for (int i=0; i<numEntries; i++)
	{
		u32 ptr = entries[i].vaddress;
		if (ptr)
		{
			if (entries[i].type == ST_FUNCTION)
			{
				for (int a = 0; a<entries[i].size/4; a++)
				{
					u32 inst = CMemory::ReadUncheckedu32(ptr);

					switch (inst>>26)
					{
					case 18:
						{
							if (LK) //LK
							{
								u32 addr;
								if(AA)
									addr = SignExt26(LI << 2);
								else
									addr = ptr + SignExt26(LI << 2);

								int funNum = SymbolMap::GetSymbolNum(addr);
								if (funNum>=0) 
									entries[funNum].backwardLinks.push_back(ptr);
							}
							break;
						}
					default:
						;
					}

					ptr+=4;
				}
			}
		}
	}
#endif
}


void SymbolMap::ResetSymbolMap()
{
#ifdef BWLINKS
	for (int i=0; i<numEntries; i++)
	{
		entries[i].backwardLinks.clear();
	}
#endif
	entries.clear();
	uniqueEntries.clear();
	entryRanges.clear();
}


void SymbolMap::AddSymbol(const char *symbolname, unsigned int vaddress, size_t size, SymbolType st)
{
	MapEntry e;
	strncpy(e.name, symbolname, 127);
	e.name[127] = '\0';
	e.vaddress = vaddress;
	e.size = (u32)size;
	e.type = st;
	if (uniqueEntries.find((const MapEntryUniqueInfo)e) == uniqueEntries.end())
	{
		entries.push_back(e);
		uniqueEntries.insert((const MapEntryUniqueInfo)e);
		entryRanges[e.vaddress + e.size] = e.vaddress;
	}
}

bool SymbolMap::LoadSymbolMap(const char *filename)
{
	SymbolMap::ResetSymbolMap();
	FILE *f = fopen(filename,"r");
	if (!f)
		return false;
	//char temp[256];
	//fgets(temp,255,f); //.text section layout
	//fgets(temp,255,f); //  Starting        Virtual
	//fgets(temp,255,f); //  address  Size   address
	//fgets(temp,255,f); //  -----------------------

	bool started=false;

	while (!feof(f))
	{
		char line[512],temp[256];
		char *p = fgets(line,512,f);
		if(p == NULL)
			break;
		
		// Chop any newlines off.
		for (size_t i = strlen(line) - 1; i > 0; i--) {
			if (line[i] == '\r' || line[i] == '\n') {
				line[i] = '\0';
			}
		}

		if (strlen(line) < 4 || sscanf(line, "%s", temp) != 1)
			continue;

		if (strcmp(temp,"UNUSED")==0) continue;
		if (strcmp(temp,".text")==0)  {started=true;continue;};
		if (strcmp(temp,".init")==0)  {started=true;continue;};
		if (strcmp(temp,"Starting")==0) continue;
		if (strcmp(temp,"extab")==0) continue;
		if (strcmp(temp,".ctors")==0) break;
		if (strcmp(temp,".dtors")==0) break;
		if (strcmp(temp,".rodata")==0) continue;
		if (strcmp(temp,".data")==0) continue;
		if (strcmp(temp,".sbss")==0) continue;
		if (strcmp(temp,".sdata")==0) continue;
		if (strcmp(temp,".sdata2")==0) continue;
		if (strcmp(temp,"address")==0)  continue;
		if (strcmp(temp,"-----------------------")==0)  continue;
		if (strcmp(temp,".sbss2")==0) break;
		if (temp[1]==']') continue;

		if (!started) continue;
		MapEntry e;
		memset(&e, 0, sizeof(e));
		sscanf(line,"%08x %08x %08x %i %127c",&e.address,&e.size,&e.vaddress,(int*)&e.type,e.name);
		
		if (e.type == ST_DATA && e.size==0)
			e.size=4;

		//e.vaddress|=0x80000000;
		if (strcmp(e.name,".text")==0 || strcmp(e.name,".init")==0 || strlen(e.name)<=1) { 
			;
		} else {
			e.UndecorateName();
			entries.push_back(e);
			uniqueEntries.insert((const MapEntryUniqueInfo)e);
			entryRanges[e.vaddress + e.size] = e.vaddress;
		}
	}
	fclose(f);
	SymbolMap::SortSymbols();
//	SymbolMap::AnalyzeBackwards();
	return true;
}


void SymbolMap::SaveSymbolMap(const char *filename) const
{
	FILE *f = fopen(filename,"w");
	if (!f)
		return;
	fprintf(f,".text\n");
	for (auto it = entries.begin(), end = entries.end(); it != end; ++it)
	{
		const MapEntry &e = *it;
		fprintf(f,"%08x %08x %08x %i %s\n",e.address,e.size,e.vaddress,e.type,e.name);
	}
	fclose(f);
}

int SymbolMap::GetSymbolNum(unsigned int address, SymbolType symmask) const
{
	for (size_t i = 0, n = entries.size(); i < n; i++)
	{
		const MapEntry &entry = entries[i];
		unsigned int addr = entry.vaddress;
		if (address >= addr)
		{
			if (address < addr + entry.size)
			{
				if (entries[i].type & symmask)
					return (int) i;
				else
					return -1;
			}
		}
		else
			break;
	}
	return -1;
}

bool SymbolMap::GetSymbolInfo(SymbolInfo *info, u32 address, SymbolType symmask) const
{
	// entryRanges is indexed by end.  The first entry after address should contain address.
	// Otherwise, we have no entry that contains it, unless things overlap (which they shouldn't.)
	const auto containingEntry = entryRanges.upper_bound(address);
	if (containingEntry == entryRanges.end())
		return false;

	// The next most common case is a single symbol by start address.
	// So we optimize for that by looking in our uniqueEntry set.
	u32 start_address = containingEntry->second;
	if (start_address <= address)
	{
		const MapEntryUniqueInfo searchKey = {start_address, start_address};
		const auto entry = uniqueEntries.find(searchKey);
		// In case there were duplicates at some point, double check the end address.
		if (entry != uniqueEntries.end() && entry->vaddress + entry->size > address && (entry->type & symmask) != 0)
		{
			info->address = entry->vaddress;
			info->size = entry->size;
			return true;
		}
	}

	// Fall back to a slower scan.
	int n = GetSymbolNum(address, symmask);
	if (n != -1)
	{
		info->address = GetSymbolAddr(n);
		info->size = GetSymbolSize(n);
		return true;
	}

	return false;
}

const char* SymbolMap::getDirectSymbol(u32 address)
{
	for (auto it = entries.begin(), end = entries.end(); it != end; ++it)
	{
		const MapEntry &entry = *it;
		unsigned int addr = entry.vaddress;
		if (addr == address) return entry.name;
	}
	return NULL;
}

bool SymbolMap::getSymbolValue(char* symbol, u32& dest)
{
	for (auto it = entries.begin(), end = entries.end(); it != end; ++it)
	{
		const MapEntry &entry = *it;
#ifdef _WIN32
		if (stricmp(entry.name,symbol) == 0)
#else
		if (strcasecmp(entry.name,symbol) == 0)
#endif
		{
			dest = entry.vaddress;
			return true;
		}
	}
	return false;
}

static char descriptionTemp[256];

const char *SymbolMap::GetDescription(unsigned int address) const
{
	int fun = SymbolMap::GetSymbolNum(address);
	//if (address == entries[fun].vaddress)
	//{
	if (fun!=-1)
		return entries[fun].name;
	else
	{
		sprintf(descriptionTemp, "(%08x)", address);
		return descriptionTemp;
	}
	//}
	//else
	//	return "";
}

#ifdef _WIN32

static const int defaultSymbolsAddresses[] = {
	0x08800000, 0x08804000, 0x04000000, 0x88000000, 0x00010000
};

static const char* defaultSymbolsNames[] = {
	"User memory", "Default load address", "VRAM","Kernel memory","Scratchpad"
};

static const int defaultSymbolsAmount = sizeof(defaultSymbolsAddresses)/sizeof(const int);

void SymbolMap::FillSymbolListBox(HWND listbox,SymbolType symmask) const
{
	ShowWindow(listbox,SW_HIDE);
	ListBox_ResetContent(listbox);

	if (symmask & ST_DATA)
	{
		for (int i = 0; i < defaultSymbolsAmount; i++)
		{
			char temp[256];
			sprintf(temp,"0x%08X (%s)",defaultSymbolsAddresses[i],defaultSymbolsNames[i]);
			int index = ListBox_AddString(listbox,temp);
			ListBox_SetItemData(listbox,index,defaultSymbolsAddresses[i]);
		}
	}

	SendMessage(listbox, WM_SETREDRAW, FALSE, 0);
	SendMessage(listbox, LB_INITSTORAGE, (WPARAM)entries.size(), (LPARAM)entries.size() * 30);
	for (auto it = entries.begin(), end = entries.end(); it != end; ++it)
	{
		const MapEntry &entry = *it;
		if (entry.type & symmask)
		{
			char temp[256];
			if (entry.type & ST_FUNCTION || !(entry.type & ST_DATA))
			{
				sprintf(temp,"%s",entry.name);
			} else {
				sprintf(temp,"0x%08X (%s)",entry.vaddress,entry.name);
			}

			int index = ListBox_AddString(listbox,temp);
			ListBox_SetItemData(listbox,index,entry.vaddress);
		}
	}
	SendMessage(listbox, WM_SETREDRAW, TRUE, 0);
	RedrawWindow(listbox, NULL, NULL, RDW_ERASE | RDW_FRAME | RDW_INVALIDATE | RDW_ALLCHILDREN);

	ShowWindow(listbox,SW_SHOW);
}

void SymbolMap::FillSymbolComboBox(HWND listbox,SymbolType symmask) const
{
	ShowWindow(listbox,SW_HIDE);
	ComboBox_ResetContent(listbox);

	//int style = GetWindowLong(listbox,GWL_STYLE);

	ComboBox_AddString(listbox,"(0x02000000)");
	ComboBox_SetItemData(listbox,0,0x02000000);

	//ListBox_AddString(listbox,"(0x80002000)");
	//ListBox_SetItemData(listbox,1,0x80002000);

	SendMessage(listbox, WM_SETREDRAW, FALSE, 0);
	SendMessage(listbox, CB_INITSTORAGE, (WPARAM)entries.size(), (LPARAM)entries.size() * 30);
	for (auto it = entries.begin(), end = entries.end(); it != end; ++it)
	{
		const MapEntry &entry = *it;
		if (entry.type & symmask)
		{
			char temp[256];
			sprintf(temp,"%s (%d)",entry.name,entry.size);
			int index = ComboBox_AddString(listbox,temp);
			ComboBox_SetItemData(listbox,index,entry.vaddress);
		}
	}
	SendMessage(listbox, WM_SETREDRAW, TRUE, 0);
	RedrawWindow(listbox, NULL, NULL, RDW_ERASE | RDW_FRAME | RDW_INVALIDATE | RDW_ALLCHILDREN);
	
	ShowWindow(listbox,SW_SHOW);
}

void SymbolMap::FillListBoxBLinks(HWND listbox, int num) const
{	
	ListBox_ResetContent(listbox);
		
	int style = GetWindowLong(listbox,GWL_STYLE);

	const MapEntry &e = entries[num];
#ifdef BWLINKS
	for (int i=0; i<e.backwardLinks.size(); i++)
	{
		u32 addr = e.backwardLinks[i];
		int index = ListBox_AddString(listbox,SymbolMap::GetSymbolName(SymbolMap::GetSymbolNum(addr)));
		ListBox_SetItemData(listbox,index,addr);
	}
#endif
}
#endif

int SymbolMap::GetNumSymbols() const
{
	return (int)entries.size();
}
SymbolType SymbolMap::GetSymbolType(int i) const
{
	return entries[i].type;
}

const char *SymbolMap::GetSymbolName(int i) const
{
	return entries[i].name;
}
void SymbolMap::SetSymbolName(int i, const char *newname)
{
	strncpy(entries[i].name, newname, sizeof(entries[i].name));
}

u32 SymbolMap::GetSymbolAddr(int i) const
{
	return entries[i].vaddress;
}

u32 SymbolMap::GetSymbolSize(int i) const
{
	return entries[i].size;
}

int SymbolMap::FindSymbol(const char *name) const
{
	for (size_t i = 0; i < entries.size(); i++)
		if (strcmp(entries[i].name,name)==0)
			return (int) i;
	return -1;
}

u32 SymbolMap::GetAddress(int num) const
{
	return entries[num].vaddress;
}

void SymbolMap::IncreaseRunCount(int num)
{
	entries[num].runCount++;
}

unsigned int SymbolMap::GetRunCount(int num) const
{
	if (num>=0)
		return entries[num].runCount;
	else
		return 0;
}

// Load an elf with symbols, use SymbolMap::compilefuncsignaturesfile 
// to make a symbol map, load a dol or somethin without symbols, then apply 
// the map with SymbolMap::usefuncsignaturesfile.

void SymbolMap::CompileFuncSignaturesFile(const char *filename) const
{
	// Store name,length,first instruction,hash into file
	FILE *f = fopen(filename, "w");
	fprintf(f,"00000000\n");
	int count=0;
	for (auto it = entries.begin(), end = entries.end(); it != end; ++it)
	{
		const MapEntry &entry = *it;
		int size = entry.size;
		if (size >= 16 && entry.type == ST_FUNCTION)
		{
			u32 inst = Memory::Read_Instruction(entry.vaddress); //try to make a bigger number of different vals sometime
			if (inst != 0)
			{
				char temp[64];
				strncpy(temp,entry.name,63);
				fprintf(f, "%08x\t%08x\t%08x\t%s\n", inst, size, ComputeHash(entry.vaddress,size), temp);    
				count++;
			}
		}
	}
	fseek(f,0,SEEK_SET);
	fprintf(f,"%08x",count);
	fclose(f);
}


struct Sig
{
	u32 inst;
	u32 size;
	u32 hash;
	char name[64];
	Sig(){}
	Sig(u32 _inst, u32 _size, u32 _hash, char *_name) : inst(_inst), size(_size), hash(_hash)
	{
		strncpy(name,_name,63);
	}
	bool operator <(const Sig &other) const {
		return inst < other.inst;
	}
};

std::vector<Sig> sigs;

typedef std::map <u32,Sig *> Sigmap;
Sigmap sigmap;

void SymbolMap::UseFuncSignaturesFile(const char *filename, u32 maxAddress)
{
	sigs.clear();
	//SymbolMap::ResetSymbolMap();
	//#1: Read the signature file and put them in a fast data structure
	FILE *f = fopen(filename, "r");
	int count;
	if (fscanf(f, "%08x\n", &count) != 1)
		count = 0;
	char name[256];
	for (int a=0; a<count; a++)
	{
		u32 inst, size, hash;
		if (fscanf(f,"%08x\t%08x\t%08x\t%s\n",&inst,&size,&hash,name)!=EOF)
			sigs.push_back(Sig(inst,size,hash,name));
		else
			break;
	}
	size_t numSigs = sigs.size();
	fclose(f);
	std::sort(sigs.begin(), sigs.end());

	f = fopen("C:\\mojs.txt", "w");
	fprintf(f,"00000000\n");
	for (size_t j=0; j<numSigs; j++)
		fprintf(f, "%08x\t%08x\t%08x\t%s\n", sigs[j].inst, sigs[j].size, sigs[j].hash, sigs[j].name);    
	fseek(f,0,SEEK_SET);
	fprintf(f,"%08x", (unsigned int)numSigs);
	fclose(f);

	u32 last = 0xc0d3babe;
	for (size_t i=0; i<numSigs; i++)
	{
		if (sigs[i].inst != last)
		{
			sigmap.insert(Sigmap::value_type(sigs[i].inst, &sigs[i]));
			last = sigs[i].inst;
		}
	}

	//#2: Scan/hash the memory and locate functions
	char temp[256];
	u32 lastAddr=0;
	for (u32 addr = 0x80000000; addr<maxAddress; addr+=4)
	{
		if ((addr&0xFFFF0000) != (lastAddr&0xFFFF0000))
		{
			sprintf(temp,"Scanning: %08x",addr);
			lastAddr=addr;
		}
		u32 inst = Memory::Read_Instruction(addr);
		if (!inst) 
			continue;

		Sigmap::iterator iter = sigmap.find(inst);
		if (iter != sigmap.end())
		{
			Sig *sig = iter->second;
			while (true)
			{
				if (sig->inst != inst)
					break;

				u32 hash = ComputeHash(addr,sig->size);				
				if (hash==sig->hash)
				{
					//MATCH!!!!
					MapEntry e;
					e.address=addr;
					e.size= sig->size;
					e.vaddress = addr;
					e.type=ST_FUNCTION;
					strcpy(e.name,sig->name);
					addr+=sig->size-4; //don't need to check function interior
					entries.push_back(e);
					uniqueEntries.insert((const MapEntryUniqueInfo)e);
					entryRanges[e.vaddress + e.size] = e.vaddress;
					break;
				}
				sig++;
			}
		}
	}
	//ensure code coloring even if symbols were loaded before
	SymbolMap::SortSymbols();
}
