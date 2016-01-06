#include <sstream>

#include "Common/StringUtils.h"
#include "Common/Vulkan/SPIRVDisasm.h"
#include "ext/vulkan/spirv.hpp"

#define WRITE p+=sprintf

struct SpirvID {
	std::string name;
	int opcode;
};

struct OpInfo {
	const char *name;
	const char *extra;
};

static const char *executionModelNames[] = {
	"Vertex",  // 0,
	"TessellationControl",  // 1,
	"TessellationEvaluation",  // 2,
	"Geometry",  // 3,
	"Fragment",  // 4,
	"GLCompute",  // 5,
	"Kernel",  // 6,
};

static const char *storageClassNames[] = {
	"UniformConstant",
	"Input",
	"Uniform",
	"Output",
	"Workgroup",
	"CrossWorkgroup",
	"Private",
	"Function",
	"Generic",
	"PushConstant",
	"AtomicCounter",
	"Image",
};

static const char *decorationNames[] = {
	"RelaxedPrecision",  // 0,
	"SpecId",  // 1,
	"Block",  // 2,
	"BufferBlock",  // 3,
	"RowMajor",  // 4,
	"ColMajor",  // 5,
	"ArrayStride",  // 6,
	"MatrixStride",  // 7,
	"GLSLShared",  // 8,
	"GLSLPacked",  // 9,
	"CPacked",  // 10,
	"BuiltIn",  // 11,
	nullptr,  // 12
	"NoPerspective",  // 13,
	"Flat",  // 14,
	"Patch",  // 15,
	"Centroid",  // 16,
	"Sample",  // 17,
	"Invariant",  // 18,
	"Restrict",  // 19,
	"Aliased",  // 20,
	"Volatile",  // 21,
	"Constant",  // 22,
	"Coherent",  // 23,
	"NonWritable",  // 24,
	"NonReadable",  // 25,
	"Uniform",  // 26,
	nullptr,  // 27
	"SaturatedConversion",  // 28,
	"Stream",  // 29,
	"Location",  // 30,
	"Component",  // 31,
	"Index",  // 32,
	"Binding",  // 33,
	"DescriptorSet",  // 34,
	"Offset",  // 35,
	"XfbBuffer",  // 36,
	"XfbStride",  // 37,
	"FuncParamAttr",  // 38,
	"FPRoundingMode",  // 39,
	"FPFastMathMode",  // 40,
	"LinkageAttributes",  // 41,
	"NoContraction",  // 42,
	"InputAttachmentIndex",  // 43,
	"Alignment",  // 44,
};

static const OpInfo opInfo[] = {
	{"Nop"}, // 0,
	{"Undef"}, // 1,
	{"SourceContinued"}, // 2,
	{"Source"}, // 3,
	{"SourceExtension"}, // 4,
	{"Name"}, // 5,
	{"MemberName"}, // 6,
	{"String"}, // 7,
	{"Line"}, // 8,
	{nullptr},  // 9
	{"Extension"}, // 10,
	{"ExtInstImport"}, // 11,
	{"ExtInst"}, // 12,
	{nullptr},  // 13
	{"MemoryModel"}, // 14,
	{"EntryPoint"}, // 15,
	{"ExecutionMode"}, // 16,
	{"Capability"}, // 17,
	{nullptr},  // 18
	{"TypeVoid"}, // 19,
	{"TypeBool"}, // 20,
	{"TypeInt"}, // 21,
	{"TypeFloat"}, // 22,
	{"TypeVector"}, // 23,
	{"TypeMatrix"}, // 24,
	{"TypeImage"}, // 25,
	{"TypeSampler"}, // 26,
	{"TypeSampledImage"}, // 27,
	{"TypeArray"}, // 28,
	{"TypeRuntimeArray"}, // 29,
	{"TypeStruct"}, // 30,
	{"TypeOpaque"}, // 31,
	{"TypePointer"}, // 32,
	{"TypeFunction"}, // 33,
	{"TypeEvent"}, // 34,
	{"TypeDeviceEvent"}, // 35,
	{"TypeReserveId"}, // 36,
	{"TypeQueue"}, // 37,
	{"TypePipe"}, // 38,
	{"TypeForwardPointer"}, // 39,
	{nullptr},  // 40
	{"ConstantTrue"}, // 41,
	{"ConstantFalse"}, // 42,
	{"Constant"}, // 43,
	{"ConstantComposite"}, // 44,
	{"ConstantSampler"}, // 45,
	{"ConstantNull"}, // 46,
	{nullptr},  // 47
	{"SpecConstantTrue"}, // 48,
	{"SpecConstantFalse"}, // 49,
	{"SpecConstant"}, // 50,
	{"SpecConstantComposite"}, // 51,
	{"SpecConstantOp"}, // 52,
	{nullptr},  // 53
	{"Function"}, // 54,
	{"FunctionParameter"}, // 55,
	{"FunctionEnd"}, // 56,
	{"FunctionCall"}, // 57,
	{nullptr},  // 58
	{"Variable"}, // 59,
	{"ImageTexelPointer"}, // 60,
	{"Load"}, // 61,
	{"Store"}, // 62,
	{"CopyMemory"}, // 63,
	{"CopyMemorySized"}, // 64,
	{"AccessChain"}, // 65,
	{"InBoundsAccessChain"}, // 66,
	{"PtrAccessChain"}, // 67,
	{"ArrayLength"}, // 68,
	{"GenericPtrMemSemantics"}, // 69,
	{"InBoundsPtrAccessChain"}, // 70,
	{"Decorate"}, // 71,
	{"MemberDecorate"}, // 72,
	{"DecorationGroup"}, // 73,
	{"GroupDecorate"}, // 74,
	{"GroupMemberDecorate"}, // 75,
	{nullptr},  // 76
	{"VectorExtractDynamic"}, // 77,
	{"VectorInsertDynamic"}, // 78,
	{"VectorShuffle"}, // 79,
	{"CompositeConstruct"}, // 80,
	{"CompositeExtract"}, // 81,
	{"CompositeInsert"}, // 82,
	{"CopyObject"}, // 83,
	{"Transpose"}, // 84,
	{nullptr},  // 85
	{"SampledImage"}, // 86,
	{"ImageSampleImplicitLod"}, // 87,
	{"ImageSampleExplicitLod"}, // 88,
	{"ImageSampleDrefImplicitLod"}, // 89,
	{"ImageSampleDrefExplicitLod"}, // 90,
	{"ImageSampleProjImplicitLod"}, // 91,
	{"ImageSampleProjExplicitLod"}, // 92,
	{"ImageSampleProjDrefImplicitLod"}, // 93,
	{"ImageSampleProjDrefExplicitLod"}, // 94,
	{"ImageFetch"}, // 95,
	{"ImageGather"}, // 96,
	{"ImageDrefGather"}, // 97,
	{"ImageRead"}, // 98,
	{"ImageWrite"}, // 99,
	{"Image"}, // 100,
	{"ImageQueryFormat"}, // 101,
	{"ImageQueryOrder"}, // 102,
	{"ImageQuerySizeLod"}, // 103,
	{"ImageQuerySize"}, // 104,
	{"ImageQueryLod"}, // 105,
	{"ImageQueryLevels"}, // 106,
	{"ImageQuerySamples"}, // 107,
	{nullptr},  // 108
	{"ConvertFToU"}, // 109,
	{"ConvertFToS"}, // 110,
	{"ConvertSToF"}, // 111,
	{"ConvertUToF"}, // 112,
	{"UConvert"}, // 113,
	{"SConvert"}, // 114,
	{"FConvert"}, // 115,
	{"QuantizeToF16"}, // 116,
	{"ConvertPtrToU"}, // 117,
	{"SatConvertSToU"}, // 118,
	{"SatConvertUToS"}, // 119,
	{"ConvertUToPtr"}, // 120,
	{"PtrCastToGeneric"}, // 121,
	{"GenericCastToPtr"}, // 122,
	{"GenericCastToPtrExplicit"}, // 123,
	{"Bitcast"}, // 124,
	{nullptr},  // 125
	{"SNegate"}, // 126,
	{"FNegate"}, // 127,
	{"IAdd"}, // 128,
	{"FAdd", "+"}, // 129,
	{"ISub"}, // 130,
	{"FSub", "-"}, // 131,
	{"IMul"}, // 132,
	{"FMul", "*"}, // 133,
	{"UDiv"}, // 134,
	{"SDiv"}, // 135,
	{"FDiv", "/"}, // 136,
	{"UMod"}, // 137,
	{"SRem"}, // 138,
	{"SMod"}, // 139,
	{"FRem"}, // 140,
	{"FMod", "%"}, // 141,
	{"VectorTimesScalar"}, // 142,
	{"MatrixTimesScalar"}, // 143,
	{"VectorTimesMatrix"}, // 144,
	{"MatrixTimesVector"}, // 145,
	{"MatrixTimesMatrix"}, // 146,
	{"OuterProduct"}, // 147,
	{"Dot", " dot "}, // 148,
	{"IAddCarry"}, // 149,
	{"ISubBorrow"}, // 150,
	{"UMulExtended"}, // 151,
	{"SMulExtended"}, // 152,
	{nullptr},  // 153
	{"Any"}, // 154,
	{"All"}, // 155,
	{"IsNan"}, // 156,
	{"IsInf"}, // 157,
	{"IsFinite"}, // 158,
	{"IsNormal"}, // 159,
	{"SignBitSet"}, // 160,
	{"LessOrGreater"}, // 161,
	{"Ordered"}, // 162,
	{"Unordered"}, // 163,
	{"LogicalEqual"}, // 164,
	{"LogicalNotEqual"}, // 165,
	{"LogicalOr"}, // 166,
	{"LogicalAnd"}, // 167,
	{"LogicalNot"}, // 168,
	{"Select"}, // 169,
	{"IEqual"}, // 170,
	{"INotEqual"}, // 171,
	{"UGreaterThan"}, // 172,
	{"SGreaterThan"}, // 173,
	{"UGreaterThanEqual"}, // 174,
	{"SGreaterThanEqual"}, // 175,
	{"ULessThan"}, // 176,
	{"SLessThan"}, // 177,
	{"ULessThanEqual"}, // 178,
	{"SLessThanEqual"}, // 179,
	{"FOrdEqual", "=="}, // 180,
	{"FUnordEqual", "=="}, // 181,
	{"FOrdNotEqual", "!="}, // 182,
	{"FUnordNotEqual", "!="}, // 183,
	{"FOrdLessThan"}, // 184,
	{"FUnordLessThan"}, // 185,
	{"FOrdGreaterThan"}, // 186,
	{"FUnordGreaterThan"}, // 187,
	{"FOrdLessThanEqual"}, // 188,
	{"FUnordLessThanEqual"}, // 189,
	{"FOrdGreaterThanEqual"}, // 190,
	{"FUnordGreaterThanEqual"}, // 191,
	{ nullptr },  // 192
	{ nullptr },  // 193
	{"ShiftRightLogical"}, // 194,
	{"ShiftRightArithmetic"}, // 195,
	{"ShiftLeftLogical"}, // 196,
	{"BitwiseOr", "|"}, // 197,
	{"BitwiseXor", "^"}, // 198,
	{"BitwiseAnd", "&"}, // 199,
	{"Not", "~"}, // 200,
	{"BitFieldInsert"}, // 201,
	{"BitFieldSExtract"}, // 202,
	{"BitFieldUExtract"}, // 203,
	{"BitReverse"}, // 204,
	{"BitCount"}, // 205,
	{nullptr},
	{"DPdx"}, // 207,
	{"DPdy"}, // 208,
	{"Fwidth"}, // 209,
	{"DPdxFine"}, // 210,
	{"DPdyFine"}, // 211,
	{"FwidthFine"}, // 212,
	{"DPdxCoarse"}, // 213,
	{"DPdyCoarse"}, // 214,
	{"FwidthCoarse"}, // 215,
	{nullptr},  // 216
	{nullptr},  // 217
	{"EmitVertex"}, // 218,
	{"EndPrimitive"}, // 219,
	{"EmitStreamVertex"}, // 220,
	{"EndStreamPrimitive"}, // 221,
	{nullptr},  // 222
	{nullptr},  // 223
	{"ControlBarrier"}, // 224,
	{"MemoryBarrier"}, // 225,
	{nullptr},  // 226
	{"AtomicLoad"}, // 227,
	{"AtomicStore"}, // 228,
	{"AtomicExchange"}, // 229,
	{"AtomicCompareExchange"}, // 230,
	{"AtomicCompareExchangeWeak"}, // 231,
	{"AtomicIIncrement"}, // 232,
	{"AtomicIDecrement"}, // 233,
	{"AtomicIAdd"}, // 234,
	{"AtomicISub"}, // 235,
	{"AtomicSMin"}, // 236,
	{"AtomicUMin"}, // 237,
	{"AtomicSMax"}, // 238,
	{"AtomicUMax"}, // 239,
	{"AtomicAnd"}, // 240,
	{"AtomicOr"}, // 241,
	{"AtomicXor"}, // 242,
	{ nullptr },  // 243,
	{ nullptr },  // 244,
	{"Phi"}, // 245,
	{"LoopMerge"}, // 246,
	{"SelectionMerge"}, // 247,
	{"Label"}, // 248,
	{"Branch"}, // 249,
	{"BranchConditional"}, // 250,
	{"Switch"}, // 251,
	{"Kill"}, // 252,
	{"Return"}, // 253,
	{"ReturnValue"}, // 254,
	{"Unreachable"}, // 255,
	{"LifetimeStart"}, // 256,
	{"LifetimeStop"}, // 257,
	{ nullptr },
	{"GroupAsyncCopy"}, // 259,
	{"GroupWaitEvents"}, // 260,
	{"GroupAll"}, // 261,
	{"GroupAny"}, // 262,
	{"GroupBroadcast"}, // 263,
	{"GroupIAdd"}, // 264,
	{"GroupFAdd"}, // 265,
	{"GroupFMin"}, // 266,
	{"GroupUMin"}, // 267,
	{"GroupSMin"}, // 268,
	{"GroupFMax"}, // 269,
	{"GroupUMax"}, // 270,
	{"GroupSMax"}, // 271,
	{nullptr},  // 272
	{nullptr},  // 273
	{"ReadPipe"}, // 274,
	{"WritePipe"}, // 275,
	{"ReservedReadPipe"}, // 276,
	{"ReservedWritePipe"}, // 277,
	{"ReserveReadPipePackets"}, // 278,
	{"ReserveWritePipePackets"}, // 279,
	{"CommitReadPipe"}, // 280,
	{"CommitWritePipe"}, // 281,
	{"IsValidReserveId"}, // 282,
	{"GetNumPipePackets"}, // 283,
	{"GetMaxPipePackets"}, // 284,
	{"GroupReserveReadPipePackets"}, // 285,
	{"GroupReserveWritePipePackets"}, // 286,
	{"GroupCommitReadPipe"}, // 287,
	{"GroupCommitWritePipe"}, // 288,
	{ nullptr },  // 289
	{ nullptr },  // 290
	{"EnqueueMarker"}, // 291,
	{"EnqueueKernel"}, // 292,
	{"GetKernelNDrangeSubGroupCount"}, // 293,
	{"GetKernelNDrangeMaxSubGroupSize"}, // 294,
	{"GetKernelWorkGroupSize"}, // 295,
	{"GetKernelPreferredWorkGroupSizeMultiple"}, // 296,
	{"RetainEvent"}, // 297,
	{"ReleaseEvent"}, // 298,
	{"CreateUserEvent"}, // 299,
	{"IsValidEvent"}, // 300,
	{"SetUserEventStatus"}, // 301,
	{"CaptureEventProfilingInfo"}, // 302,
	{"GetDefaultQueue"}, // 303,
	{"BuildNDRange"}, // 304,
	{"ImageSparseSampleImplicitLod"}, // 305,
	{"ImageSparseSampleExplicitLod"}, // 306,
	{"ImageSparseSampleDrefImplicitLod"}, // 307,
	{"ImageSparseSampleDrefExplicitLod"}, // 308,
	{"ImageSparseSampleProjImplicitLod"}, // 309,
	{"ImageSparseSampleProjExplicitLod"}, // 310,
	{"ImageSparseSampleProjDrefImplicitLod"}, // 311,
	{"ImageSparseSampleProjDrefExplicitLod"}, // 312,
	{"ImageSparseFetch"}, // 313,
	{"ImageSparseGather"}, // 314,
	{"ImageSparseDrefGather"}, // 315,
	{"ImageSparseTexelsResident"}, // 316,
	{"NoLine"}, // 317,
	{"AtomicFlagTestAndSet"}, // 318,
	{"AtomicFlagClear"}, // 319,
};

static std::string ReadSpirvString(const std::vector<uint32_t> &spirv, int offset, int *outOffset = nullptr) {
	bool done = false;
	std::string temp;
	while (!done) {
		uint32_t data = spirv[offset++];
		for (int i = 0; i < 4; i++) {
			char c = (char)(data & 0xff);
			if (!c) {
				done = true;
				break;
			}
			temp.push_back(data);
			data >>= 8;
		}
		if (offset == (int)spirv.size())
			break;
	}
	if (outOffset)
		*outOffset = offset;
	return temp;
}

bool DisassembleSPIRV(std::vector<uint32_t> spirv, std::string *output) {
	if (spirv.size() < 10) {
		*output = "Too small";
		return false;
	}
	uint32_t magic = spirv[0];
	uint32_t version = spirv[1];
	uint32_t generator = spirv[2];

	int bound = (int)spirv[3];  // Max ID used in file.
	// spirv[4] is reserved for schema.
	std::vector<SpirvID> ids;
	ids.resize(bound);
	for (int i = 0; i < bound; i++) {
		ids[i].name = StringFromFormat("%%%d", i);
	}

	if (magic != 0x07230203) {
		*output = "Not SPIRV";
		return false;
	}

	int indent = 0;

	char *buffer = new char[1024 * 1024];
	char *p = buffer;

	WRITE(p, "// ======= SPIR-V version %08x =======\n// Max ID: %d\n", version, bound);  // GLSL ES

	int i = 5;
	while (i < (int)spirv.size()) {
		uint32_t d = spirv[i];
		int wordCount = d >> 16;
		int opcode = d & 0xFFFF;
		const OpInfo &op = opInfo[opcode];
		int target = (i < (int)spirv.size() - 1) ? spirv[(i + 1)] : 0;  // Not always, but used often enough that we get it here.
		int source, source2, resType;
		std::string name;
		switch (opcode) {
		case spv::OpTypeVoid:
		case spv::OpTypeBool:
		case spv::OpTypeInt:
		case spv::OpTypeFloat:
		case spv::OpTypeMatrix:
		case spv::OpTypeImage:
		case spv::OpTypeSampler:
		case spv::OpTypeSampledImage:
		case spv::OpTypeRuntimeArray:
		case spv::OpTypeOpaque:
		case spv::OpTypePointer:
		case spv::OpTypeFunction:
		case spv::OpTypeEvent:
		case spv::OpTypeDeviceEvent:
		case spv::OpTypeReserveId:
		case spv::OpTypeQueue:
		case spv::OpTypePipe:
		case spv::OpTypeForwardPointer:
			ids[target].name = op.name + 4;  // Remove "Type"
			ids[target].opcode = opcode;
			break;
		case spv::OpTypeVector:
			source = spirv[i + 2];
			source2 = spirv[i + 3];
			ids[target].name = ids[source].name + StringFromFormat("%d", source2); break;
			break;
		case spv::OpTypeArray:
			source = spirv[i + 2];
			source2 = spirv[i + 3];
			ids[target].name = ids[source].name + "[" + ids[source2].name + "]"; break;
			break;
		case spv::OpTypeStruct:
			{
				ids[target].name = "struct" + ids[target].name;
				WRITE(p, "struct {\n");
				if (wordCount == 3) {
					source = spirv[i + 2];
					WRITE(p, "  %s\n", ids[source].name.c_str());
				} else {
					int numMembers = (wordCount - 2) / 2;
					for (int m = 0; m < numMembers; m++) {
						int id = spirv[i + 2 + m];
						int type = spirv[i + 2 + m + numMembers];
						WRITE(p, "  %s %s;\n", ids[type].name.c_str(), ids[id].name.c_str());
					}
				}
				WRITE(p, "};\n");
			}
			break;
		case spv::OpVariable:
			resType = spirv[i + 1];
			target = spirv[i + 2];
			source = spirv[i + 3];
			source2 = spirv[i + 4];
			ids[target].name = storageClassNames[source];
			break;
		case spv::OpDecorate:
			source = spirv[i + 2];
			ids[target].name += std::string("[") + decorationNames[source] + "]";
			break;
		case spv::OpName:
			ids[target].name = ReadSpirvString(spirv, i + 2);
			// WRITE(p, "Name %d: '%s'\n", target, ids[target].name.c_str());
			break;
		case spv::OpMemberName:
			break;
		case spv::OpStore:
			source = spirv[i + 2];
			WRITE(p, "Store(%s, %s)\n", ids[target].name.c_str(), ids[source].name.c_str());
			break;
		case spv::OpLoad:
			resType = spirv[i + 1];
			target = spirv[i + 2];
			source = spirv[i + 3];
			WRITE(p, "%s (%s) := Load(%s)\n", ids[target].name.c_str(), ids[resType].name.c_str(), ids[source].name.c_str());
			break;
		case spv::OpFAdd:
		case spv::OpFMul:
		case spv::OpFDiv:
		case spv::OpFSub:
			resType = spirv[i + 2];
			source = spirv[i + 3];
			source2 = spirv[i + 4];
			WRITE(p, "%s (%s) := %s %s %s\n", ids[target].name.c_str(), ids[resType].name.c_str(), ids[source].name.c_str(), op.extra, ids[source2].name.c_str());
			break;

		case spv::OpCapability:
		case spv::OpMemoryModel:
		case spv::OpExtInstImport:
		case spv::OpExecutionMode:
		case spv::OpGroupAsyncCopy:
		case spv::OpSource:
		case spv::OpExtension:
			// hide these for now
			break;

		case spv::OpEntryPoint:
			source = spirv[i + 1];
			source2 = spirv[i + 2];
			name = ReadSpirvString(spirv, i + 3);
			WRITE(p, "EntryPoint %s: '%s' %s : %s\n", executionModelNames[target], ids[source].name.c_str(), ids[source2].name.c_str(), name.c_str());
			break;

		case spv::OpFunction:
			resType = spirv[i + 1];
			target = spirv[i + 2];
			source = spirv[i + 3];
			ids[target].name = ids[resType].name + "()";
			WRITE(p, "Function %s {\n", ids[target].name.c_str());
			break;

		case spv::OpLabel:
			WRITE(p, "label %s:\n", ids[target].name.c_str());
			break;

		case spv::OpReturnValue:
			WRITE(p, "return %s\n", ids[target].name.c_str());
			break;

		case spv::OpReturn:
			WRITE(p, "return\n");
			break;

		case spv::OpFunctionEnd:
			WRITE(p, "}\n");
			break;

		case spv::OpSourceExtension:
			break;

		default:
			WRITE(p, "%s (%d data words)\n", op.name, wordCount - 1);
			break;
		}
		i += wordCount;
	}

	*output = buffer;
	delete[] buffer;
	return true;
}