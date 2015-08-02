#include "base/basictypes.h"
#include "base/logging.h"
#include "GPU/High/Command.h"
#include "GPU/GPUState.h"

namespace HighGpu {

// Collects all the "enable" flags. Note that they don't currently correspond perfectly with
// the "state chunks" we have defined, some state chunks cover multiple of these, and some of these
static u32 LoadEnables(const GPUgstate *gstate) {
	u32 val = 0;
	// Vertex
	if (!gstate->isModeThrough()) {
		val |= ENABLE_TRANSFORM;
		if (gstate->isLightingEnabled()) {
			val |= ENABLE_LIGHTS;
		}
		if (gstate->isSkinningEnabled()) {
			val |= ENABLE_BONES;
		}
	}
	// Fragment/raster
	if (!gstate->isModeClear()) {
		// None of this is relevant in clear mode.
		if (gstate->isAlphaBlendEnabled()) val |= ENABLE_BLEND;
		if (gstate->isAlphaTestEnabled()) val |= ENABLE_ALPHA_TEST;
		if (gstate->isColorTestEnabled()) val |= ENABLE_COLOR_TEST;
		if ((val & ENABLE_TRANSFORM) && gstate->isFogEnabled()) val |= ENABLE_FOG;
		if (gstate->isLogicOpEnabled()) val |= ENABLE_LOGIC_OP;
		if (gstate->isStencilTestEnabled()) val |= ENABLE_STENCIL_TEST;
		if (gstate->isDepthTestEnabled()) val |= ENABLE_DEPTH_TEST;
		if (gstate->isTextureMapEnabled()) val |= ENABLE_TEXTURE;
	}
	return val;
}

static void LoadBlendState(BlendState *blend, const GPUgstate *gstate) {
	blend->blendSrc = gstate->getBlendFuncA();
	blend->blendDst = gstate->getBlendFuncB();
	blend->blendEq = gstate->getBlendEq();
	blend->blendfixa = gstate->getFixA();
	blend->blendfixb = gstate->getFixB();
	blend->alphaTestFunc = gstate->getAlphaTestFunction();
	blend->alphaTestRef = gstate->getAlphaTestRef();
	blend->alphaTestMask = gstate->getAlphaTestMask();
	blend->colorWriteMask = gstate->getColorMask();
	blend->logicOpFunc = gstate->getLogicOp();
}

static void LoadLightGlobalState(LightGlobalState *light, const GPUgstate *gstate) {
	light->materialUpdate = gstate->getMaterialUpdate();
	light->materialAmbient = gstate->getMaterialAmbientRGBA();
	light->specularCoef = gstate->getMaterialSpecularCoef();
	light->materialEmissive = gstate->getMaterialEmissive();
	light->materialSpecular = gstate->getMaterialSpecular();
}

static void LoadLightState(LightState *light, const GPUgstate *gstate, int lightnum) {
	light->type = gstate->getLightType(lightnum);
	light->diffuseColor = gstate->getDiffuseColor(lightnum);
	light->specularColor = gstate->getSpecularColor(lightnum);
	light->ambientColor = gstate->getLightAmbientColor(lightnum);
	// ...
	if (light->type == GE_LIGHTTYPE_SPOT) {
		// light->cutoff =
	}
}

static void LoadFragmentState(FragmentState *fragment, const GPUgstate *gstate) {
	fragment->fogCoef1 = gstate->getFogCoef1();
	fragment->fogCoef2 = gstate->getFogCoef2();
	fragment->fogColor = gstate->getFogColor();
	fragment->texEnvColor = gstate->getTextureEnvColor();
	fragment->texFunc = gstate->getTextureFunction();
	fragment->useTextureAlpha = gstate->isTextureAlphaUsed();
	fragment->colorDouble = gstate->isColorDoublingEnabled();
	fragment->shadeMode = gstate->getShadeMode();  // flat/gouraud.
}

static void LoadRasterState(RasterState *raster, const GPUgstate *gstate) {
	raster->clearMode = gstate->isModeClear();
	raster->offsetX = gstate->getOffsetX();
	raster->offsetY = gstate->getOffsetY();
	raster->scissorX1 = gstate->getScissorX1();
	raster->scissorY1 = gstate->getScissorY1();
	raster->scissorX2 = gstate->getScissorX2();
	raster->scissorY2 = gstate->getScissorY2();
	raster->cullFaceEnable = gstate->isCullEnabled();
	raster->cullMode = gstate->getCullMode();
	raster->defaultVertexColor = gstate->getMaterialAmbientRGBA();
}

static void LoadViewportState(ViewportState *viewport, const GPUgstate *gstate) {
	viewport->x1 = gstate->getViewportX1();
	viewport->y1 = gstate->getViewportY1();
	viewport->z1 = gstate->getViewportZ1();
	viewport->x2 = gstate->getViewportX2();
	viewport->y2 = gstate->getViewportY2();
	viewport->z2 = gstate->getViewportZ2();
}

static void LoadDepthStencilState(DepthStencilState *depthStencil, const GPUgstate *gstate) {
	depthStencil->depthFunc = gstate->getDepthTestFunction();
	depthStencil->depthWriteEnable = gstate->isDepthWriteEnabled();
	depthStencil->stencilRef = gstate->getStencilTestRef();
	depthStencil->stencilMask = gstate->getStencilTestMask();
	depthStencil->stencilFunc = gstate->getStencilTestFunction();
	depthStencil->stencilOpSFail = gstate->getStencilOpSFail();
	depthStencil->stencilOpZFail = gstate->getStencilOpZFail();
	depthStencil->stencilOpZPass = gstate->getStencilOpZPass();
}

static void LoadTextureState(TextureState *texture, const GPUgstate *gstate) {
	texture->maxLevel = gstate->getTextureMaxLevel();
	// SIMD opportunity
	for (int i = 0; i < texture->maxLevel; i++) {
		texture->addr[i] = gstate->getTextureAddress(i);
		texture->dim[i] = gstate->getTextureDimension(i);
		texture->stride[i] = gstate->getTextureStride(i);
	}
	texture->format = gstate->getTextureFormat();
}

static void LoadSamplerState(SamplerState *sampler, const GPUgstate *gstate) {
	sampler->mag = gstate->getTexMagFilter();
	sampler->min = gstate->getTexMinFilter();
	sampler->bias = gstate->getTexLodBias();
	sampler->clamp_s = gstate->isTexCoordClampedS();
	sampler->clamp_t = gstate->isTexCoordClampedT();
	sampler->levelMode = gstate->getTexLevelMode();
}

static void LoadMorphState(MorphState *morph, const GPUgstate *gstate) {
	int numWeights = 8;  // TODO: Cut down on this when possible
	// SIMD possible
	for (int i = 0; i < numWeights; i++) {
		morph->weights[i] = getFloat24(gstate->morphwgt[i]);
	}
}

inline void LoadMatrix4x3(Matrix4x3 *mtx, const float *data) {
	// TODO: Move the to-float left-shift here, as it's easier to do SIMD here
	// than in the fastrunloop. Requires changing the other backends though.
	memcpy(mtx, data, 12 * sizeof(float));
}

inline void LoadMatrix4x4(Matrix4x4 *mtx, const float *data) {
	memcpy(mtx, data, 16 * sizeof(float));
}

// TODO: De-duplicate states, looking a couple of items back in each list.
// This algorithm can be refined in the future.
u32 LoadStates(CommandPacket *cmdPacket, Command *last, Command *command, MemoryArena *arena, const GPUgstate *gstate, u32 dirty) {
	// Early out for repeated commands with no state changes in between.
	if (!dirty) {
		command->draw.enabled = last->draw.enabled;
		return dirty;
	}

	u32 enabled = LoadEnables(gstate);

	bool full = false;

	// Regardless of Enabled flags, there's always a rasterizer state.
	if (dirty & STATE_RASTERIZER) {
		command->draw.raster = cmdPacket->numRaster;
		LoadRasterState(arena->Allocate(&cmdPacket->raster[cmdPacket->numRaster++]), gstate);
		if (cmdPacket->numRaster == ARRAY_SIZE(cmdPacket->raster)) full = true;
		dirty &= ~STATE_RASTERIZER;
	} else {
		command->draw.raster = last->draw.raster;
	}

	if (dirty & STATE_FRAGMENT) {
		command->draw.fragment = cmdPacket->numFragment;
		LoadFragmentState(arena->Allocate(&cmdPacket->fragment[cmdPacket->numFragment++]), gstate);
		if (cmdPacket->numFragment == ARRAY_SIZE(cmdPacket->fragment)) full = true;
		dirty &= ~STATE_FRAGMENT;
	} else {
		command->draw.fragment = last->draw.fragment;
	}

	if ((enabled & (ENABLE_BLEND|ENABLE_ALPHA_TEST|ENABLE_COLOR_TEST)) && (dirty & STATE_BLEND)) {
		command->draw.blend = cmdPacket->numBlend;
		LoadBlendState(arena->Allocate(&cmdPacket->blend[cmdPacket->numBlend++]), gstate);
		if (cmdPacket->numBlend == ARRAY_SIZE(cmdPacket->blend)) full = true;
		dirty &= ~STATE_BLEND;
	} else {
		command->draw.blend = last->draw.blend;
	}

	if (enabled & (ENABLE_DEPTH_TEST|ENABLE_STENCIL_TEST) && (dirty & STATE_DEPTHSTENCIL)) {
		command->draw.depthStencil = cmdPacket->numDepthStencil;
		LoadDepthStencilState(arena->Allocate(&cmdPacket->depthStencil[cmdPacket->numDepthStencil++]), gstate);
		if (cmdPacket->numDepthStencil == ARRAY_SIZE(cmdPacket->depthStencil)) full = true;
		dirty &= ~STATE_DEPTHSTENCIL;
	} else {
		command->draw.depthStencil = last->draw.depthStencil;
	}

	if (enabled & ENABLE_TEXTURE) {
		if (dirty & STATE_TEXTURE) {
			command->draw.texture = cmdPacket->numTexture;
			LoadTextureState(arena->Allocate(&cmdPacket->texture[cmdPacket->numTexture++]), gstate);
			if (cmdPacket->numTexture == ARRAY_SIZE(cmdPacket->texture)) full = true;
			dirty &= ~STATE_TEXTURE;
		}
		if (dirty & STATE_SAMPLER) {
			command->draw.sampler = cmdPacket->numSampler;
			LoadSamplerState(arena->Allocate(&cmdPacket->sampler[cmdPacket->numSampler++]), gstate);
			if (cmdPacket->numSampler == ARRAY_SIZE(cmdPacket->sampler)) full = true;
			dirty &= ~STATE_SAMPLER;
		}
	}

	if (enabled & ENABLE_TRANSFORM) {
		if (dirty & STATE_VIEWPORT) {
			command->draw.viewport = cmdPacket->numViewport;
			LoadViewportState(arena->Allocate(&cmdPacket->viewport[cmdPacket->numViewport++]), gstate);
			if (cmdPacket->numViewport == ARRAY_SIZE(cmdPacket->viewport)) full = true;
			dirty &= ~STATE_VIEWPORT;
		} else {
			command->draw.viewport = last->draw.viewport;
		}
		if (dirty & STATE_WORLDMATRIX) {
			command->draw.worldMatrix = cmdPacket->numWorldMatrix;
			LoadMatrix4x3(arena->Allocate(&cmdPacket->worldMatrix[cmdPacket->numWorldMatrix++]), gstate->worldMatrix);
			if (cmdPacket->numWorldMatrix == ARRAY_SIZE(cmdPacket->worldMatrix)) full = true;
			dirty &= ~STATE_WORLDMATRIX;
		} else {
			command->draw.worldMatrix = last->draw.worldMatrix;
		}
		if (dirty & STATE_VIEWMATRIX) {
			command->draw.viewMatrix = cmdPacket->numViewMatrix;
			LoadMatrix4x3(arena->Allocate(&cmdPacket->viewMatrix[cmdPacket->numViewMatrix++]), gstate->viewMatrix);
			if (cmdPacket->numViewMatrix == ARRAY_SIZE(cmdPacket->viewMatrix)) full = true;
			dirty &= ~STATE_VIEWMATRIX;
		} else {
			command->draw.viewMatrix = last->draw.viewMatrix;
		}
		if (dirty & STATE_PROJMATRIX) {
			command->draw.projMatrix = cmdPacket->numProjMatrix;
			LoadMatrix4x4(arena->Allocate(&cmdPacket->projMatrix[cmdPacket->numProjMatrix++]), gstate->projMatrix);
			if (cmdPacket->numProjMatrix == ARRAY_SIZE(cmdPacket->projMatrix)) full = true;
			dirty &= ~STATE_PROJMATRIX;
		} else {
			command->draw.projMatrix = last->draw.projMatrix;
		}
		if (enabled & ENABLE_LIGHTS) {
			if (dirty & STATE_VIEWPORT) {
				command->draw.lightGlobal = cmdPacket->numLightGlobal;
				LoadLightGlobalState(arena->Allocate(&cmdPacket->lightGlobal[cmdPacket->numLightGlobal++]), gstate);
				if (cmdPacket->numLightGlobal == ARRAY_SIZE(cmdPacket->lightGlobal)) full = true;
				dirty &= ~STATE_VIEWPORT;
			} else {
				command->draw.lightGlobal = last->draw.lightGlobal;
			}
			for (int i = 0; i < 4; i++) {
				if (dirty & (STATE_LIGHT0 << i)) {
					command->draw.lights[i] = cmdPacket->numLight;
					LoadLightState(arena->Allocate(&cmdPacket->lights[cmdPacket->numLight++]), gstate, i);
					if (cmdPacket->numLight >= ARRAY_SIZE(cmdPacket->lights) - 4) full = true;
				} else {
					command->draw.lights[i] = last->draw.lights[i];
				}
			}
		} else {
			for (int i = 0; i < 4; i++) {
				command->draw.lights[i] = last->draw.lights[i];
			}
		}

		if (enabled & ENABLE_BONES) {
			int numBones = vertTypeGetNumBoneWeights(gstate->vertType);
			command->draw.numBones = numBones;
			for (int i = 0; i < numBones; i++) {
				if (dirty & (STATE_BONE0 << i)) {
					command->draw.boneMatrix[i] = cmdPacket->numBoneMatrix;
					LoadMatrix4x3(arena->Allocate(&cmdPacket->boneMatrix[cmdPacket->numBoneMatrix++]), &gstate->boneMatrix[i * 12]);
					if (cmdPacket->numBoneMatrix >= ARRAY_SIZE(cmdPacket->boneMatrix) - 4) full = true;
				} else {
					command->draw.boneMatrix[i] = last->draw.boneMatrix[i];
				}
			}
			for (int i = numBones; i < 8; i++) {
				command->draw.boneMatrix[i] = last->draw.boneMatrix[i];
			}
		} else {
			for (int i = 0; i < 8; i++) {
				command->draw.boneMatrix[i] = last->draw.boneMatrix[i];
			}
		}
	} else {
		command->draw.viewport = last->draw.viewport;
		command->draw.worldMatrix = last->draw.worldMatrix;
		command->draw.viewMatrix = last->draw.viewMatrix;
		command->draw.projMatrix = last->draw.projMatrix;
		command->draw.lightGlobal = last->draw.lightGlobal;
		for (int i = 0; i < 4; i++) {
			command->draw.lights[i] = last->draw.lights[i];
		}
		for (int i = 0; i < 8; i++) {
			command->draw.boneMatrix[i] = last->draw.boneMatrix[i];
		}
	}

	if ((dirty & STATE_MORPH) && (enabled & ENABLE_MORPH)) {
		command->draw.morph = cmdPacket->numMorph;
		LoadMorphState(arena->Allocate(&cmdPacket->morph[cmdPacket->numMorph++]), gstate);
		if (cmdPacket->numMorph >= ARRAY_SIZE(cmdPacket->morph)) full = true;
		dirty &= ~STATE_MORPH;
	} else {
		command->draw.morph = last->draw.morph;
	}

	if (full) {
		cmdPacket->full = true;
	}

	// Morph, etc...
	return dirty;
}

void CommandSubmitTransfer(CommandPacket *cmdPacket, const GPUgstate *gstate) {
	Command *cmd = &cmdPacket->commands[cmdPacket->numCommands++];
	if (cmdPacket->numCommands == cmdPacket->maxCommands) cmdPacket->full = true;
	cmd->type = CMD_TRANSFER;
	cmd->transfer.srcPtr = gstate->getTransferSrcAddress();
	cmd->transfer.dstPtr = gstate->getTransferDstAddress();
	cmd->transfer.srcStride = gstate->getTransferSrcStride();
	cmd->transfer.dstStride = gstate->getTransferDstStride();
	cmd->transfer.srcX = gstate->getTransferSrcX();
	cmd->transfer.srcY = gstate->getTransferSrcY();
	cmd->transfer.dstX = gstate->getTransferDstX();
	cmd->transfer.dstY = gstate->getTransferDstY();
	cmd->transfer.width = gstate->getTransferWidth();
	cmd->transfer.height = gstate->getTransferHeight();
	cmd->transfer.bpp = gstate->getTransferBpp();
}

// Returns dirty flags
u32 CommandSubmitDraw(CommandPacket *cmdPacket, MemoryArena *arena, const GPUgstate *gstate, u32 dirty, u32 primAndCount, u32 vertexAddr, u32 indexAddr) {
	if (cmdPacket->full) {
		ELOG("Cannot submit draw commands to a full packet");
		return dirty;
	}

	Command *cmd = &cmdPacket->commands[cmdPacket->numCommands++];
	if (cmdPacket->numCommands == cmdPacket->maxCommands) cmdPacket->full = true;
	cmd->draw.count = primAndCount & 0xFFFF;
	int prim = (primAndCount >> 16) & 0xF;
	cmd->draw.prim = prim;
	cmd->draw.vtxAddr = vertexAddr;
	cmd->draw.idxAddr = indexAddr;
	switch (prim) {
	case GE_PRIM_LINES:
	case GE_PRIM_LINE_STRIP:
		cmd->type = CMD_DRAWLINE;
		break;
	case GE_PRIM_TRIANGLES:
	case GE_PRIM_TRIANGLE_STRIP:
	case GE_PRIM_TRIANGLE_FAN:
	case GE_PRIM_RECTANGLES:  // Rects get expanded into triangles later.
		cmd->type = CMD_DRAWTRI;
		break;
	case GE_PRIM_POINTS:
		cmd->type = CMD_DRAWPOINT;
		break;
	}
	u32 newDirty = LoadStates(cmdPacket, cmdPacket->lastDraw, cmd, arena, gstate, dirty);
	cmdPacket->lastDraw = cmd;
	return newDirty;
}

void CommandSubmitLoadClut(CommandPacket *cmdPacket, GPUgstate *gstate) {
	Command *cmd = &cmdPacket->commands[cmdPacket->numCommands++];
	if (cmdPacket->numCommands == cmdPacket->maxCommands) cmdPacket->full = true;
	cmd->type = CMD_LOADCLUT;
	cmd->clut.addr = gstate->getClutAddress();
	cmd->clut.bytes = gstate->getClutLoadBytes();
}

void CommandSubmitSync(CommandPacket *cmdPacket) {
	Command *cmd = &cmdPacket->commands[cmdPacket->numCommands++];
	if (cmdPacket->numCommands == cmdPacket->maxCommands) cmdPacket->full = true;
	cmd->type = CMD_SYNC;
}

static const char *cmdNames[8] = {
	"TRIS ",
	"LINES",
	"POINT",
	"BEZ ",
	"SPLIN",
	"XFER ",
	"SYNC ",
};

void PrintCommandPacket(CommandPacket *cmdPacket) {
	for (int i = 0; i < cmdPacket->numCommands; i++) {
		char line[1024];
		const Command &cmd = cmdPacket->commands[i];
		switch (cmd.type) {
		case CMD_DRAWTRI:
		case CMD_DRAWLINE:
		case CMD_DRAWPOINT:
			snprintf(line, sizeof(line), "DRAW %s : %d (v %08x, i %08x)", cmdNames[cmd.type], cmd.draw.count, cmd.draw.vtxAddr, cmd.draw.idxAddr);
			break;
		case CMD_TRANSFER:
			snprintf(line, sizeof(line), "%s : %dx%d", cmdNames[cmd.type], cmd.transfer.width, cmd.transfer.height);
			break;
		case CMD_SYNC:
			snprintf(line, sizeof(line), "%s", cmdNames[cmd.type]);
			break;

		default:
			snprintf(line, sizeof(line), "Bad command %d", cmd.type);
			break;
		}
	}
}

void CommandPacketInit(CommandPacket *cmdPacket, int size) {
	memset(cmdPacket, 0, sizeof(CommandPacket));
	cmdPacket->commands = new Command[size];
	cmdPacket->maxCommands = size;
}

void CommandPacketDeinit(CommandPacket *cmdPacket) {
	delete [] cmdPacket->commands;
}

}  // HighGpu
