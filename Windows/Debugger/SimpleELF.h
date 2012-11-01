#ifndef GCELF_H
#define GCELF_H

// ELF File Types
#define ET_NONE			0 // No file type
#define ET_REL			1 // Relocatable file
#define ET_EXEC			2 // Executable file
#define ET_DYN			3 // Shared object file
#define ET_CORE			4 // Core file
#define ET_LOPROC		0xFF00 // Processor specific
#define ET_HIPROC		0xFFFF // Processor specific

// ELF Machine Types
#define EM_NONE			0 // No machine
#define EM_M32			1 // AT&T WE 32100
#define EM_SPARC		2 // SPARC
#define EM_386			3 // Intel Architecture
#define EM_68K			4 // Motorola 68000
#define EM_88K			5 // Motorola 88000
#define EM_860			6 // Intel 80860
#define EM_MIPS			7 // MIPS RS3000 Big-Endian
#define EM_MIPS_RS4_BE	8 // MIPS RS4000 Big-Endian
#define EM_ARM			40 // ARM/Thumb Architecture

// ELF Version Types
#define EV_NONE			0 // Invalid version
#define EV_CURRENT		1 // Current version

// ELF Section Header Types
#define SHT_NULL		0
#define SHT_PROGBITS	1
#define SHT_SYMTAB		2
#define SHT_STRTAB		3
#define SHT_RELA		4
#define SHT_HASH		5
#define SHT_DYNAMIC		6
#define SHT_NOTE		7
#define SHT_NOBITS		8
#define SHT_REL			9
#define SHT_SHLIB		10
#define SHT_DYNSYM		11


typedef struct
{
  	unsigned char ID[4];
  	unsigned char clazz;
  	unsigned char data;
  	unsigned char version;
  	unsigned char pad[9];
  	unsigned short e_type; // ELF file type
	unsigned short e_machine; // ELF target machine
	unsigned long e_version; // ELF file version number
	unsigned long e_entry;
	unsigned long e_phoff;
	unsigned long e_shoff;
	unsigned long e_flags;
	unsigned short e_ehsize;
	unsigned short e_phentsize;
	unsigned short e_phnum;
	unsigned short e_shentsize;
	unsigned short e_shnum;
	unsigned short e_shtrndx;
} ELF_Header;

typedef struct {
  unsigned long type;
  unsigned long offset;
  unsigned long vaddr;
  unsigned long paddr;
  unsigned long filesz;
  unsigned long memsz;
  unsigned long flags;
  unsigned long align;
} Program_Header;

typedef struct
{
	unsigned long name;
	unsigned long type;
	unsigned long flags;
	unsigned long addr;
	unsigned long offset;
	unsigned long size;
	unsigned long link;
	unsigned long info;
	unsigned long addralign;
	unsigned long entsize;
} Section_Header;

typedef struct {
  unsigned long name;
  unsigned long value;
  unsigned long size;
  unsigned char info;
  unsigned char other;
  unsigned short shndx;
} Symbol_Header;

typedef struct {
	unsigned long offset;
	unsigned long info;
	signed long addend;
} Rela_Header;

const char ELFID[4] = {0x7F, 'E', 'L', 'F'};

#endif