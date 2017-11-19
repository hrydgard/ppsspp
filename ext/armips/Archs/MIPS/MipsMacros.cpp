#include "stdafx.h"
#include "MipsMacros.h"
#include "CMipsInstruction.h"
#include "Core/Common.h"
#include "Mips.h"
#include "MipsOpcodes.h"
#include "Parser/Parser.h"
#include "MipsParser.h"

MipsMacroCommand::MipsMacroCommand(CAssemblerCommand* content, int macroFlags)
{
	this->content = content;
	this->macroFlags = macroFlags;
	IgnoreLoadDelay = Mips.GetIgnoreDelay();
}

MipsMacroCommand::~MipsMacroCommand()
{
	delete content;
}

bool MipsMacroCommand::Validate()
{
	int64_t memoryPos = g_fileManager->getVirtualAddress();
	content->applyFileInfo();
	bool result = content->Validate();
	int64_t newMemoryPos = g_fileManager->getVirtualAddress();

	applyFileInfo();

	if (IgnoreLoadDelay == false && Mips.GetDelaySlot() == true && (newMemoryPos-memoryPos) > 4
		&& (macroFlags & MIPSM_DONTWARNDELAYSLOT) == 0)
	{
		Logger::queueError(Logger::Warning,L"Macro with multiple opcodes used inside a delay slot");
	}

	if (newMemoryPos == memoryPos)
		Logger::queueError(Logger::Warning,L"Empty macro content");

	return result;
}

void MipsMacroCommand::Encode() const
{
	content->Encode();
}

void MipsMacroCommand::writeTempData(TempData& tempData) const
{
	content->applyFileInfo();
	content->writeTempData(tempData);
}

std::wstring preprocessMacro(const wchar_t* text, MipsImmediateData& immediates)
{
	// A macro is turned into a sequence of opcodes that are parsed seperately.
	// Any expressions used in the macro may be evaluated at a different memory
	// position, so the '.' operator needs to be replaced by a label at the start
	// of the macro
	std::wstring labelName = Global.symbolTable.getUniqueLabelName(true);
	immediates.primary.expression.replaceMemoryPos(labelName);
	immediates.secondary.expression.replaceMemoryPos(labelName);

	return formatString(L"%s: %s",labelName,text);
}

CAssemblerCommand* createMacro(Parser& parser, const std::wstring& text, int flags, std::initializer_list<AssemblyTemplateArgument> variables)
{
	CAssemblerCommand* content = parser.parseTemplate(text,variables);
	return new MipsMacroCommand(content,flags);
}

CAssemblerCommand* generateMipsMacroAbs(Parser& parser, MipsRegisterData& registers, MipsImmediateData& immediates, int flags)
{
	const wchar_t* templateAbs = LR"(
		%sraop% 	r1,%rs%,31
		xor 		%rd%,%rs%,r1
		%subop% 	%rd%,%rd%,r1
	)";

	std::wstring sraop, subop;

	switch (flags & MIPSM_ACCESSMASK)
	{
	case MIPSM_W:	sraop = L"sra"; subop = L"subu"; break;
	case MIPSM_DW:	sraop = L"dsra32"; subop = L"dsubu"; break;
	default: return nullptr;
	}

	std::wstring macroText = preprocessMacro(templateAbs,immediates);
	return createMacro(parser,macroText,flags, {
			{ L"%rd%",		registers.grd.name },
			{ L"%rs%",		registers.grs.name },
			{ L"%sraop%",	sraop },
			{ L"%subop%",	subop },
	});
}

CAssemblerCommand* generateMipsMacroLiFloat(Parser& parser, MipsRegisterData& registers, MipsImmediateData& immediates, int flags)
{
	const wchar_t* templateLiFloat = LR"(
		li 		r1,float(%imm%)
		mtc1	r1,%rs%
	)";

	std::wstring sraop, subop;

	std::wstring macroText = preprocessMacro(templateLiFloat,immediates);
	return createMacro(parser,macroText,flags, {
			{ L"%imm%",		immediates.secondary.expression.toString() },
			{ L"%rs%",		registers.frs.name },
	});
}

CAssemblerCommand* generateMipsMacroLi(Parser& parser, MipsRegisterData& registers, MipsImmediateData& immediates, int flags)
{
	const wchar_t* templateLi = LR"(
		.if abs(%imm%) > 0xFFFFFFFF
			.error "Immediate value too big"
		.elseif %imm% & ~0xFFFF
			.if (%imm% & 0xFFFF8000) == 0xFFFF8000
				.if %lower%
					addiu	%rs%,r0, lo(%imm%)
				.endif
			.elseif (%imm% & 0xFFFF) == 0
				.if %upper%
					lui		%rs%, hi(%imm%)
				.elseif %lower%
					nop
				.endif
			.else
				.if %upper%
					lui		%rs%, hi(%imm%)
				.endif
				.if %lower%
					addiu 	%rs%, lo(%imm%)
				.endif
			.endif
		.else
			.if %lower%
				ori		%rs%,r0,%imm%
			.endif
		.endif
	)";

	// floats need to be treated as integers, convert them
	if (immediates.secondary.expression.isConstExpression())
	{
		ExpressionValue value = immediates.secondary.expression.evaluate();
		if (value.isFloat())
		{
			int32_t newValue = getFloatBits((float)value.floatValue);
			immediates.secondary.expression = createConstExpression(newValue);
		}
	}

	std::wstring macroText = preprocessMacro(templateLi,immediates);
	return createMacro(parser,macroText,flags, {
			{ L"%upper%",	(flags & MIPSM_UPPER) ? L"1" : L"0" },
			{ L"%lower%",	(flags & MIPSM_LOWER) ? L"1" : L"0" },
			{ L"%rs%",		registers.grs.name },
			{ L"%imm%",		immediates.secondary.expression.toString() },
	});
}

CAssemblerCommand* generateMipsMacroLoadStore(Parser& parser, MipsRegisterData& registers, MipsImmediateData& immediates, int flags)
{
	const wchar_t* templateLoadStore = LR"(
		.if %imm% & ~0xFFFFFFFF
			.error "Address too big"
		.elseif %imm% < 0x8000 || (%imm% & 0xFFFF8000) == 0xFFFF8000
			.if %lower%
				%op%	%rs%, lo(%imm%)(r0)
			.elseif %upper%
				nop
			.endif
		.else
			.if %upper%
				lui		%temp%, hi(%imm%)
			.endif
			.if %lower%
				%op%	%rs%, lo(%imm%)(%temp%)
			.endif
		.endif
	)";

	const wchar_t* op;
	bool isCop = false;
	switch (flags & (MIPSM_ACCESSMASK|MIPSM_LOAD|MIPSM_STORE))
	{
	case MIPSM_LOAD|MIPSM_B:		op = L"lb"; break;
	case MIPSM_LOAD|MIPSM_BU:		op = L"lbu"; break;
	case MIPSM_LOAD|MIPSM_HW:		op = L"lh"; break;
	case MIPSM_LOAD|MIPSM_HWU:		op = L"lhu"; break;
	case MIPSM_LOAD|MIPSM_W:		op = L"lw"; break;
	case MIPSM_LOAD|MIPSM_WU:		op = L"lwu"; break;
	case MIPSM_LOAD|MIPSM_DW:		op = L"ld"; break;
	case MIPSM_LOAD|MIPSM_LLSCW:	op = L"ll"; break;
	case MIPSM_LOAD|MIPSM_LLSCDW:	op = L"lld"; break;
	case MIPSM_LOAD|MIPSM_COP1:		op = L"lwc1"; isCop = true; break;
	case MIPSM_LOAD|MIPSM_COP2:		op = L"lwc2"; isCop = true; break;
	case MIPSM_LOAD|MIPSM_DCOP1:	op = L"ldc1"; isCop = true; break;
	case MIPSM_LOAD|MIPSM_DCOP2:	op = L"ldc2"; isCop = true; break;
	case MIPSM_STORE|MIPSM_B:		op = L"sb"; break;
	case MIPSM_STORE|MIPSM_HW:		op = L"sh"; break;
	case MIPSM_STORE|MIPSM_W:		op = L"sw"; break;
	case MIPSM_STORE|MIPSM_DW:		op = L"sd"; break;
	case MIPSM_STORE|MIPSM_LLSCW:	op = L"sc"; break;
	case MIPSM_STORE|MIPSM_LLSCDW:	op = L"scd"; break;
	case MIPSM_STORE|MIPSM_COP1:	op = L"swc1"; isCop = true; break;
	case MIPSM_STORE|MIPSM_COP2:	op = L"swc2"; isCop = true; break;
	case MIPSM_STORE|MIPSM_DCOP1:	op = L"sdc1"; isCop = true; break;
	case MIPSM_STORE|MIPSM_DCOP2:	op = L"sdc2"; isCop = true; break;
	default: return nullptr;
	}

	std::wstring macroText = preprocessMacro(templateLoadStore,immediates);

	bool store = (flags & MIPSM_STORE) != 0;
	return createMacro(parser,macroText,flags, {
			{ L"%upper%",	(flags & MIPSM_UPPER) ? L"1" : L"0" },
			{ L"%lower%",	(flags & MIPSM_LOWER) ? L"1" : L"0" },
			{ L"%rs%",		isCop ? registers.frs.name : registers.grs.name },
			{ L"%temp%",	isCop || store ? L"r1" : registers.grs.name },
			{ L"%imm%",		immediates.secondary.expression.toString() },
			{ L"%op%",		op },
	});
}

CAssemblerCommand* generateMipsMacroLoadUnaligned(Parser& parser, MipsRegisterData& registers, MipsImmediateData& immediates, int flags)
{
	const wchar_t* selectedTemplate;

	std::wstring op, size;
	int type = flags & MIPSM_ACCESSMASK;
	if (type == MIPSM_HW || type == MIPSM_HWU)
	{
		const wchar_t* templateHalfword = LR"(
			.if (%off% < 0x8000) && ((%off%+1) >= 0x8000)
				.error "Immediate offset too big"
			.else
				%op%	r1,%off%+1(%rs%)
				%op%	%rd%,%off%(%rs%)
				sll		r1,8
				or		%rd%,r1
			.endif
		)";

		op = type == MIPSM_HWU ? L"lbu" : L"lb";
		selectedTemplate = templateHalfword;
	} else if (type == MIPSM_W || type == MIPSM_DW)
	{
		const wchar_t* templateWord = LR"(
			.if (%off% < 0x8000) && ((%off%+%size%-1) >= 0x8000)
				.error "Immediate offset too big"
			.else
				%op%l	%rd%,%off%+%size%-1(%rs%)
				%op%r	%rd%,%off%(%rs%)
			.endif
		)";

		if (registers.grs.num == registers.grd.num)
		{
			Logger::printError(Logger::Error,L"Cannot use same register as source and destination");
			return new DummyCommand();
		}

		op = type == MIPSM_W ? L"lw" : L"ld";
		size = type == MIPSM_W ? L"4" : L"8";
		selectedTemplate = templateWord;
	} else {
		return nullptr;
	}

	std::wstring macroText = preprocessMacro(selectedTemplate,immediates);
	return createMacro(parser,macroText,flags, {
			{ L"%rs%",		registers.grs.name },
			{ L"%rd%",		registers.grd.name },
			{ L"%off%",		immediates.primary.expression.toString() },
			{ L"%op%",		op },
			{ L"%size%",    size },
	});
}

CAssemblerCommand* generateMipsMacroStoreUnaligned(Parser& parser, MipsRegisterData& registers, MipsImmediateData& immediates, int flags)
{
	const wchar_t* selectedTemplate;

	std::wstring op, size;
	int type = flags & MIPSM_ACCESSMASK;
	if (type == MIPSM_HW)
	{
		const wchar_t* templateHalfword = LR"(
			.if (%off% < 0x8000) && ((%off%+1) >= 0x8000)
				.error "Immediate offset too big"
			.else
				sb		%rd%,%off%(%rs%)
				srl		r1,%rd%,8
				sb		r1,%off%+1(%rs%)
			.endif
		)";

		selectedTemplate = templateHalfword;
	} else if (type == MIPSM_W || type == MIPSM_DW)
	{
		const wchar_t* templateWord = LR"(
			.if (%off% < 0x8000) && ((%off%+%size%-1) >= 0x8000)
				.error "Immediate offset too big"
			.else
				%op%l	%rd%,%off%+%size%-1(%rs%)
				%op%r	%rd%,%off%(%rs%)
			.endif
		)";

		if (registers.grs.num == registers.grd.num)
		{
			Logger::printError(Logger::Error,L"Cannot use same register as source and destination");
			return new DummyCommand();
		}

		op = type == MIPSM_W ? L"sw" : L"sd";
		size = type == MIPSM_W ? L"4" : L"8";
		selectedTemplate = templateWord;
	} else {
		return nullptr;
	}

	std::wstring macroText = preprocessMacro(selectedTemplate,immediates);
	return createMacro(parser,macroText,flags, {
			{ L"%rs%",		registers.grs.name },
			{ L"%rd%",		registers.grd.name },
			{ L"%off%",		immediates.primary.expression.toString() },
			{ L"%op%",		op },
			{ L"%size%",	size },
	});
}

CAssemblerCommand* generateMipsMacroBranch(Parser& parser, MipsRegisterData& registers, MipsImmediateData& immediates, int flags)
{
	const wchar_t* selectedTemplate;

	int type = flags & MIPSM_CONDITIONMASK;

	bool bne = type == MIPSM_NE;
	bool beq = type == MIPSM_EQ;
	bool beqz = type == MIPSM_GE || type == MIPSM_GEU;
	bool bnez = type == MIPSM_LT || type == MIPSM_LTU;
	bool unsigned_ = type == MIPSM_GEU || type == MIPSM_LTU;
	bool immediate = (flags & MIPSM_IMM) != 0;
	bool likely = (flags & MIPSM_LIKELY) != 0;
	bool revcmp = (flags & MIPSM_REVCMP) != 0;

	std::wstring op;
	if (bne || beq)
	{
		const wchar_t* templateNeEq = LR"(
			.if %imm% == 0
				%op%	%rs%,r0,%dest%
			.else
				li		r1,%imm%
				%op%	%rs%,r1,%dest%
			.endif
		)";

		selectedTemplate = templateNeEq;
		if(likely)
			op = bne ? L"bnel" : L"beql";
		else
			op = bne ? L"bne" : L"beq";
	} else if (immediate && (beqz || bnez))
	{
		const wchar_t* templateImmediate = LR"(
			.if %revcmp% && %imm% == 0
				slt%u% 	r1,r0,%rs%
			.elseif %revcmp%
				li		r1,%imm%
				slt%u%	r1,r1,%rs%
			.elseif (%imm% < -0x8000) || (%imm% >= 0x8000)
				li		r1,%imm%
				slt%u%	r1,%rs%,r1
			.else
				slti%u%	r1,%rs%,%imm%
			.endif
			%op%	r1,%dest%
		)";

		selectedTemplate = templateImmediate;
		if(likely)
			op = bnez ? L"bnezl" : L"beqzl";
		else
			op = bnez ? L"bnez" : L"beqz";
	} else if (beqz || bnez)
	{
		const wchar_t* templateRegister = LR"(
			.if %revcmp%
				slt%u%	r1,%rt%,%rs%
			.else
				slt%u%	r1,%rs%,%rt%
			.endif
			%op%	r1,%dest%
		)";

		selectedTemplate = templateRegister;
		if(likely)
			op = bnez ? L"bnezl" : L"beqzl";
		else
			op = bnez ? L"bnez" : L"beqz";
	} else {
		return nullptr;
	}
	
	std::wstring macroText = preprocessMacro(selectedTemplate,immediates);
	return createMacro(parser,macroText,flags, {
			{ L"%op%",		op },
			{ L"%u%",		unsigned_ ? L"u" : L""},
			{ L"%revcmp%",	revcmp ? L"1" : L"0"},
			{ L"%rs%",		registers.grs.name },
			{ L"%rt%",		registers.grt.name },
			{ L"%imm%",		immediates.primary.expression.toString() },
			{ L"%dest%",	immediates.secondary.expression.toString() },
	});
}

CAssemblerCommand* generateMipsMacroSet(Parser& parser, MipsRegisterData& registers, MipsImmediateData& immediates, int flags)
{
	const wchar_t* selectedTemplate;

	int type = flags & MIPSM_CONDITIONMASK;

	bool ne = type == MIPSM_NE;
	bool eq = type == MIPSM_EQ;
	bool ge = type == MIPSM_GE || type == MIPSM_GEU;
	bool lt = type == MIPSM_LT || type == MIPSM_LTU;
	bool unsigned_ = type == MIPSM_GEU || type == MIPSM_LTU;
	bool immediate = (flags & MIPSM_IMM) != 0;
	bool revcmp = (flags & MIPSM_REVCMP) != 0;

	if (immediate && (ne || eq))
	{
		const wchar_t* templateImmediateEqNe = LR"(
			.if %imm% & ~0xFFFF
				li		%rd%,%imm%
				xor		%rd%,%rs%,%rd%
			.else
				xori	%rd%,%rs%,%imm%
			.endif
			.if %eq%
				sltiu	%rd%,%rd%,1
			.else
				sltu	%rd%,r0,%rd%
			.endif
		)";

		selectedTemplate = templateImmediateEqNe;
	} else if (ne || eq)
	{
		const wchar_t* templateEqNe = LR"(
			xor		%rd%,%rs%,%rt%
			.if %eq%
				sltiu	%rd%,%rd%,1
			.else
				sltu	%rd%,r0,%rd%
			.endif
		)";

		selectedTemplate = templateEqNe;
	} else if (immediate && (ge || lt))
	{
		const wchar_t* templateImmediateGeLt = LR"(
			.if %revcmp% && %imm% == 0
				slt%u%	%rd%,r0,%rs%
			.elseif %revcmp%
				li		%rd%,%imm%
				slt%u%	%rd%,%rd%,%rs%
			.elseif (%imm% < -0x8000) || (%imm% >= 0x8000)
				li		%rd%,%imm%
				slt%u%	%rd%,%rs%,%rd%
			.else
				slti%u%	%rd%,%rs%,%imm%
			.endif
			.if %ge%
				xori	%rd%,%rd%,1
			.endif
		)";

		selectedTemplate = templateImmediateGeLt;
	} else if (ge)
	{
		const wchar_t* templateGe = LR"(
			.if %revcmp%
				slt%u%	%rd%,%rt%,%rs%
			.else
				slt%u%	%rd%,%rs%,%rt%
			.endif
			xori	%rd%,%rd%,1
		)";

		selectedTemplate = templateGe;
	} else
	{
		return nullptr;
	}

	std::wstring macroText = preprocessMacro(selectedTemplate,immediates);
	return createMacro(parser,macroText,flags, {
			{ L"%u%",		unsigned_ ? L"u" : L""},
			{ L"%eq%",		eq ? L"1" : L"0" },
			{ L"%ge%",		ge ? L"1" : L"0" },
			{ L"%revcmp%",	revcmp ? L"1" : L"0" },
			{ L"%rd%",		registers.grd.name },
			{ L"%rs%",		registers.grs.name },
			{ L"%rt%",		registers.grt.name },
			{ L"%imm%",		immediates.secondary.expression.toString() },
	});
}

CAssemblerCommand* generateMipsMacroRotate(Parser& parser, MipsRegisterData& registers, MipsImmediateData& immediates, int flags)
{
	bool left = (flags & MIPSM_LEFT) != 0;
	bool immediate = (flags & MIPSM_IMM) != 0;
	bool psp = Mips.GetVersion() == MARCH_PSP;

	const wchar_t* selectedTemplate;
	if (psp && immediate)
	{
		const wchar_t* templatePspImmediate = LR"(
			.if %amount% != 0
				.if %left%
					rotr	%rd%,%rs%,-%amount%&31
				.else
					rotr	%rd%,%rs%,%amount%
				.endif
			.else
				move	%rd%,%rs%
			.endif
		)";

		selectedTemplate = templatePspImmediate;
	} else if (psp)
	{
		const wchar_t* templatePspRegister = LR"(
			.if %left%
				negu	r1,%rt%
				rotrv	%rd%,%rs%,r1
			.else
				rotrv	%rd%,%rs%,%rt%
			.endif
		)";

		selectedTemplate = templatePspRegister;
	} else if (immediate)
	{
		const wchar_t* templateImmediate = LR"(
			.if %amount% != 0
				.if %left%
					srl	r1,%rs%,-%amount%&31
					sll	%rd%,%rs%,%amount%
				.else
					sll	r1,%rs%,-%amount%&31
					srl	%rd%,%rs%,%amount%
				.endif
				or		%rd%,%rd%,r1
			.else
				move	%rd%,%rs%
			.endif
		)";
		
		selectedTemplate = templateImmediate;
	} else {
		const wchar_t* templateRegister = LR"(
			negu	r1,%rt%
			.if %left%
				srlv	r1,%rs%,r1
				sllv	%rd%,%rs%,%rt%
			.else
				sllv	r1,%rs%,r1
				srlv	%rd%,%rs%,%rt%
			.endif
			or	%rd%,%rd%,r1
		)";

		selectedTemplate = templateRegister;
	}
	
	std::wstring macroText = preprocessMacro(selectedTemplate,immediates);
	return createMacro(parser,macroText,flags, {
			{ L"%left%",	left ? L"1" : L"0" },
			{ L"%rd%",		registers.grd.name },
			{ L"%rs%",		registers.grs.name },
			{ L"%rt%",		registers.grt.name },
			{ L"%amount%",	immediates.primary.expression.toString() },
	});
}

/* Placeholders
	i = i1 = 16 bit immediate
	I = i2 = 32 bit immediate
	s,t,d = registers */
const MipsMacroDefinition mipsMacros[] = {
	{ L"abs",	L"d,s",		&generateMipsMacroAbs,				MIPSM_W },
	{ L"dabs",	L"d,s",		&generateMipsMacroAbs,				MIPSM_DW },

	{ L"li",	L"s,I",		&generateMipsMacroLi,				MIPSM_IMM|MIPSM_UPPER|MIPSM_LOWER },
	{ L"li.u",	L"s,I",		&generateMipsMacroLi,				MIPSM_IMM|MIPSM_UPPER },
	{ L"li.l",	L"s,I",		&generateMipsMacroLi,				MIPSM_IMM|MIPSM_LOWER },
	{ L"la",	L"s,I",		&generateMipsMacroLi,				MIPSM_IMM|MIPSM_UPPER|MIPSM_LOWER },
	{ L"la.u",	L"s,I",		&generateMipsMacroLi,				MIPSM_IMM|MIPSM_UPPER },
	{ L"la.l",	L"s,I",		&generateMipsMacroLi,				MIPSM_IMM|MIPSM_LOWER },

	{ L"li.s",	L"S,I",		&generateMipsMacroLiFloat,			MIPSM_IMM },

	{ L"lb",	L"s,I",		&generateMipsMacroLoadStore,		MIPSM_LOAD|MIPSM_B|MIPSM_UPPER|MIPSM_LOWER },
	{ L"lbu",	L"s,I",		&generateMipsMacroLoadStore,		MIPSM_LOAD|MIPSM_BU|MIPSM_UPPER|MIPSM_LOWER },
	{ L"lh",	L"s,I",		&generateMipsMacroLoadStore,		MIPSM_LOAD|MIPSM_HW|MIPSM_UPPER|MIPSM_LOWER },
	{ L"lhu",	L"s,I",		&generateMipsMacroLoadStore,		MIPSM_LOAD|MIPSM_HWU|MIPSM_UPPER|MIPSM_LOWER },
	{ L"lw",	L"s,I",		&generateMipsMacroLoadStore,		MIPSM_LOAD|MIPSM_W|MIPSM_UPPER|MIPSM_LOWER },
	{ L"lwu",	L"s,I",		&generateMipsMacroLoadStore,		MIPSM_LOAD|MIPSM_WU|MIPSM_UPPER|MIPSM_LOWER },
	{ L"ld",    L"s,I",		&generateMipsMacroLoadStore,		MIPSM_LOAD|MIPSM_DW|MIPSM_UPPER|MIPSM_LOWER },
	{ L"ll",	L"s,I",		&generateMipsMacroLoadStore,		MIPSM_LOAD|MIPSM_LLSCW|MIPSM_UPPER|MIPSM_LOWER },
	{ L"lld",	L"s,I",		&generateMipsMacroLoadStore,		MIPSM_LOAD|MIPSM_LLSCDW|MIPSM_UPPER|MIPSM_LOWER },
	{ L"lwc1",	L"S,I",		&generateMipsMacroLoadStore,		MIPSM_LOAD|MIPSM_COP1|MIPSM_UPPER|MIPSM_LOWER },
	{ L"l.s",	L"S,I",		&generateMipsMacroLoadStore,		MIPSM_LOAD|MIPSM_COP1|MIPSM_UPPER|MIPSM_LOWER },
	{ L"lwc2",	L"S,I",		&generateMipsMacroLoadStore,		MIPSM_LOAD|MIPSM_COP2|MIPSM_UPPER|MIPSM_LOWER },
	{ L"ldc1",	L"S,I",		&generateMipsMacroLoadStore,		MIPSM_LOAD|MIPSM_DCOP1|MIPSM_UPPER|MIPSM_LOWER },
	{ L"l.d",	L"S,I",		&generateMipsMacroLoadStore,		MIPSM_LOAD|MIPSM_DCOP1|MIPSM_UPPER|MIPSM_LOWER },
	{ L"ldc2",	L"S,I",		&generateMipsMacroLoadStore,		MIPSM_LOAD|MIPSM_DCOP2|MIPSM_UPPER|MIPSM_LOWER },

	{ L"lb.u",	L"s,I",		&generateMipsMacroLoadStore,		MIPSM_LOAD|MIPSM_B|MIPSM_UPPER },
	{ L"lbu.u",	L"s,I",		&generateMipsMacroLoadStore,		MIPSM_LOAD|MIPSM_BU|MIPSM_UPPER },
	{ L"lh.u",	L"s,I",		&generateMipsMacroLoadStore,		MIPSM_LOAD|MIPSM_HW|MIPSM_UPPER },
	{ L"lhu.u",	L"s,I",		&generateMipsMacroLoadStore,		MIPSM_LOAD|MIPSM_HWU|MIPSM_UPPER },
	{ L"lw.u",	L"s,I",		&generateMipsMacroLoadStore,		MIPSM_LOAD|MIPSM_W|MIPSM_UPPER },
	{ L"lwu.u",	L"s,I",		&generateMipsMacroLoadStore,		MIPSM_LOAD|MIPSM_WU|MIPSM_UPPER },
	{ L"ld.u",  L"s,I",		&generateMipsMacroLoadStore,		MIPSM_LOAD|MIPSM_DW|MIPSM_UPPER },
	{ L"ll.u",	L"s,I",		&generateMipsMacroLoadStore,		MIPSM_LOAD|MIPSM_LLSCW|MIPSM_UPPER },
	{ L"lld.u",	L"s,I",		&generateMipsMacroLoadStore,		MIPSM_LOAD|MIPSM_LLSCDW|MIPSM_UPPER },
	{ L"lwc1.u",L"S,I",		&generateMipsMacroLoadStore,		MIPSM_LOAD|MIPSM_COP1|MIPSM_UPPER },
	{ L"l.s.u",	L"S,I",		&generateMipsMacroLoadStore,		MIPSM_LOAD|MIPSM_COP1|MIPSM_UPPER },
	{ L"lwc2.u",L"S,I",		&generateMipsMacroLoadStore,		MIPSM_LOAD|MIPSM_COP2|MIPSM_UPPER },
	{ L"ldc1.u",L"S,I",		&generateMipsMacroLoadStore,		MIPSM_LOAD|MIPSM_DCOP1|MIPSM_UPPER },
	{ L"l.d.u",	L"S,I",		&generateMipsMacroLoadStore,		MIPSM_LOAD|MIPSM_DCOP1|MIPSM_UPPER },
	{ L"ldc2.u",L"S,I",		&generateMipsMacroLoadStore,		MIPSM_LOAD|MIPSM_DCOP2|MIPSM_UPPER },

	{ L"lb.l",	L"s,I",		&generateMipsMacroLoadStore,		MIPSM_LOAD|MIPSM_B|MIPSM_LOWER },
	{ L"lbu.l",	L"s,I",		&generateMipsMacroLoadStore,		MIPSM_LOAD|MIPSM_BU|MIPSM_LOWER },
	{ L"lh.l",	L"s,I",		&generateMipsMacroLoadStore,		MIPSM_LOAD|MIPSM_HW|MIPSM_LOWER },
	{ L"lhu.l",	L"s,I",		&generateMipsMacroLoadStore,		MIPSM_LOAD|MIPSM_HWU|MIPSM_LOWER },
	{ L"lw.l",	L"s,I",		&generateMipsMacroLoadStore,		MIPSM_LOAD|MIPSM_W|MIPSM_LOWER },
	{ L"lwu.l",	L"s,I",		&generateMipsMacroLoadStore,		MIPSM_LOAD|MIPSM_WU|MIPSM_LOWER },
	{ L"ld.l",	L"s,I",		&generateMipsMacroLoadStore,		MIPSM_LOAD|MIPSM_DW|MIPSM_LOWER },
	{ L"ll.l",	L"s,I",		&generateMipsMacroLoadStore,		MIPSM_LOAD|MIPSM_LLSCW|MIPSM_LOWER },
	{ L"lld.l",	L"s,I",		&generateMipsMacroLoadStore,		MIPSM_LOAD|MIPSM_LLSCDW|MIPSM_LOWER },
	{ L"lwc1.l",L"S,I",		&generateMipsMacroLoadStore,		MIPSM_LOAD|MIPSM_COP1|MIPSM_LOWER },
	{ L"l.s.l",	L"S,I",		&generateMipsMacroLoadStore,		MIPSM_LOAD|MIPSM_COP1|MIPSM_LOWER },
	{ L"lwc2.l",L"S,I",		&generateMipsMacroLoadStore,		MIPSM_LOAD|MIPSM_COP2|MIPSM_LOWER },
	{ L"ldc1.l",L"S,I",		&generateMipsMacroLoadStore,		MIPSM_LOAD|MIPSM_DCOP1|MIPSM_LOWER },
	{ L"l.d.l",	L"S,I",		&generateMipsMacroLoadStore,		MIPSM_LOAD|MIPSM_DCOP1|MIPSM_LOWER },
	{ L"ldc2.l",L"S,I",		&generateMipsMacroLoadStore,		MIPSM_LOAD|MIPSM_DCOP2|MIPSM_LOWER },

	{ L"ulh",	L"d,i(s)",	&generateMipsMacroLoadUnaligned,	MIPSM_HW|MIPSM_IMM },
	{ L"ulh",	L"d,(s)",	&generateMipsMacroLoadUnaligned,	MIPSM_HW },
	{ L"ulhu",	L"d,i(s)",	&generateMipsMacroLoadUnaligned,	MIPSM_HWU|MIPSM_IMM },
	{ L"ulhu",	L"d,(s)",	&generateMipsMacroLoadUnaligned,	MIPSM_HWU },
	{ L"ulw",	L"d,i(s)",	&generateMipsMacroLoadUnaligned,	MIPSM_W|MIPSM_IMM },
	{ L"ulw",	L"d,(s)",	&generateMipsMacroLoadUnaligned,	MIPSM_W },
	{ L"uld",	L"d,i(s)",	&generateMipsMacroLoadUnaligned,	MIPSM_DW|MIPSM_IMM },
	{ L"uld",	L"d,(s)",	&generateMipsMacroLoadUnaligned,	MIPSM_DW },

	{ L"sb",	L"s,I",		&generateMipsMacroLoadStore,		MIPSM_STORE|MIPSM_B|MIPSM_UPPER|MIPSM_LOWER },
	{ L"sh",	L"s,I",		&generateMipsMacroLoadStore,		MIPSM_STORE|MIPSM_HW|MIPSM_UPPER|MIPSM_LOWER },
	{ L"sw",	L"s,I",		&generateMipsMacroLoadStore,		MIPSM_STORE|MIPSM_W|MIPSM_UPPER|MIPSM_LOWER },
	{ L"sd",	L"s,I",		&generateMipsMacroLoadStore,		MIPSM_STORE|MIPSM_DW|MIPSM_UPPER|MIPSM_LOWER },
	{ L"sc",	L"s,I",		&generateMipsMacroLoadStore,		MIPSM_STORE|MIPSM_LLSCW|MIPSM_UPPER|MIPSM_LOWER },
	{ L"scd",	L"s,I",		&generateMipsMacroLoadStore,		MIPSM_STORE|MIPSM_LLSCDW|MIPSM_UPPER|MIPSM_LOWER },
	{ L"swc1",	L"S,I",		&generateMipsMacroLoadStore,		MIPSM_STORE|MIPSM_COP1|MIPSM_UPPER|MIPSM_LOWER },
	{ L"s.s",	L"S,I",		&generateMipsMacroLoadStore,		MIPSM_STORE|MIPSM_COP1|MIPSM_UPPER|MIPSM_LOWER },
	{ L"swc2",	L"S,I",		&generateMipsMacroLoadStore,		MIPSM_STORE|MIPSM_COP2|MIPSM_UPPER|MIPSM_LOWER },
	{ L"sdc1",	L"S,I",		&generateMipsMacroLoadStore,		MIPSM_STORE|MIPSM_DCOP1|MIPSM_UPPER|MIPSM_LOWER },
	{ L"s.d",	L"S,I",		&generateMipsMacroLoadStore,		MIPSM_STORE|MIPSM_DCOP1|MIPSM_UPPER|MIPSM_LOWER },
	{ L"sdc2",	L"S,I",		&generateMipsMacroLoadStore,		MIPSM_STORE|MIPSM_DCOP2|MIPSM_UPPER|MIPSM_LOWER },

	{ L"sb.u",	L"s,I",		&generateMipsMacroLoadStore,		MIPSM_STORE|MIPSM_B|MIPSM_UPPER },
	{ L"sh.u",	L"s,I",		&generateMipsMacroLoadStore,		MIPSM_STORE|MIPSM_HW|MIPSM_UPPER },
	{ L"sw.u",	L"s,I",		&generateMipsMacroLoadStore,		MIPSM_STORE|MIPSM_W|MIPSM_UPPER },
	{ L"sd.u",	L"s,I",		&generateMipsMacroLoadStore,		MIPSM_STORE|MIPSM_DW|MIPSM_UPPER },
	{ L"sc.u",	L"s,I",		&generateMipsMacroLoadStore,		MIPSM_STORE|MIPSM_LLSCW|MIPSM_UPPER },
	{ L"scd.u",	L"s,I",		&generateMipsMacroLoadStore,		MIPSM_STORE|MIPSM_LLSCDW|MIPSM_UPPER },
	{ L"swc1.u",L"S,I",		&generateMipsMacroLoadStore,		MIPSM_STORE|MIPSM_COP1|MIPSM_UPPER },
	{ L"s.s.u",	L"S,I",		&generateMipsMacroLoadStore,		MIPSM_STORE|MIPSM_COP1|MIPSM_UPPER },
	{ L"swc2.u",L"S,I",		&generateMipsMacroLoadStore,		MIPSM_STORE|MIPSM_COP2|MIPSM_UPPER },
	{ L"sdc1.u",L"S,I",		&generateMipsMacroLoadStore,		MIPSM_STORE|MIPSM_DCOP1|MIPSM_UPPER },
	{ L"s.d.u",	L"S,I",		&generateMipsMacroLoadStore,		MIPSM_STORE|MIPSM_DCOP1|MIPSM_UPPER },
	{ L"sdc2.u",L"S,I",		&generateMipsMacroLoadStore,		MIPSM_STORE|MIPSM_DCOP2|MIPSM_UPPER },

	{ L"sb.l",	L"s,I",		&generateMipsMacroLoadStore,		MIPSM_STORE|MIPSM_B|MIPSM_LOWER },
	{ L"sh.l",	L"s,I",		&generateMipsMacroLoadStore,		MIPSM_STORE|MIPSM_HW|MIPSM_LOWER },
	{ L"sw.l",	L"s,I",		&generateMipsMacroLoadStore,		MIPSM_STORE|MIPSM_W|MIPSM_LOWER },
	{ L"sd.l",	L"s,I",		&generateMipsMacroLoadStore,		MIPSM_STORE|MIPSM_DW|MIPSM_LOWER },
	{ L"sc.l",	L"s,I",		&generateMipsMacroLoadStore,		MIPSM_STORE|MIPSM_LLSCW|MIPSM_LOWER },
	{ L"scd.l",	L"s,I",		&generateMipsMacroLoadStore,		MIPSM_STORE|MIPSM_LLSCDW|MIPSM_LOWER },
	{ L"swc1.l",L"S,I",		&generateMipsMacroLoadStore,		MIPSM_STORE|MIPSM_COP1|MIPSM_LOWER },
	{ L"s.s.l",	L"S,I",		&generateMipsMacroLoadStore,		MIPSM_STORE|MIPSM_COP1|MIPSM_LOWER },
	{ L"swc2.l",L"S,I",		&generateMipsMacroLoadStore,		MIPSM_STORE|MIPSM_COP2|MIPSM_LOWER },
	{ L"sdc1.l",L"S,I",		&generateMipsMacroLoadStore,		MIPSM_STORE|MIPSM_DCOP1|MIPSM_LOWER },
	{ L"s.d.l",	L"S,I",		&generateMipsMacroLoadStore,		MIPSM_STORE|MIPSM_DCOP1|MIPSM_LOWER },
	{ L"sdc2.l",L"S,I",		&generateMipsMacroLoadStore,		MIPSM_STORE|MIPSM_DCOP2|MIPSM_LOWER },

	{ L"ush",	L"d,i(s)",	&generateMipsMacroStoreUnaligned,	MIPSM_HW|MIPSM_IMM },
	{ L"ush",	L"d,(s)",	&generateMipsMacroStoreUnaligned,	MIPSM_HW },
	{ L"usw",	L"d,i(s)",	&generateMipsMacroStoreUnaligned,	MIPSM_W|MIPSM_IMM },
	{ L"usw",	L"d,(s)",	&generateMipsMacroStoreUnaligned,	MIPSM_W },
	{ L"usd",	L"d,i(s)",	&generateMipsMacroStoreUnaligned,	MIPSM_DW|MIPSM_IMM },
	{ L"usd",	L"d,(s)",	&generateMipsMacroStoreUnaligned,	MIPSM_DW },

	{ L"blt",	L"s,t,I",	&generateMipsMacroBranch,			MIPSM_LT|MIPSM_DONTWARNDELAYSLOT },
	{ L"blt",	L"s,i,I",	&generateMipsMacroBranch,			MIPSM_LT|MIPSM_IMM|MIPSM_DONTWARNDELAYSLOT },
	{ L"bgt",	L"s,t,I",	&generateMipsMacroBranch,			MIPSM_LT|MIPSM_REVCMP|MIPSM_DONTWARNDELAYSLOT },
	{ L"bgt",	L"s,i,I",	&generateMipsMacroBranch,			MIPSM_LT|MIPSM_IMM|MIPSM_REVCMP|MIPSM_DONTWARNDELAYSLOT },
	{ L"bltu",	L"s,t,I",	&generateMipsMacroBranch,			MIPSM_LTU|MIPSM_DONTWARNDELAYSLOT },
	{ L"bltu",	L"s,i,I",	&generateMipsMacroBranch,			MIPSM_LTU|MIPSM_IMM|MIPSM_DONTWARNDELAYSLOT },
	{ L"bgtu",	L"s,t,I",	&generateMipsMacroBranch,			MIPSM_LTU|MIPSM_REVCMP|MIPSM_DONTWARNDELAYSLOT },
	{ L"bgtu",	L"s,i,I",	&generateMipsMacroBranch,			MIPSM_LTU|MIPSM_IMM|MIPSM_REVCMP|MIPSM_DONTWARNDELAYSLOT },
	{ L"bge",	L"s,t,I",	&generateMipsMacroBranch,			MIPSM_GE|MIPSM_DONTWARNDELAYSLOT },
	{ L"bge",	L"s,i,I",	&generateMipsMacroBranch,			MIPSM_GE|MIPSM_IMM|MIPSM_DONTWARNDELAYSLOT },
	{ L"ble",	L"s,t,I",	&generateMipsMacroBranch,			MIPSM_GE|MIPSM_REVCMP|MIPSM_DONTWARNDELAYSLOT },
	{ L"ble",	L"s,i,I",	&generateMipsMacroBranch,			MIPSM_GE|MIPSM_IMM|MIPSM_REVCMP|MIPSM_DONTWARNDELAYSLOT },
	{ L"bgeu",	L"s,t,I",	&generateMipsMacroBranch,			MIPSM_GEU|MIPSM_DONTWARNDELAYSLOT },
	{ L"bgeu",	L"s,i,I",	&generateMipsMacroBranch,			MIPSM_GEU|MIPSM_IMM|MIPSM_DONTWARNDELAYSLOT },
	{ L"bleu",	L"s,t,I",	&generateMipsMacroBranch,			MIPSM_GEU|MIPSM_REVCMP|MIPSM_DONTWARNDELAYSLOT },
	{ L"bleu",	L"s,i,I",	&generateMipsMacroBranch,			MIPSM_GEU|MIPSM_IMM|MIPSM_REVCMP|MIPSM_DONTWARNDELAYSLOT },
	{ L"bne",	L"s,i,I",	&generateMipsMacroBranch,			MIPSM_NE|MIPSM_IMM|MIPSM_DONTWARNDELAYSLOT },
	{ L"beq",	L"s,i,I",	&generateMipsMacroBranch,			MIPSM_EQ|MIPSM_IMM|MIPSM_DONTWARNDELAYSLOT },
	{ L"bltl",	L"s,t,I",	&generateMipsMacroBranch,			MIPSM_LT|MIPSM_DONTWARNDELAYSLOT|MIPSM_LIKELY },
	{ L"bltl",	L"s,i,I",	&generateMipsMacroBranch,			MIPSM_LT|MIPSM_IMM|MIPSM_DONTWARNDELAYSLOT|MIPSM_LIKELY },
	{ L"bgtl",	L"s,t,I",	&generateMipsMacroBranch,			MIPSM_LT|MIPSM_REVCMP|MIPSM_DONTWARNDELAYSLOT|MIPSM_LIKELY },
	{ L"bgtl",	L"s,i,I",	&generateMipsMacroBranch,			MIPSM_LT|MIPSM_IMM|MIPSM_REVCMP|MIPSM_DONTWARNDELAYSLOT|MIPSM_LIKELY },
	{ L"bltul",	L"s,t,I",	&generateMipsMacroBranch,			MIPSM_LTU|MIPSM_DONTWARNDELAYSLOT|MIPSM_LIKELY },
	{ L"bltul",	L"s,i,I",	&generateMipsMacroBranch,			MIPSM_LTU|MIPSM_IMM|MIPSM_DONTWARNDELAYSLOT|MIPSM_LIKELY },
	{ L"bgtul",	L"s,t,I",	&generateMipsMacroBranch,			MIPSM_LTU|MIPSM_REVCMP|MIPSM_DONTWARNDELAYSLOT|MIPSM_LIKELY },
	{ L"bgtul",	L"s,i,I",	&generateMipsMacroBranch,			MIPSM_LTU|MIPSM_IMM|MIPSM_REVCMP|MIPSM_DONTWARNDELAYSLOT|MIPSM_LIKELY },
	{ L"bgel",	L"s,t,I",	&generateMipsMacroBranch,			MIPSM_GE|MIPSM_DONTWARNDELAYSLOT|MIPSM_LIKELY },
	{ L"bgel",	L"s,i,I",	&generateMipsMacroBranch,			MIPSM_GE|MIPSM_IMM|MIPSM_DONTWARNDELAYSLOT|MIPSM_LIKELY },
	{ L"blel",	L"s,t,I",	&generateMipsMacroBranch,			MIPSM_GE|MIPSM_REVCMP|MIPSM_DONTWARNDELAYSLOT|MIPSM_LIKELY },
	{ L"blel",	L"s,i,I",	&generateMipsMacroBranch,			MIPSM_GE|MIPSM_IMM|MIPSM_REVCMP|MIPSM_DONTWARNDELAYSLOT|MIPSM_LIKELY },
	{ L"bgeul",	L"s,t,I",	&generateMipsMacroBranch,			MIPSM_GEU|MIPSM_DONTWARNDELAYSLOT|MIPSM_LIKELY },
	{ L"bgeul",	L"s,i,I",	&generateMipsMacroBranch,			MIPSM_GEU|MIPSM_IMM|MIPSM_DONTWARNDELAYSLOT|MIPSM_LIKELY },
	{ L"bleul",	L"s,t,I",	&generateMipsMacroBranch,			MIPSM_GEU|MIPSM_REVCMP|MIPSM_DONTWARNDELAYSLOT|MIPSM_LIKELY },
	{ L"bleul",	L"s,i,I",	&generateMipsMacroBranch,			MIPSM_GEU|MIPSM_IMM|MIPSM_REVCMP|MIPSM_DONTWARNDELAYSLOT|MIPSM_LIKELY },
	{ L"bnel",	L"s,i,I",	&generateMipsMacroBranch,			MIPSM_NE|MIPSM_IMM|MIPSM_DONTWARNDELAYSLOT|MIPSM_LIKELY },
	{ L"beql",	L"s,i,I",	&generateMipsMacroBranch,			MIPSM_EQ|MIPSM_IMM|MIPSM_DONTWARNDELAYSLOT|MIPSM_LIKELY },

	{ L"slt",	L"d,s,I",	&generateMipsMacroSet,				MIPSM_LT|MIPSM_IMM },
	{ L"sltu",	L"d,s,I",	&generateMipsMacroSet,				MIPSM_LTU|MIPSM_IMM },
	{ L"sgt",	L"d,s,I",	&generateMipsMacroSet,				MIPSM_LT|MIPSM_IMM|MIPSM_REVCMP },
	{ L"sgtu",	L"d,s,I",	&generateMipsMacroSet,				MIPSM_LTU|MIPSM_IMM|MIPSM_REVCMP },
	{ L"sge",	L"d,s,t",	&generateMipsMacroSet,				MIPSM_GE },
	{ L"sge",	L"d,s,I",	&generateMipsMacroSet,				MIPSM_GE|MIPSM_IMM },
	{ L"sle",	L"d,s,t",	&generateMipsMacroSet,				MIPSM_GE|MIPSM_REVCMP },
	{ L"sle",	L"d,s,I",	&generateMipsMacroSet,				MIPSM_GE|MIPSM_IMM|MIPSM_REVCMP },
	{ L"sgeu",	L"d,s,t",	&generateMipsMacroSet,				MIPSM_GEU },
	{ L"sgeu",	L"d,s,I",	&generateMipsMacroSet,				MIPSM_GEU|MIPSM_IMM },
	{ L"sleu",	L"d,s,t",	&generateMipsMacroSet,				MIPSM_GEU|MIPSM_REVCMP },
	{ L"sleu",	L"d,s,I",	&generateMipsMacroSet,				MIPSM_GEU|MIPSM_IMM|MIPSM_REVCMP },
	{ L"sne",	L"d,s,t",	&generateMipsMacroSet,				MIPSM_NE },
	{ L"sne",	L"d,s,I",	&generateMipsMacroSet,				MIPSM_NE|MIPSM_IMM },
	{ L"seq",	L"d,s,t",	&generateMipsMacroSet,				MIPSM_EQ },
	{ L"seq",	L"d,s,I",	&generateMipsMacroSet,				MIPSM_EQ|MIPSM_IMM },

	{ L"rol",	L"d,s,t",	&generateMipsMacroRotate,			MIPSM_LEFT },
	{ L"rol",	L"d,s,i",	&generateMipsMacroRotate,			MIPSM_LEFT|MIPSM_IMM },
	{ L"ror",	L"d,s,t",	&generateMipsMacroRotate,			MIPSM_RIGHT },
	{ L"ror",	L"d,s,i",	&generateMipsMacroRotate,			MIPSM_RIGHT|MIPSM_IMM },

	{ NULL,		NULL,		NULL,								0 }
};
