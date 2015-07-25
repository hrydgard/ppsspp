#include "GPU/High/Command.h"
#include "GPU/GPUState.h"

namespace HighGpu {

// Collects all the "enable" flags. Note that they don't currently correspond perfectly with
// the "state chunks" we have defined, some state chunks cover multiple of these, and some of these
static u32 LoadEnables(const GPUgstate *gstate) {
	u32 val = 0;
	if (!gstate->isModeThrough()) val |= ENABLE_TRANSFORM;
	if (gstate->isAlphaBlendEnabled()) val |= ENABLE_BLEND;
	if (gstate->isAlphaTestEnabled()) val |= ENABLE_ALPHA_TEST;
	if (gstate->isColorTestEnabled()) val |= ENABLE_COLOR_TEST;
	if (gstate->isFogEnabled()) val |= ENABLE_FOG;
	if (gstate->isLogicOpEnabled()) val |= ENABLE_LOGIC_OP;
	if (gstate->isStencilTestEnabled()) val |= ENABLE_STENCIL_TEST;
	if (gstate->isDepthTestEnabled()) val |= ENABLE_DEPTH_TEST;
	if (gstate->isTextureMapEnabled()) val |= ENABLE_TEXTURE;
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
	light->materialupdate = gstate->getMaterialUpdate();
	light->materialambient = gstate->getMaterialAmbientRGBA();
	light->specularCoef = gstate->getMaterialSpecularCoef();
	light->materialemissive = gstate->getMaterialEmissive();
	light->materialspecular = gstate->getMaterialSpecular();
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

static void LoadRasterState(RasterState *raster, const GPUgstate *gstate) {
	raster->scissorX1 = gstate->getScissorX1();
	raster->scissorY1 = gstate->getScissorY1();
	raster->scissorX2 = gstate->getScissorX2();
	raster->scissorY2 = gstate->getScissorY2();
	raster->fogCoef1 = gstate->getFogCoef1();
	raster->fogCoef2 = gstate->getFogCoef2();
	raster->cullFaceEnable = gstate->isCullEnabled();
	raster->cullMode = gstate->getCullMode();
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
	depthStencil->depthWrite = gstate->isDepthWriteEnabled();
}

inline void LoadMatrix4x3(Matrix4x3 *mtx, const float *data) {
	memcpy(mtx, data, 12 * sizeof(float));
}

inline void LoadMatrix4x4(Matrix4x4 *mtx, const float *data) {
	memcpy(mtx, data, 16 * sizeof(float));
}

// TODO: De-duplicate states, looking a couple of items back in each list.
u32 LoadStates(CommandPacket *cmdPacket, Command *command, const GPUgstate *gstate, u32 dirty) {
	u32 enabled = LoadEnables(gstate);
	command->draw.enabled = enabled;
	if (!dirty) {
		return dirty;
	}
	if (enabled & (ENABLE_BLEND|ENABLE_ALPHA_TEST|ENABLE_COLOR_TEST)) {
		if (dirty & STATE_BLEND) {
			command->draw.blend = cmdPacket->numBlend;
			LoadBlendState(&cmdPacket->blend[cmdPacket->numBlend++], gstate);
			dirty &= ~STATE_BLEND;
		} else {
			command->draw.blend = cmdPacket->numBlend - 1;
		}
	} else {
		command->draw.blend = -1;
	}

	// TODO: update the rest
	if ((dirty & STATE_DEPTHSTENCIL) && (enabled & (ENABLE_DEPTH_TEST|ENABLE_STENCIL_TEST))) {
		command->draw.depthStencil = cmdPacket->numDepthStencil;
		LoadDepthStencilState(&cmdPacket->depthStencil[cmdPacket->numDepthStencil++], gstate);
		dirty &= ~STATE_DEPTHSTENCIL;
	}
	if ((dirty & STATE_WORLDMATRIX) && (enabled & ENABLE_TRANSFORM)) {
		command->draw.worldMatrix = cmdPacket->numWorldMatrix;
		LoadMatrix4x3(&cmdPacket->worldMatrix[cmdPacket->numWorldMatrix++], gstate->worldMatrix);
		dirty &= ~STATE_WORLDMATRIX;
	}
	if ((dirty & STATE_VIEWMATRIX) && (enabled & ENABLE_TRANSFORM)) {
		command->draw.viewMatrix = cmdPacket->numViewMatrix;
		LoadMatrix4x3(&cmdPacket->viewMatrix[cmdPacket->numViewMatrix++], gstate->viewMatrix);
		dirty &= ~STATE_VIEWMATRIX;
	}
	if ((dirty & STATE_PROJMATRIX) && (enabled & ENABLE_TRANSFORM)) {
		command->draw.projMatrix = cmdPacket->numProjMatrix;
		LoadMatrix4x4(&cmdPacket->projMatrix[cmdPacket->numProjMatrix++], gstate->projMatrix);
		dirty &= ~STATE_PROJMATRIX;
	}

	if (enabled & ENABLE_LIGHTS) {
		for (int i = 0; i < 4; i++) {
			if (dirty & (STATE_LIGHT0 << i)) {
				command->draw.lights[0] = cmdPacket->numLights;
				LoadLightState(&cmdPacket->lights[cmdPacket->numLights++], gstate, i);
			}
		}
	}
	// Morph, etc...
	return dirty;
}

void CommandSubmitTransfer(CommandPacket *cmdPacket, const GPUgstate *gstate) {
	Command *cmd = &cmdPacket->commands[cmdPacket->numCommands++];
	cmd->type = CMD_TRANSFER;
	cmd->srcPtr = gstate->getTransferSrcAddress();
	cmd->dstPtr = gstate->getTransferDstAddress();
	cmd->srcStride = gstate->getTransferSrcStride();
	cmd->dstStride = gstate->getTransferDstStride();
	cmd->srcX = gstate->getTransferSrcX();
	cmd->srcY = gstate->getTransferSrcY();
	cmd->dstX = gstate->getTransferDstX();
	cmd->dstY = gstate->getTransferDstY();
	cmd->width = gstate->getTransferWidth();
	cmd->height = gstate->getTransferHeight();
	cmd->bpp = gstate->getTransferBpp();
}

// Returns dirty flags
u32 CommandSubmitDraw(CommandPacket *cmdPacket, const GPUgstate *gstate, u32 dirty, u32 data) {
	int numCmd = cmdPacket->numCommands++;
	Command *cmd = &cmdPacket->commands[numCmd];
	cmd->draw.count = data & 0xFFFF;
	int prim = (data >> 16) & 0xF;
	cmd->draw.prim = prim;
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
	return LoadStates(cmdPacket, gstate, numCmd, dirty);
}

void CommandSubmitLoadClut(CommandPacket *cmdPacket, GPUgstate *gstate) {
	Command *cmd = &cmdPacket->commands[numCmd];
	cmd->type = CMD_LOADCLUT;
	cmd->clut.addr = gstate->getClutAddress();
	cmd->clut.bytes = gstate->getClutLoadBytes();
}

void CommandSubmitSync(CommandPacket *cmdPacket) {
	int numCmd = cmdPacket->numCommands++;
	Command *cmd = &cmdPacket->commands[numCmd];
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

void PrintCommandPacket(CommandPacket *cmdPacket, int start, int count) {
	for (int i = start; i < count; i++) {
		char line[1024];
		const Command &cmd = cmdPacket->commands[i];
		switch (cmd.type) {
		case CMD_DRAWTRI:
		case CMD_DRAWLINE:
		case CMD_DRAWPOINT:
		case CMD_BEZIER:
		case CMD_SPLINE:
			snprintf(line, sizeof(line), "DRAW %s : %d", cmdNames[cmd.type], cmd.draw.count);
			break;
		case CMD_TRANSFER:
			snprintf(line, sizeof(line), "%s : %dx%d", cmdNames[cmd.type], cmd.transfer.width, cmd.transfer.height);
			break;
		case CMD_SYNC:
			snprintf(line, sizeof(line), "%s", cmdNames[cmd.type]);
			break;

		default:
			snprintf(line, "Bad command %d", type);
		}
	}
}

}  // HighGpu
