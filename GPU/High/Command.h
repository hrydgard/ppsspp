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

namespace HighGpu {

#define BIT(x) (1UL << x)

// Possibly we should boil down Beziers and Splines into Triangles etc already here?
// Might be beneficial to process them on the GPU thread though...
enum CommandType : u8 {
	CMD_DRAWTRI,
	CMD_DRAWLINE,
	CMD_DRAWPOINT,
	CMD_DRAWBEZIER,
	CMD_DRAWSPLINE,
	CMD_TRANSFER,
	CMD_LOADCLUT,
	CMD_SYNC,
};

enum EnableFlags : u16 {
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
	ENABLE_LIGHTS         = BIT(10),
	ENABLE_BONES          = BIT(11),
};

// All the individual state sections, both enable-able and not, for helping keeping track of them.
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
};

// 12 + 2 + 6 + 4 + 8 = 32 bytes (no way this will be enough...)
struct DrawCommandData {
	u32 vtxformat;
	u32 vtxAddr;
	u32 idxAddr;
	u32 enabled;  // Bitfield for fast checks, EnableFlags
	u16 count;
	u8 prim;
	u8 framebuf;
	u8 worldMatrix;
	u8 viewMatrix;
	u8 projMatrix;
	u8 texMatrix;
	u8 blend;
	u8 depthStencil;
	u8 tex;
	u8 lights[4];
	u8 bones[8];
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
};

struct ClutLoadCommandData {
	u32 addr;
	u32 size;
};

struct Command {
	u8 type;
	union {
		DrawCommandData draw;
		TransferCommandData transfer;
		ClutLoadCommandData clut;
	};
};

struct FramebufState {
	u32 colorPtr;
	u16 colorStride;
	u16 colorFormat;
	u32 depthPtr;
	u32 depthStride;
	// There's only one depth format.
};

// Dumping ground for pixel state that doesn't belong anywhere else.
struct RasterState {
	u8 clearMode;
	u8 shadeMode;
	u32 offsetX;
	u32 offsetY;
	float fogCoef1;
	float fogCoef2;
	u16 scissorX1;
	u16 scissorY1;
	u16 scissorX2;
	u16 scissorY2;
	bool cullFaceEnable;
	bool cullMode;
};

struct ViewportState {
	float x1, y1, z1;
	float x2, y2, z2;
};

struct BlendState {
	u8 blendEnable;
	u8 blendSrc;
	u8 blendDst;
	u8 blendEq;
	u32 blendfixa;
	u32 blendfixb;
	u8 alphaTestEnable;
	u8 alphaTestFunc;
	u8 alphaTestRef;
	u8 alphaTestMask;
	u32 colorWriteMask;
	u8 logicOpEnable;
	u8 logicOpFunc;
};

struct DepthStencilState {
	u8 depthFunc;
	u8 depthTestEnable;
	u8 depthWriteEnable;
	u8 stencilTestEnable;
	u8 stencilRef;
	u8 stencilMask;
	u8 stencilFunc;
	u8 stencilOpSFail;
	u8 stencilOpZFail;
	u8 stencilOpZPass;
};

struct TextureState {
	float offsetX;
	float offsetY;
	float scaleX;
	float scaleY;
	u8 dim;
	u32 texptr[8];
	u16 stride[8];
};

struct SamplerState {
	u8 mag;
	u8 min;
	u8 bias;
	u8 wrap_s;   // packed
	u8 wrap_t;
};

struct LightGlobalState {
	u32 materialupdate;
	u32 materialemissive;
	u32 materialambient;   // materialAlpha gets baked into this
	u32 materialdiffuse;
	u32 materialspecular;
	u32 materialspecularcoef;
	u32 ambientcolor;
	u32 lmode;
	float specularCoef;
};

// TODO: Note that depending on the light type, not the entire struct needs to be allocated (!)
struct LightState {
	u8 enabled;
	u8 type;
	float pos[3];
	float dir[3];
	u32 diffuseColor;
	u32 ambientColor;
	u32 specularColor;
	// spotlight-only data
	float att[3];
	float lconv;
	float lcutoff;
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

// A tree of display lists up to an "END" gets combined into a CommandPacket.
// Essentially, this is a compressed version of a full list of draws with all their state.
// We achieve the compression by only storing a new "block" of a type of settings if something changed.
// If nothing changed, draw calls can share the block.
// This allows safe command reordering (where applicable), multiple passes and similar tricks, without
// completely blowing up the CPU cache.
struct CommandPacket {
	Command *commands;
	int numCommands;
	FramebufState *framebuf; int numFramebuf;
	Matrix4x3 *worldMatrix; int numWorldMatrix;
	Matrix4x3 *viewMatrix; int numViewMatrix;
	Matrix4x4 *projMatrix; int numProjMatrix;
	Matrix4x3 *texMatrix; int numTexMatrix;
	Matrix4x3 *boneMatrix; int numBoneMatrix;
	ViewportState *viewport; int numViewport;
	BlendState *blend; int numBlend;
	DepthStencilState *depthStencil; int numDepthStencil;
	TextureState *tex; int numTex;
	SamplerState *sampler; int numSampler;
	LightGlobalState *lightGlobal; int numLightGlobals;
	LightState *lights; int numLights;
	RasterState *raster; int numRaster;
	MorphState *morph; int numMorph;
	u8 clut[4096];
};

// Submitting commands to a CommandPacket
void CommandSubmitTransfer(CommandPacket *packet, const GPUgstate *gstate, u32 data);
void CommandSubmitDraw(CommandPacket *packet, const GPUgstate *gstate, u32 dirty, u32 data);


void PrintCommandPacket(CommandPacket *cmd, int start, int count);

#undef BIT

}  // namespace
