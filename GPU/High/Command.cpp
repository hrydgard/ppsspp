#include "base/basictypes.h"
#include "base/logging.h"
#include "GPU/High/Command.h"
#include "GPU/GPUState.h"
#include "GPU/Common/TextureDecoder.h"

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
			for (int i = 0; i < 4; i++) {
				if (gstate->isLightChanEnabled(i)) val |= (ENABLE_LIGHT0 << i);
			}
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
		if (gstate->isTextureMapEnabled()) {
			val |= ENABLE_TEXTURE;
		}
		// TODO: Compute an enable flag for the tex matrix
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
	light->pos[0] = getFloat24(gstate->lpos[lightnum * 4 + 0]);
	light->pos[1] = getFloat24(gstate->lpos[lightnum * 4 + 1]);
	light->pos[2] = getFloat24(gstate->lpos[lightnum * 4 + 2]);
	if (light->type == GE_LIGHTTYPE_DIRECTIONAL) {
		memset(light->att, 0, sizeof(light->att));
	} else {
		light->att[0] = getFloat24(gstate->latt[lightnum * 4 + 0]);
		light->att[1] = getFloat24(gstate->latt[lightnum * 4 + 1]);
		light->att[2] = getFloat24(gstate->latt[lightnum * 4 + 2]);
	}
	if (light->type == GE_LIGHTTYPE_SPOT) {
		light->dir[0] = getFloat24(gstate->ldir[lightnum * 4 + 0]);
		light->dir[1] = getFloat24(gstate->ldir[lightnum * 4 + 1]);
		light->dir[2] = getFloat24(gstate->ldir[lightnum * 4 + 2]);
		light->lcutoff = getFloat24(gstate->lcutoff[lightnum]);
	} else {
		memset(light->dir, 0, sizeof(light->dir));
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

static void LoadFramebufState(FramebufState *framebuf, const GPUgstate *gstate) {
	framebuf->colorPtr = gstate->getFrameBufAddress();
	framebuf->colorStride = gstate->FrameBufStride();
	framebuf->colorFormat = gstate->FrameBufFormat();
	framebuf->depthPtr = gstate->getDepthBufAddress();
	framebuf->depthStride = gstate->DepthBufStride();
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
	// These are all from texmode
	texture->maxLevel = gstate->getTextureMaxLevel();
	texture->swizzled = gstate->isTextureSwizzled();
	texture->mipClutMode = gstate->isClutSharedForMipmaps();

	texture->format = gstate->getTextureFormat();
	if (gstate->isTextureFormatIndexed()) {
		texture->clutFormat = gstate->getClutPaletteFormat();
		texture->clutIndexMaskShiftStart = gstate->clutformat & (~3 & 0xFFFFFF);
	}
	// SIMD opportunity. Or we could just optimize and just not store dim for mip levels above 0...
	// We would have to accomodate the few games that uses degenerate mipmapping to blend between two equal-sized
	// textures though, perhaps with a flag.
	int i;
	for (i = 0; i < texture->maxLevel + 1; i++) {
		texture->addr[i] = gstate->getTextureAddress(i);
		texture->dim[i] = gstate->getTextureDimension(i);
		texture->stride[i] = gstate->getTextureStride(i);
	}

	// This can be removed when not debugging.
	for (; i < 8; i++) {
		texture->addr[i] = 0;
		texture->dim[i] = 0;
		texture->stride[i] = 0;
	}
}

static void LoadTexScaleState(TexScaleState *texScale, const GPUgstate *gstate) {
	// SIMDable
	texScale->scaleU = getFloat24(gstate->texscaleu);
	texScale->scaleV = getFloat24(gstate->texscalev);
	texScale->offsetU = getFloat24(gstate->texoffsetu);
	texScale->offsetV = getFloat24(gstate->texoffsetv);
}

static void LoadSamplerState(SamplerState *sampler, const GPUgstate *gstate) {
	sampler->mag = gstate->getTexMagFilter();
	sampler->min = gstate->getTexMinFilter();
	sampler->bias = gstate->getTexLodBias();
	sampler->clamp_s = gstate->isTexCoordClampedS();
	sampler->clamp_t = gstate->isTexCoordClampedT();
	sampler->levelMode = gstate->getTexLevelMode();
}

static void LoadClutState(ClutState *clut, const u8 *clutBuffer, MemoryArena *arena, const GPUgstate *gstate) {
	int bytes = gstate->getClutLoadBytes();
	u8 *data = arena->AllocateAligned(bytes, 16);
	memcpy(data, clutBuffer, bytes);
	clut->data = data;
	clut->size = bytes;
	// TODO: We should compute this the same way the old texcache did, taking into account the clut base etc.
	clut->hash = DoReliableHash32((const char *)data, bytes, 0xC0108888);
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

// Only send in a single bit!
const char *StateBitToString(u32 bit) {
	switch (bit) {
	case STATE_FRAMEBUF:     return "FRAMEBUF";
	case STATE_BLEND:        return "BLEND";
	case STATE_FRAGMENT:     return "FRAGMENT";
	case STATE_WORLDMATRIX:  return "WORLDMATRIX";
	case STATE_VIEWMATRIX:   return "VIEWMATRIX";
	case STATE_PROJMATRIX:   return "PROJMATRIX";
	case STATE_TEXMATRIX:    return "TEXMATRIX";
	case STATE_TEXTURE:      return "TEXTURE";
	case STATE_DEPTHSTENCIL: return "DEPTHSTENCIL";
	case STATE_MORPH:        return "MORPH";
	case STATE_RASTERIZER:   return "RASTERIZER";
	case STATE_SAMPLER:      return "SAMPLER";
	case STATE_CLUT:         return "CLUT";
	case STATE_TEXSCALE:     return "TEXSCALE";
	case STATE_VIEWPORT:     return "VIEWPORT";
	case STATE_LIGHTGLOBAL:  return "LIGHTGLOBAL";
	case STATE_LIGHT0:       return "LIGHT0";
	case STATE_LIGHT1:       return "LIGHT1";
	case STATE_LIGHT2:       return "LIGHT2";
	case STATE_LIGHT3:       return "LIGHT3";
	case STATE_BONE0:        return "BONE0";
	case STATE_BONE1:        return "BONE1";
	case STATE_BONE2:        return "BONE2";
	case STATE_BONE3:        return "BONE3";
	case STATE_BONE4:        return "BONE4";
	case STATE_BONE5:        return "BONE5";
	case STATE_BONE6:        return "BONE6";
	case STATE_BONE7:        return "BONE7";
	case 0:                  return "none";
	default:
		return "(unknown)";
	}
}

// TODO: De-duplicate states, looking a couple of items back in each list.
// This algorithm can be refined in the future.
static u32 LoadStates(CommandPacket *cmdPacket, const Command *last, Command *command, MemoryArena *arena, const GPUgstate *gstate, u32 dirty) {
	// Early out for repeated commands with no state changes in between.
	if (!dirty) {
		command->draw.enabled = last->draw.enabled;
		return dirty;
	}

	u32 enabled = (dirty & STATE_ENABLES) ? LoadEnables(gstate) : last->draw.enabled;
	command->draw.enabled = enabled;

	u32 full = 0;

	if (dirty & STATE_FRAMEBUF) {
		command->draw.framebuf = cmdPacket->numFramebuf;
		LoadFramebufState(arena->Allocate(&cmdPacket->framebuf[cmdPacket->numFramebuf++]), gstate);
		if (cmdPacket->numFramebuf == ARRAY_SIZE(cmdPacket->framebuf)) full = STATE_FRAMEBUF;
		dirty &= ~STATE_FRAMEBUF;
	} else {
		command->draw.framebuf = last->draw.framebuf;
	}

	// Regardless of Enabled flags, there's always a rasterizer state.
	if (dirty & STATE_RASTERIZER) {
		command->draw.raster = cmdPacket->numRaster;
		LoadRasterState(arena->Allocate(&cmdPacket->raster[cmdPacket->numRaster++]), gstate);
		// Check if state was just toggled. In that case we simply reuse the last index.
		// TODO: Find a generic way to add this check to all types of states.
		if (cmdPacket->numRaster > 1 && !memcmp(cmdPacket->raster[cmdPacket->numRaster-2], cmdPacket->raster[cmdPacket->numRaster-1], sizeof(RasterState))) {
			cmdPacket->numRaster--;
			arena->Rewind(sizeof(RasterState));
			command->draw.raster = cmdPacket->numRaster - 1;
		}
		if (cmdPacket->numRaster == ARRAY_SIZE(cmdPacket->raster)) full = STATE_RASTERIZER;
		dirty &= ~STATE_RASTERIZER;
	} else {
		command->draw.raster = last->draw.raster;
	}

	if (dirty & STATE_FRAGMENT) {
		command->draw.fragment = cmdPacket->numFragment;
		LoadFragmentState(arena->Allocate(&cmdPacket->fragment[cmdPacket->numFragment++]), gstate);
		if (cmdPacket->numFragment == ARRAY_SIZE(cmdPacket->fragment)) full = STATE_FRAGMENT;
		dirty &= ~STATE_FRAGMENT;
	} else {
		command->draw.fragment = last->draw.fragment;
	}

	if ((enabled & (ENABLE_BLEND|ENABLE_ALPHA_TEST|ENABLE_COLOR_TEST)) && (dirty & STATE_BLEND)) {
		command->draw.blend = cmdPacket->numBlend;
		LoadBlendState(arena->Allocate(&cmdPacket->blend[cmdPacket->numBlend++]), gstate);
		if (cmdPacket->numBlend == ARRAY_SIZE(cmdPacket->blend)) full = STATE_BLEND;
		dirty &= ~STATE_BLEND;
	} else {
		command->draw.blend = last->draw.blend;
	}

	if (enabled & (ENABLE_DEPTH_TEST|ENABLE_STENCIL_TEST) && (dirty & STATE_DEPTHSTENCIL)) {
		command->draw.depthStencil = cmdPacket->numDepthStencil;
		LoadDepthStencilState(arena->Allocate(&cmdPacket->depthStencil[cmdPacket->numDepthStencil++]), gstate);
		if (cmdPacket->numDepthStencil == ARRAY_SIZE(cmdPacket->depthStencil)) full = STATE_DEPTHSTENCIL;
		dirty &= ~STATE_DEPTHSTENCIL;
	} else {
		command->draw.depthStencil = last->draw.depthStencil;
	}

	if (enabled & ENABLE_TEXTURE) {
		if (dirty & STATE_TEXTURE) {
			command->draw.texture = cmdPacket->numTexture;
			LoadTextureState(arena->Allocate(&cmdPacket->texture[cmdPacket->numTexture++]), gstate);
			if (cmdPacket->numTexture == ARRAY_SIZE(cmdPacket->texture)) full = STATE_TEXTURE;
			dirty &= ~STATE_TEXTURE;
		} else {
			command->draw.texture = last->draw.texture;
		}
		if ((enabled && ENABLE_TRANSFORM) && (dirty & STATE_TEXSCALE)) {
			command->draw.texScale = cmdPacket->numTexScale;
			LoadTexScaleState(arena->Allocate(&cmdPacket->texScale[cmdPacket->numTexScale++]), gstate);
			if (cmdPacket->numTexScale == ARRAY_SIZE(cmdPacket->texScale)) full = STATE_TEXSCALE;
			dirty &= ~STATE_TEXSCALE;
		} else {
			command->draw.texScale = last->draw.texScale;
		}
		if (dirty & STATE_SAMPLER) {
			command->draw.sampler = cmdPacket->numSampler;
			LoadSamplerState(arena->Allocate(&cmdPacket->sampler[cmdPacket->numSampler++]), gstate);
			if (cmdPacket->numSampler == ARRAY_SIZE(cmdPacket->sampler)) full = STATE_SAMPLER;
			dirty &= ~STATE_SAMPLER;
		} else {
			command->draw.sampler = last->draw.sampler;
		}
		if (dirty & STATE_CLUT) {
			command->draw.clut = cmdPacket->numClut;
			LoadClutState(arena->Allocate(&cmdPacket->clut[cmdPacket->numClut++]), cmdPacket->clutBuffer, arena, gstate);
			if (cmdPacket->numClut == ARRAY_SIZE(cmdPacket->clut)) full = STATE_CLUT;
			dirty &= ~STATE_CLUT;
		} else {
			command->draw.clut = last->draw.clut;
		}
	} else {
		command->draw.texture = last->draw.texture;
		command->draw.texScale = last->draw.texScale;
		command->draw.sampler = last->draw.sampler;
		command->draw.clut = last->draw.clut;
	}

	if (enabled & ENABLE_TRANSFORM) {
		if (dirty & STATE_VIEWPORT) {
			command->draw.viewport = cmdPacket->numViewport;
			LoadViewportState(arena->Allocate(&cmdPacket->viewport[cmdPacket->numViewport++]), gstate);
			if (cmdPacket->numViewport == ARRAY_SIZE(cmdPacket->viewport)) full = STATE_VIEWPORT;
			dirty &= ~STATE_VIEWPORT;
		} else {
			command->draw.viewport = last->draw.viewport;
		}
		if (dirty & STATE_WORLDMATRIX) {
			command->draw.worldMatrix = cmdPacket->numWorldMatrix;
			LoadMatrix4x3(arena->Allocate(&cmdPacket->worldMatrix[cmdPacket->numWorldMatrix++]), gstate->worldMatrix);
			if (cmdPacket->numWorldMatrix == ARRAY_SIZE(cmdPacket->worldMatrix)) full = STATE_WORLDMATRIX;
			dirty &= ~STATE_WORLDMATRIX;
		} else {
			command->draw.worldMatrix = last->draw.worldMatrix;
		}
		if (dirty & STATE_VIEWMATRIX) {
			command->draw.viewMatrix = cmdPacket->numViewMatrix;
			LoadMatrix4x3(arena->Allocate(&cmdPacket->viewMatrix[cmdPacket->numViewMatrix++]), gstate->viewMatrix);
			if (cmdPacket->numViewMatrix == ARRAY_SIZE(cmdPacket->viewMatrix)) full = STATE_VIEWMATRIX;
			dirty &= ~STATE_VIEWMATRIX;
		} else {
			command->draw.viewMatrix = last->draw.viewMatrix;
		}
		if (dirty & STATE_PROJMATRIX) {
			command->draw.projMatrix = cmdPacket->numProjMatrix;
			LoadMatrix4x4(arena->Allocate(&cmdPacket->projMatrix[cmdPacket->numProjMatrix++]), gstate->projMatrix);
			if (cmdPacket->numProjMatrix == ARRAY_SIZE(cmdPacket->projMatrix)) full = STATE_WORLDMATRIX;
			dirty &= ~STATE_PROJMATRIX;
		} else {
			command->draw.projMatrix = last->draw.projMatrix;
		}
		if (dirty & STATE_TEXMATRIX) {
			command->draw.texMatrix = cmdPacket->numTexMatrix;
			LoadMatrix4x3(arena->Allocate(&cmdPacket->texMatrix[cmdPacket->numTexMatrix++]), gstate->tgenMatrix);
			if (cmdPacket->numTexMatrix == ARRAY_SIZE(cmdPacket->texMatrix)) full = STATE_TEXMATRIX;
			dirty &= ~STATE_TEXMATRIX;
		} else {
			command->draw.texMatrix = last->draw.texMatrix;
		}
		if (enabled & ENABLE_LIGHTS) {
			if (dirty & STATE_VIEWPORT) {
				command->draw.lightGlobal = cmdPacket->numLightGlobal;
				LoadLightGlobalState(arena->Allocate(&cmdPacket->lightGlobal[cmdPacket->numLightGlobal++]), gstate);
				if (cmdPacket->numLightGlobal == ARRAY_SIZE(cmdPacket->lightGlobal)) full = STATE_VIEWPORT;
				dirty &= ~STATE_VIEWPORT;
			} else {
				command->draw.lightGlobal = last->draw.lightGlobal;
			}
			for (int i = 0; i < 4; i++) {
				if ((enabled & (ENABLE_LIGHT0 << i)) && (dirty & (STATE_LIGHT0 << i))) {
					command->draw.lights[i] = cmdPacket->numLight;
					LoadLightState(arena->Allocate(&cmdPacket->lights[cmdPacket->numLight++]), gstate, i);
					if (cmdPacket->numLight >= ARRAY_SIZE(cmdPacket->lights) - 4) full = STATE_LIGHT0 << i;
				} else {
					command->draw.lights[i] = last->draw.lights[i];
				}
			}
		} else {
			command->draw.lightGlobal = last->draw.lightGlobal;
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
					if (cmdPacket->numBoneMatrix >= ARRAY_SIZE(cmdPacket->boneMatrix) - 4) full = STATE_BONE0 << i;
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

		if ((dirty & STATE_MORPH) && (enabled & ENABLE_MORPH)) {
			command->draw.morph = cmdPacket->numMorph;
			LoadMorphState(arena->Allocate(&cmdPacket->morph[cmdPacket->numMorph++]), gstate);
			if (cmdPacket->numMorph >= ARRAY_SIZE(cmdPacket->morph)) full = STATE_MORPH;
			dirty &= ~STATE_MORPH;
		} else {
			command->draw.morph = last->draw.morph;
		}
	} else {
		command->draw.viewport = last->draw.viewport;
		command->draw.worldMatrix = last->draw.worldMatrix;
		command->draw.viewMatrix = last->draw.viewMatrix;
		command->draw.projMatrix = last->draw.projMatrix;
		command->draw.texMatrix = last->draw.texMatrix;
		command->draw.lightGlobal = last->draw.lightGlobal;
		for (int i = 0; i < 4; i++) {
			command->draw.lights[i] = last->draw.lights[i];
		}
		for (int i = 0; i < 8; i++) {
			command->draw.boneMatrix[i] = last->draw.boneMatrix[i];
		}
		command->draw.morph = last->draw.morph;
	}

	if (full) {
		cmdPacket->full = full;
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
		ELOG("Cannot submit draw commands to a full packet (full: %08x %s)", cmdPacket->full, StateBitToString(cmdPacket->full));
		return dirty;
	}

	Command *cmd = &cmdPacket->commands[cmdPacket->numCommands++];
	if (cmdPacket->numCommands == cmdPacket->maxCommands) cmdPacket->full = true;
	cmd->draw.count = primAndCount & 0xFFFF;
	int prim = (primAndCount >> 16) & 0xF;
	cmd->draw.prim = prim;
	cmd->draw.vtxformat = gstate->vertType;
	cmd->draw.vtxAddr = vertexAddr;
	cmd->draw.idxAddr = indexAddr;
	cmd->type = CMD_DRAWPRIM;
	u32 newDirty = LoadStates(cmdPacket, cmdPacket->lastDraw, cmd, arena, gstate, dirty);
	cmdPacket->lastDraw = cmd;
	return newDirty;
}

static const char *primNames[8] = {
	"POINTS",
	"LINES",
	"LINESTRIP",
	"TRIANGLES",
	"TRISTRIP",
	"TRIFAN",
	"RECTANGLE",
	"CONTINUE",  // :(
};


void LogMatrix4x3(const char *name, int i, const Matrix4x3 *m) {
	ILOG("%s matrix %d: [[%f, %f, %f][%f, %f, %f][%f, %f, %f][%f, %f, %f]]",
			name, i,
			m->v[0], m->v[1], m->v[2], m->v[3], m->v[4], m->v[5], m->v[6], m->v[7], m->v[8], m->v[9], m->v[10], m->v[11]);
}

void LogMatrix4x4(const char *name, int i, const Matrix4x4 *m) {
	ILOG("%s matrix %d: [[%f, %f, %f, %f][%f, %f, %f, %f][%f, %f, %f, %f][%f, %f, %f, %f]]",
			name, i,
			m->v[0], m->v[1], m->v[2], m->v[3],
			m->v[4], m->v[5], m->v[6], m->v[7],
			m->v[8], m->v[9], m->v[10], m->v[11],
			m->v[12], m->v[13], m->v[14], m->v[15]);
}

void PrintCommandPacket(CommandPacket *cmdPacket) {
	ILOG("========= Commands: %d (full: %s) ==========", cmdPacket->numCommands, StateBitToString(cmdPacket->full));
	for (int i = 0; i < cmdPacket->numCommands; i++) {
		char line[1024];
		const Command &cmd = cmdPacket->commands[i];
		switch (cmd.type) {
		case CMD_DRAWPRIM:
			snprintf(line, sizeof(line), "DRAW %s : %d fmt %08x (v %08x, i %x) enabled: %08x : "
					"frbuf:%d rast:%d frag:%d vport:%d text:%d ts:%d samp:%d "
					"blend:%d depth:%d "
					"world:%d view:%d proj:%d tex:%d "
					"lg:%d l0:%d l1:%d l2:%d l3:%d "
					"morph:%d b0:%d",
					primNames[cmd.draw.prim], cmd.draw.count, cmd.draw.vtxformat, cmd.draw.vtxAddr, cmd.draw.idxAddr, cmd.draw.enabled,
					cmd.draw.framebuf, cmd.draw.raster, cmd.draw.fragment, cmd.draw.viewport, cmd.draw.texture, cmd.draw.texScale, cmd.draw.sampler,
					cmd.draw.blend, cmd.draw.depthStencil,
					cmd.draw.worldMatrix, cmd.draw.viewMatrix, cmd.draw.projMatrix, cmd.draw.texMatrix,
					cmd.draw.lightGlobal, cmd.draw.lights[0], cmd.draw.lights[1], cmd.draw.lights[2], cmd.draw.lights[3],
					cmd.draw.morph, cmd.draw.boneMatrix[0]);
			break;
		case CMD_TRANSFER:
			snprintf(line, sizeof(line), "TRANSFER : %dx%d", cmd.transfer.width, cmd.transfer.height);
			break;

		default:
			snprintf(line, sizeof(line), "Bad command %d", cmd.type);
			break;
		}
		ILOG("%d: %s", i, line);
	}
	ILOG("========= State Blocks =========");
	for (int i = 0; i < cmdPacket->numFramebuf; i++) {
		const FramebufState *f = cmdPacket->framebuf[i];
		ILOG("Framebuf %d: addr: %08x stride: %d fmt: %d zaddr: %08x stride: %d", i, f->colorPtr, f->colorStride, f->colorFormat, f->depthPtr, f->depthStride);
	}
	for (int i = 0; i < cmdPacket->numRaster; i++) {
		const RasterState *r = cmdPacket->raster[i];
		ILOG("Raster %d: clearmode=%d scissor: %d,%d-%d,%d offset: %d,%d, cull: %d,%d defcol:%08x", i, r->clearMode, r->scissorX1, r->scissorY1, r->scissorX2, r->scissorY2, r->offsetX, r->offsetY, r->cullFaceEnable, r->cullMode, r->defaultVertexColor);

	}
	for (int i = 0; i < cmdPacket->numFragment; i++) {
		const FragmentState *f = cmdPacket->fragment[i];
		ILOG("Fragment %d: double=%d texFunc=%d texEnv=%d shadeMode=%d fog=%.3f,%.3f,%06x",
				i, f->colorDouble, f->texFunc, f->texEnvColor, f->shadeMode, f->fogCoef1, f->fogCoef2, f->fogColor);
	}
	for (int i = 0; i < cmdPacket->numTexture; i++) {
		const TextureState *t = cmdPacket->texture[i];
		ILOG("Texture %d: format: %d addr[0]: %08x  dim[0]: %04x  stride[0]=%d",
			i, t->format, t->addr[0], t->dim[0], t->stride[0]);
	}
	for (int i = 0; i < cmdPacket->numTexScale; i++) {
		const TexScaleState *t = cmdPacket->texScale[i];
		ILOG("TexScale %d: %f x %f, offset %f x %f",
			i, t->scaleU, t->scaleV, t->offsetU, t->offsetV);
	}
	for (int i = 0; i < cmdPacket->numSampler; i++) {
		const SamplerState *s = cmdPacket->sampler[i];
		ILOG("Sampler %d: mag=%d min=%d bias=%d clamp_s=%d clamp_t=%d levelMode=%d",
				i, s->mag, s->min, s->bias, s->clamp_s, s->clamp_t, s->levelMode);
	}
	for (int i = 0; i < cmdPacket->numClut; i++) {
		const ClutState *c = cmdPacket->clut[i];
		ILOG("Clut %d: hash=%08x size=%d ptr=%p",
				i, c->hash, c->size, c->data);
	}
	for (int i = 0; i < cmdPacket->numBlend; i++) {
		const BlendState *b = cmdPacket->blend[i];
		ILOG("Blend %d: src:%d dst:%d eq:%d fixa:%06x fixb:%06x ",
				i, b->blendSrc, b->blendDst, b->blendEq, b->blendfixa, b->blendfixb);
	}
	for (int i = 0; i < cmdPacket->numDepthStencil; i++) {
		const DepthStencilState *b = cmdPacket->depthStencil[i];
		ILOG("DepthStencil %d: dfunc=%d dwrite=%d sfunc=%d sref=%02x smask=%02x sfail=%d szfail=%d szpass=%d",
				i, b->depthFunc, b->depthWriteEnable, b->stencilFunc, b->stencilRef, b->stencilMask, b->stencilOpSFail, b->stencilOpZFail, b->stencilOpZPass);
	}
	for (int i = 0; i < cmdPacket->numViewport; i++) {
		const ViewportState *v = cmdPacket->viewport[i];
		ILOG("Viewport %d: C: %f, %f, %f  SZ: %f, %f, %f", i, v->x1, v->y1, v->z1, v->x2, v->y2, v->z2);
	}
	for (int i = 0; i < cmdPacket->numWorldMatrix; i++) {
		LogMatrix4x3("World", i, cmdPacket->worldMatrix[i]);
	}
	for (int i = 0; i < cmdPacket->numViewMatrix; i++) {
		LogMatrix4x3("View", i, cmdPacket->viewMatrix[i]);
	}
	for (int i = 0; i < cmdPacket->numProjMatrix; i++) {
		LogMatrix4x4("Proj", i, cmdPacket->projMatrix[i]);
	}
	for (int i = 0; i < cmdPacket->numTexMatrix; i++) {
		LogMatrix4x3("Tex", i, cmdPacket->texMatrix[i]);
	}
	for (int i = 0; i < cmdPacket->numBoneMatrix; i++) {
		LogMatrix4x3("Bone", i, cmdPacket->boneMatrix[i]);
	}
	for (int i = 0; i < cmdPacket->numLightGlobal; i++) {
		const LightGlobalState *lg = cmdPacket->lightGlobal[i];
		ILOG("LightGlobal %d: MU: %d matambient: %08x diff: %06x spec: %06x emiss: %06x speccoef: %f lmode: %d", i, lg->materialUpdate, lg->materialAmbient, lg->materialDiffuse, lg->materialSpecular, lg->materialEmissive, lg->specularCoef, lg->lmode);
	}
	for (int i = 0; i < cmdPacket->numLight; i++) {
		const LightState *l = cmdPacket->lights[i];
		switch (l->type) {
		case GE_LIGHTTYPE_DIRECTIONAL:
			ILOG("Light %d: DIRECTIONAL dir=%f %f %f", i, l->pos[0], l->pos[1], l->pos[2]);
			break;
		case GE_LIGHTTYPE_POINT:
			ILOG("Light %d: POINT att=%f %f %f pos=%f %f %f", i, l->att[0], l->att[1], l->att[2], l->pos[0], l->pos[1], l->pos[2]);
			break;
		case GE_LIGHTTYPE_SPOT:
			ILOG("Light %d: SPOT pos=%f %f %f dir=%f %f %f cutoff=%f", i, l->pos[0], l->pos[1], l->pos[2], l->dir[0], l->dir[1], l->dir[2], l->lcutoff);
			break;
		}
	}
}

void CommandPacketInit(CommandPacket *cmdPacket, int size, u8 *clutBuffer) {
	memset(cmdPacket, 0, sizeof(CommandPacket));
	cmdPacket->commands = new Command[size];
	cmdPacket->maxCommands = size;
	cmdPacket->clutBuffer = clutBuffer;
}

void CommandPacketReset(CommandPacket *cmdPacket, const Command *dummyDraw) {
	// Save the two pieces of state that remains relevant.
	Command *temp = cmdPacket->commands;
	int max = cmdPacket->maxCommands;
	u8 *clutBuf = cmdPacket->clutBuffer;
	memset(cmdPacket, 0, sizeof(CommandPacket));
	cmdPacket->commands = temp;
	cmdPacket->maxCommands = max;

	// Create the dummy command that the first draw can diff against.
	cmdPacket->numCommands = 0;
	cmdPacket->lastDraw = dummyDraw;
	cmdPacket->clutBuffer = clutBuf;
}

void CommandPacketDeinit(CommandPacket *cmdPacket) {
	delete [] cmdPacket->commands;
	cmdPacket->commands = nullptr;
}

void CommandInitDummyDraw(Command *cmd) {
	memset(cmd, 0xFF, sizeof(*cmd));
	cmd->type = CMD_DRAWPRIM;
	cmd->draw.prim = GE_PRIM_INVALID;
	cmd->draw.count = 0;
	cmd->draw.enabled = 0;
}

}  // HighGpu
