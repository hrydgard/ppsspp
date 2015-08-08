#pragma once

// The idea is to raise the level of abstraction from a raw display list to a list of
// nice structured draw calls, and then lower back onto our current graphics API.

// The lowering may be done very differently depending on the API. For OpenGL ES,
// it may pay off to do very expensive sorting and joining of draw calls with different
// matrices using software transform, while for DX12, Vulkan or Metal you probably just
// want to translate directly.

// We separate DRAWTRI, LINE and POINT because they can never be combined.
// To be decided: It's possibly better to evaluate splines early.

#include "Common/CommonTypes.h"
#include "GPU/GPUState.h"
#include "GPU/Common/MemoryArena.h"

namespace HighGpu {

#define BIT(x) (1UL << x)

// Possibly we should boil down Beziers and Splines into Triangles etc already here?
// Might be beneficial to process them on the GPU thread though...
enum CommandType : u8 {
	CMD_DRAWPRIM,
	CMD_DRAWBEZIER,
	CMD_DRAWSPLINE,
	CMD_TRANSFER,
};

enum EnableFlags : u32 {
	ENABLE_BLEND          = BIT(0),
	ENABLE_ALPHA_TEST     = BIT(1),
	ENABLE_COLOR_TEST     = BIT(2),
	ENABLE_FOG            = BIT(3),
	ENABLE_LOGIC_OP       = BIT(4),
	ENABLE_STENCIL_TEST   = BIT(5),
	ENABLE_DEPTH_TEST     = BIT(6),
	ENABLE_TEXTURE        = BIT(7),
	ENABLE_TRANSFORM      = BIT(8),
	ENABLE_TEX_TRANSFORM  = BIT(9),
	ENABLE_BONES          = BIT(10),
	ENABLE_MORPH          = BIT(11),
	ENABLE_TEXMATRIX      = BIT(12),
	ENABLE_LIGHTS         = BIT(13),
	ENABLE_LIGHT0         = BIT(14),
	ENABLE_LIGHT1         = BIT(15),
	ENABLE_LIGHT2         = BIT(16),
	ENABLE_LIGHT3         = BIT(17),
	ENABLE_CLUT           = BIT(18),
};

// All the individual state blocks, both enable-able and not, for helping keeping track of them.
// However, it's always the case that one of these flags cover one or more "enable-sections", they're
// never split between them.
enum StateFlags : u32 {
	STATE_FRAMEBUF = BIT(0),
	STATE_BLEND    = BIT(1),
	STATE_FRAGMENT = BIT(2),
	STATE_WORLDMATRIX = BIT(3),
	STATE_VIEWMATRIX = BIT(4),
	STATE_PROJMATRIX = BIT(5),
	STATE_TEXMATRIX = BIT(6),
	STATE_TEXTURE = BIT(7),
	STATE_DEPTHSTENCIL = BIT(8),
	STATE_MORPH = BIT(9),
	STATE_RASTERIZER = BIT(10),
	STATE_SAMPLER = BIT(11),
	STATE_CLUT = BIT(12),
	STATE_TEXSCALE = BIT(13),
	STATE_ENABLES = BIT(14),
	STATE_VIEWPORT = BIT(18),
	STATE_LIGHTGLOBAL = BIT(19),
	STATE_LIGHT0 = BIT(20),
	STATE_LIGHT1 = BIT(21),
	STATE_LIGHT2 = BIT(22),
	STATE_LIGHT3 = BIT(23),
	STATE_BONE0 = BIT(24),
	STATE_BONE1 = BIT(25),
	STATE_BONE2 = BIT(26),
	STATE_BONE3 = BIT(27),
	STATE_BONE4 = BIT(28),
	STATE_BONE5 = BIT(29),
	STATE_BONE6 = BIT(30),
	STATE_BONE7 = BIT(31),
	STATE_ALL = 0xFFFC3FFF,
};

// This is starting to grow large... but I think we have nearly all state now.
struct DrawCommandData {
	u32 vtxformat;
	u32 vtxAddr;
	u32 idxAddr;
	u32 enabled;  // Bitfield for fast checks, EnableFlags
	u16 count;
	u8 prim;
	u8 framebuf;
	u8 raster;
	u8 fragment;
	u8 worldMatrix;
	u8 viewMatrix;
	u8 projMatrix;
	u8 texMatrix;
	u8 viewport;
	u8 blend;
	u8 depthStencil;
	u8 texture;
	u8 texScale;
	u8 sampler;
	u8 morph;
	u8 lightGlobal;
	u8 lights[4];
	u8 boneMatrix[8];
	u8 numBones;
	u8 clut;
};

struct TransferCommandData {
	u32 srcPtr;
	u32 dstPtr;
	u16 srcStride;
	u16 dstStride;
	u16 srcX, srcY;
	u16 dstX, dstY;
	u16 width;
	u16 height;
	u8 bpp;
};

struct ClutLoadCommandData {
	u32 addr;
	u32 bytes;
};

struct Command {
	u8 type;
	union {
		DrawCommandData draw;
		TransferCommandData transfer;
		ClutLoadCommandData clut;
	};
	void *userData;  // Put temporary translated data here that should be attached to this command.
};

// State objects
// As these will be frequently memcmp'd, it's important to avoid padding. We primarily do this
// by sometimes increasing the data size unnecessarily.


struct FramebufState {
	u32 colorPtr;
	u16 colorStride;
	u16 colorFormat;  // could be u8 but for padding
	u32 depthPtr;
	u32 depthStride;
	// There's only one depth format.
};

// Dumping ground for pixel state that doesn't belong anywhere else, and is always needed regardless
// of "enables". Well, culling is not applied in throughmode but apart from that...
struct RasterState {
	u32 defaultVertexColor;  // materialambient gets duplicated here so we don't need to involve lights.
	u32 offsetX;
	u32 offsetY;
	u16 scissorX1;
	u16 scissorY1;
	u16 scissorX2;
	u16 scissorY2;
	u16 clearMode;
	u8 cullFaceEnable;
	u8 cullMode;
};

// TODO: fogCoefs are more like transform state. Maybe move?
struct FragmentState {
	u8 colorDouble;
	u8 useTextureAlpha;
	u8 texFunc;
	u8 shadeMode;
	u32 texEnvColor;
	float fogCoef1;
	float fogCoef2;
	u32 fogColor;
};

struct ViewportState {
	float x1, y1, z1;
	float x2, y2, z2;
	short regionX1, regionY1;
	short regionX2, regionY2;
};

struct BlendState {
	u8 blendSrc;
	u8 blendDst;
	u8 blendEq;
	u8 logicOpFunc;
	u32 blendfixa;
	u32 blendfixb;
	u8 alphaTestFunc;
	u8 alphaTestRef;
	u8 alphaTestMask;
	u8 colorTestFunc;
	u32 colorTestRef;
	u32 colorTestMask;
	u32 colorWriteMask;
};

struct DepthStencilState {
	u8 depthFunc;
	u8 depthWriteEnable;  // Part of the depth test
	u8 stencilRef;
	u8 stencilMask;
	u8 stencilFunc;
	u8 stencilOpSFail;
	u8 stencilOpZFail;
	u8 stencilOpZPass;
};

// This is quite big. Might want to use the fact that pretty much all textures are valid
// partial mip pyramids and only store the size of the first level. I think we do need to store
// all addresses though, although might be able to delta compress them.
struct TextureState {
	u32 addr[8];
	u32 clutHash;  // Hack, but useful to be able to iterate over unique tex/clut combinations effortlessly.
	u16 dim[8];
	u16 stride[8];
	u16 maxLevel; // u16 to eliminate padding
	u8 format;
	u8 swizzled;
	u8 mipClutMode;
	u8 clutFormat;  // RGB565, etc...
	u8 clutIndexMaskShiftStart;
	u8 clutStateIndex;
};

// Really, UV generation state.
struct TexScaleState {
	float scaleU;
	float scaleV;
	float offsetU;
	float offsetV;
	int texMapMode;  // UVGen mode (uv, texmtx, etc)
};

struct SamplerState {
	u8 mag;
	u8 min;
	u8 bias;
	u8 clamp_s;
	u8 clamp_t;
	u8 levelMode;
};

struct LightGlobalState {
	u32 materialUpdate;
	u32 materialEmissive;
	u32 materialAmbient;   // materialAlpha gets baked into this. This is actually loaded again in the rasterstate as the default vertex color.
	u32 materialDiffuse;
	u32 materialSpecular;
	u32 ambientcolor;
	float specularCoef;
	u32 lmode;
};

// TODO: Note that depending on the light type, not the entire struct needs to be allocated (!)
struct LightState {
	int type;  // int instead of u8 to avoid padding
	float pos[3];  // pos is used as direction for directional lights.
	u32 diffuseColor;
	u32 ambientColor;
	u32 specularColor;
	// pointlight-and-spotlight-only data
	float att[3];
	// spotlight-only data
	float dir[3];
	float lconv;
	float lcutoff;
};

struct ClutState {
	u32 hash;
	int size;
	u8 *data;
};

struct MorphState {
	float weights[8];
};

struct Matrix4x3 {
	float v[12];
};

struct Matrix4x4 {
	float v[16];
};

const int INVALID_STATE = 255;

// A tree of display lists up to an "END" gets combined into a CommandPacket.
//
// Essentially, this is a compressed version of a full list of draws with all their state.
// We achieve the compression by only storing a new "block" of a type of settings if something changed.
// If nothing changed, draw calls can share the state block.
//
// This allows safe command reordering (where applicable), multiple passes and similar tricks, without
// completely blowing up the CPU cache, hopefully.
struct CommandPacket {
	Command *commands;
	const Command *lastDraw;
	int numCommands;
	int maxCommands;
	u32 full;  // Needs a flush
	// TODO: Shrink these.
	int numFramebuf;
	int numFragment;
	int numRaster;
	int numWorldMatrix;
	int numViewMatrix;
	int numProjMatrix;
	int numTexMatrix;
	int numBoneMatrix;
	int numViewport;
	int numBlend;
	int numDepthStencil;
	int numTexture;
	int numTexScale;
	int numSampler;
	int numClut;
	int numLightGlobal;
	int numLight;
	int numMorph;
	// No idea what the appropriate counts for each feature are. Probably varies per game.
	// Might be better to dynamically allocate these arrays, but that's for later.
	FramebufState *framebuf[16];
	FragmentState *fragment[32];
	RasterState *raster[16];
	Matrix4x3 *worldMatrix[254];
	Matrix4x3 *viewMatrix[8];
	Matrix4x4 *projMatrix[8];
	Matrix4x3 *texMatrix[8];
	Matrix4x3 *boneMatrix[254];
	ViewportState *viewport[16];
	BlendState *blend[64];
	DepthStencilState *depthStencil[16];
	TextureState *texture[64];
	TexScaleState *texScale[254];  // Some games change this on pretty much every draw.
	SamplerState *sampler[8];
	ClutState *clut[64];
	LightGlobalState *lightGlobal[16];
	LightState *lights[64];
	MorphState *morph[64];

	u8 *clutBuffer;
	int latchedClutLoadBytes;
};

// Utility functions
// Does not take ownership of the clut buffer. It's just where clut data gets copied from.
void CommandPacketInit(CommandPacket *cmdPacket, int size, u8 *clutBuffer);
void CommandPacketDeinit(CommandPacket *cmdPacket);
void CommandPacketReset(CommandPacket *cmdPacket, const Command *dummyDraw);

void CommandInitDummyDraw(Command *cmd);

// Submitting commands to a CommandPacket
void CommandSubmitTransfer(CommandPacket *cmdPacket, const GPUgstate *gstate);
u32 CommandSubmitDraw(CommandPacket *packet, MemoryArena *arena, const GPUgstate *gstate, u32 dirty, u32 primAndCount, u32 vertexAddr, u32 indexAddr);
void CommandSubmitSync(CommandPacket *cmdPacket);

void PrintCommandPacket(CommandPacket *cmd);
#undef BIT

}  // namespace
