
#include "GPU/GX2/GX2Shaders.h"
#include <wiiu/gx2/common.h>
#include <wiiu/gx2/shaders_asm.h>

// clang-format off
__attribute__((aligned(GX2_SHADER_ALIGNMENT)))
static u64 depalVCode [32] =
{
	CALL_FS NO_BARRIER,
	EXP_DONE(POS0,   _R1, _x, _y, _z, _1),
	EXP_DONE(PARAM0, _R2, _x, _y, _0, _0) NO_BARRIER
	END_OF_PROGRAM
};
// clang-format on

GX2VertexShader defVShaderGX2 = {
	{
		.sq_pgm_resources_vs.num_gprs = 3,
		.sq_pgm_resources_vs.stack_size = 1,
		.spi_vs_out_config.vs_export_count = 1,
		.num_spi_vs_out_id = 1,
		{
			{ .semantic_0 = 0x00, .semantic_1 = 0xFF, .semantic_2 = 0xFF, .semantic_3 = 0xFF },
			{ .semantic_0 = 0xFF, .semantic_1 = 0xFF, .semantic_2 = 0xFF, .semantic_3 = 0xFF },
			{ .semantic_0 = 0xFF, .semantic_1 = 0xFF, .semantic_2 = 0xFF, .semantic_3 = 0xFF },
			{ .semantic_0 = 0xFF, .semantic_1 = 0xFF, .semantic_2 = 0xFF, .semantic_3 = 0xFF },
			{ .semantic_0 = 0xFF, .semantic_1 = 0xFF, .semantic_2 = 0xFF, .semantic_3 = 0xFF },
			{ .semantic_0 = 0xFF, .semantic_1 = 0xFF, .semantic_2 = 0xFF, .semantic_3 = 0xFF },
			{ .semantic_0 = 0xFF, .semantic_1 = 0xFF, .semantic_2 = 0xFF, .semantic_3 = 0xFF },
			{ .semantic_0 = 0xFF, .semantic_1 = 0xFF, .semantic_2 = 0xFF, .semantic_3 = 0xFF },
			{ .semantic_0 = 0xFF, .semantic_1 = 0xFF, .semantic_2 = 0xFF, .semantic_3 = 0xFF },
			{ .semantic_0 = 0xFF, .semantic_1 = 0xFF, .semantic_2 = 0xFF, .semantic_3 = 0xFF },
		},
		.sq_vtx_semantic_clear = ~0x3,
		.num_sq_vtx_semantic = 2,
		{ 0, 1, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff },
		.vgt_vertex_reuse_block_cntl.vtx_reuse_depth = 0xE,
		.vgt_hos_reuse_depth.reuse_depth = 0x10,
	}, /* regs */
	sizeof(depalVCode),
	(uint8_t *)&depalVCode,
	GX2_SHADER_MODE_UNIFORM_BLOCK,
	.gx2rBuffer.flags = GX2R_RESOURCE_LOCKED_READ_ONLY,
};

// clang-format off
__attribute__((aligned(GX2_SHADER_ALIGNMENT))) static struct {
	u64 cf[32];
	u64 tex[1 * 2];
} quadPCode = {
	{
		TEX(32, 1) VALID_PIX,
		EXP_DONE(PIX0, _R0, _r, _g, _b, _a)
		END_OF_PROGRAM
	},
	{
		TEX_SAMPLE(_R0, _r, _g, _b, _a, _R0, _x, _y, _0, _0, _t0, _s0)
	}
};
// clang-format on

GX2PixelShader defPShaderGX2 = {
	{
		.sq_pgm_resources_ps.num_gprs = 1,
		.sq_pgm_exports_ps.export_mode = 0x2,
		.spi_ps_in_control_0.num_interp = 1,
		.spi_ps_in_control_0.persp_gradient_ena = 1,
		.spi_ps_in_control_0.baryc_sample_cntl = spi_baryc_cntl_centers_only,
		.num_spi_ps_input_cntl = 1,
		{ { .semantic = 0, .default_val = 1 } },
		.cb_shader_mask.output0_enable = 0xF,
		.cb_shader_control.rt0_enable = TRUE,
		.db_shader_control.z_order = db_z_order_early_z_then_late_z,
	}, /* regs */
	sizeof(quadPCode),
	(uint8_t *)&quadPCode,
	GX2_SHADER_MODE_UNIFORM_BLOCK,
	.gx2rBuffer.flags = GX2R_RESOURCE_LOCKED_READ_ONLY,
};

__attribute__((aligned(GX2_SHADER_ALIGNMENT)))
static struct
{
	u64 cf[32];
} stencilVCode =
{
	{
		CALL_FS NO_BARRIER,
		EXP_DONE(POS0, _R1,_x,_y,_z,_w),
		EXP_DONE(PARAM0, _R2,_x,_y,_0,_0) NO_BARRIER
		END_OF_PROGRAM
	},
};

GX2VertexShader stencilUploadVSshaderGX2 = {
	{
		.sq_pgm_resources_vs.num_gprs = 3,
		.sq_pgm_resources_vs.stack_size = 1,
		.spi_vs_out_config.vs_export_count = 0,
		.num_spi_vs_out_id = 1,
		{
			{ .semantic_0 = 0x00, .semantic_1 = 0xFF, .semantic_2 = 0xFF, .semantic_3 = 0xFF },
			{ .semantic_0 = 0xFF, .semantic_1 = 0xFF, .semantic_2 = 0xFF, .semantic_3 = 0xFF },
			{ .semantic_0 = 0xFF, .semantic_1 = 0xFF, .semantic_2 = 0xFF, .semantic_3 = 0xFF },
			{ .semantic_0 = 0xFF, .semantic_1 = 0xFF, .semantic_2 = 0xFF, .semantic_3 = 0xFF },
			{ .semantic_0 = 0xFF, .semantic_1 = 0xFF, .semantic_2 = 0xFF, .semantic_3 = 0xFF },
			{ .semantic_0 = 0xFF, .semantic_1 = 0xFF, .semantic_2 = 0xFF, .semantic_3 = 0xFF },
			{ .semantic_0 = 0xFF, .semantic_1 = 0xFF, .semantic_2 = 0xFF, .semantic_3 = 0xFF },
			{ .semantic_0 = 0xFF, .semantic_1 = 0xFF, .semantic_2 = 0xFF, .semantic_3 = 0xFF },
			{ .semantic_0 = 0xFF, .semantic_1 = 0xFF, .semantic_2 = 0xFF, .semantic_3 = 0xFF },
			{ .semantic_0 = 0xFF, .semantic_1 = 0xFF, .semantic_2 = 0xFF, .semantic_3 = 0xFF },
		},
		.sq_vtx_semantic_clear = ~0x3,
		.num_sq_vtx_semantic = 2,
		{ 0, 1, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff },
		.vgt_vertex_reuse_block_cntl.vtx_reuse_depth = 0xE,
		.vgt_hos_reuse_depth.reuse_depth = 0x10,
	}, /* regs */
	sizeof(stencilVCode),
	(uint8_t *)&stencilVCode,
	GX2_SHADER_MODE_UNIFORM_BLOCK,
	.gx2rBuffer.flags = GX2R_RESOURCE_LOCKED_READ_ONLY,
};
// clang-format off
__attribute__((aligned(GX2_SHADER_ALIGNMENT)))
static struct
{
	u64 cf[32];
	u64 alu[2];          /* 32 */
	u64 alu1[9];         /* 34 */
	u64 alu2[5];         /* 43 */
	u64 tex3[1 * 2];     /* 48 */
} stencilPCode =
{
	{
		ALU_PUSH_BEFORE(32,2) KCACHE0(CB1, _0_15),
		JUMP(0,4) VALID_PIX,
		TEX(48,1) VALID_PIX,
		ALU(34,9) KCACHE0(CB1, _0_15),
		ELSE(1, 6) VALID_PIX,
		ALU_POP_AFTER(43,1),
		EXP_DONE(PIX0, _R0,_x,_x,_x,_x)
		END_OF_PROGRAM
	},
	{
		/* 0 */
		ALU_SETE_INT(_R0,_w, KC0(0),_x, ALU_SRC_0,_x)
		ALU_LAST,
		/* 1 */
		ALU_PRED_SETE_INT(__,_x, _R0,_w, ALU_SRC_0,_x) UPDATE_EXEC_MASK(DEACTIVATE) UPDATE_PRED
		ALU_LAST,
	},
	{
		/* 2 */
		ALU_MOV(_R0,_x, _R0,_w),
		ALU_MUL(__,_y, _R0,_w, ALU_SRC_LITERAL,_x)
		ALU_LAST,
		ALU_LITERAL(0x437FFD71),
		/* 3 */
		ALU_FLOOR(__,_x, ALU_SRC_PV,_y)
		ALU_LAST,
		/* 4 */
		ALU_FLT_TO_INT(__,_x, ALU_SRC_PV,_x) SCL_210
		ALU_LAST,
		/* 5 */
		ALU_AND_INT(__,_z, ALU_SRC_PS,_x, ALU_SRC_LITERAL,_x)
		ALU_LAST,
		ALU_LITERAL(0x000000FF),
		/* 6 */
		ALU_AND_INT(__,_y, KC0(0),_x, ALU_SRC_PV,_z)
		ALU_LAST,
		/* 7 */
		ALU_KILLE_INT(__,_x, ALU_SRC_PV,_y, ALU_SRC_0,_x)
		ALU_LAST,
	},
	{
		/* 8 */
		ALU_MOV(_R0,_x, ALU_SRC_0,_x)
		ALU_LAST,
	},
	{
		TEX_SAMPLE(_R0,_m,_m,_m,_w, _R0,_x,_y,_0,_x, _t0, _s0),
	},
};
// clang-format on

GX2PixelShader stencilUploadPSshaderGX2 = {
	{
		.sq_pgm_resources_ps.num_gprs = 1,
		.sq_pgm_resources_ps.stack_size = 1,
		.sq_pgm_exports_ps.export_mode = 0x2,
		.spi_ps_in_control_0.num_interp = 1,
		.spi_ps_in_control_0.persp_gradient_ena = 1,
		.spi_ps_in_control_0.baryc_sample_cntl = spi_baryc_cntl_centers_only,
		.num_spi_ps_input_cntl = 1,
		{ { .semantic = 0, .default_val = 1 } },
		.cb_shader_mask.output0_enable = 0xF,
		.cb_shader_control.rt0_enable = TRUE,
		.db_shader_control.z_order = db_z_order_early_z_then_late_z,
		.db_shader_control.kill_enable = TRUE,
	}, /* regs */
	sizeof(stencilPCode),
	(uint8_t *)&stencilPCode,
	GX2_SHADER_MODE_UNIFORM_BLOCK,
	.gx2rBuffer.flags = GX2R_RESOURCE_LOCKED_READ_ONLY,
};

// clang-format off
__attribute__((aligned(GX2_SHADER_ALIGNMENT)))
static struct {
	u64 cf[32];
	u64 alu[16];
} vsSTCode = {
	{
		CALL_FS NO_BARRIER,
		ALU(32, 16) KCACHE0(CB1, _0_15),
		EXP_DONE(POS0, _R1, _x, _y, _z, _w),
		EXP(PARAM0, _R2, _x, _y, _0, _0) NO_BARRIER,
		EXP_DONE(PARAM1, _R3, _r, _g, _b, _a) NO_BARRIER
		END_OF_PROGRAM
	},
	{
		ALU_MUL(__, _x, _R1, _w, KC0(3), _y),
		ALU_MUL(__, _y, _R1, _w, KC0(3), _x),
		ALU_MUL(__, _z, _R1, _w, KC0(3), _w),
		ALU_MUL(__, _w, _R1, _w, KC0(3), _z)
		ALU_LAST,
		ALU_MULADD(_R123, _x, _R1, _z, KC0(2), _y, ALU_SRC_PV, _x),
		ALU_MULADD(_R123, _y, _R1, _z, KC0(2), _x, ALU_SRC_PV, _y),
		ALU_MULADD(_R123, _z, _R1, _z, KC0(2), _w, ALU_SRC_PV, _z),
		ALU_MULADD(_R123, _w, _R1, _z, KC0(2), _z, ALU_SRC_PV, _w)
		ALU_LAST,
		ALU_MULADD(_R123, _x, _R1, _y, KC0(1), _y, ALU_SRC_PV, _x),
		ALU_MULADD(_R123, _y, _R1, _y, KC0(1), _x, ALU_SRC_PV, _y),
		ALU_MULADD(_R123, _z, _R1, _y, KC0(1), _w, ALU_SRC_PV, _z),
		ALU_MULADD(_R123, _w, _R1, _y, KC0(1), _z, ALU_SRC_PV, _w)
		ALU_LAST,
		ALU_MULADD(_R1, _x, _R1, _x, KC0(0), _x, ALU_SRC_PV, _y),
		ALU_MULADD(_R1, _y, _R1, _x, KC0(0), _y, ALU_SRC_PV, _x),
		ALU_MULADD(_R1, _z, _R1, _x, KC0(0), _z, ALU_SRC_PV, _w),
		ALU_MULADD(_R1, _w, _R1, _x, KC0(0), _w, ALU_SRC_PV, _z)
		ALU_LAST,
	}
};

__attribute__((aligned(GX2_SHADER_ALIGNMENT))) static struct {
	u64 cf[32];
	u64 alu[16];
	u64 tex[1 * 2];
} fsSTCode =
{
	{
		TEX(48, 1) VALID_PIX,
		ALU(32, 4),
		EXP_DONE(PIX0, _R0, _r, _g, _b, _a)
		END_OF_PROGRAM
	},
	{
		ALU_MUL(_R0, _r, _R0, _r, _R1, _r),
		ALU_MUL(_R0, _g, _R0, _g, _R1, _g),
		ALU_MUL(_R0, _b, _R0, _b, _R1, _b),
		ALU_MUL(_R0, _a, _R0, _a, _R1, _a)
		ALU_LAST
	},
	{
		TEX_SAMPLE(_R0, _r, _g, _b, _a, _R0, _x, _y, _0, _0, _t0, _s0)
	}
};
// clang-format on

GX2VertexShader STVshaderGX2 = {
	{
		.sq_pgm_resources_vs.num_gprs = 4,
		.sq_pgm_resources_vs.stack_size = 1,
		.spi_vs_out_config.vs_export_count = 1,
		.num_spi_vs_out_id = 1,
		{
			{ .semantic_0 = 0x00, .semantic_1 = 0x01, .semantic_2 = 0xFF, .semantic_3 = 0xFF },
			{ .semantic_0 = 0xFF, .semantic_1 = 0xFF, .semantic_2 = 0xFF, .semantic_3 = 0xFF },
			{ .semantic_0 = 0xFF, .semantic_1 = 0xFF, .semantic_2 = 0xFF, .semantic_3 = 0xFF },
			{ .semantic_0 = 0xFF, .semantic_1 = 0xFF, .semantic_2 = 0xFF, .semantic_3 = 0xFF },
			{ .semantic_0 = 0xFF, .semantic_1 = 0xFF, .semantic_2 = 0xFF, .semantic_3 = 0xFF },
			{ .semantic_0 = 0xFF, .semantic_1 = 0xFF, .semantic_2 = 0xFF, .semantic_3 = 0xFF },
			{ .semantic_0 = 0xFF, .semantic_1 = 0xFF, .semantic_2 = 0xFF, .semantic_3 = 0xFF },
			{ .semantic_0 = 0xFF, .semantic_1 = 0xFF, .semantic_2 = 0xFF, .semantic_3 = 0xFF },
			{ .semantic_0 = 0xFF, .semantic_1 = 0xFF, .semantic_2 = 0xFF, .semantic_3 = 0xFF },
			{ .semantic_0 = 0xFF, .semantic_1 = 0xFF, .semantic_2 = 0xFF, .semantic_3 = 0xFF },
		},
		.sq_vtx_semantic_clear = ~0x7,
		.num_sq_vtx_semantic = 3,
		{
			0, 1, 2, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
		},
		.vgt_vertex_reuse_block_cntl.vtx_reuse_depth = 0xE,
		.vgt_hos_reuse_depth.reuse_depth = 0x10,
	}, /* regs */
	.size = sizeof(vsSTCode),
	.program = (u8 *)&vsSTCode,
	.mode = GX2_SHADER_MODE_UNIFORM_BLOCK,
	.gx2rBuffer.flags = GX2R_RESOURCE_LOCKED_READ_ONLY,
};

GX2PixelShader STPshaderGX2 = {
	{
		.sq_pgm_resources_ps.num_gprs = 2,
		.sq_pgm_exports_ps.export_mode = 0x2,
		.spi_ps_in_control_0.num_interp = 2,
		.spi_ps_in_control_0.persp_gradient_ena = 1,
		.spi_ps_in_control_0.baryc_sample_cntl = spi_baryc_cntl_centers_only,
		.num_spi_ps_input_cntl = 2,
		{ { .semantic = 0, .default_val = 1 }, { .semantic = 1, .default_val = 1 } },
		.cb_shader_mask.output0_enable = 0xF,
		.cb_shader_control.rt0_enable = TRUE,
		.db_shader_control.z_order = db_z_order_early_z_then_late_z,
	}, /* regs */
	.size = sizeof(fsSTCode),
	.program = (uint8_t *)&fsSTCode,
	.mode = GX2_SHADER_MODE_UNIFORM_BLOCK,
	.gx2rBuffer.flags = GX2R_RESOURCE_LOCKED_READ_ONLY,
};

__attribute__((aligned(GX2_SHADER_ALIGNMENT)))
static struct
{
	u64 cf[32];
	u64 alu[4];          /* 32 */
	u64 alu1[2];         /* 36 */
	u64 alu2[2];         /* 38 */
	u64 alu3[5];         /* 40 */
	u64 alu4[1];         /* 45 */
	u64 alu5[3];         /* 46 */
	u64 alu6[4];         /* 49 */
	u64 alu7[1];         /* 53 */
	u64 alu8[16];        /* 54 */
	u64 alu9[16];        /* 70 */
	u64 alu10[1];        /* 86 */
	u64 alu11[8];        /* 87 */
	u64 alu12[2];        /* 95 */
} VShaderSWCode =
{
	{
		CALL_FS NO_BARRIER,
		ALU_PUSH_BEFORE(32,4) KCACHE0(CB4, _0_15),
		JUMP(1, 7),
		ALU_PUSH_BEFORE(36,2) KCACHE0(CB4, _0_15),
		JUMP(1, 6),
		ALU_POP_AFTER(38,2) KCACHE0(CB4, _0_15),
		ALU_POP_AFTER(40,5),
		ALU_PUSH_BEFORE(45,1) KCACHE0(CB4, _0_15),
		JUMP(0,10),
		ALU(46,3) KCACHE0(CB4, _0_15),
		ELSE(1, 12),
		ALU_POP_AFTER(49,4) KCACHE0(CB1, _16_31),
		ALU_PUSH_BEFORE(53,1) KCACHE0(CB4, _0_15),
		JUMP(0,15),
		ALU(54,16) KCACHE0(CB1, _0_15),
		ELSE(1, 17),
		ALU_POP_AFTER(70,16) KCACHE0(CB1, _0_15),
		ALU_PUSH_BEFORE(86,1) KCACHE0(CB4, _0_15),
		JUMP(1, 20),
		ALU_POP_AFTER(87,8) KCACHE0(CB1, _16_31),
		ALU(95,2),
		EXP_DONE(POS0, _R2,_x,_y,_z,_w),
		EXP(PARAM0, _R1,_x,_y,_z,_w) NO_BARRIER,
		EXP(PARAM1, _R5,_x,_y,_z,_w) NO_BARRIER,
		EXP(PARAM2, _R0,_x,_y,_z,_w) NO_BARRIER,
		EXP_DONE(PARAM3, _R3,_x,_y,_y,_y) NO_BARRIER
		END_OF_PROGRAM
	},
	{
		/* 0 */
		ALU_MOV(_R5,_x, ALU_SRC_0,_x),
		ALU_MOV(_R5,_y, ALU_SRC_0,_x),
		ALU_MOV(_R5,_z, ALU_SRC_0,_x)
		ALU_LAST,
		/* 1 */
		ALU_PRED_SETNE_INT(__,_x, KC0(5),_x, ALU_SRC_0,_x) UPDATE_EXEC_MASK(DEACTIVATE) UPDATE_PRED
		ALU_LAST,
	},
	{
		/* 2 */
		ALU_MOV(_R0,_w, ALU_SRC_0,_x)
		ALU_LAST,
		/* 3 */
		ALU_PRED_SETNE_INT(__,_x, KC0(5),_y, ALU_SRC_0,_x) UPDATE_EXEC_MASK(DEACTIVATE) UPDATE_PRED
		ALU_LAST,
	},
	{
		/* 4 */
		ALU_NOT_INT(__,_x, KC0(4),_y)
		ALU_LAST,
		/* 5 */
		ALU_CNDE_INT(_R0,_w, ALU_SRC_PV,_x, ALU_SRC_0,_x, ALU_SRC_M_1_INT,_x)
		ALU_LAST,
	},
	{
		/* 6 */
		ALU_CNDE_INT(_R0,_x, _R0,_w, _R4,_x, _R4,_x),
		ALU_CNDE_INT(_R0,_y, _R0,_w, _R4,_y, _R4,_y),
		ALU_MOV(_R0,_z, ALU_SRC_LITERAL,_x)
		ALU_LAST,
		ALU_LITERAL(0x3F800000),
		/* 7 */
		ALU_CNDE_INT(_R0,_z, _R0,_w, ALU_SRC_PV,_z, _R4,_z)
		ALU_LAST,
	},
	{
		/* 8 */
		ALU_PRED_SETNE_INT(__,_x, KC0(4),_w, ALU_SRC_0,_x) UPDATE_EXEC_MASK(DEACTIVATE) UPDATE_PRED
		ALU_LAST,
	},
	{
		/* 9 */
		ALU_CNDE_INT(_R5,_x, KC0(4),_x, ALU_SRC_0,_x, _R2,_x),
		ALU_CNDE_INT(_R5,_y, KC0(4),_x, ALU_SRC_0,_x, _R2,_y),
		ALU_CNDE_INT(_R5,_z, KC0(4),_x, ALU_SRC_0,_x, _R2,_z)
		ALU_LAST,
	},
	{
		/* 10 */
		ALU_MOV(_R1,_x, KC0(4),_x),
		ALU_MOV(_R1,_y, KC0(4),_y),
		ALU_MOV(_R1,_z, KC0(4),_z),
		ALU_MOV(_R1,_w, KC0(4),_w)
		ALU_LAST,
	},
	{
		/* 11 */
		ALU_PRED_SETNE_INT(__,_x, KC0(4),_y, ALU_SRC_0,_x) UPDATE_EXEC_MASK(DEACTIVATE) UPDATE_PRED
		ALU_LAST,
	},
	{
		/* 12 */
		ALU_MUL(__,_x, KC0(7),_w, ALU_SRC_1,_x),
		ALU_MUL(__,_y, KC0(7),_z, ALU_SRC_1,_x),
		ALU_MUL(__,_z, KC0(7),_y, ALU_SRC_1,_x),
		ALU_MUL(__,_w, KC0(7),_x, ALU_SRC_1,_x)
		ALU_LAST,
		/* 13 */
		ALU_MULADD(_R123,_x, _R3,_z, KC0(6),_w, ALU_SRC_PV,_x),
		ALU_MULADD(_R123,_y, _R3,_z, KC0(6),_z, ALU_SRC_PV,_y),
		ALU_MULADD(_R123,_z, _R3,_z, KC0(6),_y, ALU_SRC_PV,_z),
		ALU_MULADD(_R123,_w, _R3,_z, KC0(6),_x, ALU_SRC_PV,_w)
		ALU_LAST,
		/* 14 */
		ALU_MULADD(_R123,_x, _R3,_y, KC0(5),_w, ALU_SRC_PV,_x),
		ALU_MULADD(_R123,_y, _R3,_y, KC0(5),_z, ALU_SRC_PV,_y),
		ALU_MULADD(_R123,_z, _R3,_y, KC0(5),_y, ALU_SRC_PV,_z),
		ALU_MULADD(_R123,_w, _R3,_y, KC0(5),_x, ALU_SRC_PV,_w)
		ALU_LAST,
		/* 15 */
		ALU_MULADD(_R2,_x, _R3,_x, KC0(4),_x, ALU_SRC_PV,_w),
		ALU_MULADD(_R2,_y, _R3,_x, KC0(4),_y, ALU_SRC_PV,_z),
		ALU_MULADD(_R2,_z, _R3,_x, KC0(4),_z, ALU_SRC_PV,_y),
		ALU_MULADD(_R2,_w, _R3,_x, KC0(4),_w, ALU_SRC_PV,_x)
		ALU_LAST,
	},
	{
		/* 16 */
		ALU_MUL(__,_x, KC0(3),_w, ALU_SRC_1,_x),
		ALU_MUL(__,_y, KC0(3),_z, ALU_SRC_1,_x),
		ALU_MUL(__,_z, KC0(3),_y, ALU_SRC_1,_x),
		ALU_MUL(__,_w, KC0(3),_x, ALU_SRC_1,_x)
		ALU_LAST,
		/* 17 */
		ALU_MULADD(_R123,_x, _R3,_z, KC0(2),_w, ALU_SRC_PV,_x),
		ALU_MULADD(_R123,_y, _R3,_z, KC0(2),_z, ALU_SRC_PV,_y),
		ALU_MULADD(_R123,_z, _R3,_z, KC0(2),_y, ALU_SRC_PV,_z),
		ALU_MULADD(_R123,_w, _R3,_z, KC0(2),_x, ALU_SRC_PV,_w)
		ALU_LAST,
		/* 18 */
		ALU_MULADD(_R123,_x, _R3,_y, KC0(1),_w, ALU_SRC_PV,_x),
		ALU_MULADD(_R123,_y, _R3,_y, KC0(1),_z, ALU_SRC_PV,_y),
		ALU_MULADD(_R123,_z, _R3,_y, KC0(1),_y, ALU_SRC_PV,_z),
		ALU_MULADD(_R123,_w, _R3,_y, KC0(1),_x, ALU_SRC_PV,_w)
		ALU_LAST,
		/* 19 */
		ALU_MULADD(_R2,_x, _R3,_x, KC0(0),_x, ALU_SRC_PV,_w),
		ALU_MULADD(_R2,_y, _R3,_x, KC0(0),_y, ALU_SRC_PV,_z),
		ALU_MULADD(_R2,_z, _R3,_x, KC0(0),_z, ALU_SRC_PV,_y),
		ALU_MULADD(_R2,_w, _R3,_x, KC0(0),_w, ALU_SRC_PV,_x)
		ALU_LAST,
	},
	{
		/* 20 */
		ALU_PRED_SETNE_INT(__,_x, KC0(10),_y, ALU_SRC_0,_x) UPDATE_EXEC_MASK(DEACTIVATE) UPDATE_PRED
		ALU_LAST,
	},
	{
		/* 21 */
		ALU_RECIP_IEEE(__,_x, _R2,_w) SCL_210
		ALU_LAST,
		/* 22 */
		ALU_MUL_IEEE(__,_y, _R2,_z, ALU_SRC_PS,_x)
		ALU_LAST,
		/* 23 */
		ALU_MUL(__,_x, ALU_SRC_PV,_y, KC0(2),_x)
		ALU_LAST,
		/* 24 */
		ALU_ADD(__,_z, KC0(2),_y, ALU_SRC_PV,_x)
		ALU_LAST,
		/* 25 */
		ALU_FLOOR(__,_w, ALU_SRC_PV,_z)
		ALU_LAST,
		/* 26 */
		ALU_ADD(__,_y, KC0(2) _NEG,_z, ALU_SRC_PV,_w)
		ALU_LAST,
		/* 27 */
		ALU_MUL(__,_x, KC0(2),_w, ALU_SRC_PV,_y)
		ALU_LAST,
		/* 28 */
		ALU_MUL(_R2,_z, _R2,_w, ALU_SRC_PV,_x)
		ALU_LAST,
	},
	{
		/* 29 */
		ALU_NOP(__,_x),
		ALU_MOV(_R3,_x, _R3,_w)
		ALU_LAST,
	},
};

GX2VertexShader VShaderSWGX2 = {
	{
		.sq_pgm_resources_vs.num_gprs = 6,
		.sq_pgm_resources_vs.stack_size = 1,
		.spi_vs_out_config.vs_export_count = 3,
		.num_spi_vs_out_id = 1,
		{
			{ .semantic_0 = 0x00, .semantic_1 = 0x01, .semantic_2 = 0x03, .semantic_3 = 0x02 },
			{ .semantic_0 = 0xFF, .semantic_1 = 0xFF, .semantic_2 = 0xFF, .semantic_3 = 0xFF },
			{ .semantic_0 = 0xFF, .semantic_1 = 0xFF, .semantic_2 = 0xFF, .semantic_3 = 0xFF },
			{ .semantic_0 = 0xFF, .semantic_1 = 0xFF, .semantic_2 = 0xFF, .semantic_3 = 0xFF },
			{ .semantic_0 = 0xFF, .semantic_1 = 0xFF, .semantic_2 = 0xFF, .semantic_3 = 0xFF },
			{ .semantic_0 = 0xFF, .semantic_1 = 0xFF, .semantic_2 = 0xFF, .semantic_3 = 0xFF },
			{ .semantic_0 = 0xFF, .semantic_1 = 0xFF, .semantic_2 = 0xFF, .semantic_3 = 0xFF },
			{ .semantic_0 = 0xFF, .semantic_1 = 0xFF, .semantic_2 = 0xFF, .semantic_3 = 0xFF },
			{ .semantic_0 = 0xFF, .semantic_1 = 0xFF, .semantic_2 = 0xFF, .semantic_3 = 0xFF },
			{ .semantic_0 = 0xFF, .semantic_1 = 0xFF, .semantic_2 = 0xFF, .semantic_3 = 0xFF },
		},
		.sq_vtx_semantic_clear = ~0xF,
		.num_sq_vtx_semantic = 4,
		{
			2, 3, 0, 1, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
		},
		.vgt_vertex_reuse_block_cntl.vtx_reuse_depth = 0xE,
		.vgt_hos_reuse_depth.reuse_depth = 0x10,
	}, /* regs */
	.size = sizeof(VShaderSWCode),
	.program = (u8 *)&VShaderSWCode,
	.mode = GX2_SHADER_MODE_UNIFORM_BLOCK,
	.gx2rBuffer.flags = GX2R_RESOURCE_LOCKED_READ_ONLY,
};


__attribute__((aligned(GX2_SHADER_ALIGNMENT)))
static struct
{
	u64 cf[320];
	u64 alu[8];          /* 320 */
	u64 alu1[1];         /* 328 */
	u64 alu2[1];         /* 329 */
	u64 alu3[6];         /* 330 */
	u64 alu4[3];         /* 336 */
	u64 alu5[6];         /* 339 */
	u64 alu6[1];         /* 345 */
	u64 alu7[3];         /* 346 */
	u64 alu8[6];         /* 349 */
	u64 alu9[1];         /* 355 */
	u64 alu10[2];        /* 356 */
	u64 alu11[2];        /* 358 */
	u64 alu12[4];        /* 360 */
	u64 alu13[3];        /* 364 */
	u64 alu14[1];        /* 367 */
	u64 alu15[1];        /* 368 */
	u64 alu16[1];        /* 369 */
	u64 alu17[7];        /* 370 */
	u64 alu18[16];       /* 377 */
	u64 alu19[14];       /* 393 */
	u64 alu20[38];       /* 407 */
	u64 alu21[2];        /* 445 */
	u64 alu22[19];       /* 447 */
	u64 alu23[53];       /* 466 */
	u64 alu24[3];        /* 519 */
	u64 alu25[19];       /* 522 */
	u64 alu26[53];       /* 541 */
	u64 alu27[3];        /* 594 */
	u64 alu28[19];       /* 597 */
	u64 alu29[53];       /* 616 */
	u64 alu30[4];        /* 669 */
	u64 alu31[1];        /* 673 */
	u64 alu32[12];       /* 674 */
	u64 alu33[24];       /* 686 */
	u64 alu34[5];        /* 710 */
	u64 alu35[2];        /* 715 */
	u64 alu36[4];        /* 717 */
	u64 alu37[2];        /* 721 */
	u64 alu38[6];        /* 723 */
	u64 alu39[3];        /* 729 */
	u64 alu40[7];        /* 732 */
	u64 alu41[2];        /* 739 */
	u64 alu42[3];        /* 741 */
	u64 alu43[5];        /* 744 */
	u64 alu44[3];        /* 749 */
	u64 alu45[5];        /* 752 */
	u64 alu46[13];       /* 757 */
	u64 alu47[4];        /* 770 */
	u64 alu48[2];        /* 774 */
	u64 alu49[3];        /* 776 */
	u64 alu50[2];        /* 779 */
	u64 alu51[3];        /* 781 */
	u64 alu52[3];        /* 784 */
	u64 alu53[7];        /* 787 */
	u64 alu54[3];        /* 794 */
	u64 alu55[5];        /* 797 */
	u64 alu56[3];        /* 802 */
	u64 alu57[5];        /* 805 */
	u64 alu58[3];        /* 810 */
	u64 alu59[5];        /* 813 */
	u64 alu60[13];       /* 818 */
	u64 alu61[11];       /* 831 */
	u64 alu62[1];        /* 842 */
	u64 alu63[5];        /* 843 */
	u64 alu64[2];        /* 848 */
	u64 alu65[2];        /* 850 */
	u64 alu66[2];        /* 852 */
	u64 alu67[1];        /* 854 */
	u64 alu68[3];        /* 855 */
	u64 alu69[1];        /* 858 */
	u64 alu70[3];        /* 859 */
	u64 alu71[6];        /* 862 */
	u64 alu72[1];        /* 868 */
	u64 alu73[3];        /* 869 */
	u64 alu74[6];        /* 872 */
	u64 alu75[1];        /* 878 */
	u64 alu76[3];        /* 879 */
	u64 alu77[7];        /* 882 */
	u64 alu78[1];        /* 889 */
	u64 alu79[3];        /* 890 */
	u64 alu80[6];        /* 893 */
	u64 alu81[1];        /* 899 */
	u64 alu82[3];        /* 900 */
	u64 alu83[7];        /* 903 */
	u64 alu84[1];        /* 910 */
	u64 alu85[3];        /* 911 */
	u64 alu86[6];        /* 914 */
	u64 alu87[1];        /* 920 */
	u64 alu88[1];        /* 921 */
	u64 alu89[1];        /* 922 */
	u64 alu90[2];        /* 923 */
	u64 alu91[1];        /* 925 */
	u64 alu92[1];        /* 926 */
	u64 alu93[1];        /* 927 */
	u64 alu94[2];        /* 928 */
	u64 alu95[3];        /* 930 */
	u64 alu96[1];        /* 933 */
	u64 alu97[1];        /* 934 */
	u64 alu98[1];        /* 935 */
	u64 alu99[2];        /* 936 */
	u64 alu100[2];       /* 938 */
	u64 alu101[2];       /* 940 */
	u64 alu102[19];      /* 942 */
	u64 alu103[1];       /* 961 */
	u64 alu104[1];       /* 962 */
	u64 alu105[1];       /* 963 */
	u64 alu106[19];      /* 964 */
	u64 alu107[1];       /* 983 */
	u64 alu108[4];       /* 984 */
	u64 alu109[3];       /* 988 */
	u64 alu110[10];      /* 991 */
	u64 alu111[1];       /* 1001 */
	u64 alu112[13];      /* 1002 */
	u64 alu113[5];       /* 1015 */
	u64 alu114[3];       /* 1020 */
	u64 alu115[3];       /* 1023 */
	u64 alu116[3];       /* 1026 */
	u64 alu117[4];       /* 1029 */
	u64 alu118[3];       /* 1033 */
	u64 alu119[4];       /* 1036 */
	u64 alu120[12];      /* 1040 */
	u64 alu121[3];       /* 1052 */
	u64 alu122[3];       /* 1055 */
	u64 alu123[3];       /* 1058 */
	u64 alu124[2];       /* 1061 */
	u64 alu125[2];       /* 1063 */
	u64 alu126[3];       /* 1065 */
	u64 alu127[3];       /* 1068 */
	u64 alu128[3];       /* 1071 */
	u64 alu129[3];       /* 1074 */
	u64 alu130[3];       /* 1077 */
	u64 alu131[3];       /* 1080 */
	u64 alu132[3];       /* 1083 */
	u64 alu133[3];       /* 1086 */
	u64 alu134[3];       /* 1089 */
	u64 alu135[3];       /* 1092 */
	u64 alu136[4];       /* 1095 */
	u64 alu137[3];       /* 1099 */
	u64 alu138[5];       /* 1102 */
	u64 alu139[3];       /* 1107 */
	u64 alu140[4];       /* 1110 */
	u64 alu141[12];      /* 1114 */
	u64 alu142[3];       /* 1126 */
	u64 alu143[4];       /* 1129 */
	u64 alu144[2];       /* 1133 */
	u64 alu145[3];       /* 1135 */
	u64 alu146[3];       /* 1138 */
	u64 alu147[3];       /* 1141 */
	u64 alu148[3];       /* 1144 */
	u64 alu149[3];       /* 1147 */
	u64 alu150[3];       /* 1150 */
	u64 alu151[3];       /* 1153 */
	u64 alu152[3];       /* 1156 */
	u64 alu153[3];       /* 1159 */
	u64 alu154[3];       /* 1162 */
	u64 alu155[4];       /* 1165 */
	u64 alu156[3];       /* 1169 */
	u64 alu157[5];       /* 1172 */
	u64 alu158[3];       /* 1177 */
	u64 alu159[4];       /* 1180 */
	u64 alu160[11];      /* 1184 */
	u64 alu161[3];       /* 1195 */
	u64 alu162[5];       /* 1198 */
	u64 alu163[3];       /* 1203 */
	u64 alu164[2];       /* 1206 */
	u64 alu165[3];       /* 1208 */
	u64 alu166[3];       /* 1211 */
	u64 alu167[3];       /* 1214 */
	u64 alu168[3];       /* 1217 */
	u64 alu169[3];       /* 1220 */
	u64 alu170[20];      /* 1223 */
	u64 alu171[9];       /* 1243 */
	u64 alu172[2];       /* 1252 */
	u64 alu173[2];       /* 1254 */
	u64 alu174[1];       /* 1256 */
	u64 alu175[1];       /* 1257 */
	u64 alu176[3];       /* 1258 */
	u64 alu177[2];       /* 1261 */
	u64 alu178[3];       /* 1263 */
	u64 alu179[2];       /* 1266 */
	u64 alu180[3];       /* 1268 */
	u64 alu181[2];       /* 1271 */
	u64 alu182[10];      /* 1273 */
	u64 alu183[3];       /* 1283 */
	u64 alu184[8];       /* 1286 */
	u64 alu185[2];       /* 1294 */
	u64 alu186[5];       /* 1296 */
	u64 alu187[1];       /* 1301 */
	u64 alu188[1];       /* 1302 */
	u64 alu189[4];       /* 1303 */
	u64 alu190[2];       /* 1307 */
	u64 alu191[4];       /* 1309 */
	u64 alu192[3];       /* 1313 */
	u64 alu193[3];       /* 1316 */
	u64 alu194[1];       /* 1319 */
	u64 alu195[1];       /* 1320 */
	u64 alu196[20];      /* 1321 */
	u64 alu197[19];      /* 1341 */
	u64 tex198[1 * 2];   /* 1360 */
	u64 tex199[3 * 2];   /* 1362 */
	u64 tex200[1 * 2];   /* 1368 */
	u64 tex201[3 * 2];   /* 1370 */
	u64 tex202[1 * 2];   /* 1376 */
	u64 tex203[1 * 2];   /* 1378 */
	u64 tex204[3 * 2];   /* 1380 */
	u64 tex205[1 * 2];   /* 1386 */
} PShaderAllCode =
{
	{
		ALU_PUSH_BEFORE(320,8) KCACHE0(CB5, _0_15),
		JUMP(1, 289),
		ALU_PUSH_BEFORE(328,1) KCACHE0(CB5, _0_15),
		JUMP(1, 131),
		ALU_PUSH_BEFORE(329,1) KCACHE0(CB5, _0_15),
		JUMP(0,19),
		ALU_PUSH_BEFORE(330,6) KCACHE0(CB5, _0_15),
		JUMP(0,9),
		ALU(336,3) KCACHE0(CB1, _16_31),
		ELSE(1, 11),
		ALU_POP_AFTER(339,6) KCACHE0(CB1, _16_31),
		ALU_PUSH_BEFORE(345,1) KCACHE0(CB5, _0_15),
		JUMP(0,14),
		ALU(346,3) KCACHE0(CB1, _16_31),
		ELSE(1, 16),
		ALU_POP_AFTER(349,6) KCACHE0(CB1, _16_31),
		ALU_PUSH_BEFORE(355,1) KCACHE0(CB5, _0_15),
		JUMP(1, 19),
		ALU_POP_AFTER(356,2) KCACHE0(CB1, _16_31),
		ELSE(1, 21),
		ALU_POP_AFTER(358,2),
		ALU_PUSH_BEFORE(360,4) KCACHE0(CB5, _0_15),
		JUMP(0,29),
		ALU(364,3),
		TEX(1360,1) VALID_PIX,
		ALU_PUSH_BEFORE(367,1) KCACHE0(CB5, _0_15),
		JUMP(1, 29) VALID_PIX,
		TEX(1362,3) VALID_PIX,
		POP(1, 29),
		ELSE(1, 35),
		TEX(1368,1) VALID_PIX,
		ALU_PUSH_BEFORE(368,1) KCACHE0(CB5, _0_15),
		JUMP(2, 35) VALID_PIX,
		TEX(1370,3) VALID_PIX,
		POP(2, 35),
		ALU_PUSH_BEFORE(369,1) KCACHE0(CB5, _0_15),
		JUMP(1, 71) VALID_PIX,
		ALU(370,7) KCACHE0(CB1, _16_31),
		TEX(1376,1) VALID_PIX,
		ALU_PUSH_BEFORE(377,16) KCACHE0(CB1, _16_31),
		JUMP(0,44) VALID_PIX,
		ALU_PUSH_BEFORE(393,14),
		JUMP(1, 44) VALID_PIX,
		ALU_POP_AFTER(407,38),
		ELSE(1, 64) VALID_PIX,
		ALU_PUSH_BEFORE(445,2),
		JUMP(0,50) VALID_PIX,
		ALU_PUSH_BEFORE(447,19),
		JUMP(1, 50) VALID_PIX,
		ALU_POP_AFTER(466,53),
		ELSE(1, 63) VALID_PIX,
		ALU_PUSH_BEFORE(519,3),
		JUMP(0,56) VALID_PIX,
		ALU_PUSH_BEFORE(522,19),
		JUMP(1, 56) VALID_PIX,
		ALU_POP_AFTER(541,53),
		ELSE(0,62) VALID_PIX,
		ALU_PUSH_BEFORE(594,3),
		JUMP(4, 64) VALID_PIX,
		ALU_PUSH_BEFORE(597,19),
		JUMP(5, 64) VALID_PIX,
		ALU_POP2_AFTER(616,53),
		POP(2, 63),
		POP(1, 64),
		ALU(669,4),
		TEX(1378,1) VALID_PIX,
		ALU_PUSH_BEFORE(673,1),
		JUMP(2, 71) VALID_PIX,
		ALU(674,12),
		TEX(1380,3) VALID_PIX,
		ALU_POP2_AFTER(686,24),
		ALU_PUSH_BEFORE(710,5) KCACHE0(CB5, _0_15),
		JUMP(0,101) VALID_PIX,
		ALU_PUSH_BEFORE(715,2) KCACHE0(CB5, _0_15),
		JUMP(0,76) VALID_PIX,
		ALU(717,4),
		ELSE(1, 101) VALID_PIX,
		ALU_PUSH_BEFORE(721,2) KCACHE0(CB5, _0_15),
		JUMP(0,80) VALID_PIX,
		ALU(723,6),
		ELSE(1, 100) VALID_PIX,
		ALU_PUSH_BEFORE(729,3) KCACHE0(CB5, _0_15),
		JUMP(0,84) VALID_PIX,
		ALU(732,7) KCACHE0(CB1, _16_31),
		ELSE(0,99) VALID_PIX,
		ALU_PUSH_BEFORE(739,2) KCACHE0(CB5, _0_15),
		JUMP(0,97) VALID_PIX,
		ALU_PUSH_BEFORE(741,3) KCACHE0(CB5, _0_15),
		JUMP(0,90) VALID_PIX,
		ALU(744,5),
		ELSE(1, 97) VALID_PIX,
		ALU_PUSH_BEFORE(749,3) KCACHE0(CB5, _0_15),
		JUMP(0,94) VALID_PIX,
		ALU(752,5),
		ELSE(1, 96) VALID_PIX,
		ALU_POP_AFTER(757,13) KCACHE0(CB5, _0_15),
		POP(1, 97),
		ELSE(1, 99) VALID_PIX,
		ALU_POP_AFTER(770,4),
		POP(2, 100),
		POP(1, 101),
		ELSE(1, 130) VALID_PIX,
		ALU_PUSH_BEFORE(774,2) KCACHE0(CB5, _0_15),
		JUMP(0,105) VALID_PIX,
		ALU(776,3),
		ELSE(0,129) VALID_PIX,
		ALU_PUSH_BEFORE(779,2) KCACHE0(CB5, _0_15),
		JUMP(0,109) VALID_PIX,
		ALU(781,3),
		ELSE(1, 129) VALID_PIX,
		ALU_PUSH_BEFORE(784,3) KCACHE0(CB5, _0_15),
		JUMP(0,113) VALID_PIX,
		ALU(787,7) KCACHE0(CB1, _16_31),
		ELSE(0,128) VALID_PIX,
		ALU_PUSH_BEFORE(794,3) KCACHE0(CB5, _0_15),
		JUMP(0,117) VALID_PIX,
		ALU(797,5),
		ELSE(1, 128) VALID_PIX,
		ALU_PUSH_BEFORE(802,3) KCACHE0(CB5, _0_15),
		JUMP(0,121) VALID_PIX,
		ALU(805,5),
		ELSE(0,127) VALID_PIX,
		ALU_PUSH_BEFORE(810,3) KCACHE0(CB5, _0_15),
		JUMP(0,125) VALID_PIX,
		ALU(813,5),
		ELSE(1, 127) VALID_PIX,
		ALU_POP_AFTER(818,13) KCACHE0(CB5, _0_15),
		POP(2, 128),
		POP(2, 129),
		POP(2, 130),
		POP(1, 131),
		ALU_PUSH_BEFORE(831,11) KCACHE0(CB5, _0_15),
		JUMP(1, 190) VALID_PIX,
		ALU_PUSH_BEFORE(842,1) KCACHE0(CB5, _0_15),
		JUMP(0,148) VALID_PIX,
		ALU_PUSH_BEFORE(843,5) KCACHE0(CB5, _0_15),
		JUMP(0,140) VALID_PIX,
		ALU(848,2),
		ALU_PUSH_BEFORE(850,2),
		POP(1, 140),
		ELSE(1, 148) VALID_PIX,
		ALU(852,2) KCACHE0(CB5, _0_15),
		ALU_PUSH_BEFORE(854,1),
		ELSE(1, 147) VALID_PIX,
		ALU(855,3),
		ALU_PUSH_BEFORE(858,1),
		POP(2, 147),
		POP(1, 148),
		ELSE(0,189) VALID_PIX,
		ALU_PUSH_BEFORE(859,3) KCACHE0(CB5, _0_15),
		ALU(862,6) KCACHE0(CB1, _16_31),
		ALU_PUSH_BEFORE(868,1) KCACHE0(CB1, _16_31),
		POP(1, 153),
		ELSE(1, 189) VALID_PIX,
		ALU_PUSH_BEFORE(869,3) KCACHE0(CB5, _0_15),
		ALU(872,6) KCACHE0(CB1, _16_31),
		ALU_PUSH_BEFORE(878,1) KCACHE0(CB1, _16_31),
		POP(1, 158),
		ELSE(0,188) VALID_PIX,
		ALU_PUSH_BEFORE(879,3) KCACHE0(CB5, _0_15),
		ALU(882,7) KCACHE0(CB1, _16_31),
		ALU_PUSH_BEFORE(889,1),
		POP(1, 163),
		ELSE(1, 188) VALID_PIX,
		ALU_PUSH_BEFORE(890,3) KCACHE0(CB5, _0_15),
		ALU(893,6) KCACHE0(CB1, _16_31),
		ALU_PUSH_BEFORE(899,1) KCACHE0(CB1, _16_31),
		POP(1, 168),
		ELSE(0,187) VALID_PIX,
		ALU_PUSH_BEFORE(900,3) KCACHE0(CB5, _0_15),
		ALU(903,7) KCACHE0(CB1, _16_31),
		ALU_PUSH_BEFORE(910,1),
		POP(1, 173),
		ELSE(1, 187) VALID_PIX,
		ALU_PUSH_BEFORE(911,3) KCACHE0(CB5, _0_15),
		ALU(914,6) KCACHE0(CB1, _16_31),
		ALU_PUSH_BEFORE(920,1) KCACHE0(CB1, _16_31),
		POP(1, 178),
		ELSE(0,186) VALID_PIX,
		ALU(921,1),
		ALU_PUSH_BEFORE(922,1),
		ELSE(1, 186) VALID_PIX,
		ALU(923,2) KCACHE0(CB5, _0_15),
		ALU_PUSH_BEFORE(925,1),
		ELSE(0,185) VALID_PIX,
		POP(2, 186),
		POP(2, 187),
		POP(2, 188),
		POP(2, 189),
		POP(2, 190),
		ALU_PUSH_BEFORE(926,1) KCACHE0(CB5, _0_15),
		ALU_PUSH_BEFORE(927,1) KCACHE0(CB5, _0_15),
		ALU_PUSH_BEFORE(928,2) KCACHE0(CB5, _0_15),
		ALU(930,3),
		ALU_PUSH_BEFORE(933,1),
		POP(1, 196),
		ELSE(1, 204) VALID_PIX,
		ALU(934,1),
		ALU_PUSH_BEFORE(935,1),
		ALU(936,2),
		ALU_PUSH_BEFORE(938,2),
		POP(1, 202),
		ELSE(0,203) VALID_PIX,
		POP(2, 204),
		ELSE(0,217) VALID_PIX,
		ALU_PUSH_BEFORE(940,2) KCACHE0(CB5, _0_15),
		ALU(942,19) KCACHE0(CB1, _16_31),
		ALU_PUSH_BEFORE(961,1),
		POP(1, 209),
		ELSE(1, 217) VALID_PIX,
		ALU(962,1),
		ALU_PUSH_BEFORE(963,1),
		ALU(964,19) KCACHE0(CB1, _16_31),
		ALU_PUSH_BEFORE(983,1),
		POP(1, 215),
		ELSE(0,216) VALID_PIX,
		POP(2, 217),
		POP(2, 218),
		ALU_PUSH_BEFORE(984,4) KCACHE0(CB5, _0_15),
		ALU_ELSE_AFTER(988,3),
		ALU_POP_AFTER(991,10) KCACHE0(CB5, _0_15),
		ALU_PUSH_BEFORE(1001,1) KCACHE0(CB5, _0_15),
		ALU_POP_AFTER(1002,13) KCACHE0(CB1, _16_31),
		ALU_PUSH_BEFORE(1015,5) KCACHE0(CB5, _0_15),
		ALU_PUSH_BEFORE(1020,3) KCACHE0(CB5, _0_15),
		ALU_ELSE_AFTER(1023,3),
		ALU_PUSH_BEFORE(1026,3) KCACHE0(CB5, _0_15),
		ALU_ELSE_AFTER(1029,4),
		ALU_PUSH_BEFORE(1033,3) KCACHE0(CB5, _0_15),
		ALU_ELSE_AFTER(1036,4),
		ALU_POP2_AFTER(1040,12) KCACHE0(CB5, _0_15) KCACHE1(CB1, _16_31),
		POP(1, 232),
		ALU_POP_AFTER(1052,3),
		ALU_PUSH_BEFORE(1055,3) KCACHE0(CB5, _0_15),
		ALU(1058,3),
		TEX(1386,1) VALID_PIX,
		ALU_PUSH_BEFORE(1061,2) KCACHE0(CB5, _0_15),
		ALU_PUSH_BEFORE(1063,2) KCACHE0(CB5, _0_15),
		ALU_ELSE_AFTER(1065,3),
		ALU_PUSH_BEFORE(1068,3) KCACHE0(CB5, _0_15),
		ALU_ELSE_AFTER(1071,3),
		ALU_PUSH_BEFORE(1074,3) KCACHE0(CB5, _0_15),
		ALU_ELSE_AFTER(1077,3),
		ALU_PUSH_BEFORE(1080,3) KCACHE0(CB5, _0_15),
		ALU_ELSE_AFTER(1083,3),
		ALU_PUSH_BEFORE(1086,3) KCACHE0(CB5, _0_15),
		ALU_ELSE_AFTER(1089,3),
		ALU_PUSH_BEFORE(1092,3) KCACHE0(CB5, _0_15),
		ALU_ELSE_AFTER(1095,4),
		ALU_PUSH_BEFORE(1099,3) KCACHE0(CB5, _0_15),
		ALU_ELSE_AFTER(1102,5),
		ALU_PUSH_BEFORE(1107,3) KCACHE0(CB5, _0_15),
		ALU_ELSE_AFTER(1110,4),
		ALU_POP2_AFTER(1114,12) KCACHE0(CB5, _0_15) KCACHE1(CB1, _16_31),
		POP(6, 255),
		ELSE(1, 257) VALID_PIX,
		ALU_POP_AFTER(1126,3),
		ALU_PUSH_BEFORE(1129,4) KCACHE0(CB5, _0_15),
		ALU_PUSH_BEFORE(1133,2) KCACHE0(CB5, _0_15),
		ALU_ELSE_AFTER(1135,3),
		ALU_PUSH_BEFORE(1138,3) KCACHE0(CB5, _0_15),
		ALU_ELSE_AFTER(1141,3),
		ALU_PUSH_BEFORE(1144,3) KCACHE0(CB5, _0_15),
		ALU_ELSE_AFTER(1147,3),
		ALU_PUSH_BEFORE(1150,3) KCACHE0(CB5, _0_15),
		ALU_ELSE_AFTER(1153,3),
		ALU_PUSH_BEFORE(1156,3) KCACHE0(CB5, _0_15),
		ALU_ELSE_AFTER(1159,3),
		ALU_PUSH_BEFORE(1162,3) KCACHE0(CB5, _0_15),
		ALU_ELSE_AFTER(1165,4),
		ALU_PUSH_BEFORE(1169,3) KCACHE0(CB5, _0_15),
		ALU_ELSE_AFTER(1172,5),
		ALU_PUSH_BEFORE(1177,3) KCACHE0(CB5, _0_15),
		ALU_ELSE_AFTER(1180,4),
		ALU_POP2_AFTER(1184,11) KCACHE0(CB5, _0_15) KCACHE1(CB1, _16_31),
		POP(6, 276),
		ELSE(1, 278) VALID_PIX,
		ALU_POP_AFTER(1195,3),
		ALU_PUSH_BEFORE(1198,5) KCACHE0(CB5, _0_15),
		ALU_ELSE_AFTER(1203,3),
		ALU_PUSH_BEFORE(1206,2) KCACHE0(CB5, _0_15),
		ALU_ELSE_AFTER(1208,3),
		ALU_PUSH_BEFORE(1211,3) KCACHE0(CB5, _0_15),
		ALU_ELSE_AFTER(1214,3),
		ALU_PUSH_BEFORE(1217,3) KCACHE0(CB5, _0_15),
		ALU_ELSE_AFTER(1220,3),
		ALU_POP2_AFTER(1223,20) KCACHE0(CB5, _0_15),
		POP(3, 288),
		ALU_POP_AFTER(1243,9) KCACHE0(CB5, _0_15),
		ALU_PUSH_BEFORE(1252,2) KCACHE0(CB5, _0_15),
		ALU_PUSH_BEFORE(1254,2) KCACHE0(CB5, _0_15),
		ALU_ELSE_AFTER(1256,1) KCACHE0(CB1, _16_31),
		ALU_PUSH_BEFORE(1257,1) KCACHE0(CB5, _0_15),
		ALU_PUSH_BEFORE(1258,3) KCACHE0(CB5, _0_15),
		ALU_ELSE_AFTER(1261,2),
		ALU_PUSH_BEFORE(1263,3) KCACHE0(CB5, _0_15),
		ALU_ELSE_AFTER(1266,2),
		ALU_PUSH_BEFORE(1268,3) KCACHE0(CB5, _0_15),
		ALU_ELSE_AFTER(1271,2),
		ALU_POP2_AFTER(1273,10) KCACHE0(CB5, _0_15),
		POP(4, 301),
		ALU_PUSH_BEFORE(1283,3) KCACHE0(CB5, _0_15),
		ALU_ELSE_AFTER(1286,8),
		ALU_PUSH_BEFORE(1294,2) KCACHE0(CB5, _0_15),
		ALU_ELSE_AFTER(1296,5),
		ALU(1301,1) KCACHE0(CB5, _0_15),
		ALU_PUSH_BEFORE(1302,1) KCACHE0(CB5, _0_15),
		ELSE(1, 309) VALID_PIX,
		ALU_POP_AFTER(1303,4),
		POP(2, 310),
		ALU_PUSH_BEFORE(1307,2) KCACHE0(CB5, _0_15),
		ALU_ELSE_AFTER(1309,4),
		ALU_PUSH_BEFORE(1313,3) KCACHE0(CB5, _0_15),
		ALU_ELSE_AFTER(1316,3),
		ALU(1319,1) KCACHE0(CB5, _0_15),
		POP(2, 316),
		ALU_PUSH_BEFORE(1320,1) KCACHE0(CB5, _0_15),
		ALU_POP_AFTER(1321,20) KCACHE0(CB5, _0_15),
		ALU(1341,14),
		EXP_DONE(PIX0, _R0,_x,_y,_z,_w) BURSTCNT(2)
		END_OF_PROGRAM
	},
	{
		/* 0 */
		ALU_SETE_INT(_R3,_z, KC0(3),_z, ALU_SRC_LITERAL,_x),
		ALU_SETNE_INT(_R0,_w, ALU_SRC_0,_x, KC0(3),_z),
		ALU_MOV(_R2,_w, ALU_SRC_0,_x)
		ALU_LAST,
		ALU_LITERAL(0x00000003),
		/* 1 */
		ALU_SETNE_INT(_R3,_y, KC0(2),_w, ALU_SRC_1_INT,_x),
		ALU_SETNE_INT(_R3,_w, KC0(2),_w, ALU_SRC_0,_x)
		ALU_LAST,
		/* 2 */
		ALU_NOT_INT(_R4,_w, KC0(0),_x)
		ALU_LAST,
		/* 3 */
		ALU_PRED_SETNE_INT(__,_x, _R4,_w, ALU_SRC_0,_x) UPDATE_EXEC_MASK(DEACTIVATE) UPDATE_PRED
		ALU_LAST,
	},
	{
		/* 4 */
		ALU_PRED_SETNE_INT(__,_x, KC0(0),_y, ALU_SRC_0,_x) UPDATE_EXEC_MASK(DEACTIVATE) UPDATE_PRED
		ALU_LAST,
	},
	{
		/* 5 */
		ALU_PRED_SETNE_INT(__,_x, KC0(1),_y, ALU_SRC_0,_x) UPDATE_EXEC_MASK(DEACTIVATE) UPDATE_PRED
		ALU_LAST,
	},
	{
		/* 6 */
		ALU_RECIP_IEEE(__,_x, _R4,_z) SCL_210
		ALU_LAST,
		/* 7 */
		ALU_MUL_IEEE(__,_y, _R4,_y, ALU_SRC_PS,_x),
		ALU_MUL_IEEE(__,_z, _R4,_x, ALU_SRC_PS,_x)
		ALU_LAST,
		/* 8 */
		ALU_CNDE_INT(_R4,_x, KC0(4),_y, _R4,_y, ALU_SRC_PV,_y),
		ALU_CNDE_INT(_R4,_w, KC0(4),_y, _R4,_x, ALU_SRC_PV,_z)
		ALU_LAST,
		/* 9 */
		ALU_PRED_SETNE_INT(__,_x, KC0(1),_z, ALU_SRC_0,_x) UPDATE_EXEC_MASK(DEACTIVATE) UPDATE_PRED
		ALU_LAST,
	},
	{
		/* 10 */
		ALU_MAX(__,_y, _R4,_w, KC0(12),_z),
		ALU_ADD(__,_z, KC0(12),_x, KC0(12) _NEG,_z)
		ALU_LAST,
		/* 11 */
		ALU_MIN(_R6,_x, ALU_SRC_PV,_z, ALU_SRC_PV,_y)
		ALU_LAST,
	},
	{
		/* 12 */
		ALU_ADD(__,_y, _R4,_w, KC0(12) _NEG,_x),
		ALU_RECIP_IEEE(__,_y, KC0(12),_x) SCL_210
		ALU_LAST,
		/* 13 */
		ALU_MUL_IEEE(__,_w, ALU_SRC_PV,_y, ALU_SRC_PS,_x)
		ALU_LAST,
		/* 14 */
		ALU_ADD(__,_z, ALU_SRC_PV,_w, ALU_SRC_1,_x)
		ALU_LAST,
		/* 15 */
		ALU_FLOOR(__,_y, ALU_SRC_PV,_z)
		ALU_LAST,
		/* 16 */
		ALU_MULADD(_R6,_x, ALU_SRC_PV _NEG,_y, KC0(12),_x, _R4,_w)
		ALU_LAST,
	},
	{
		/* 17 */
		ALU_PRED_SETNE_INT(__,_x, KC0(1),_w, ALU_SRC_0,_x) UPDATE_EXEC_MASK(DEACTIVATE) UPDATE_PRED
		ALU_LAST,
	},
	{
		/* 18 */
		ALU_MAX(__,_x, _R4,_x, KC0(12),_w),
		ALU_ADD(__,_z, KC0(12),_y, KC0(12) _NEG,_w)
		ALU_LAST,
		/* 19 */
		ALU_MIN(_R6,_y, ALU_SRC_PV,_z, ALU_SRC_PV,_x)
		ALU_LAST,
	},
	{
		/* 20 */
		ALU_ADD(__,_x, _R4,_x, KC0(12) _NEG,_y),
		ALU_RECIP_IEEE(__,_x, KC0(12),_y) SCL_210
		ALU_LAST,
		/* 21 */
		ALU_MUL_IEEE(__,_w, ALU_SRC_PV,_x, ALU_SRC_PS,_x)
		ALU_LAST,
		/* 22 */
		ALU_ADD(__,_z, ALU_SRC_PV,_w, ALU_SRC_1,_x)
		ALU_LAST,
		/* 23 */
		ALU_FLOOR(__,_x, ALU_SRC_PV,_z)
		ALU_LAST,
		/* 24 */
		ALU_MULADD(_R6,_y, ALU_SRC_PV _NEG,_x, KC0(12),_y, _R4,_x)
		ALU_LAST,
	},
	{
		/* 25 */
		ALU_PRED_SETNE_INT(__,_x, KC0(2),_x, ALU_SRC_0,_x) UPDATE_EXEC_MASK(DEACTIVATE) UPDATE_PRED
		ALU_LAST,
	},
	{
		/* 26 */
		ALU_ADD(_R6,_x, _R6,_x, KC0(13),_x),
		ALU_ADD(_R6,_y, _R6,_y, KC0(13),_y)
		ALU_LAST,
	},
	{
		/* 27 */
		ALU_MOV(_R6,_x, _R4,_x),
		ALU_MOV(_R6,_y, _R4,_y)
		ALU_LAST,
	},
	{
		/* 28 */
		ALU_NOT_INT(__,_y, KC0(1),_y)
		ALU_LAST,
		/* 29 */
		ALU_CNDE_INT(_R123,_x, ALU_SRC_PV,_y, ALU_SRC_0,_x, ALU_SRC_M_1_INT,_x)
		ALU_LAST,
		/* 30 */
		ALU_CNDE_INT(_R4,_w, KC0(4),_y, ALU_SRC_0,_x, ALU_SRC_PV,_x)
		ALU_LAST,
		/* 31 */
		ALU_PRED_SETNE_INT(__,_x, _R4,_w, ALU_SRC_0,_x) UPDATE_EXEC_MASK(DEACTIVATE) UPDATE_PRED
		ALU_LAST,
	},
	{
		/* 32 */
		ALU_RECIP_IEEE(__,_x, _R4,_z) SCL_210
		ALU_LAST,
		/* 33 */
		ALU_MUL(_R4,_x, _R6,_x, ALU_SRC_PS,_x),
		ALU_MUL(_R4,_y, _R6,_y, ALU_SRC_PS,_x)
		ALU_LAST,
	},
	{
		/* 34 */
		ALU_PRED_SETNE_INT(__,_x, KC0(1),_x, ALU_SRC_0,_x) UPDATE_EXEC_MASK(DEACTIVATE) UPDATE_PRED
		ALU_LAST,
	},
	{
		/* 35 */
		ALU_PRED_SETNE_INT(__,_x, KC0(1),_x, ALU_SRC_0,_x) UPDATE_EXEC_MASK(DEACTIVATE) UPDATE_PRED
		ALU_LAST,
	},
	{
		/* 36 */
		ALU_PRED_SETNE_INT(__,_x, KC0(1),_x, ALU_SRC_0,_x) UPDATE_EXEC_MASK(DEACTIVATE) UPDATE_PRED
		ALU_LAST,
	},
	{
		/* 37 */
		ALU_LSHR_INT(_R4,_x, KC0(5),_y, ALU_SRC_LITERAL,_x),
		ALU_LSHR_INT(_R4,_y, KC0(5),_y, ALU_SRC_LITERAL,_y),
		ALU_MOV(_R4,_z, ALU_SRC_0,_x),
		ALU_LSHR_INT(_R4,_w, KC0(5),_y, ALU_SRC_LITERAL,_z),
		ALU_LSHR_INT(_R5,_z, KC0(5),_y, ALU_SRC_LITERAL,_w)
		ALU_LAST,
		ALU_LITERAL4(0x00000010, 0x00000008, 0x00000018, 0x0000001F),
	},
	{
		/* 38 */
		ALU_AND_INT(_R11,_x, KC0(5),_y, ALU_SRC_LITERAL,_x),
		ALU_AND_INT(__,_y, _R4,_x, ALU_SRC_LITERAL,_x),
		ALU_AND_INT(_R4,_z, _R4,_y, ALU_SRC_LITERAL,_x),
		ALU_AND_INT(_R5,_w, _R4,_w, ALU_SRC_LITERAL,_y),
		ALU_SETNE_INT(_R12,_x, ALU_SRC_0,_x, _R5,_z)
		ALU_LAST,
		ALU_LITERAL2(0x000000FF, 0x00000003),
		/* 39 */
		ALU_SETNE_INT(_R5,_z, ALU_SRC_PV,_w, ALU_SRC_0,_x),
		ALU_LSHL_INT(_R4,_w, ALU_SRC_PV,_y, ALU_SRC_LITERAL,_x),
		ALU_INT_TO_FLT(__,_z, _R5,_x) SCL_210
		ALU_LAST,
		ALU_LITERAL(0x00000004),
		/* 40 */
		ALU_MUL(__,_y, _R6,_x, ALU_SRC_PS,_x),
		ALU_INT_TO_FLT(__,_y, _R5,_y) SCL_210
		ALU_LAST,
		/* 41 */
		ALU_MUL(__,_x, _R6,_y, ALU_SRC_PS,_x),
		ALU_FRACT(_R10,_x, ALU_SRC_PV,_y)
		ALU_LAST,
		/* 42 */
		ALU_FRACT(_R10,_y, ALU_SRC_PV,_x)
		ALU_LAST,
		/* 43 */
		ALU_PRED_SETE_INT(__,_x, _R5,_z, ALU_SRC_0,_x) UPDATE_EXEC_MASK(DEACTIVATE) UPDATE_PRED
		ALU_LAST,
	},
	{
		/* 44 */
		ALU_MUL(__,_x, _R16,_y, ALU_SRC_LITERAL,_x),
		ALU_MUL(_R127,_y, _R16,_x, ALU_SRC_LITERAL,_y),
		ALU_MUL(_R127,_w, _R16,_z, ALU_SRC_LITERAL,_y)
		ALU_LAST,
		ALU_LITERAL2(0x427FF5C3, 0x41FFEB85),
		/* 45 */
		ALU_FLT_TO_UINT(__,_x, ALU_SRC_PV,_x) SCL_210
		ALU_LAST,
		/* 46 */
		ALU_LSHL_INT(_R127,_w, ALU_SRC_PS,_x, ALU_SRC_LITERAL,_x),
		ALU_FLT_TO_UINT(__,_w, _R127,_w) SCL_210
		ALU_LAST,
		ALU_LITERAL(0x00000005),
		/* 47 */
		ALU_LSHL_INT(__,_z, ALU_SRC_PS,_x, ALU_SRC_LITERAL,_x),
		ALU_FLT_TO_UINT(_R127,_z, _R127,_y) SCL_210
		ALU_LAST,
		ALU_LITERAL(0x0000000B),
		/* 48 */
		ALU_OR_INT(__,_y, _R127,_w, ALU_SRC_PV,_z)
		ALU_LAST,
		/* 49 */
		ALU_OR_INT(_R13,_x, _R127,_z, ALU_SRC_PV,_y)
		ALU_LAST,
		/* 50 */
		ALU_PRED_SETNE_INT(__,_x, _R12,_x, ALU_SRC_0,_x) UPDATE_EXEC_MASK(DEACTIVATE) UPDATE_PRED
		ALU_LAST,
	},
	{
		/* 51 */
		ALU_MUL(_R127,_x, _R8,_z, ALU_SRC_LITERAL,_x),
		ALU_MUL(_R127,_y, _R8,_y, ALU_SRC_LITERAL,_y),
		ALU_MUL(_R127,_z, _R9,_z, ALU_SRC_LITERAL,_x) VEC_120,
		ALU_MUL(__,_w, _R9,_y, ALU_SRC_LITERAL,_y) VEC_120,
		ALU_MUL(_R126,_x, _R7,_y, ALU_SRC_LITERAL,_y)
		ALU_LAST,
		ALU_LITERAL2(0x41FFEB85, 0x427FF5C3),
		/* 52 */
		ALU_MUL(_R125,_x, _R9,_x, ALU_SRC_LITERAL,_x),
		ALU_MUL(_R126,_y, _R7,_x, ALU_SRC_LITERAL,_x) VEC_120,
		ALU_MUL(_R126,_z, _R8,_x, ALU_SRC_LITERAL,_x) VEC_201,
		ALU_MUL(_R127,_w, _R7,_z, ALU_SRC_LITERAL,_x),
		ALU_FLT_TO_UINT(__,_x, ALU_SRC_PV,_w) SCL_210
		ALU_LAST,
		ALU_LITERAL(0x41FFEB85),
		/* 53 */
		ALU_LSHL_INT(_R127,_z, ALU_SRC_PS,_x, ALU_SRC_LITERAL,_x),
		ALU_FLT_TO_UINT(__,_z, _R127,_z) SCL_210
		ALU_LAST,
		ALU_LITERAL(0x00000005),
		/* 54 */
		ALU_LSHL_INT(__,_y, ALU_SRC_PS,_x, ALU_SRC_LITERAL,_x),
		ALU_FLT_TO_UINT(__,_y, _R127,_y) SCL_210
		ALU_LAST,
		ALU_LITERAL(0x0000000B),
		/* 55 */
		ALU_LSHL_INT(_R127,_x, ALU_SRC_PS,_x, ALU_SRC_LITERAL,_x),
		ALU_OR_INT(_R126,_w, _R127,_z, ALU_SRC_PV,_y),
		ALU_FLT_TO_UINT(__,_x, _R127,_x) SCL_210
		ALU_LAST,
		ALU_LITERAL(0x00000005),
		/* 56 */
		ALU_LSHL_INT(__,_w, ALU_SRC_PS,_x, ALU_SRC_LITERAL,_x),
		ALU_FLT_TO_UINT(__,_w, _R126,_x) SCL_210
		ALU_LAST,
		ALU_LITERAL(0x0000000B),
		/* 57 */
		ALU_OR_INT(_R127,_z, _R127,_x, ALU_SRC_PV,_w),
		ALU_LSHL_INT(_R127,_w, ALU_SRC_PS,_x, ALU_SRC_LITERAL,_x),
		ALU_FLT_TO_UINT(__,_z, _R127,_w) SCL_210
		ALU_LAST,
		ALU_LITERAL(0x00000005),
		/* 58 */
		ALU_LSHL_INT(__,_z, ALU_SRC_PS,_x, ALU_SRC_LITERAL,_x),
		ALU_FLT_TO_UINT(__,_z, _R125,_x) SCL_210
		ALU_LAST,
		ALU_LITERAL(0x0000000B),
		/* 59 */
		ALU_OR_INT(_R18,_x, ALU_SRC_PS,_x, _R126,_w),
		ALU_OR_INT(_R127,_y, _R127,_w, ALU_SRC_PV,_z),
		ALU_FLT_TO_UINT(__,_x, _R126,_z) SCL_210
		ALU_LAST,
		/* 60 */
		ALU_OR_INT(_R15,_x, ALU_SRC_PS,_x, _R127,_z),
		ALU_FLT_TO_UINT(__,_x, _R126,_y) SCL_210
		ALU_LAST,
		/* 61 */
		ALU_OR_INT(_R14,_x, ALU_SRC_PS,_x, _R127,_y)
		ALU_LAST,
	},
	{
		/* 62 */
		ALU_SETNE_INT(_R4,_y, _R5,_w, ALU_SRC_1_INT,_x)
		ALU_LAST,
		/* 63 */
		ALU_PRED_SETE_INT(__,_x, _R4,_y, ALU_SRC_0,_x) UPDATE_EXEC_MASK(DEACTIVATE) UPDATE_PRED
		ALU_LAST,
	},
	{
		/* 64 */
		ALU_MUL(_R126,_x, _R16,_x, ALU_SRC_LITERAL,_x),
		ALU_MUL(_R127,_y, _R16,_w, ALU_SRC_1,_x),
		ALU_MUL(__,_z, _R16,_z, ALU_SRC_LITERAL,_x),
		ALU_MUL(_R127,_w, _R16,_y, ALU_SRC_LITERAL,_x)
		ALU_LAST,
		ALU_LITERAL(0x41FFEB85),
		/* 65 */
		ALU_FLT_TO_UINT(__,_x, ALU_SRC_PV,_z) SCL_210
		ALU_LAST,
		/* 66 */
		ALU_LSHL_INT(_R127,_x, ALU_SRC_PS,_x, ALU_SRC_LITERAL,_x),
		ALU_FLT_TO_UINT(__,_x, _R127,_y) SCL_210
		ALU_LAST,
		ALU_LITERAL(0x0000000A),
		/* 67 */
		ALU_LSHL_INT(__,_w, ALU_SRC_PS,_x, ALU_SRC_LITERAL,_x),
		ALU_FLT_TO_UINT(__,_w, _R127,_w) SCL_210
		ALU_LAST,
		ALU_LITERAL(0x0000000F),
		/* 68 */
		ALU_LSHL_INT(__,_y, ALU_SRC_PS,_x, ALU_SRC_LITERAL,_x),
		ALU_OR_INT(__,_z, _R127,_x, ALU_SRC_PV,_w),
		ALU_FLT_TO_UINT(_R127,_y, _R126,_x) SCL_210
		ALU_LAST,
		ALU_LITERAL(0x00000005),
		/* 69 */
		ALU_OR_INT(__,_y, ALU_SRC_PV,_y, ALU_SRC_PV,_z)
		ALU_LAST,
		/* 70 */
		ALU_OR_INT(_R13,_x, _R127,_y, ALU_SRC_PV,_y)
		ALU_LAST,
		/* 71 */
		ALU_PRED_SETNE_INT(__,_x, _R12,_x, ALU_SRC_0,_x) UPDATE_EXEC_MASK(DEACTIVATE) UPDATE_PRED
		ALU_LAST,
	},
	{
		/* 72 */
		ALU_MUL(_R127,_x, _R9,_w, ALU_SRC_1,_x),
		ALU_MUL(__,_y, _R9,_z, ALU_SRC_LITERAL,_x),
		ALU_MUL(_R127,_z, _R8,_w, ALU_SRC_1,_x) VEC_120,
		ALU_MUL(_R127,_w, _R8,_z, ALU_SRC_LITERAL,_x) VEC_120,
		ALU_MUL(_R127,_y, _R7,_w, ALU_SRC_1,_x)
		ALU_LAST,
		ALU_LITERAL(0x41FFEB85),
		/* 73 */
		ALU_MUL(_R126,_x, _R9,_y, ALU_SRC_LITERAL,_x),
		ALU_MUL(_R125,_y, _R8,_y, ALU_SRC_LITERAL,_x) VEC_120,
		ALU_MUL(_R126,_z, _R7,_z, ALU_SRC_LITERAL,_x),
		ALU_MUL(_R125,_w, _R7,_y, ALU_SRC_LITERAL,_x) VEC_201,
		ALU_FLT_TO_UINT(__,_x, ALU_SRC_PV,_y) SCL_210
		ALU_LAST,
		ALU_LITERAL(0x41FFEB85),
		/* 74 */
		ALU_MUL(_R124,_x, _R9,_x, ALU_SRC_LITERAL,_x),
		ALU_MUL(_R124,_y, _R8,_x, ALU_SRC_LITERAL,_x) VEC_120,
		ALU_LSHL_INT(_R126,_w, ALU_SRC_PS,_x, ALU_SRC_LITERAL,_y),
		ALU_FLT_TO_UINT(__,_x, _R127,_x) SCL_210
		ALU_LAST,
		ALU_LITERAL2(0x41FFEB85, 0x0000000A),
		/* 75 */
		ALU_MUL(_R4,_x, _R7,_x, ALU_SRC_LITERAL,_x),
		ALU_LSHL_INT(__,_z, ALU_SRC_PS,_x, ALU_SRC_LITERAL,_y),
		ALU_FLT_TO_UINT(__,_x, _R127,_w) SCL_210
		ALU_LAST,
		ALU_LITERAL2(0x41FFEB85, 0x0000000F),
		/* 76 */
		ALU_OR_INT(_R125,_x, _R126,_w, ALU_SRC_PV,_z),
		ALU_LSHL_INT(_R126,_y, ALU_SRC_PS,_x, ALU_SRC_LITERAL,_x),
		ALU_FLT_TO_UINT(__,_x, _R127,_z) SCL_210
		ALU_LAST,
		ALU_LITERAL(0x0000000A),
		/* 77 */
		ALU_LSHL_INT(__,_x, ALU_SRC_PS,_x, ALU_SRC_LITERAL,_x),
		ALU_FLT_TO_UINT(__,_x, _R126,_z) SCL_210
		ALU_LAST,
		ALU_LITERAL(0x0000000F),
		/* 78 */
		ALU_LSHL_INT(_R127,_x, ALU_SRC_PS,_x, ALU_SRC_LITERAL,_x),
		ALU_OR_INT(_R126,_w, _R126,_y, ALU_SRC_PV,_x),
		ALU_FLT_TO_UINT(__,_x, _R127,_y) SCL_210
		ALU_LAST,
		ALU_LITERAL(0x0000000A),
		/* 79 */
		ALU_LSHL_INT(__,_w, ALU_SRC_PS,_x, ALU_SRC_LITERAL,_x),
		ALU_FLT_TO_UINT(__,_w, _R126,_x) SCL_210
		ALU_LAST,
		ALU_LITERAL(0x0000000F),
		/* 80 */
		ALU_LSHL_INT(__,_x, ALU_SRC_PS,_x, ALU_SRC_LITERAL,_x),
		ALU_OR_INT(_R126,_z, _R127,_x, ALU_SRC_PV,_w),
		ALU_FLT_TO_UINT(__,_x, _R125,_y) SCL_210
		ALU_LAST,
		ALU_LITERAL(0x00000005),
		/* 81 */
		ALU_LSHL_INT(__,_z, ALU_SRC_PS,_x, ALU_SRC_LITERAL,_x),
		ALU_OR_INT(_R125,_w, ALU_SRC_PV,_x, _R125,_x),
		ALU_FLT_TO_UINT(__,_z, _R125,_w) SCL_210
		ALU_LAST,
		ALU_LITERAL(0x00000005),
		/* 82 */
		ALU_LSHL_INT(__,_y, ALU_SRC_PS,_x, ALU_SRC_LITERAL,_x),
		ALU_OR_INT(_R127,_z, ALU_SRC_PV,_z, _R126,_w),
		ALU_FLT_TO_UINT(__,_y, _R124,_x) SCL_210
		ALU_LAST,
		ALU_LITERAL(0x00000005),
		/* 83 */
		ALU_OR_INT(_R18,_x, ALU_SRC_PS,_x, _R125,_w),
		ALU_OR_INT(_R124,_y, ALU_SRC_PV,_y, _R126,_z),
		ALU_FLT_TO_UINT(__,_x, _R124,_y) SCL_210
		ALU_LAST,
		/* 84 */
		ALU_OR_INT(_R15,_x, ALU_SRC_PS,_x, _R127,_z),
		ALU_FLT_TO_UINT(__,_x, _R4,_x) SCL_210
		ALU_LAST,
		/* 85 */
		ALU_OR_INT(_R14,_x, ALU_SRC_PS,_x, _R124,_y)
		ALU_LAST,
	},
	{
		/* 86 */
		ALU_SETNE_INT(_R4,_x, _R5,_w, ALU_SRC_LITERAL,_x)
		ALU_LAST,
		ALU_LITERAL(0x00000002),
		/* 87 */
		ALU_PRED_SETE_INT(__,_x, _R4,_x, ALU_SRC_0,_x) UPDATE_EXEC_MASK(DEACTIVATE) UPDATE_PRED
		ALU_LAST,
	},
	{
		/* 88 */
		ALU_MUL(_R127,_x, _R16,_y, ALU_SRC_LITERAL,_x),
		ALU_MUL(_R127,_y, _R16,_x, ALU_SRC_LITERAL,_x),
		ALU_MUL(_R127,_z, _R16,_w, ALU_SRC_LITERAL,_x),
		ALU_MUL(__,_w, _R16,_z, ALU_SRC_LITERAL,_x)
		ALU_LAST,
		ALU_LITERAL(0x417FD70A),
		/* 89 */
		ALU_FLT_TO_UINT(__,_x, ALU_SRC_PV,_w) SCL_210
		ALU_LAST,
		/* 90 */
		ALU_LSHL_INT(_R126,_x, ALU_SRC_PS,_x, ALU_SRC_LITERAL,_x),
		ALU_FLT_TO_UINT(__,_x, _R127,_z) SCL_210
		ALU_LAST,
		ALU_LITERAL(0x00000008),
		/* 91 */
		ALU_LSHL_INT(__,_w, ALU_SRC_PS,_x, ALU_SRC_LITERAL,_x),
		ALU_FLT_TO_UINT(__,_w, _R127,_x) SCL_210
		ALU_LAST,
		ALU_LITERAL(0x0000000C),
		/* 92 */
		ALU_LSHL_INT(__,_y, ALU_SRC_PS,_x, ALU_SRC_LITERAL,_x),
		ALU_OR_INT(__,_z, _R126,_x, ALU_SRC_PV,_w),
		ALU_FLT_TO_UINT(_R127,_y, _R127,_y) SCL_210
		ALU_LAST,
		ALU_LITERAL(0x00000004),
		/* 93 */
		ALU_OR_INT(__,_y, ALU_SRC_PV,_y, ALU_SRC_PV,_z)
		ALU_LAST,
		/* 94 */
		ALU_OR_INT(_R13,_x, _R127,_y, ALU_SRC_PV,_y)
		ALU_LAST,
		/* 95 */
		ALU_PRED_SETNE_INT(__,_x, _R12,_x, ALU_SRC_0,_x) UPDATE_EXEC_MASK(DEACTIVATE) UPDATE_PRED
		ALU_LAST,
	},
	{
		/* 96 */
		ALU_MUL(_R127,_x, _R8,_z, ALU_SRC_LITERAL,_x),
		ALU_MUL(_R127,_y, _R9,_w, ALU_SRC_LITERAL,_x),
		ALU_MUL(__,_z, _R9,_z, ALU_SRC_LITERAL,_x) VEC_120,
		ALU_MUL(_R127,_w, _R8,_w, ALU_SRC_LITERAL,_x) VEC_120,
		ALU_MUL(_R125,_w, _R7,_z, ALU_SRC_LITERAL,_x)
		ALU_LAST,
		ALU_LITERAL(0x417FD70A),
		/* 97 */
		ALU_MUL(_R126,_x, _R7,_y, ALU_SRC_LITERAL,_x),
		ALU_MUL(_R126,_y, _R8,_y, ALU_SRC_LITERAL,_x) VEC_120,
		ALU_MUL(_R127,_z, _R7,_w, ALU_SRC_LITERAL,_x),
		ALU_MUL(_R124,_w, _R9,_y, ALU_SRC_LITERAL,_x) VEC_201,
		ALU_FLT_TO_UINT(__,_x, ALU_SRC_PV,_z) SCL_210
		ALU_LAST,
		ALU_LITERAL(0x417FD70A),
		/* 98 */
		ALU_MUL(_R124,_x, _R9,_x, ALU_SRC_LITERAL,_x),
		ALU_MUL(_R125,_y, _R7,_x, ALU_SRC_LITERAL,_x) VEC_120,
		ALU_MUL(_R126,_z, _R8,_x, ALU_SRC_LITERAL,_x) VEC_201,
		ALU_LSHL_INT(_R126,_w, ALU_SRC_PS,_x, ALU_SRC_LITERAL,_y),
		ALU_FLT_TO_UINT(__,_x, _R127,_y) SCL_210
		ALU_LAST,
		ALU_LITERAL2(0x417FD70A, 0x00000008),
		/* 99 */
		ALU_LSHL_INT(__,_z, ALU_SRC_PS,_x, ALU_SRC_LITERAL,_x),
		ALU_FLT_TO_UINT(__,_z, _R127,_x) SCL_210
		ALU_LAST,
		ALU_LITERAL(0x0000000C),
		/* 100 */
		ALU_OR_INT(_R125,_x, _R126,_w, ALU_SRC_PV,_z),
		ALU_LSHL_INT(_R127,_y, ALU_SRC_PS,_x, ALU_SRC_LITERAL,_x),
		ALU_FLT_TO_UINT(__,_x, _R127,_w) SCL_210
		ALU_LAST,
		ALU_LITERAL(0x00000008),
		/* 101 */
		ALU_LSHL_INT(__,_x, ALU_SRC_PS,_x, ALU_SRC_LITERAL,_x),
		ALU_FLT_TO_UINT(__,_x, _R125,_w) SCL_210
		ALU_LAST,
		ALU_LITERAL(0x0000000C),
		/* 102 */
		ALU_LSHL_INT(_R127,_x, ALU_SRC_PS,_x, ALU_SRC_LITERAL,_x),
		ALU_OR_INT(_R125,_w, _R127,_y, ALU_SRC_PV,_x),
		ALU_FLT_TO_UINT(__,_x, _R127,_z) SCL_210
		ALU_LAST,
		ALU_LITERAL(0x00000008),
		/* 103 */
		ALU_LSHL_INT(__,_w, ALU_SRC_PS,_x, ALU_SRC_LITERAL,_x),
		ALU_FLT_TO_UINT(__,_w, _R124,_w) SCL_210
		ALU_LAST,
		ALU_LITERAL(0x0000000C),
		/* 104 */
		ALU_LSHL_INT(__,_x, ALU_SRC_PS,_x, ALU_SRC_LITERAL,_x),
		ALU_OR_INT(_R127,_z, _R127,_x, ALU_SRC_PV,_w),
		ALU_FLT_TO_UINT(__,_x, _R126,_y) SCL_210
		ALU_LAST,
		ALU_LITERAL(0x00000004),
		/* 105 */
		ALU_LSHL_INT(__,_z, ALU_SRC_PS,_x, ALU_SRC_LITERAL,_x),
		ALU_OR_INT(_R124,_w, ALU_SRC_PV,_x, _R125,_x),
		ALU_FLT_TO_UINT(__,_z, _R126,_x) SCL_210
		ALU_LAST,
		ALU_LITERAL(0x00000004),
		/* 106 */
		ALU_LSHL_INT(__,_y, ALU_SRC_PS,_x, ALU_SRC_LITERAL,_x),
		ALU_OR_INT(_R125,_z, ALU_SRC_PV,_z, _R125,_w),
		ALU_FLT_TO_UINT(__,_y, _R124,_x) SCL_210
		ALU_LAST,
		ALU_LITERAL(0x00000004),
		/* 107 */
		ALU_OR_INT(_R18,_x, ALU_SRC_PS,_x, _R124,_w),
		ALU_OR_INT(_R126,_y, ALU_SRC_PV,_y, _R127,_z),
		ALU_FLT_TO_UINT(__,_x, _R126,_z) SCL_210
		ALU_LAST,
		/* 108 */
		ALU_OR_INT(_R15,_x, ALU_SRC_PS,_x, _R125,_z),
		ALU_FLT_TO_UINT(__,_x, _R125,_y) SCL_210
		ALU_LAST,
		/* 109 */
		ALU_OR_INT(_R14,_x, ALU_SRC_PS,_x, _R126,_y)
		ALU_LAST,
	},
	{
		/* 110 */
		ALU_SETNE_INT(_R5,_w, _R5,_w, ALU_SRC_LITERAL,_x)
		ALU_LAST,
		ALU_LITERAL(0x00000003),
		/* 111 */
		ALU_PRED_SETE_INT(__,_x, _R5,_w, ALU_SRC_0,_x) UPDATE_EXEC_MASK(DEACTIVATE) UPDATE_PRED
		ALU_LAST,
	},
	{
		/* 112 */
		ALU_MUL(_R127,_x, _R16,_y, ALU_SRC_LITERAL,_x),
		ALU_MUL(_R127,_y, _R16,_x, ALU_SRC_LITERAL,_x),
		ALU_MUL(_R127,_z, _R16,_w, ALU_SRC_LITERAL,_x),
		ALU_MUL(__,_w, _R16,_z, ALU_SRC_LITERAL,_x)
		ALU_LAST,
		ALU_LITERAL(0x437FFD71),
		/* 113 */
		ALU_FLT_TO_UINT(__,_x, ALU_SRC_PV,_w) SCL_210
		ALU_LAST,
		/* 114 */
		ALU_LSHL_INT(_R126,_x, ALU_SRC_PS,_x, ALU_SRC_LITERAL,_x),
		ALU_FLT_TO_UINT(__,_x, _R127,_z) SCL_210
		ALU_LAST,
		ALU_LITERAL(0x00000010),
		/* 115 */
		ALU_LSHL_INT(__,_w, ALU_SRC_PS,_x, ALU_SRC_LITERAL,_x),
		ALU_FLT_TO_UINT(__,_w, _R127,_x) SCL_210
		ALU_LAST,
		ALU_LITERAL(0x00000018),
		/* 116 */
		ALU_LSHL_INT(__,_y, ALU_SRC_PS,_x, ALU_SRC_LITERAL,_x),
		ALU_OR_INT(__,_z, _R126,_x, ALU_SRC_PV,_w),
		ALU_FLT_TO_UINT(_R127,_y, _R127,_y) SCL_210
		ALU_LAST,
		ALU_LITERAL(0x00000008),
		/* 117 */
		ALU_OR_INT(__,_y, ALU_SRC_PV,_y, ALU_SRC_PV,_z)
		ALU_LAST,
		/* 118 */
		ALU_OR_INT(_R13,_x, _R127,_y, ALU_SRC_PV,_y)
		ALU_LAST,
		/* 119 */
		ALU_PRED_SETNE_INT(__,_x, _R12,_x, ALU_SRC_0,_x) UPDATE_EXEC_MASK(DEACTIVATE) UPDATE_PRED
		ALU_LAST,
	},
	{
		/* 120 */
		ALU_MUL(_R127,_x, _R8,_z, ALU_SRC_LITERAL,_x),
		ALU_MUL(_R127,_y, _R9,_w, ALU_SRC_LITERAL,_x),
		ALU_MUL(__,_z, _R9,_z, ALU_SRC_LITERAL,_x) VEC_120,
		ALU_MUL(_R127,_w, _R8,_w, ALU_SRC_LITERAL,_x) VEC_120,
		ALU_MUL(_R125,_w, _R7,_z, ALU_SRC_LITERAL,_x)
		ALU_LAST,
		ALU_LITERAL(0x437FFD71),
		/* 121 */
		ALU_MUL(_R126,_x, _R7,_y, ALU_SRC_LITERAL,_x),
		ALU_MUL(_R126,_y, _R8,_y, ALU_SRC_LITERAL,_x) VEC_120,
		ALU_MUL(_R127,_z, _R7,_w, ALU_SRC_LITERAL,_x),
		ALU_MUL(_R124,_w, _R9,_y, ALU_SRC_LITERAL,_x) VEC_201,
		ALU_FLT_TO_UINT(__,_x, ALU_SRC_PV,_z) SCL_210
		ALU_LAST,
		ALU_LITERAL(0x437FFD71),
		/* 122 */
		ALU_MUL(_R124,_x, _R9,_x, ALU_SRC_LITERAL,_x),
		ALU_MUL(_R125,_y, _R7,_x, ALU_SRC_LITERAL,_x) VEC_120,
		ALU_MUL(_R126,_z, _R8,_x, ALU_SRC_LITERAL,_x) VEC_201,
		ALU_LSHL_INT(_R126,_w, ALU_SRC_PS,_x, ALU_SRC_LITERAL,_y),
		ALU_FLT_TO_UINT(__,_x, _R127,_y) SCL_210
		ALU_LAST,
		ALU_LITERAL2(0x437FFD71, 0x00000010),
		/* 123 */
		ALU_LSHL_INT(__,_z, ALU_SRC_PS,_x, ALU_SRC_LITERAL,_x),
		ALU_FLT_TO_UINT(__,_z, _R127,_x) SCL_210
		ALU_LAST,
		ALU_LITERAL(0x00000018),
		/* 124 */
		ALU_OR_INT(_R125,_x, _R126,_w, ALU_SRC_PV,_z),
		ALU_LSHL_INT(_R127,_y, ALU_SRC_PS,_x, ALU_SRC_LITERAL,_x),
		ALU_FLT_TO_UINT(__,_x, _R127,_w) SCL_210
		ALU_LAST,
		ALU_LITERAL(0x00000010),
		/* 125 */
		ALU_LSHL_INT(__,_x, ALU_SRC_PS,_x, ALU_SRC_LITERAL,_x),
		ALU_FLT_TO_UINT(__,_x, _R125,_w) SCL_210
		ALU_LAST,
		ALU_LITERAL(0x00000018),
		/* 126 */
		ALU_LSHL_INT(_R127,_x, ALU_SRC_PS,_x, ALU_SRC_LITERAL,_x),
		ALU_OR_INT(_R125,_w, _R127,_y, ALU_SRC_PV,_x),
		ALU_FLT_TO_UINT(__,_x, _R127,_z) SCL_210
		ALU_LAST,
		ALU_LITERAL(0x00000010),
		/* 127 */
		ALU_LSHL_INT(__,_w, ALU_SRC_PS,_x, ALU_SRC_LITERAL,_x),
		ALU_FLT_TO_UINT(__,_w, _R124,_w) SCL_210
		ALU_LAST,
		ALU_LITERAL(0x00000018),
		/* 128 */
		ALU_LSHL_INT(__,_x, ALU_SRC_PS,_x, ALU_SRC_LITERAL,_x),
		ALU_OR_INT(_R127,_z, _R127,_x, ALU_SRC_PV,_w),
		ALU_FLT_TO_UINT(__,_x, _R126,_y) SCL_210
		ALU_LAST,
		ALU_LITERAL(0x00000008),
		/* 129 */
		ALU_LSHL_INT(__,_z, ALU_SRC_PS,_x, ALU_SRC_LITERAL,_x),
		ALU_OR_INT(_R124,_w, ALU_SRC_PV,_x, _R125,_x),
		ALU_FLT_TO_UINT(__,_z, _R126,_x) SCL_210
		ALU_LAST,
		ALU_LITERAL(0x00000008),
		/* 130 */
		ALU_LSHL_INT(__,_y, ALU_SRC_PS,_x, ALU_SRC_LITERAL,_x),
		ALU_OR_INT(_R125,_z, ALU_SRC_PV,_z, _R125,_w),
		ALU_FLT_TO_UINT(__,_y, _R124,_x) SCL_210
		ALU_LAST,
		ALU_LITERAL(0x00000008),
		/* 131 */
		ALU_OR_INT(_R18,_x, ALU_SRC_PS,_x, _R124,_w),
		ALU_OR_INT(_R126,_y, ALU_SRC_PV,_y, _R127,_z),
		ALU_FLT_TO_UINT(__,_x, _R126,_z) SCL_210
		ALU_LAST,
		/* 132 */
		ALU_OR_INT(_R15,_x, ALU_SRC_PS,_x, _R125,_z),
		ALU_FLT_TO_UINT(__,_x, _R125,_y) SCL_210
		ALU_LAST,
		/* 133 */
		ALU_OR_INT(_R14,_x, ALU_SRC_PS,_x, _R126,_y)
		ALU_LAST,
	},
	{
		/* 134 */
		ALU_MOV(_R4,_y, ALU_SRC_0,_x),
		ALU_LSHR_INT(__,_w, _R13,_x, _R4,_z)
		ALU_LAST,
		/* 135 */
		ALU_AND_INT(__,_z, _R11,_x, ALU_SRC_PV,_w)
		ALU_LAST,
		/* 136 */
		ALU_OR_INT(_R4,_x, _R4,_w, ALU_SRC_PV,_z)
		ALU_LAST,
	},
	{
		/* 137 */
		ALU_PRED_SETNE_INT(__,_x, _R12,_x, ALU_SRC_0,_x) UPDATE_EXEC_MASK(DEACTIVATE) UPDATE_PRED
		ALU_LAST,
	},
	{
		/* 138 */
		ALU_LSHR_INT(__,_x, _R14,_x, _R4,_z),
		ALU_LSHR_INT(__,_y, _R18,_x, _R4,_z) VEC_120,
		ALU_MOV(_R4,_z, ALU_SRC_0,_x),
		ALU_LSHR_INT(__,_w, _R15,_x, _R4,_z) VEC_201,
		ALU_MOV(_R4,_y, ALU_SRC_0,_x)
		ALU_LAST,
		/* 139 */
		ALU_AND_INT(__,_x, _R11,_x, ALU_SRC_PV,_y),
		ALU_AND_INT(__,_y, _R11,_x, ALU_SRC_PV,_x),
		ALU_AND_INT(__,_z, _R11,_x, ALU_SRC_PV,_w),
		ALU_MOV(_R5,_w, ALU_SRC_0,_x)
		ALU_LAST,
		/* 140 */
		ALU_OR_INT(_R4,_x, _R4,_w, ALU_SRC_PV,_x),
		ALU_OR_INT(_R5,_y, _R4,_w, ALU_SRC_PV,_y),
		ALU_OR_INT(_R4,_w, _R4,_w, ALU_SRC_PV,_z)
		ALU_LAST,
	},
	{
		/* 141 */
		ALU_ADD(_R127,_x, _R16 _NEG,_w, _R6,_w),
		ALU_ADD(_R127,_y, _R16 _NEG,_z, _R6,_z),
		ALU_ADD(__,_z, _R16 _NEG,_y, _R6,_y),
		ALU_ADD(_R127,_w, _R16 _NEG,_x, _R6,_x)
		ALU_LAST,
		/* 142 */
		ALU_ADD(_R126,_x, _R4 _NEG,_w, _R5,_w),
		ALU_ADD(__,_y, _R4 _NEG,_z, _R5,_z),
		ALU_ADD(__,_z, _R4 _NEG,_y, _R5,_y) VEC_120,
		ALU_ADD(_R126,_w, _R4 _NEG,_x, _R5,_x) VEC_021,
		ALU_MULADD(_R126,_z, ALU_SRC_PV,_z, _R10,_x, _R16,_y)
		ALU_LAST,
		/* 143 */
		ALU_MULADD(_R127,_x, _R127,_x, _R10,_x, _R16,_w),
		ALU_MULADD(_R126,_y, _R127,_y, _R10,_x, _R16,_z),
		ALU_MULADD(_R123,_z, ALU_SRC_PV,_z, _R10,_x, _R4,_y),
		ALU_MULADD(_R127,_w, _R127,_w, _R10,_x, _R16,_x),
		ALU_MULADD(_R122,_x, ALU_SRC_PV,_y, _R10,_x, _R4,_z)
		ALU_LAST,
		/* 144 */
		ALU_MULADD(_R123,_x, _R126,_x, _R10,_x, _R4,_w),
		ALU_ADD(_R127,_y, ALU_SRC_PV _NEG,_y, ALU_SRC_PS,_x),
		ALU_ADD(_R127,_z, _R126 _NEG,_z, ALU_SRC_PV,_z),
		ALU_MULADD(_R123,_w, _R126,_w, _R10,_x, _R4,_x)
		ALU_LAST,
		/* 145 */
		ALU_ADD(__,_x, _R127 _NEG,_x, ALU_SRC_PV,_x),
		ALU_ADD(__,_w, _R127 _NEG,_w, ALU_SRC_PV,_w)
		ALU_LAST,
		/* 146 */
		ALU_MULADD(_R16,_x, ALU_SRC_PV,_w, _R10,_y, _R127,_w),
		ALU_MULADD(_R16,_y, _R127,_z, _R10,_y, _R126,_z),
		ALU_MULADD(_R16,_z, _R127,_y, _R10,_y, _R126,_y),
		ALU_MULADD(_R16,_w, ALU_SRC_PV,_x, _R10,_y, _R127,_x)
		ALU_LAST,
	},
	{
		/* 147 */
		ALU_ADD(_R5,_x, _R1,_x, _R16,_x),
		ALU_ADD(_R5,_y, _R1,_y, _R16,_y),
		ALU_ADD(_R4,_z, _R1,_z, _R16,_z),
		ALU_MUL(_R5,_z, _R1,_w, _R16,_w)
		ALU_LAST,
		/* 148 */
		ALU_PRED_SETNE_INT(__,_x, KC0(0),_w, ALU_SRC_0,_x) UPDATE_EXEC_MASK(DEACTIVATE) UPDATE_PRED
		ALU_LAST,
	},
	{
		/* 149 */
		ALU_SETNE_INT(_R4,_y, KC0(0),_z, ALU_SRC_0,_x)
		ALU_LAST,
		/* 150 */
		ALU_PRED_SETE_INT(__,_x, _R4,_y, ALU_SRC_0,_x) UPDATE_EXEC_MASK(DEACTIVATE) UPDATE_PRED
		ALU_LAST,
	},
	{
		/* 151 */
		ALU_MUL(_R1,_x, _R1,_x, _R16,_x),
		ALU_MUL(_R1,_y, _R1,_y, _R16,_y),
		ALU_MUL(_R1,_z, _R1,_z, _R16,_z),
		ALU_MUL(_R1,_w, _R1,_w, _R16,_w)
		ALU_LAST,
	},
	{
		/* 152 */
		ALU_SETNE_INT(_R4,_x, KC0(0),_z, ALU_SRC_1_INT,_x)
		ALU_LAST,
		/* 153 */
		ALU_PRED_SETE_INT(__,_x, _R4,_x, ALU_SRC_0,_x) UPDATE_EXEC_MASK(DEACTIVATE) UPDATE_PRED
		ALU_LAST,
	},
	{
		/* 154 */
		ALU_ADD(__,_x, _R1 _NEG,_y, _R16,_y),
		ALU_ADD(__,_y, _R1 _NEG,_x, _R16,_x),
		ALU_ADD(__,_w, _R1 _NEG,_z, _R16,_z)
		ALU_LAST,
		/* 155 */
		ALU_MULADD(_R1,_x, ALU_SRC_PV,_y, _R16,_w, _R1,_x),
		ALU_MULADD(_R1,_y, ALU_SRC_PV,_x, _R16,_w, _R1,_y),
		ALU_MULADD(_R1,_z, ALU_SRC_PV,_w, _R16,_w, _R1,_z)
		ALU_LAST,
	},
	{
		/* 156 */
		ALU_SETNE_INT(_R4,_w, KC0(0),_z, ALU_SRC_LITERAL,_x)
		ALU_LAST,
		ALU_LITERAL(0x00000002),
		/* 157 */
		ALU_PRED_SETE_INT(__,_x, _R4,_w, ALU_SRC_0,_x) UPDATE_EXEC_MASK(DEACTIVATE) UPDATE_PRED
		ALU_LAST,
	},
	{
		/* 158 */
		ALU_ADD(__,_x, _R1 _NEG,_z, KC0(7),_z),
		ALU_ADD(__,_y, _R1 _NEG,_y, KC0(7),_y),
		ALU_ADD(__,_z, _R1 _NEG,_x, KC0(7),_x)
		ALU_LAST,
		/* 159 */
		ALU_MULADD(_R1,_x, ALU_SRC_PV,_z, _R16,_x, _R1,_x),
		ALU_MULADD(_R1,_y, ALU_SRC_PV,_y, _R16,_y, _R1,_y),
		ALU_MULADD(_R1,_z, ALU_SRC_PV,_x, _R16,_z, _R1,_z),
		ALU_MOV(_R1,_w, _R5,_z)
		ALU_LAST,
	},
	{
		/* 160 */
		ALU_PRED_SETNE_INT(__,_x, KC0(0),_z, ALU_SRC_LITERAL,_x) UPDATE_EXEC_MASK(DEACTIVATE) UPDATE_PRED
		ALU_LAST,
		ALU_LITERAL(0x00000003),
	},
	{
		/* 161 */
		ALU_SETNE_INT(_R6,_z, KC0(0),_z, ALU_SRC_LITERAL,_x)
		ALU_LAST,
		ALU_LITERAL(0x00000004),
		/* 162 */
		ALU_PRED_SETE_INT(__,_x, _R6,_z, ALU_SRC_0,_x) UPDATE_EXEC_MASK(DEACTIVATE) UPDATE_PRED
		ALU_LAST,
	},
	{
		/* 163 */
		ALU_MOV(_R1,_w, _R5,_z)
		ALU_LAST,
		/* 164 */
		ALU_MOV(_R1,_x, _R5,_x),
		ALU_MOV(_R1,_y, _R5,_y),
		ALU_MOV(_R1,_z, _R4,_z),
		ALU_MOV(_R1,_w, ALU_SRC_PV,_w)
		ALU_LAST,
	},
	{
		/* 165 */
		ALU_SETNE_INT(_R4,_y, KC0(0),_z, ALU_SRC_LITERAL,_x)
		ALU_LAST,
		ALU_LITERAL(0x00000005),
		/* 166 */
		ALU_PRED_SETE_INT(__,_x, _R4,_y, ALU_SRC_0,_x) UPDATE_EXEC_MASK(DEACTIVATE) UPDATE_PRED
		ALU_LAST,
	},
	{
		/* 167 */
		ALU_MOV(_R1,_w, _R5,_z)
		ALU_LAST,
		/* 168 */
		ALU_MOV(_R1,_x, _R5,_x),
		ALU_MOV(_R1,_y, _R5,_y),
		ALU_MOV(_R1,_z, _R4,_z),
		ALU_MOV(_R1,_w, ALU_SRC_PV,_w)
		ALU_LAST,
	},
	{
		/* 169 */
		ALU_SETNE_INT(__,_x, KC0(0),_z, ALU_SRC_LITERAL,_x),
		ALU_SETNE_INT(_R127,_y, KC0(0),_z, ALU_SRC_LITERAL,_y),
		ALU_MOV(_R4,_w, _R5,_z),
		ALU_MOV(_R5,_w, _R5,_z)
		ALU_LAST,
		ALU_LITERAL2(0x00000007, 0x00000006),
		/* 170 */
		ALU_CNDE_INT(_R123,_x, ALU_SRC_PV,_x, ALU_SRC_PV,_w, _R1,_w),
		ALU_CNDE_INT(_R123,_y, ALU_SRC_PV,_x, _R4,_z, _R1,_z),
		ALU_CNDE_INT(_R123,_z, ALU_SRC_PV,_x, _R5,_y, _R1,_y),
		ALU_CNDE_INT(_R123,_w, ALU_SRC_PV,_x, _R5,_x, _R1,_x)
		ALU_LAST,
		/* 171 */
		ALU_CNDE_INT(_R1,_x, _R127,_y, _R5,_x, ALU_SRC_PV,_w),
		ALU_CNDE_INT(_R1,_y, _R127,_y, _R5,_y, ALU_SRC_PV,_z),
		ALU_CNDE_INT(_R1,_z, _R127,_y, _R4,_z, ALU_SRC_PV,_y),
		ALU_CNDE_INT(_R1,_w, _R127,_y, _R5,_w, ALU_SRC_PV,_x)
		ALU_LAST,
	},
	{
		/* 172 */
		ALU_MOV(_R1,_x, _R16,_x),
		ALU_MOV(_R1,_y, _R16,_y),
		ALU_MOV(_R1,_z, _R16,_z),
		ALU_MOV(_R1,_w, _R16,_w)
		ALU_LAST,
	},
	{
		/* 173 */
		ALU_SETNE_INT(_R5,_z, KC0(0),_z, ALU_SRC_0,_x)
		ALU_LAST,
		/* 174 */
		ALU_PRED_SETE_INT(__,_x, _R5,_z, ALU_SRC_0,_x) UPDATE_EXEC_MASK(DEACTIVATE) UPDATE_PRED
		ALU_LAST,
	},
	{
		/* 175 */
		ALU_MUL(_R1,_x, _R1,_x, _R16,_x),
		ALU_MUL(_R1,_y, _R1,_y, _R16,_y),
		ALU_MUL(_R1,_z, _R1,_z, _R16,_z)
		ALU_LAST,
	},
	{
		/* 176 */
		ALU_SETNE_INT(_R4,_y, KC0(0),_z, ALU_SRC_1_INT,_x)
		ALU_LAST,
		/* 177 */
		ALU_PRED_SETE_INT(__,_x, _R4,_y, ALU_SRC_0,_x) UPDATE_EXEC_MASK(DEACTIVATE) UPDATE_PRED
		ALU_LAST,
	},
	{
		/* 178 */
		ALU_MOV(_R1,_x, _R16,_x),
		ALU_MOV(_R1,_y, _R16,_y),
		ALU_MOV(_R1,_z, _R16,_z)
		ALU_LAST,
	},
	{
		/* 179 */
		ALU_SETNE_INT(_R4,_x, KC0(0),_z, ALU_SRC_LITERAL,_x)
		ALU_LAST,
		ALU_LITERAL(0x00000002),
		/* 180 */
		ALU_PRED_SETE_INT(__,_x, _R4,_x, ALU_SRC_0,_x) UPDATE_EXEC_MASK(DEACTIVATE) UPDATE_PRED
		ALU_LAST,
	},
	{
		/* 181 */
		ALU_ADD(__,_x, _R1 _NEG,_z, KC0(7),_z),
		ALU_ADD(__,_y, _R1 _NEG,_y, KC0(7),_y),
		ALU_ADD(__,_z, _R1 _NEG,_x, KC0(7),_x)
		ALU_LAST,
		/* 182 */
		ALU_MULADD(_R1,_x, ALU_SRC_PV,_z, _R16,_x, _R1,_x),
		ALU_MULADD(_R1,_y, ALU_SRC_PV,_y, _R16,_y, _R1,_y),
		ALU_MULADD(_R1,_z, ALU_SRC_PV,_x, _R16,_z, _R1,_z),
		ALU_MOV(_R1,_w, _R1,_w)
		ALU_LAST,
	},
	{
		/* 183 */
		ALU_SETNE_INT(_R4,_w, KC0(0),_z, ALU_SRC_LITERAL,_x)
		ALU_LAST,
		ALU_LITERAL(0x00000003),
		/* 184 */
		ALU_PRED_SETE_INT(__,_x, _R4,_w, ALU_SRC_0,_x) UPDATE_EXEC_MASK(DEACTIVATE) UPDATE_PRED
		ALU_LAST,
	},
	{
		/* 185 */
		ALU_MOV(_R1,_w, _R1,_w)
		ALU_LAST,
		/* 186 */
		ALU_MOV(_R1,_x, _R16,_x),
		ALU_MOV(_R1,_y, _R16,_y),
		ALU_MOV(_R1,_z, _R16,_z),
		ALU_MOV(_R1,_w, ALU_SRC_PV,_w)
		ALU_LAST,
	},
	{
		/* 187 */
		ALU_SETNE_INT(_R5,_z, KC0(0),_z, ALU_SRC_LITERAL,_x)
		ALU_LAST,
		ALU_LITERAL(0x00000004),
		/* 188 */
		ALU_PRED_SETE_INT(__,_x, _R5,_z, ALU_SRC_0,_x) UPDATE_EXEC_MASK(DEACTIVATE) UPDATE_PRED
		ALU_LAST,
	},
	{
		/* 189 */
		ALU_MOV(_R1,_w, _R1,_w)
		ALU_LAST,
		/* 190 */
		ALU_MOV(_R1,_x, _R5,_x),
		ALU_MOV(_R1,_y, _R5,_y),
		ALU_MOV(_R1,_z, _R4,_z),
		ALU_MOV(_R1,_w, ALU_SRC_PV,_w)
		ALU_LAST,
	},
	{
		/* 191 */
		ALU_SETNE_INT(_R4,_y, KC0(0),_z, ALU_SRC_LITERAL,_x)
		ALU_LAST,
		ALU_LITERAL(0x00000005),
		/* 192 */
		ALU_PRED_SETE_INT(__,_x, _R4,_y, ALU_SRC_0,_x) UPDATE_EXEC_MASK(DEACTIVATE) UPDATE_PRED
		ALU_LAST,
	},
	{
		/* 193 */
		ALU_MOV(_R1,_w, _R1,_w)
		ALU_LAST,
		/* 194 */
		ALU_MOV(_R1,_x, _R5,_x),
		ALU_MOV(_R1,_y, _R5,_y),
		ALU_MOV(_R1,_z, _R4,_z),
		ALU_MOV(_R1,_w, ALU_SRC_PV,_w)
		ALU_LAST,
	},
	{
		/* 195 */
		ALU_SETNE_INT(__,_x, KC0(0),_z, ALU_SRC_LITERAL,_x),
		ALU_SETNE_INT(_R127,_y, KC0(0),_z, ALU_SRC_LITERAL,_y),
		ALU_MOV(_R4,_w, _R1,_w),
		ALU_MOV(_R5,_w, _R1,_w)
		ALU_LAST,
		ALU_LITERAL2(0x00000007, 0x00000006),
		/* 196 */
		ALU_CNDE_INT(_R123,_x, ALU_SRC_PV,_x, ALU_SRC_PV,_w, _R1,_w),
		ALU_CNDE_INT(_R123,_y, ALU_SRC_PV,_x, _R4,_z, _R1,_z),
		ALU_CNDE_INT(_R123,_z, ALU_SRC_PV,_x, _R5,_y, _R1,_y),
		ALU_CNDE_INT(_R123,_w, ALU_SRC_PV,_x, _R5,_x, _R1,_x)
		ALU_LAST,
		/* 197 */
		ALU_CNDE_INT(_R1,_x, _R127,_y, _R5,_x, ALU_SRC_PV,_w),
		ALU_CNDE_INT(_R1,_y, _R127,_y, _R5,_y, ALU_SRC_PV,_z),
		ALU_CNDE_INT(_R1,_z, _R127,_y, _R4,_z, ALU_SRC_PV,_y),
		ALU_CNDE_INT(_R1,_w, _R127,_y, _R5,_w, ALU_SRC_PV,_x)
		ALU_LAST,
	},
	{
		/* 198 */
		ALU_ADD(_R2,_x, _R2,_x, _R1,_x),
		ALU_ADD(_R2,_y, _R2,_y, _R1,_y),
		ALU_ADD(_R2,_z, _R2,_z, _R1,_z),
		ALU_ADD(_R2,_w, _R2,_w, _R1,_w)
		ALU_LAST,
		/* 199 */
		ALU_ADD(__,_x, ALU_SRC_PV,_x, ALU_SRC_PV,_y)
		ALU_LAST,
		/* 200 */
		ALU_ADD(_R4,_y, _R2,_z, ALU_SRC_PV,_x)
		ALU_LAST,
		/* 201 */
		ALU_MULADD(_R4,_x, _R2,_x, ALU_SRC_LITERAL,_x, ALU_SRC_0_5,_x),
		ALU_MULADD(_R5,_y, _R2,_y, ALU_SRC_LITERAL,_x, ALU_SRC_0_5,_x),
		ALU_MULADD(_R4,_z, _R2,_z, ALU_SRC_LITERAL,_x, ALU_SRC_0_5,_x)
		ALU_LAST,
		ALU_LITERAL(0x437F0000),
		/* 202 */
		ALU_PRED_SETNE_INT(__,_x, KC0(2),_z, ALU_SRC_0,_x) UPDATE_EXEC_MASK(DEACTIVATE) UPDATE_PRED
		ALU_LAST,
	},
	{
		/* 203 */
		ALU_PRED_SETNE_INT(__,_x, KC0(3),_x, ALU_SRC_0,_x) UPDATE_EXEC_MASK(DEACTIVATE) UPDATE_PRED
		ALU_LAST,
	},
	{
		/* 204 */
		ALU_SETE_INT(__,_x, KC0(2),_w, ALU_SRC_LITERAL,_x),
		ALU_SETE_INT(__,_y, KC0(2),_w, ALU_SRC_LITERAL,_y)
		ALU_LAST,
		ALU_LITERAL2(0x00000006, 0x00000003),
		/* 205 */
		ALU_CNDE_INT(_R1,_z, ALU_SRC_PV,_y, ALU_SRC_PV,_x, ALU_SRC_M_1_INT,_x)
		ALU_LAST,
		/* 206 */
		ALU_PRED_SETNE_INT(__,_x, _R1,_z, ALU_SRC_0,_x) UPDATE_EXEC_MASK(DEACTIVATE) UPDATE_PRED
		ALU_LAST,
	},
	{
		/* 207 */
		ALU_KILLGT(__,_x, ALU_SRC_LITERAL,_x, _R2,_w)
		ALU_LAST,
		ALU_LITERAL(0x3B03126F),
	},
	{
		/* 208 */
		ALU_PRED_SETGT(__,_x, ALU_SRC_LITERAL,_x, _R2,_w) UPDATE_EXEC_MASK(DEACTIVATE) UPDATE_PRED
		ALU_LAST,
		ALU_LITERAL(0x3B03126F),
	},
	{
		/* 209 */
		ALU_KILLE_INT(__,_x, KC0(2),_w, ALU_SRC_0,_x),
		ALU_SETE_INT(_R1,_y, KC0(2),_w, ALU_SRC_0,_x)
		ALU_LAST,
	},
	{
		/* 210 */
		ALU_PRED_SETNE_INT(__,_x, _R1,_y, ALU_SRC_0,_x) UPDATE_EXEC_MASK(DEACTIVATE) UPDATE_PRED
		ALU_LAST,
	},
	{
		/* 211 */
		ALU_KILLGT(__,_x, _R2,_w, ALU_SRC_LITERAL,_x),
		ALU_SETGT_DX10(_R1,_x, _R2,_w, ALU_SRC_LITERAL,_x)
		ALU_LAST,
		ALU_LITERAL(0x3B03126F),
	},
	{
		/* 212 */
		ALU_PRED_SETNE_INT(__,_x, _R1,_x, ALU_SRC_0,_x) UPDATE_EXEC_MASK(DEACTIVATE) UPDATE_PRED
		ALU_LAST,
	},
	{
		/* 213 */
		ALU_SETNE_INT(_R1,_w, KC0(2),_w, ALU_SRC_LITERAL,_x)
		ALU_LAST,
		ALU_LITERAL(0x00000002),
		/* 214 */
		ALU_PRED_SETE_INT(__,_x, _R1,_w, ALU_SRC_0,_x) UPDATE_EXEC_MASK(DEACTIVATE) UPDATE_PRED
		ALU_LAST,
	},
	{
		/* 215 */
		ALU_MULADD(_R123,_z, _R2,_w, ALU_SRC_LITERAL,_x, ALU_SRC_0_5,_x)
		ALU_LAST,
		ALU_LITERAL(0x437F0000),
		/* 216 */
		ALU_FLOOR(__,_y, ALU_SRC_PV,_z)
		ALU_LAST,
		/* 217 */
		ALU_FLT_TO_INT(__,_x, ALU_SRC_PV,_y) SCL_210
		ALU_LAST,
		/* 218 */
		ALU_AND_INT(_R1,_w, ALU_SRC_PS,_x, KC0(9),_w)
		ALU_LAST,
		/* 219 */
		ALU_KILLNE_INT(__,_x, KC0(8),_w, ALU_SRC_PV,_w)
		ALU_LAST,
	},
	{
		/* 220 */
		ALU_PRED_SETNE_INT(__,_x, KC0(8),_w, _R1,_w) UPDATE_EXEC_MASK(DEACTIVATE) UPDATE_PRED
		ALU_LAST,
	},
	{
		/* 221 */
		ALU_SETNE_INT(_R1,_z, KC0(2),_w, ALU_SRC_LITERAL,_x)
		ALU_LAST,
		ALU_LITERAL(0x00000003),
		/* 222 */
		ALU_PRED_SETE_INT(__,_x, _R1,_z, ALU_SRC_0,_x) UPDATE_EXEC_MASK(DEACTIVATE) UPDATE_PRED
		ALU_LAST,
	},
	{
		/* 223 */
		ALU_MULADD(_R123,_w, _R2,_w, ALU_SRC_LITERAL,_x, ALU_SRC_0_5,_x)
		ALU_LAST,
		ALU_LITERAL(0x437F0000),
		/* 224 */
		ALU_FLOOR(__,_y, ALU_SRC_PV,_w)
		ALU_LAST,
		/* 225 */
		ALU_FLT_TO_INT(__,_x, ALU_SRC_PV,_y) SCL_210
		ALU_LAST,
		/* 226 */
		ALU_AND_INT(_R1,_z, ALU_SRC_PS,_x, KC0(9),_w)
		ALU_LAST,
		/* 227 */
		ALU_KILLE_INT(__,_x, ALU_SRC_PV,_z, KC0(8),_w)
		ALU_LAST,
	},
	{
		/* 228 */
		ALU_PRED_SETE_INT(__,_x, _R1,_z, KC0(8),_w) UPDATE_EXEC_MASK(DEACTIVATE) UPDATE_PRED
		ALU_LAST,
	},
	{
		/* 229 */
		ALU_SETNE_INT(_R1,_y, KC0(2),_w, ALU_SRC_LITERAL,_x)
		ALU_LAST,
		ALU_LITERAL(0x00000004),
		/* 230 */
		ALU_PRED_SETE_INT(__,_x, _R1,_y, ALU_SRC_0,_x) UPDATE_EXEC_MASK(DEACTIVATE) UPDATE_PRED
		ALU_LAST,
	},
	{
		/* 231 */
		ALU_MULADD(_R123,_x, _R2,_w, ALU_SRC_LITERAL,_x, ALU_SRC_0_5,_x)
		ALU_LAST,
		ALU_LITERAL(0x437F0000),
		/* 232 */
		ALU_FLOOR(__,_w, ALU_SRC_PV,_x)
		ALU_LAST,
		/* 233 */
		ALU_FLT_TO_INT(__,_x, ALU_SRC_PV,_w) SCL_210
		ALU_LAST,
		/* 234 */
		ALU_AND_INT(__,_y, ALU_SRC_PS,_x, KC0(9),_w)
		ALU_LAST,
		/* 235 */
		ALU_KILLGE_INT(__,_x, ALU_SRC_PV,_y, KC0(8),_w),
		ALU_SETGE_INT(_R1,_x, ALU_SRC_PV,_y, KC0(8),_w)
		ALU_LAST,
	},
	{
		/* 236 */
		ALU_PRED_SETNE_INT(__,_x, _R1,_x, ALU_SRC_0,_x) UPDATE_EXEC_MASK(DEACTIVATE) UPDATE_PRED
		ALU_LAST,
	},
	{
		/* 237 */
		ALU_SETNE_INT(_R1,_w, KC0(2),_w, ALU_SRC_LITERAL,_x)
		ALU_LAST,
		ALU_LITERAL(0x00000005),
		/* 238 */
		ALU_PRED_SETE_INT(__,_x, _R1,_w, ALU_SRC_0,_x) UPDATE_EXEC_MASK(DEACTIVATE) UPDATE_PRED
		ALU_LAST,
	},
	{
		/* 239 */
		ALU_MULADD(_R123,_z, _R2,_w, ALU_SRC_LITERAL,_x, ALU_SRC_0_5,_x)
		ALU_LAST,
		ALU_LITERAL(0x437F0000),
		/* 240 */
		ALU_FLOOR(__,_y, ALU_SRC_PV,_z)
		ALU_LAST,
		/* 241 */
		ALU_FLT_TO_INT(__,_x, ALU_SRC_PV,_y) SCL_210
		ALU_LAST,
		/* 242 */
		ALU_AND_INT(_R1,_w, ALU_SRC_PS,_x, KC0(9),_w)
		ALU_LAST,
		/* 243 */
		ALU_KILLGT_INT(__,_x, ALU_SRC_PV,_w, KC0(8),_w)
		ALU_LAST,
	},
	{
		/* 244 */
		ALU_PRED_SETGT_INT(__,_x, _R1,_w, KC0(8),_w) UPDATE_EXEC_MASK(DEACTIVATE) UPDATE_PRED
		ALU_LAST,
	},
	{
		/* 245 */
		ALU_SETNE_INT(_R1,_z, KC0(2),_w, ALU_SRC_LITERAL,_x)
		ALU_LAST,
		ALU_LITERAL(0x00000006),
		/* 246 */
		ALU_PRED_SETE_INT(__,_x, _R1,_z, ALU_SRC_0,_x) UPDATE_EXEC_MASK(DEACTIVATE) UPDATE_PRED
		ALU_LAST,
	},
	{
		/* 247 */
		ALU_MULADD(_R123,_y, _R2,_w, ALU_SRC_LITERAL,_x, ALU_SRC_0_5,_x)
		ALU_LAST,
		ALU_LITERAL(0x437F0000),
		/* 248 */
		ALU_FLOOR(__,_w, ALU_SRC_PV,_y)
		ALU_LAST,
		/* 249 */
		ALU_FLT_TO_INT(__,_x, ALU_SRC_PV,_w) SCL_210
		ALU_LAST,
		/* 250 */
		ALU_AND_INT(__,_x, ALU_SRC_PS,_x, KC0(9),_w)
		ALU_LAST,
		/* 251 */
		ALU_KILLGE_INT(__,_x, KC0(8),_w, ALU_SRC_PV,_x),
		ALU_SETGE_INT(_R1,_y, KC0(8),_w, ALU_SRC_PV,_x)
		ALU_LAST,
	},
	{
		/* 252 */
		ALU_PRED_SETNE_INT(__,_x, _R1,_y, ALU_SRC_0,_x) UPDATE_EXEC_MASK(DEACTIVATE) UPDATE_PRED
		ALU_LAST,
	},
	{
		/* 253 */
		ALU_SETNE_INT(_R1,_x, KC0(2),_w, ALU_SRC_LITERAL,_x)
		ALU_LAST,
		ALU_LITERAL(0x00000007),
		/* 254 */
		ALU_PRED_SETE_INT(__,_x, _R1,_x, ALU_SRC_0,_x) UPDATE_EXEC_MASK(DEACTIVATE) UPDATE_PRED
		ALU_LAST,
	},
	{
		/* 255 */
		ALU_MULADD(_R123,_w, _R2,_w, ALU_SRC_LITERAL,_x, ALU_SRC_0_5,_x)
		ALU_LAST,
		ALU_LITERAL(0x437F0000),
		/* 256 */
		ALU_FLOOR(__,_z, ALU_SRC_PV,_w)
		ALU_LAST,
		/* 257 */
		ALU_FLT_TO_INT(__,_x, ALU_SRC_PV,_z) SCL_210
		ALU_LAST,
		/* 258 */
		ALU_AND_INT(_R1,_x, ALU_SRC_PS,_x, KC0(9),_w)
		ALU_LAST,
		/* 259 */
		ALU_KILLGT_INT(__,_x, KC0(8),_w, ALU_SRC_PV,_x)
		ALU_LAST,
	},
	{
		/* 260 */
		ALU_PRED_SETGT_INT(__,_x, KC0(8),_w, _R1,_x) UPDATE_EXEC_MASK(DEACTIVATE) UPDATE_PRED
		ALU_LAST,
	},
	{
		/* 261 */
		ALU_KILLE_INT(__,_x, _R3,_w, ALU_SRC_0,_x)
		ALU_LAST,
	},
	{
		/* 262 */
		ALU_PRED_SETE_INT(__,_x, _R3,_w, ALU_SRC_0,_x) UPDATE_EXEC_MASK(DEACTIVATE) UPDATE_PRED
		ALU_LAST,
	},
	{
		/* 263 */
		ALU_KILLE_INT(__,_x, _R3,_y, ALU_SRC_0,_x),
		ALU_KILLNE_INT(__,_y, KC0(2),_w, ALU_SRC_1_INT,_x)
		ALU_LAST,
	},
	{
		/* 264 */
		ALU_PRED_SETE_INT(__,_x, _R3,_y, ALU_SRC_0,_x) UPDATE_EXEC_MASK(DEACTIVATE) UPDATE_PRED
		ALU_LAST,
	},
	{
		/* 265 */
		ALU_PRED_SETNE_INT(__,_x, KC0(3),_y, ALU_SRC_0,_x) UPDATE_EXEC_MASK(DEACTIVATE) UPDATE_PRED
		ALU_LAST,
	},
	{
		/* 266 */
		ALU_PRED_SETNE_INT(__,_x, KC0(3),_w, ALU_SRC_0,_x) UPDATE_EXEC_MASK(DEACTIVATE) UPDATE_PRED
		ALU_LAST,
	},
	{
		/* 267 */
		ALU_PRED_SETE_INT(__,_x, KC0(3),_z, ALU_SRC_LITERAL,_x) UPDATE_EXEC_MASK(DEACTIVATE) UPDATE_PRED
		ALU_LAST,
		ALU_LITERAL(0x00000003),
	},
	{
		/* 268 */
		ALU_KILLGT(__,_x, ALU_SRC_LITERAL,_x, _R4,_y),
		ALU_SETGT_DX10(_R0,_w, ALU_SRC_LITERAL,_x, _R4,_y)
		ALU_LAST,
		ALU_LITERAL(0x3B03126F),
	},
	{
		/* 269 */
		ALU_PRED_SETNE_INT(__,_x, _R0,_w, ALU_SRC_0,_x) UPDATE_EXEC_MASK(DEACTIVATE) UPDATE_PRED
		ALU_LAST,
	},
	{
		/* 270 */
		ALU_KILLE_INT(__,_x, _R0,_w, ALU_SRC_0,_x)
		ALU_LAST,
	},
	{
		/* 271 */
		ALU_PRED_SETNE_INT(__,_x, _R0,_w, ALU_SRC_0,_x) UPDATE_EXEC_MASK(DEACTIVATE) UPDATE_PRED
		ALU_LAST,
	},
	{
		/* 272 */
		ALU_KILLGT(__,_x, _R4,_y, ALU_SRC_LITERAL,_x)
		ALU_LAST,
		ALU_LITERAL(0x3B03126F),
	},
	{
		/* 273 */
		ALU_PRED_SETGT(__,_x, _R4,_y, ALU_SRC_LITERAL,_x) UPDATE_EXEC_MASK(DEACTIVATE) UPDATE_PRED
		ALU_LAST,
		ALU_LITERAL(0x3B03126F),
	},
	{
		/* 274 */
		ALU_PRED_SETE_INT(__,_x, KC0(3),_z, ALU_SRC_LITERAL,_x) UPDATE_EXEC_MASK(DEACTIVATE) UPDATE_PRED
		ALU_LAST,
		ALU_LITERAL(0x00000002),
	},
	{
		/* 275 */
		ALU_FLOOR(_R127,_x, _R4,_z),
		ALU_FLOOR(_R127,_y, _R5,_y),
		ALU_FLOOR(__,_z, _R4,_x),
		ALU_AND_INT(_R127,_w, KC0(8),_x, KC0(9),_x),
		ALU_AND_INT(_R126,_x, KC0(8),_y, KC0(9),_y)
		ALU_LAST,
		/* 276 */
		ALU_AND_INT(_R126,_w, KC0(8),_z, KC0(9),_z),
		ALU_FLT_TO_INT(__,_w, ALU_SRC_PV,_z) SCL_210
		ALU_LAST,
		/* 277 */
		ALU_AND_INT(__,_x, KC0(9),_x, ALU_SRC_PS,_x),
		ALU_FLT_TO_INT(__,_x, _R127,_y) SCL_210
		ALU_LAST,
		/* 278 */
		ALU_SETE_INT(_R127,_y, _R127,_w, ALU_SRC_PV,_x),
		ALU_AND_INT(__,_w, KC0(9),_y, ALU_SRC_PS,_x),
		ALU_FLT_TO_INT(__,_y, _R127,_x) SCL_210
		ALU_LAST,
		/* 279 */
		ALU_SETE_INT(__,_x, _R126,_x, ALU_SRC_PV,_w),
		ALU_AND_INT(__,_z, KC0(9),_z, ALU_SRC_PS,_x)
		ALU_LAST,
		/* 280 */
		ALU_SETE_INT(__,_w, _R126,_w, ALU_SRC_PV,_z),
		ALU_MULLO_INT(__,_w, _R127,_y, ALU_SRC_PV,_x) SCL_210
		ALU_LAST,
		/* 281 */
		ALU_MULLO_INT(__,_x, ALU_SRC_PS,_x, ALU_SRC_PV,_w) SCL_210
		ALU_LAST,
		/* 282 */
		ALU_NOT_INT(_R1,_x, ALU_SRC_PS,_x)
		ALU_LAST,
		/* 283 */
		ALU_KILLNE_INT(__,_x, ALU_SRC_PV,_x, ALU_SRC_0,_x)
		ALU_LAST,
	},
	{
		/* 284 */
		ALU_PRED_SETNE_INT(__,_x, _R1,_x, ALU_SRC_0,_x) UPDATE_EXEC_MASK(DEACTIVATE) UPDATE_PRED
		ALU_LAST,
	},
	{
		/* 285 */
		ALU_KILLE_INT(__,_x, _R3,_z, ALU_SRC_0,_x)
		ALU_LAST,
	},
	{
		/* 286 */
		ALU_PRED_SETNE_INT(__,_x, _R3,_z, ALU_SRC_0,_x) UPDATE_EXEC_MASK(DEACTIVATE) UPDATE_PRED
		ALU_LAST,
	},
	{
		/* 287 */
		ALU_FLOOR(_R127,_x, _R5,_y),
		ALU_FLOOR(_R127,_y, _R4,_z),
		ALU_FLOOR(__,_z, _R4,_x),
		ALU_AND_INT(_R127,_w, KC0(8),_x, KC0(9),_x),
		ALU_AND_INT(_R126,_y, KC0(8),_y, KC0(9),_y)
		ALU_LAST,
		/* 288 */
		ALU_AND_INT(_R126,_w, KC0(8),_z, KC0(9),_z),
		ALU_FLT_TO_INT(__,_w, ALU_SRC_PV,_z) SCL_210
		ALU_LAST,
		/* 289 */
		ALU_AND_INT(__,_y, KC0(9),_x, ALU_SRC_PS,_x),
		ALU_FLT_TO_INT(__,_y, _R127,_x) SCL_210
		ALU_LAST,
		/* 290 */
		ALU_SETNE_INT(_R127,_x, ALU_SRC_PV,_y, _R127,_w),
		ALU_AND_INT(__,_w, KC0(9),_y, ALU_SRC_PS,_x),
		ALU_FLT_TO_INT(__,_x, _R127,_y) SCL_210
		ALU_LAST,
		/* 291 */
		ALU_SETNE_INT(__,_y, ALU_SRC_PV,_w, _R126,_y),
		ALU_AND_INT(__,_z, KC0(9),_z, ALU_SRC_PS,_x)
		ALU_LAST,
		/* 292 */
		ALU_ADD_INT(__,_z, _R127,_x, ALU_SRC_PV,_y),
		ALU_SETNE_INT(__,_w, ALU_SRC_PV,_z, _R126,_w)
		ALU_LAST,
		/* 293 */
		ALU_ADD_INT(__,_x, ALU_SRC_PV,_w, ALU_SRC_PV,_z)
		ALU_LAST,
		/* 294 */
		ALU_NOT_INT(_R1,_y, ALU_SRC_PV,_x)
		ALU_LAST,
		/* 295 */
		ALU_KILLNE_INT(__,_x, ALU_SRC_PV,_y, ALU_SRC_0,_x)
		ALU_LAST,
	},
	{
		/* 296 */
		ALU_PRED_SETNE_INT(__,_x, _R1,_y, ALU_SRC_0,_x) UPDATE_EXEC_MASK(DEACTIVATE) UPDATE_PRED
		ALU_LAST,
	},
	{
		/* 297 */
		ALU_SETE_INT(__,_x, KC0(5),_z, ALU_SRC_LITERAL,_x)
		ALU_LAST,
		ALU_LITERAL(0x00000005),
		/* 298 */
		ALU_CNDE_INT(_R0,_w, KC0(4),_z, ALU_SRC_0,_x, ALU_SRC_PV,_x)
		ALU_LAST,
		/* 299 */
		ALU_PRED_SETNE_INT(__,_x, _R0,_w, ALU_SRC_0,_x) UPDATE_EXEC_MASK(DEACTIVATE) UPDATE_PRED
		ALU_LAST,
	},
	{
		/* 300 */
		ALU_MOV_x4(_R2,_x, _R2,_x),
		ALU_MOV_x4(_R2,_y, _R2,_y),
		ALU_MOV_x4(_R2,_z, _R2,_z)
		ALU_LAST,
	},
	{
		/* 301 */
		ALU_SETE_INT(__,_x, KC0(5),_z, ALU_SRC_LITERAL,_x),
		ALU_MOV_x2(_R1,_y, _R2,_y),
		ALU_MOV_x2(_R1,_z, _R2,_z),
		ALU_MOV_x2(_R1,_x, _R2,_x)
		ALU_LAST,
		ALU_LITERAL(0x00000005),
		/* 302 */
		ALU_CNDE_INT(_R123,_w, KC0(4),_z, ALU_SRC_PV,_x, ALU_SRC_M_1_INT,_x)
		ALU_LAST,
		/* 303 */
		ALU_CNDE_INT(_R2,_x, ALU_SRC_PV,_w, _R2,_x, _R1,_x),
		ALU_CNDE_INT(_R2,_y, ALU_SRC_PV,_w, _R2,_y, _R1,_y),
		ALU_CNDE_INT(_R2,_z, ALU_SRC_PV,_w, _R2,_z, _R1,_z),
		ALU_CNDE_INT(_R2,_w, ALU_SRC_PV,_w, _R2,_w, _R2,_w)
		ALU_LAST,
	},
	{
		/* 304 */
		ALU_PRED_SETNE_INT(__,_x, KC0(4),_x, ALU_SRC_0,_x) UPDATE_EXEC_MASK(DEACTIVATE) UPDATE_PRED
		ALU_LAST,
	},
	{
		/* 305 */
		ALU_MOV(_R126,_x, _R2,_w),
		ALU_MOV(_R127,_y, KC0(6),_z),
		ALU_MOV(_R127,_z, KC0(6),_y),
		ALU_MOV(_R127,_w, KC0(6),_x),
		ALU_MOV(_R127,_x, _R3,_x) CLAMP
		ALU_LAST,
		/* 306 */
		ALU_ADD(__,_x, _R2,_w, ALU_SRC_PV _NEG,_x),
		ALU_ADD(__,_y, _R2,_z, ALU_SRC_PV _NEG,_y),
		ALU_ADD(__,_z, _R2,_y, ALU_SRC_PV _NEG,_z),
		ALU_ADD(__,_w, _R2,_x, ALU_SRC_PV _NEG,_w)
		ALU_LAST,
		/* 307 */
		ALU_MULADD(_R2,_x, ALU_SRC_PV,_w, _R127,_x, _R127,_w),
		ALU_MULADD(_R2,_y, ALU_SRC_PV,_z, _R127,_x, _R127,_z),
		ALU_MULADD(_R2,_z, ALU_SRC_PV,_y, _R127,_x, _R127,_y),
		ALU_MULADD(_R2,_w, ALU_SRC_PV,_x, _R127,_x, _R126,_x)
		ALU_LAST,
	},
	{
		/* 308 */
		ALU_SETE_INT(__,_x, KC0(5),_z, ALU_SRC_LITERAL,_x),
		ALU_SETE_INT(__,_z, KC0(5),_z, ALU_SRC_LITERAL,_y)
		ALU_LAST,
		ALU_LITERAL2(0x00000003, 0x00000002),
		/* 309 */
		ALU_CNDE_INT(_R1,_y, ALU_SRC_PV,_z, ALU_SRC_PV,_x, ALU_SRC_M_1_INT,_x)
		ALU_LAST,
		/* 310 */
		ALU_PRED_SETNE_INT(__,_x, _R1,_y, ALU_SRC_0,_x) UPDATE_EXEC_MASK(DEACTIVATE) UPDATE_PRED
		ALU_LAST,
	},
	{
		/* 311 */
		ALU_SETNE_INT(_R1,_x, KC0(6),_x, ALU_SRC_LITERAL,_x)
		ALU_LAST,
		ALU_LITERAL(0x00000002),
		/* 312 */
		ALU_PRED_SETE_INT(__,_x, _R1,_x, ALU_SRC_0,_x) UPDATE_EXEC_MASK(DEACTIVATE) UPDATE_PRED
		ALU_LAST,
	},
	{
		/* 313 */
		ALU_MOV(_R1,_x, _R2,_w),
		ALU_MOV(_R1,_y, _R2,_w),
		ALU_MOV(_R1,_z, _R2,_w)
		ALU_LAST,
	},
	{
		/* 314 */
		ALU_SETNE_INT(_R0,_w, KC0(6),_x, ALU_SRC_LITERAL,_x)
		ALU_LAST,
		ALU_LITERAL(0x00000003),
		/* 315 */
		ALU_PRED_SETE_INT(__,_x, _R0,_w, ALU_SRC_0,_x) UPDATE_EXEC_MASK(DEACTIVATE) UPDATE_PRED
		ALU_LAST,
	},
	{
		/* 316 */
		ALU_ADD(__,_w, _R2 _NEG,_w, ALU_SRC_1,_x)
		ALU_LAST,
		/* 317 */
		ALU_MOV(_R1,_x, ALU_SRC_PV,_w),
		ALU_MOV(_R1,_y, ALU_SRC_PV,_w),
		ALU_MOV(_R1,_z, ALU_SRC_PV,_w)
		ALU_LAST,
	},
	{
		/* 318 */
		ALU_SETNE_INT(_R1,_y, KC0(6),_x, ALU_SRC_LITERAL,_x)
		ALU_LAST,
		ALU_LITERAL(0x00000006),
		/* 319 */
		ALU_PRED_SETE_INT(__,_x, _R1,_y, ALU_SRC_0,_x) UPDATE_EXEC_MASK(DEACTIVATE) UPDATE_PRED
		ALU_LAST,
	},
	{
		/* 320 */
		ALU_MOV_x2(_R1,_x, _R2,_w)
		ALU_LAST,
		/* 321 */
		ALU_MOV(_R1,_x, ALU_SRC_PV,_x),
		ALU_MOV(_R1,_y, ALU_SRC_PV,_x),
		ALU_MOV(_R1,_z, ALU_SRC_PV,_x)
		ALU_LAST,
	},
	{
		/* 322 */
		ALU_SETNE_INT(_R127,_x, KC0(6),_x, ALU_SRC_LITERAL,_x),
		ALU_SETNE_INT(__,_z, KC0(6),_x, ALU_SRC_LITERAL,_y),
		ALU_MULADD(_R127,_w, _R2 _NEG,_w, ALU_SRC_LITERAL,_z, ALU_SRC_1,_x)
		ALU_LAST,
		ALU_LITERAL3(0x00000007, 0x0000000A, 0x40000000),
		/* 323 */
		ALU_CNDE_INT(_R123,_x, ALU_SRC_PV,_z, KC1(10),_y, ALU_SRC_LITERAL,_x),
		ALU_CNDE_INT(_R123,_y, ALU_SRC_PV,_z, KC1(10),_x, ALU_SRC_LITERAL,_x),
		ALU_CNDE_INT(_R123,_w, ALU_SRC_PV,_z, KC1(10),_z, ALU_SRC_LITERAL,_x)
		ALU_LAST,
		ALU_LITERAL(0x3F800000),
		/* 324 */
		ALU_CNDE_INT(_R1,_x, _R127,_x, _R127,_w, ALU_SRC_PV,_y),
		ALU_CNDE_INT(_R1,_y, _R127,_x, _R127,_w, ALU_SRC_PV,_x),
		ALU_CNDE_INT(_R1,_z, _R127,_x, _R127,_w, ALU_SRC_PV,_w)
		ALU_LAST,
	},
	{
		/* 325 */
		ALU_MUL(_R2,_x, _R2,_x, _R1,_x),
		ALU_MUL(_R2,_y, _R2,_y, _R1,_y),
		ALU_MUL(_R2,_z, _R2,_z, _R1,_z)
		ALU_LAST,
	},
	{
		/* 326 */
		ALU_ADD(_R3,_x, _R2 _NEG,_w, ALU_SRC_1,_x)
		ALU_LAST,
		/* 327 */
		ALU_PRED_SETE_INT(__,_x, KC0(5),_z, ALU_SRC_LITERAL,_x) UPDATE_EXEC_MASK(DEACTIVATE) UPDATE_PRED
		ALU_LAST,
		ALU_LITERAL(0x00000006),
	},
	{
		/* 328 */
		ALU_MOV(_R1,_w, ALU_SRC_0,_x),
		ALU_FLT_TO_INT(_R1,_x, _R0,_x) SCL_210
		ALU_LAST,
		/* 329 */
		ALU_FLT_TO_INT(_R1,_y, _R0,_y) SCL_210
		ALU_LAST,
	},
	{
		/* 330 */
		ALU_ADD(_R1,_w, _R4 _NEG,_w, ALU_SRC_1,_x)
		ALU_LAST,
		/* 331 */
		ALU_PRED_SETNE_INT(__,_x, KC0(6),_x, ALU_SRC_0,_x) UPDATE_EXEC_MASK(DEACTIVATE) UPDATE_PRED
		ALU_LAST,
	},
	{
		/* 332 */
		ALU_SETNE_INT(_R1,_z, KC0(6),_x, ALU_SRC_1_INT,_x)
		ALU_LAST,
		/* 333 */
		ALU_PRED_SETE_INT(__,_x, _R1,_z, ALU_SRC_0,_x) UPDATE_EXEC_MASK(DEACTIVATE) UPDATE_PRED
		ALU_LAST,
	},
	{
		/* 334 */
		ALU_ADD(_R1,_x, _R4 _NEG,_x, ALU_SRC_1,_x),
		ALU_ADD(_R1,_y, _R4 _NEG,_y, ALU_SRC_1,_x),
		ALU_ADD(_R1,_z, _R4 _NEG,_z, ALU_SRC_1,_x)
		ALU_LAST,
	},
	{
		/* 335 */
		ALU_SETNE_INT(_R0,_y, KC0(6),_x, ALU_SRC_LITERAL,_x)
		ALU_LAST,
		ALU_LITERAL(0x00000002),
		/* 336 */
		ALU_PRED_SETE_INT(__,_x, _R0,_y, ALU_SRC_0,_x) UPDATE_EXEC_MASK(DEACTIVATE) UPDATE_PRED
		ALU_LAST,
	},
	{
		/* 337 */
		ALU_MOV(_R1,_x, _R2,_w),
		ALU_MOV(_R1,_y, _R2,_w),
		ALU_MOV(_R1,_z, _R2,_w)
		ALU_LAST,
	},
	{
		/* 338 */
		ALU_SETNE_INT(_R0,_x, KC0(6),_x, ALU_SRC_LITERAL,_x)
		ALU_LAST,
		ALU_LITERAL(0x00000003),
		/* 339 */
		ALU_PRED_SETE_INT(__,_x, _R0,_x, ALU_SRC_0,_x) UPDATE_EXEC_MASK(DEACTIVATE) UPDATE_PRED
		ALU_LAST,
	},
	{
		/* 340 */
		ALU_MOV(_R1,_x, _R3,_x),
		ALU_MOV(_R1,_y, _R3,_x),
		ALU_MOV(_R1,_z, _R3,_x)
		ALU_LAST,
	},
	{
		/* 341 */
		ALU_SETNE_INT(_R0,_w, KC0(6),_x, ALU_SRC_LITERAL,_x)
		ALU_LAST,
		ALU_LITERAL(0x00000004),
		/* 342 */
		ALU_PRED_SETE_INT(__,_x, _R0,_w, ALU_SRC_0,_x) UPDATE_EXEC_MASK(DEACTIVATE) UPDATE_PRED
		ALU_LAST,
	},
	{
		/* 343 */
		ALU_MOV(_R1,_x, _R4,_w),
		ALU_MOV(_R1,_y, _R4,_w),
		ALU_MOV(_R1,_z, _R4,_w)
		ALU_LAST,
	},
	{
		/* 344 */
		ALU_SETNE_INT(_R1,_z, KC0(6),_x, ALU_SRC_LITERAL,_x)
		ALU_LAST,
		ALU_LITERAL(0x00000005),
		/* 345 */
		ALU_PRED_SETE_INT(__,_x, _R1,_z, ALU_SRC_0,_x) UPDATE_EXEC_MASK(DEACTIVATE) UPDATE_PRED
		ALU_LAST,
	},
	{
		/* 346 */
		ALU_MOV(_R1,_x, _R1,_w),
		ALU_MOV(_R1,_y, _R1,_w),
		ALU_MOV(_R1,_z, _R1,_w)
		ALU_LAST,
	},
	{
		/* 347 */
		ALU_SETNE_INT(_R0,_y, KC0(6),_x, ALU_SRC_LITERAL,_x)
		ALU_LAST,
		ALU_LITERAL(0x00000006),
		/* 348 */
		ALU_PRED_SETE_INT(__,_x, _R0,_y, ALU_SRC_0,_x) UPDATE_EXEC_MASK(DEACTIVATE) UPDATE_PRED
		ALU_LAST,
	},
	{
		/* 349 */
		ALU_MOV_x2(_R0,_x, _R2,_w)
		ALU_LAST,
		/* 350 */
		ALU_MOV(_R1,_x, ALU_SRC_PV,_x),
		ALU_MOV(_R1,_y, ALU_SRC_PV,_x),
		ALU_MOV(_R1,_z, ALU_SRC_PV,_x)
		ALU_LAST,
	},
	{
		/* 351 */
		ALU_SETNE_INT(_R0,_w, KC0(6),_x, ALU_SRC_LITERAL,_x)
		ALU_LAST,
		ALU_LITERAL(0x00000007),
		/* 352 */
		ALU_PRED_SETE_INT(__,_x, _R0,_w, ALU_SRC_0,_x) UPDATE_EXEC_MASK(DEACTIVATE) UPDATE_PRED
		ALU_LAST,
	},
	{
		/* 353 */
		ALU_MULADD(_R123,_w, _R2 _NEG,_w, ALU_SRC_LITERAL,_x, ALU_SRC_1,_x)
		ALU_LAST,
		ALU_LITERAL(0x40000000),
		/* 354 */
		ALU_MOV(_R1,_x, ALU_SRC_PV,_w),
		ALU_MOV(_R1,_y, ALU_SRC_PV,_w),
		ALU_MOV(_R1,_z, ALU_SRC_PV,_w)
		ALU_LAST,
	},
	{
		/* 355 */
		ALU_SETNE_INT(_R0,_y, KC0(6),_x, ALU_SRC_LITERAL,_x)
		ALU_LAST,
		ALU_LITERAL(0x00000008),
		/* 356 */
		ALU_PRED_SETE_INT(__,_x, _R0,_y, ALU_SRC_0,_x) UPDATE_EXEC_MASK(DEACTIVATE) UPDATE_PRED
		ALU_LAST,
	},
	{
		/* 357 */
		ALU_MOV_x2(_R0,_x, _R4,_w)
		ALU_LAST,
		/* 358 */
		ALU_MOV(_R1,_x, ALU_SRC_PV,_x),
		ALU_MOV(_R1,_y, ALU_SRC_PV,_x),
		ALU_MOV(_R1,_z, ALU_SRC_PV,_x)
		ALU_LAST,
	},
	{
		/* 359 */
		ALU_SETNE_INT(_R127,_x, KC0(6),_x, ALU_SRC_LITERAL,_x),
		ALU_SETNE_INT(__,_z, KC0(6),_x, ALU_SRC_LITERAL,_y),
		ALU_MULADD(_R127,_w, _R4 _NEG,_w, ALU_SRC_LITERAL,_z, ALU_SRC_1,_x)
		ALU_LAST,
		ALU_LITERAL3(0x00000009, 0x0000000A, 0x40000000),
		/* 360 */
		ALU_CNDE_INT(_R123,_x, ALU_SRC_PV,_z, KC1(10),_y, ALU_SRC_LITERAL,_x),
		ALU_CNDE_INT(_R123,_y, ALU_SRC_PV,_z, KC1(10),_x, ALU_SRC_LITERAL,_x),
		ALU_CNDE_INT(_R123,_w, ALU_SRC_PV,_z, KC1(10),_z, ALU_SRC_LITERAL,_x)
		ALU_LAST,
		ALU_LITERAL(0x3F800000),
		/* 361 */
		ALU_CNDE_INT(_R1,_x, _R127,_x, _R127,_w, ALU_SRC_PV,_y),
		ALU_CNDE_INT(_R1,_y, _R127,_x, _R127,_w, ALU_SRC_PV,_x),
		ALU_CNDE_INT(_R1,_z, _R127,_x, _R127,_w, ALU_SRC_PV,_w)
		ALU_LAST,
	},
	{
		/* 362 */
		ALU_MOV(_R1,_x, _R4,_x),
		ALU_MOV(_R1,_y, _R4,_y),
		ALU_MOV(_R1,_z, _R4,_z)
		ALU_LAST,
	},
	{
		/* 363 */
		ALU_MUL(_R1,_x, _R2,_x, _R1,_x),
		ALU_MUL(_R1,_y, _R2,_y, _R1,_y),
		ALU_MUL(_R5,_z, _R2,_z, _R1,_z)
		ALU_LAST,
		/* 364 */
		ALU_PRED_SETNE_INT(__,_x, KC0(6),_y, ALU_SRC_0,_x) UPDATE_EXEC_MASK(DEACTIVATE) UPDATE_PRED
		ALU_LAST,
	},
	{
		/* 365 */
		ALU_SETNE_INT(_R0,_x, KC0(6),_y, ALU_SRC_1_INT,_x)
		ALU_LAST,
		/* 366 */
		ALU_PRED_SETE_INT(__,_x, _R0,_x, ALU_SRC_0,_x) UPDATE_EXEC_MASK(DEACTIVATE) UPDATE_PRED
		ALU_LAST,
	},
	{
		/* 367 */
		ALU_ADD(_R3,_x, _R2 _NEG,_x, ALU_SRC_1,_x),
		ALU_ADD(_R3,_y, _R2 _NEG,_y, ALU_SRC_1,_x),
		ALU_ADD(_R3,_z, _R2 _NEG,_z, ALU_SRC_1,_x)
		ALU_LAST,
	},
	{
		/* 368 */
		ALU_SETNE_INT(_R0,_w, KC0(6),_y, ALU_SRC_LITERAL,_x)
		ALU_LAST,
		ALU_LITERAL(0x00000002),
		/* 369 */
		ALU_PRED_SETE_INT(__,_x, _R0,_w, ALU_SRC_0,_x) UPDATE_EXEC_MASK(DEACTIVATE) UPDATE_PRED
		ALU_LAST,
	},
	{
		/* 370 */
		ALU_MOV(_R3,_x, _R2,_w),
		ALU_MOV(_R3,_y, _R2,_w),
		ALU_MOV(_R3,_z, _R2,_w)
		ALU_LAST,
	},
	{
		/* 371 */
		ALU_SETNE_INT(_R1,_z, KC0(6),_y, ALU_SRC_LITERAL,_x)
		ALU_LAST,
		ALU_LITERAL(0x00000003),
		/* 372 */
		ALU_PRED_SETE_INT(__,_x, _R1,_z, ALU_SRC_0,_x) UPDATE_EXEC_MASK(DEACTIVATE) UPDATE_PRED
		ALU_LAST,
	},
	{
		/* 373 */
		ALU_MOV(_R3,_x, _R3,_x),
		ALU_MOV(_R3,_y, _R3,_x),
		ALU_MOV(_R3,_z, _R3,_x)
		ALU_LAST,
	},
	{
		/* 374 */
		ALU_SETNE_INT(_R0,_y, KC0(6),_y, ALU_SRC_LITERAL,_x)
		ALU_LAST,
		ALU_LITERAL(0x00000004),
		/* 375 */
		ALU_PRED_SETE_INT(__,_x, _R0,_y, ALU_SRC_0,_x) UPDATE_EXEC_MASK(DEACTIVATE) UPDATE_PRED
		ALU_LAST,
	},
	{
		/* 376 */
		ALU_MOV(_R3,_x, _R4,_w),
		ALU_MOV(_R3,_y, _R4,_w),
		ALU_MOV(_R3,_z, _R4,_w)
		ALU_LAST,
	},
	{
		/* 377 */
		ALU_SETNE_INT(_R0,_x, KC0(6),_y, ALU_SRC_LITERAL,_x)
		ALU_LAST,
		ALU_LITERAL(0x00000005),
		/* 378 */
		ALU_PRED_SETE_INT(__,_x, _R0,_x, ALU_SRC_0,_x) UPDATE_EXEC_MASK(DEACTIVATE) UPDATE_PRED
		ALU_LAST,
	},
	{
		/* 379 */
		ALU_MOV(_R3,_x, _R1,_w),
		ALU_MOV(_R3,_y, _R1,_w),
		ALU_MOV(_R3,_z, _R1,_w)
		ALU_LAST,
	},
	{
		/* 380 */
		ALU_SETNE_INT(_R0,_w, KC0(6),_y, ALU_SRC_LITERAL,_x)
		ALU_LAST,
		ALU_LITERAL(0x00000006),
		/* 381 */
		ALU_PRED_SETE_INT(__,_x, _R0,_w, ALU_SRC_0,_x) UPDATE_EXEC_MASK(DEACTIVATE) UPDATE_PRED
		ALU_LAST,
	},
	{
		/* 382 */
		ALU_MOV_x2(__,_w, _R2,_w)
		ALU_LAST,
		/* 383 */
		ALU_MOV(_R3,_x, ALU_SRC_PV,_w),
		ALU_MOV(_R3,_y, ALU_SRC_PV,_w),
		ALU_MOV(_R3,_z, ALU_SRC_PV,_w)
		ALU_LAST,
	},
	{
		/* 384 */
		ALU_SETNE_INT(_R0,_y, KC0(6),_y, ALU_SRC_LITERAL,_x)
		ALU_LAST,
		ALU_LITERAL(0x00000007),
		/* 385 */
		ALU_PRED_SETE_INT(__,_x, _R0,_y, ALU_SRC_0,_x) UPDATE_EXEC_MASK(DEACTIVATE) UPDATE_PRED
		ALU_LAST,
	},
	{
		/* 386 */
		ALU_MULADD(_R0,_x, _R2 _NEG,_w, ALU_SRC_LITERAL,_x, ALU_SRC_1,_x)
		ALU_LAST,
		ALU_LITERAL(0x40000000),
		/* 387 */
		ALU_MOV(_R3,_x, ALU_SRC_PV,_x),
		ALU_MOV(_R3,_y, ALU_SRC_PV,_x),
		ALU_MOV(_R3,_z, ALU_SRC_PV,_x)
		ALU_LAST,
	},
	{
		/* 388 */
		ALU_SETNE_INT(_R0,_w, KC0(6),_y, ALU_SRC_LITERAL,_x)
		ALU_LAST,
		ALU_LITERAL(0x00000008),
		/* 389 */
		ALU_PRED_SETE_INT(__,_x, _R0,_w, ALU_SRC_0,_x) UPDATE_EXEC_MASK(DEACTIVATE) UPDATE_PRED
		ALU_LAST,
	},
	{
		/* 390 */
		ALU_MOV_x2(__,_w, _R4,_w)
		ALU_LAST,
		/* 391 */
		ALU_MOV(_R3,_x, ALU_SRC_PV,_w),
		ALU_MOV(_R3,_y, ALU_SRC_PV,_w),
		ALU_MOV(_R3,_z, ALU_SRC_PV,_w)
		ALU_LAST,
	},
	{
		/* 392 */
		ALU_SETNE_INT(_R127,_x, KC0(6),_y, ALU_SRC_LITERAL,_x),
		ALU_SETNE_INT(__,_z, KC0(6),_y, ALU_SRC_LITERAL,_y),
		ALU_MULADD(_R127,_w, _R4 _NEG,_w, ALU_SRC_LITERAL,_z, ALU_SRC_1,_x)
		ALU_LAST,
		ALU_LITERAL3(0x00000009, 0x0000000A, 0x40000000),
		/* 393 */
		ALU_CNDE_INT(_R123,_x, ALU_SRC_PV,_z, KC1(11),_y, ALU_SRC_0,_x),
		ALU_CNDE_INT(_R123,_y, ALU_SRC_PV,_z, KC1(11),_x, ALU_SRC_0,_x),
		ALU_CNDE_INT(_R123,_w, ALU_SRC_PV,_z, KC1(11),_z, ALU_SRC_0,_x)
		ALU_LAST,
		/* 394 */
		ALU_CNDE_INT(_R3,_x, _R127,_x, _R127,_w, ALU_SRC_PV,_y),
		ALU_CNDE_INT(_R3,_y, _R127,_x, _R127,_w, ALU_SRC_PV,_x),
		ALU_CNDE_INT(_R3,_z, _R127,_x, _R127,_w, ALU_SRC_PV,_w)
		ALU_LAST,
	},
	{
		/* 395 */
		ALU_MOV(_R3,_x, _R2,_x),
		ALU_MOV(_R3,_y, _R2,_y),
		ALU_MOV(_R3,_z, _R2,_z)
		ALU_LAST,
	},
	{
		/* 396 */
		ALU_MUL(_R0,_x, _R4,_x, _R3,_x),
		ALU_MUL(_R0,_y, _R4,_y, _R3,_y),
		ALU_MUL(_R1,_z, _R4,_z, _R3,_z)
		ALU_LAST,
		/* 397 */
		ALU_SETNE_INT(_R3,_z, KC0(5),_w, ALU_SRC_0,_x)
		ALU_LAST,
		/* 398 */
		ALU_PRED_SETE_INT(__,_x, _R3,_z, ALU_SRC_0,_x) UPDATE_EXEC_MASK(DEACTIVATE) UPDATE_PRED
		ALU_LAST,
	},
	{
		/* 399 */
		ALU_ADD(_R2,_x, _R0,_x, _R1,_x),
		ALU_ADD(_R2,_y, _R0,_y, _R1,_y),
		ALU_ADD(_R2,_z, _R1,_z, _R5,_z)
		ALU_LAST,
	},
	{
		/* 400 */
		ALU_SETNE_INT(_R3,_y, KC0(5),_w, ALU_SRC_1_INT,_x)
		ALU_LAST,
		/* 401 */
		ALU_PRED_SETE_INT(__,_x, _R3,_y, ALU_SRC_0,_x) UPDATE_EXEC_MASK(DEACTIVATE) UPDATE_PRED
		ALU_LAST,
	},
	{
		/* 402 */
		ALU_ADD(_R2,_x, _R0 _NEG,_x, _R1,_x),
		ALU_ADD(_R2,_y, _R0 _NEG,_y, _R1,_y),
		ALU_ADD(_R2,_z, _R1 _NEG,_z, _R5,_z)
		ALU_LAST,
	},
	{
		/* 403 */
		ALU_SETNE_INT(_R3,_x, KC0(5),_w, ALU_SRC_LITERAL,_x)
		ALU_LAST,
		ALU_LITERAL(0x00000002),
		/* 404 */
		ALU_PRED_SETE_INT(__,_x, _R3,_x, ALU_SRC_0,_x) UPDATE_EXEC_MASK(DEACTIVATE) UPDATE_PRED
		ALU_LAST,
	},
	{
		/* 405 */
		ALU_ADD(_R2,_x, _R0,_x, _R1 _NEG,_x),
		ALU_ADD(_R2,_y, _R0,_y, _R1 _NEG,_y),
		ALU_ADD(_R2,_z, _R1,_z, _R5 _NEG,_z)
		ALU_LAST,
	},
	{
		/* 406 */
		ALU_SETNE_INT(_R0,_w, KC0(5),_w, ALU_SRC_LITERAL,_x)
		ALU_LAST,
		ALU_LITERAL(0x00000003),
		/* 407 */
		ALU_PRED_SETE_INT(__,_x, _R0,_w, ALU_SRC_0,_x) UPDATE_EXEC_MASK(DEACTIVATE) UPDATE_PRED
		ALU_LAST,
	},
	{
		/* 408 */
		ALU_MIN(_R2,_x, _R2,_x, _R4,_x),
		ALU_MIN(_R2,_y, _R2,_y, _R4,_y),
		ALU_MIN(_R2,_z, _R2,_z, _R4,_z)
		ALU_LAST,
	},
	{
		/* 409 */
		ALU_ADD(__,_x, _R2,_y, _R4 _NEG,_y),
		ALU_ADD(__,_y, _R2,_x, _R4 _NEG,_x),
		ALU_SETNE_INT(_R127,_z, KC0(5),_w, ALU_SRC_LITERAL,_x),
		ALU_ADD(__,_w, _R2,_z, _R4 _NEG,_z),
		ALU_SETNE_INT(_R127,_w, KC0(5),_w, ALU_SRC_LITERAL,_y)
		ALU_LAST,
		ALU_LITERAL2(0x00000005, 0x00000004),
		/* 410 */
		ALU_MAX_DX10(_R0,_x, ALU_SRC_PV,_y, ALU_SRC_PV _NEG,_y),
		ALU_MAX_DX10(_R0,_y, ALU_SRC_PV,_x, ALU_SRC_PV _NEG,_x),
		ALU_MAX_DX10(_R1,_z, ALU_SRC_PV,_w, ALU_SRC_PV _NEG,_w),
		ALU_CNDE_INT(_R126,_w, ALU_SRC_PV,_z, _R2,_w, _R2,_w),
		ALU_MAX(_R1,_x, _R2,_x, _R4,_x)
		ALU_LAST,
		/* 411 */
		ALU_CNDE_INT(_R123,_x, _R127,_z, ALU_SRC_PV,_y, _R2,_y) VEC_201,
		ALU_MAX(_R0,_y, _R2,_y, _R4,_y) VEC_021,
		ALU_MAX(_R1,_z, _R2,_z, _R4,_z),
		ALU_CNDE_INT(_R123,_w, _R127,_z, ALU_SRC_PV,_x, _R2,_x) VEC_201,
		ALU_CNDE_INT(_R122,_x, _R127,_z, ALU_SRC_PV,_z, _R2,_z)
		ALU_LAST,
		/* 412 */
		ALU_CNDE_INT(_R2,_x, _R127,_w, _R1,_x, ALU_SRC_PV,_w),
		ALU_CNDE_INT(_R2,_y, _R127,_w, ALU_SRC_PV,_y, ALU_SRC_PV,_x),
		ALU_CNDE_INT(_R2,_z, _R127,_w, ALU_SRC_PV,_z, ALU_SRC_PS,_x),
		ALU_CNDE_INT(_R2,_w, _R127,_w, _R2,_w, _R126,_w)
		ALU_LAST,
	},
	{
		/* 413 */
		ALU_SETE_INT(__,_y, KC0(5),_z, ALU_SRC_LITERAL,_x),
		ALU_SETE_INT(__,_z, KC0(5),_z, ALU_SRC_LITERAL,_y),
		ALU_MOV_x2(_R0,_w, _R2,_w)
		ALU_LAST,
		ALU_LITERAL2(0x00000003, 0x00000004),
		/* 414 */
		ALU_CNDE_INT(_R123,_x, ALU_SRC_PV,_z, ALU_SRC_PV,_y, ALU_SRC_M_1_INT,_x)
		ALU_LAST,
		/* 415 */
		ALU_CNDE_INT(_R1,_x, ALU_SRC_PV,_x, _R2,_x, _R2,_x),
		ALU_CNDE_INT(_R1,_y, ALU_SRC_PV,_x, _R2,_y, _R2,_y),
		ALU_CNDE_INT(_R1,_z, ALU_SRC_PV,_x, _R2,_z, _R2,_z),
		ALU_CNDE_INT(_R1,_w, ALU_SRC_PV,_x, _R2,_w, _R0,_w)
		ALU_LAST,
	},
	{
		/* 416 */
		ALU_MOV(_R0,_y, ALU_SRC_0,_x)
		ALU_LAST,
		/* 417 */
		ALU_PRED_SETNE_INT(__,_x, ALU_SRC_0,_x, KC0(4),_w) UPDATE_EXEC_MASK(DEACTIVATE) UPDATE_PRED
		ALU_LAST,
	},
	{
		/* 418 */
		ALU_SETNE_INT(_R0,_x, KC0(5),_x, ALU_SRC_0,_x)
		ALU_LAST,
		/* 419 */
		ALU_PRED_SETE_INT(__,_x, _R0,_x, ALU_SRC_0,_x) UPDATE_EXEC_MASK(DEACTIVATE) UPDATE_PRED
		ALU_LAST,
	},
	{
		/* 420 */
		ALU_MOV(_R0,_y, KC0(3),_z)
		ALU_LAST,
	},
	{
		/* 421 */
		ALU_PRED_SETNE_INT(__,_x, KC0(5),_x, ALU_SRC_1_INT,_x) UPDATE_EXEC_MASK(DEACTIVATE) UPDATE_PRED
		ALU_LAST,
	},
	{
		/* 422 */
		ALU_SETNE_INT(_R2,_z, KC0(5),_x, ALU_SRC_LITERAL,_x)
		ALU_LAST,
		ALU_LITERAL(0x00000002),
		/* 423 */
		ALU_PRED_SETE_INT(__,_x, _R2,_z, ALU_SRC_0,_x) UPDATE_EXEC_MASK(DEACTIVATE) UPDATE_PRED
		ALU_LAST,
	},
	{
		/* 424 */
		ALU_MOV(_R0,_y, ALU_SRC_LITERAL,_x)
		ALU_LAST,
		ALU_LITERAL(0x3F800000),
	},
	{
		/* 425 */
		ALU_SETNE_INT(_R0,_x, KC0(5),_x, ALU_SRC_LITERAL,_x)
		ALU_LAST,
		ALU_LITERAL(0x00000004),
		/* 426 */
		ALU_PRED_SETE_INT(__,_x, _R0,_x, ALU_SRC_0,_x) UPDATE_EXEC_MASK(DEACTIVATE) UPDATE_PRED
		ALU_LAST,
	},
	{
		/* 427 */
		ALU_MOV(_R0,_y, ALU_SRC_LITERAL,_x)
		ALU_LAST,
		ALU_LITERAL(0x3F800000),
	},
	{
		/* 428 */
		ALU_SETNE_INT(_R2,_z, KC0(5),_x, ALU_SRC_LITERAL,_x)
		ALU_LAST,
		ALU_LITERAL(0x00000005),
		/* 429 */
		ALU_PRED_SETE_INT(__,_x, _R2,_z, ALU_SRC_0,_x) UPDATE_EXEC_MASK(DEACTIVATE) UPDATE_PRED
		ALU_LAST,
	},
	{
		/* 430 */
		ALU_MOV(_R0,_y, ALU_SRC_LITERAL,_x)
		ALU_LAST,
		ALU_LITERAL(0x3D888889),
	},
	{
		/* 431 */
		ALU_SETNE_INT(_R127,_y, KC0(5),_x, ALU_SRC_LITERAL,_x),
		ALU_SETNE_INT(__,_w, KC0(5),_x, ALU_SRC_LITERAL,_y)
		ALU_LAST,
		ALU_LITERAL2(0x00000006, 0x00000008),
		/* 432 */
		ALU_SETNE_INT(_R127,_x, KC0(5),_x, ALU_SRC_LITERAL,_x),
		ALU_CNDE_INT(_R123,_z, ALU_SRC_PV,_w, ALU_SRC_LITERAL,_y, ALU_SRC_0,_x)
		ALU_LAST,
		ALU_LITERAL2(0x00000007, 0x3B808081),
		/* 433 */
		ALU_CNDE_INT(_R123,_x, _R127,_y, ALU_SRC_LITERAL,_x, ALU_SRC_PV,_z)
		ALU_LAST,
		ALU_LITERAL(0x3B808081),
		/* 434 */
		ALU_CNDE_INT(_R0,_y, _R127,_x, ALU_SRC_LITERAL,_x, ALU_SRC_PV,_x)
		ALU_LAST,
		ALU_LITERAL(0x3D888889),
	},
	{
		/* 435 */
		ALU_SETNE_INT(_R0,_x, KC0(4),_w, ALU_SRC_LITERAL,_x)
		ALU_LAST,
		ALU_LITERAL(0x00000002),
		/* 436 */
		ALU_PRED_SETE_INT(__,_x, _R0,_x, ALU_SRC_0,_x) UPDATE_EXEC_MASK(DEACTIVATE) UPDATE_PRED
		ALU_LAST,
	},
	{
		/* 437 */
		ALU_MOV(_R0,_x, ALU_SRC_0,_x),
		ALU_MOV(_R0,_w, _R0,_y)
		ALU_LAST,
		/* 438 */
		ALU_MOV(_R17,_x, _R1,_x),
		ALU_MOV(_R17,_y, _R1,_y),
		ALU_MOV(_R17,_z, _R1,_z),
		ALU_MOV(_R17,_w, ALU_SRC_PV,_w)
		ALU_LAST,
		/* 439 */
		ALU_MOV(_R19,_x, _R0,_x),
		ALU_MOV(_R19,_w, _R1,_w)
		ALU_LAST,
	},
	{
		/* 440 */
		ALU_SETNE_INT(_R0,_w, KC0(4),_w, ALU_SRC_1_INT,_x)
		ALU_LAST,
		/* 441 */
		ALU_PRED_SETE_INT(__,_x, _R0,_w, ALU_SRC_0,_x) UPDATE_EXEC_MASK(DEACTIVATE) UPDATE_PRED
		ALU_LAST,
	},
	{
		/* 442 */
		ALU_MOV(_R0,_w, _R0,_y)
		ALU_LAST,
		/* 443 */
		ALU_MOV(_R17,_x, _R1,_x),
		ALU_MOV(_R17,_y, _R1,_y),
		ALU_MOV(_R17,_z, _R1,_z),
		ALU_MOV(_R17,_w, ALU_SRC_PV,_w)
		ALU_LAST,
	},
	{
		/* 444 */
		ALU_KILLNE_INT(__,_x, KC0(4),_w, ALU_SRC_0,_x)
		ALU_LAST,
	},
	{
		/* 445 */
		ALU_PRED_SETNE_INT(__,_x, KC0(4),_w, ALU_SRC_0,_x) UPDATE_EXEC_MASK(DEACTIVATE) UPDATE_PRED
		ALU_LAST,
	},
	{
		/* 446 */
		ALU_MOV(_R17,_x, _R1,_x),
		ALU_MOV(_R17,_y, _R1,_y),
		ALU_MOV(_R17,_z, _R1,_z),
		ALU_MOV(_R17,_w, _R1,_w)
		ALU_LAST,
	},
	{
		/* 447 */
		ALU_SETNE_INT(_R1,_z, KC0(5),_y, ALU_SRC_1_INT,_x)
		ALU_LAST,
		/* 448 */
		ALU_PRED_SETE_INT(__,_x, _R1,_z, ALU_SRC_0,_x) UPDATE_EXEC_MASK(DEACTIVATE) UPDATE_PRED
		ALU_LAST,
	},
	{
		/* 449 */
		ALU_MOV(_R17,_x, ALU_SRC_LITERAL,_x),
		ALU_MOV(_R17,_y, ALU_SRC_LITERAL,_x),
		ALU_MOV(_R17,_z, ALU_SRC_LITERAL,_x)
		ALU_LAST,
		ALU_LITERAL(0x3F800000),
	},
	{
		/* 450 */
		ALU_SETNE_INT(_R0,_y, KC0(5),_y, ALU_SRC_LITERAL,_x)
		ALU_LAST,
		ALU_LITERAL(0x00000002),
		/* 451 */
		ALU_PRED_SETE_INT(__,_x, _R0,_y, ALU_SRC_0,_x) UPDATE_EXEC_MASK(DEACTIVATE) UPDATE_PRED
		ALU_LAST,
	},
	{
		/* 452 */
		ALU_ADD(_R17,_x, _R17 _NEG,_x, ALU_SRC_1,_x),
		ALU_ADD(_R17,_y, _R17 _NEG,_y, ALU_SRC_1,_x),
		ALU_ADD(_R17,_z, _R17 _NEG,_z, ALU_SRC_1,_x)
		ALU_LAST,
	},
	{
		/* 453 */
		ALU_KILLNE_INT(__,_x, KC0(5),_y, ALU_SRC_0,_x)
		ALU_LAST,
	},
	{
		/* 454 */
		ALU_PRED_SETNE_INT(__,_x, KC0(7),_z, ALU_SRC_0,_x) UPDATE_EXEC_MASK(DEACTIVATE) UPDATE_PRED
		ALU_LAST,
	},
	{
		/* 455 */
		ALU_NOT_INT(__,_z, KC0(7),_y)
		ALU_LAST,
		/* 456 */
		ALU_CNDE_INT(_R123,_y, ALU_SRC_PV,_z, ALU_SRC_LITERAL,_y, ALU_SRC_LITERAL,_x)
		ALU_LAST,
		ALU_LITERAL2(0x487FFF00, 0x477FFF00),
		/* 457 */
		ALU_CNDE_INT(_R123,_x, KC0(7),_x, ALU_SRC_PV,_y, ALU_SRC_LITERAL,_x)
		ALU_LAST,
		ALU_LITERAL(0x4B7FFF00),
		/* 458 */
		ALU_CNDE_INT(_R127,_w, KC0(7),_w, ALU_SRC_LITERAL,_x, ALU_SRC_PV,_x)
		ALU_LAST,
		ALU_LITERAL(0x477FFF00),
		/* 459 */
		ALU_ADD(_R127,_z, ALU_SRC_PV,_w, ALU_SRC_1 _NEG,_x),
		ALU_RECIP_IEEE(_R126,_y, ALU_SRC_PV,_w) SCL_210
		ALU_LAST,
		/* 460 */
		ALU_ADD(__,_y, ALU_SRC_PV,_z, ALU_SRC_LITERAL,_x)
		ALU_LAST,
		ALU_LITERAL(0xC0000000),
		/* 461 */
		ALU_MUL_IEEE(__,_x, ALU_SRC_PV,_y, ALU_SRC_0_5,_x)
		ALU_LAST,
		/* 462 */
		ALU_ADD(__,_w, ALU_SRC_PV,_x, ALU_SRC_1,_x)
		ALU_LAST,
		/* 463 */
		ALU_FLOOR(__,_z, ALU_SRC_PV,_w)
		ALU_LAST,
		/* 464 */
		ALU_MULADD_D2(_R127,_y, ALU_SRC_PV _NEG,_z, ALU_SRC_LITERAL,_x, _R127,_z)
		ALU_LAST,
		ALU_LITERAL(0x40000000),
		/* 465 */
		ALU_MULADD(_R123,_x, _R0,_z, _R127,_w, ALU_SRC_PV _NEG,_y)
		ALU_LAST,
		/* 466 */
		ALU_FLOOR(__,_w, ALU_SRC_PV,_x)
		ALU_LAST,
		/* 467 */
		ALU_ADD(__,_z, _R127,_y, ALU_SRC_PV,_w)
		ALU_LAST,
		/* 468 */
		ALU_MUL_IEEE(_R20,_x, ALU_SRC_PV,_z, _R126,_y)
		ALU_LAST,
	},
	{
		/* 469 */
		ALU_MOV(_R1,_y, ALU_SRC_0,_x),
		ALU_MOV(_R1,_w, ALU_SRC_1,_x)
		ALU_LAST,
		/* 470 */
		ALU_MOV(_R0,_x, _R17,_x),
		ALU_MOV(_R0,_y, _R17,_y),
		ALU_MOV(_R0,_z, _R17,_z),
		ALU_MOV(_R0,_w, _R17,_w)
		ALU_LAST,
		/* 471 */
		ALU_MOV(_R2,_x, _R20,_x),
		ALU_MOV(_R2,_y, _R1,_y),
		ALU_MOV(_R2,_z, _R1,_y),
		ALU_MOV(_R2,_w, _R1,_w)
		ALU_LAST,
		/* 472 */
		ALU_MOV(_R1,_x, _R19,_x),
		ALU_MOV(_R1,_y, _R19,_x),
		ALU_MOV(_R1,_z, _R19,_x),
		ALU_MOV(_R1,_w, _R19,_w)
		ALU_LAST,
	},
	{
		TEX_SAMPLE(_R16,_x,_y,_z,_w, _R4,_x,_y,_0,_x, _t0, _s0),
	},
	{
		TEX_SAMPLE(_R9,_x,_y,_z,_w, _R4,_x,_y,_0,_x, _t0, _s0) XOFFSET(1),
		TEX_SAMPLE(_R8,_x,_y,_z,_w, _R4,_x,_y,_0,_x, _t0, _s0) YOFFSET(1),
		TEX_SAMPLE(_R7,_x,_y,_z,_w, _R4,_x,_y,_0,_x, _t0, _s0) XOFFSET(1) YOFFSET(1),
	},
	{
		TEX_SAMPLE(_R16,_x,_y,_z,_w, _R6,_x,_y,_0,_x, _t0, _s0),
	},
	{
		TEX_SAMPLE(_R9,_x,_y,_z,_w, _R6,_x,_y,_0,_x, _t0, _s0) XOFFSET(1),
		TEX_SAMPLE(_R8,_x,_y,_z,_w, _R6,_x,_y,_0,_x, _t0, _s0) YOFFSET(1),
		TEX_SAMPLE(_R7,_x,_y,_z,_w, _R6,_x,_y,_0,_x, _t0, _s0) XOFFSET(1) YOFFSET(1),
	},
	{
		TEX_GET_TEXTURE_INFO(_R5,_x,_y,_m,_m, _R4,_z,_z,_0,_z, _t0, _s0),
	},
	{
		TEX_LD(_R16,_x,_y,_z,_w, _R4,_x,_y,_0,_y, _t2, _s0),
	},
	{
		TEX_LD(_R6,_x,_y,_z,_w, _R4,_x,_z,_0,_z, _t2, _s0),
		TEX_LD(_R4,_x,_y,_z,_w, _R4,_w,_y,_0,_y, _t2, _s0),
		TEX_LD(_R5,_x,_y,_z,_w, _R5,_y,_w,_0,_w, _t2, _s0),
	},
	{
		TEX_LD(_R4,_x,_y,_z,_w, _R1,_x,_y,_0,_w, _t1, _s0),
	},
};

GX2PixelShader PShaderAllGX2 = {
	{
		.sq_pgm_resources_ps.num_gprs = 21,
		.sq_pgm_resources_ps.stack_size = 4,
		.sq_pgm_exports_ps.export_mode = 0x6,
		.spi_ps_in_control_0.num_interp = 5,
		.spi_ps_in_control_0.position_ena = TRUE,
		.spi_ps_in_control_0.persp_gradient_ena = TRUE,
		.spi_ps_in_control_0.baryc_sample_cntl = spi_baryc_cntl_centers_only,
		.num_spi_ps_input_cntl = 5,
		{ { .semantic = 0, .default_val = 1 },
		  { .semantic = 0, .default_val = 1 },
		  { .semantic = 1, .default_val = 1 },
		  { .semantic = 2, .default_val = 1 },
		  { .semantic = 3, .default_val = 1 } },
		.cb_shader_mask.output0_enable = 0xF,
		.cb_shader_mask.output1_enable = 0xF,
		.cb_shader_mask.output2_enable = 0xF,
		.cb_shader_control.rt0_enable = TRUE,
		.cb_shader_control.rt1_enable = TRUE,
		.cb_shader_control.rt2_enable = TRUE,
		.db_shader_control.z_order = db_z_order_early_z_then_late_z,
		.db_shader_control.kill_enable = TRUE,
		.spi_input_z = TRUE,
	}, /* regs */
	.size = sizeof(PShaderAllCode),
	.program = (uint8_t *)&PShaderAllCode,
	.mode = GX2_SHADER_MODE_UNIFORM_BLOCK,
	.gx2rBuffer.flags = GX2R_RESOURCE_LOCKED_READ_ONLY,
};


__attribute__((aligned(GX2_SHADER_ALIGNMENT)))
static struct
{
	u64 cf[128];
	u64 alu[81];         /* 128 */
	u64 alu1[12];        /* 209 */
	u64 alu2[1];         /* 221 */
	u64 alu3[8];         /* 222 */
	u64 alu4[6];         /* 230 */
	u64 alu5[12];        /* 236 */
	u64 alu6[3];         /* 248 */
	u64 alu7[5];         /* 251 */
	u64 alu8[3];         /* 256 */
	u64 alu9[1];         /* 259 */
	u64 alu10[11];       /* 260 */
	u64 alu11[13];       /* 271 */
	u64 alu12[1];        /* 284 */
	u64 alu13[2];        /* 285 */
	u64 alu14[3];        /* 287 */
	u64 alu15[20];       /* 290 */
	u64 alu16[13];       /* 310 */
	u64 alu17[17];       /* 323 */
	u64 alu18[18];       /* 340 */
	u64 alu19[3];        /* 358 */
	u64 alu20[9];        /* 361 */
	u64 alu21[1];        /* 370 */
	u64 alu22[23];       /* 371 */
	u64 alu23[1];        /* 394 */
	u64 alu24[4];        /* 395 */
	u64 alu25[4];        /* 399 */
	u64 alu26[3];        /* 403 */
	u64 alu27[1];        /* 406 */
	u64 alu28[2];        /* 407 */
	u64 alu29[2];        /* 409 */
	u64 alu30[1];        /* 411 */
	u64 alu31[3];        /* 412 */
	u64 alu32[3];        /* 415 */
	u64 alu33[9];        /* 418 */
	u64 alu34[3];        /* 427 */
	u64 alu35[2];        /* 430 */
	u64 alu36[1];        /* 432 */
	u64 alu37[3];        /* 433 */
	u64 alu38[3];        /* 436 */
	u64 alu39[9];        /* 439 */
	u64 alu40[2];        /* 448 */
	u64 alu41[1];        /* 450 */
	u64 alu42[2];        /* 451 */
	u64 alu43[8];        /* 453 */
	u64 alu44[3];        /* 461 */
	u64 alu45[1];        /* 464 */
	u64 alu46[14];       /* 465 */
	u64 alu47[5];        /* 479 */
	u64 alu48[15];       /* 484 */
	u64 alu49[14];       /* 499 */
	u64 alu50[3];        /* 513 */
	u64 alu51[3];        /* 516 */
	u64 alu52[37];       /* 519 */
	u64 alu53[2];        /* 556 */
	u64 alu54[8];        /* 558 */
	u64 alu55[10];       /* 566 */
	u64 tex56[1 * 2];    /* 576 */
	u64 tex57[1 * 2];    /* 578 */
	u64 tex58[1 * 2];    /* 580 */
	u64 tex59[1 * 2];    /* 582 */
	u64 tex60[2 * 2];    /* 584 */
	u64 tex61[2 * 2];    /* 588 */
	u64 tex62[1 * 2];    /* 592 */
	u64 tex63[2 * 2];    /* 594 */
} VShaderHWNoSkinCode =
{
	{
		CALL_FS NO_BARRIER,
		ALU_PUSH_BEFORE(128,81) KCACHE0(CB1, _0_15) KCACHE1(CB4, _0_15),
		JUMP(0,41),
		ALU(209,12) KCACHE0(CB1, _16_31) KCACHE1(CB2, _0_15),
		ALU_PUSH_BEFORE(221,1) KCACHE0(CB4, _0_15),
		JUMP(1, 8),
		ALU(222,8) KCACHE0(CB4, _0_15) KCACHE1(CB1, _16_31),
		ALU_POP_AFTER(230,6) KCACHE0(CB2, _0_15),
		ALU(236,12) KCACHE0(CB2, _0_15),
		LOOP_START_DX10(40),
			ALU_BREAK(248,3),
			TEX(576,1),
			ALU_PUSH_BEFORE(251,5),
			JUMP(1, 39),
			TEX(578,1),
			ALU(256,3),
			TEX(580,1),
			ALU_PUSH_BEFORE(259,1),
			JUMP(0,22),
			ALU(260,11),
			TEX(582,1),
			ALU(271,13),
			ELSE(1, 24),
			ALU_POP_AFTER(284,1),
			ALU_PUSH_BEFORE(285,2),
			JUMP(1, 29),
			ALU(287,3),
			TEX(584,2),
			ALU_POP_AFTER(290,20),
			ALU(310,13),
			TEX(588,2),
			ALU_PUSH_BEFORE(323,17) KCACHE0(CB2, _0_15),
			JUMP(1, 38),
			ALU_PUSH_BEFORE(340,18),
			JUMP(2, 38),
			ALU(358,3),
			TEX(592,1),
			ALU_POP2_AFTER(361,9) KCACHE0(CB2, _0_15),
			ALU_POP_AFTER(370,1),
			LOOP_END(10),
		ALU(371,23) KCACHE0(CB4, _0_15),
		ELSE(1, 48),
		ALU_PUSH_BEFORE(394,1) KCACHE0(CB4, _0_15),
		JUMP(0,45),
		ALU(395,4) KCACHE0(CB1, _16_31),
		ELSE(1, 47),
		ALU_POP_AFTER(399,4),
		ALU_POP_AFTER(403,3),
		ALU_PUSH_BEFORE(406,1) KCACHE0(CB4, _0_15),
		JUMP(1, 101),
		ALU_PUSH_BEFORE(407,2) KCACHE0(CB4, _0_15),
		JUMP(0,61),
		ALU_PUSH_BEFORE(409,2) KCACHE0(CB4, _0_15),
		JUMP(0,59),
		ALU_PUSH_BEFORE(411,1) KCACHE0(CB4, _0_15),
		JUMP(0,57),
		ALU(412,3) KCACHE0(CB1, _16_31),
		ELSE(1, 59),
		ALU_POP_AFTER(415,3),
		ELSE(1, 61),
		ALU_POP_AFTER(418,9) KCACHE0(CB1, _16_31) KCACHE1(CB4, _0_15),
		ELSE(1, 100),
		ALU_PUSH_BEFORE(427,3) KCACHE0(CB4, _0_15),
		JUMP(0,73),
		ALU_PUSH_BEFORE(430,2) KCACHE0(CB4, _0_15),
		JUMP(0,71),
		ALU_PUSH_BEFORE(432,1) KCACHE0(CB4, _0_15),
		JUMP(0,69),
		ALU(433,3) KCACHE0(CB1, _16_31),
		ELSE(1, 71),
		ALU_POP_AFTER(436,3),
		ELSE(1, 73),
		ALU_POP_AFTER(439,9) KCACHE0(CB1, _16_31) KCACHE1(CB4, _0_15),
		ELSE(0,99),
		ALU_PUSH_BEFORE(448,2) KCACHE0(CB4, _0_15),
		JUMP(0,93),
		ALU_PUSH_BEFORE(450,1) KCACHE0(CB4, _0_15),
		JUMP(1, 92),
		ALU_PUSH_BEFORE(451,2) KCACHE0(CB4, _0_15),
		JUMP(0,81),
		ALU(453,8) KCACHE0(CB4, _0_15),
		ELSE(0,91),
		ALU_PUSH_BEFORE(461,3) KCACHE0(CB4, _0_15),
		JUMP(0,89),
		ALU_PUSH_BEFORE(464,1) KCACHE0(CB4, _0_15),
		JUMP(0,87),
		ALU(465,14) KCACHE0(CB4, _0_15),
		ELSE(1, 89),
		ALU_POP_AFTER(479,5),
		ELSE(1, 91),
		ALU_POP_AFTER(484,15) KCACHE0(CB4, _0_15),
		POP(2, 92),
		ALU(499,14) KCACHE0(CB1, _0_31),
		ELSE(1, 99),
		ALU_PUSH_BEFORE(513,3) KCACHE0(CB4, _0_15),
		JUMP(5, 101),
		ALU(516,3) KCACHE0(CB4, _0_15),
		TEX(594,2),
		ALU_POP2_AFTER(519,37) KCACHE0(CB1, _16_31),
		POP(2, 100),
		POP(1, 101),
		ALU_PUSH_BEFORE(556,2) KCACHE0(CB1, _16_31) KCACHE1(CB4, _0_15),
		JUMP(1, 104),
		ALU_POP_AFTER(558,8) KCACHE0(CB1, _16_31),
		ALU(566,2) KCACHE0(CB1, _16_31),
		EXP_DONE(POS0, _R9,_x,_y,_z,_w),
		EXP(PARAM0, _R5,_x,_y,_z,_w) NO_BARRIER,
		EXP(PARAM1, _R8,_x,_y,_z,_w) NO_BARRIER,
		EXP(PARAM2, _R15,_x,_y,_z,_w) NO_BARRIER,
		EXP_DONE(PARAM3, _R0,_x,_y,_y,_y) NO_BARRIER
		END_OF_PROGRAM
	},
	{
		/* 0 */
		ALU_MUL(_R127,_x, KC0(3),_x, ALU_SRC_1,_x),
		ALU_MUL(_R127,_y, KC0(3),_z, ALU_SRC_1,_x),
		ALU_MUL(_R127,_z, KC0(3),_y, ALU_SRC_1,_x),
		ALU_MOV(_R3,_w, ALU_SRC_LITERAL,_x),
		ALU_MUL(_R126,_x, KC0(3),_w, ALU_SRC_1,_x)
		ALU_LAST,
		ALU_LITERAL(0x3F800000),
		/* 1 */
		ALU_DOT4(_R6,_x, _R3,_x, KC0(11),_x),
		ALU_DOT4(__,_y, _R3,_y, KC0(11),_y),
		ALU_DOT4(__,_z, _R3,_z, KC0(11),_z),
		ALU_DOT4(__,_w, ALU_SRC_PV,_w, KC0(11),_w),
		ALU_MOV(_R7,_x, ALU_SRC_0,_x)
		ALU_LAST,
		/* 2 */
		ALU_DOT4(__,_x, _R3,_x, KC0(12),_x),
		ALU_DOT4(_R5,_y, _R3,_y, KC0(12),_y),
		ALU_DOT4(__,_z, _R3,_z, KC0(12),_z),
		ALU_DOT4(__,_w, _R3,_w, KC0(12),_w),
		ALU_MOV(_R6,_y, ALU_SRC_0,_x)
		ALU_LAST,
		/* 3 */
		ALU_DOT4(__,_x, _R3,_x, KC0(13),_x),
		ALU_DOT4(__,_y, _R3,_y, KC0(13),_y),
		ALU_DOT4(_R4,_z, _R3,_z, KC0(13),_z),
		ALU_DOT4(__,_w, _R3,_w, KC0(13),_w),
		ALU_MOV(_R0,_w, ALU_SRC_LITERAL,_x)
		ALU_LAST,
		ALU_LITERAL(0x3F800000),
		/* 4 */
		ALU_DOT4(__,_x, _R6,_x, KC0(10),_x),
		ALU_DOT4(_R7,_y, _R5,_y, KC0(10),_y),
		ALU_DOT4(__,_z, ALU_SRC_PV,_x, KC0(10),_z),
		ALU_DOT4(__,_w, ALU_SRC_PS,_x, KC0(10),_w),
		ALU_MOV(_R6,_z, ALU_SRC_0,_x)
		ALU_LAST,
		/* 5 */
		ALU_DOT4(_R125,_x, _R6,_x, KC0(9),_x),
		ALU_DOT4(__,_y, _R5,_y, KC0(9),_y),
		ALU_DOT4(__,_z, _R4,_z, KC0(9),_z),
		ALU_DOT4(__,_w, _R0,_w, KC0(9),_w),
		ALU_MOV(_R2,_w, ALU_SRC_LITERAL,_x)
		ALU_LAST,
		ALU_LITERAL(0x3F800000),
		/* 6 */
		ALU_MULADD(_R127,_x, _R7,_y, KC0(2),_w, _R126,_x),
		ALU_MULADD(_R127,_y, _R7,_y, KC0(2),_z, _R127,_y),
		ALU_MULADD(_R127,_z, _R7,_y, KC0(2),_y, _R127,_z),
		ALU_MULADD(_R127,_w, _R7,_y, KC0(2),_x, _R127,_x) VEC_021
		ALU_LAST,
		/* 7 */
		ALU_DOT4(__,_x, _R6,_x, KC0(8),_x),
		ALU_DOT4(__,_y, _R5,_y, KC0(8),_y),
		ALU_DOT4(_R126,_z, _R4,_z, KC0(8),_z),
		ALU_DOT4(__,_w, _R0,_w, KC0(8),_w)
		ALU_LAST,
		/* 8 */
		ALU_MULADD(_R123,_x, _R125,_x, KC0(1),_w, _R127,_x),
		ALU_MULADD(_R123,_y, _R125,_x, KC0(1),_z, _R127,_y),
		ALU_MULADD(_R123,_z, _R125,_x, KC0(1),_y, _R127,_z),
		ALU_MULADD(_R123,_w, _R125,_x, KC0(1),_x, _R127,_w)
		ALU_LAST,
		/* 9 */
		ALU_MULADD(_R9,_x, _R126,_z, KC0(0),_x, ALU_SRC_PV,_w),
		ALU_MULADD(_R9,_y, _R126,_z, KC0(0),_y, ALU_SRC_PV,_z),
		ALU_MULADD(_R9,_z, _R126,_z, KC0(0),_z, ALU_SRC_PV,_y),
		ALU_MULADD(_R9,_w, _R126,_z, KC0(0),_w, ALU_SRC_PV,_x)
		ALU_LAST,
		/* 10 */
		ALU_CNDE_INT(_R123,_x, KC1(5),_w, ALU_SRC_0,_x, _R2,_x),
		ALU_CNDE_INT(_R123,_y, KC1(5),_w, ALU_SRC_LITERAL,_x, _R2,_z),
		ALU_CNDE_INT(_R123,_w, KC1(5),_w, ALU_SRC_0,_x, _R2,_y)
		ALU_LAST,
		ALU_LITERAL(0x3F800000),
		/* 11 */
		ALU_CNDE_INT(_R127,_x, KC1(6),_x, ALU_SRC_PV,_x, ALU_SRC_PV _NEG,_x),
		ALU_CNDE_INT(_R127,_y, KC1(6),_x, ALU_SRC_PV,_w, ALU_SRC_PV _NEG,_w),
		ALU_CNDE_INT(_R126,_z, KC1(6),_x, ALU_SRC_PV,_y, ALU_SRC_PV _NEG,_y)
		ALU_LAST,
		/* 12 */
		ALU_DOT4(_R125,_x, ALU_SRC_PV,_x, KC0(11),_x),
		ALU_DOT4(__,_y, ALU_SRC_PV,_y, KC0(11),_y),
		ALU_DOT4(__,_z, ALU_SRC_PV,_z, KC0(11),_z),
		ALU_DOT4(__,_w, ALU_SRC_LITERAL,_x, ALU_SRC_0,_x)
		ALU_LAST,
		ALU_LITERAL(0x80000000),
		/* 13 */
		ALU_DOT4(__,_x, _R127,_x, KC0(12),_x),
		ALU_DOT4(_R126,_y, _R127,_y, KC0(12),_y),
		ALU_DOT4(__,_z, _R126,_z, KC0(12),_z),
		ALU_DOT4(__,_w, ALU_SRC_LITERAL,_x, ALU_SRC_0,_x)
		ALU_LAST,
		ALU_LITERAL(0x80000000),
		/* 14 */
		ALU_DOT4(__,_x, _R127,_x, KC0(13),_x),
		ALU_DOT4(__,_y, _R127,_y, KC0(13),_y),
		ALU_DOT4(_R126,_z, _R126,_z, KC0(13),_z),
		ALU_DOT4(__,_w, ALU_SRC_LITERAL,_x, ALU_SRC_0,_x)
		ALU_LAST,
		ALU_LITERAL(0x80000000),
		/* 15 */
		ALU_DOT4_IEEE(__,_x, _R125,_x, _R125,_x),
		ALU_DOT4_IEEE(__,_y, _R126,_y, _R126,_y),
		ALU_DOT4_IEEE(__,_z, ALU_SRC_PV,_x, ALU_SRC_PV,_x),
		ALU_DOT4_IEEE(__,_w, ALU_SRC_LITERAL,_x, ALU_SRC_0,_x)
		ALU_LAST,
		ALU_LITERAL(0x80000000),
		/* 16 */
		ALU_RECIPSQRT_IEEE(__,_x, ALU_SRC_PV,_x) SCL_210
		ALU_LAST,
		/* 17 */
		ALU_MUL(_R17,_x, _R125,_x, ALU_SRC_PS,_x),
		ALU_MUL(_R17,_y, _R126,_y, ALU_SRC_PS,_x),
		ALU_MUL(_R7,_z, _R126,_z, ALU_SRC_PS,_x)
		ALU_LAST,
		/* 18 */
		ALU_PRED_SETNE_INT(__,_x, KC1(9),_y, ALU_SRC_0,_x) UPDATE_EXEC_MASK(DEACTIVATE) UPDATE_PRED
		ALU_LAST,
	},
	{
		/* 19 */
		ALU_MOV(_R10,_x, KC0(4),_x),
		ALU_MOV(_R10,_y, KC0(4),_y),
		ALU_MOV(_R10,_z, KC0(4),_z),
		ALU_MOV(_R10,_w, KC0(4),_w)
		ALU_LAST,
		/* 20 */
		ALU_MOV(_R11,_x, KC1(1),_x),
		ALU_MOV(_R11,_y, KC1(1),_y),
		ALU_MOV(_R11,_z, KC1(1),_z),
		ALU_MOV(_R0,_w, KC1(1),_w)
		ALU_LAST,
		/* 21 */
		ALU_MOV(_R12,_x, KC1(2),_x),
		ALU_MOV(_R12,_y, KC1(2),_y),
		ALU_MOV(_R12,_z, KC1(2),_z),
		ALU_MOV(_R0,_w, KC1(2),_w)
		ALU_LAST,
	},
	{
		/* 22 */
		ALU_PRED_SETNE_INT(__,_x, KC0(4),_w, ALU_SRC_0,_x) UPDATE_EXEC_MASK(DEACTIVATE) UPDATE_PRED
		ALU_LAST,
	},
	{
		/* 23 */
		ALU_AND_INT(_R0,_x, KC0(8),_w, ALU_SRC_LITERAL,_x),
		ALU_AND_INT(__,_y, KC0(8),_w, ALU_SRC_1_INT,_x),
		ALU_AND_INT(_R0,_w, KC0(8),_w, ALU_SRC_LITERAL,_y)
		ALU_LAST,
		ALU_LITERAL2(0x00000002, 0x00000004),
		/* 24 */
		ALU_CNDE_INT(_R10,_x, ALU_SRC_PV,_y, KC1(4),_x, _R1,_x),
		ALU_CNDE_INT(_R10,_y, ALU_SRC_PV,_y, KC1(4),_y, _R1,_y),
		ALU_CNDE_INT(_R10,_z, ALU_SRC_PV,_y, KC1(4),_z, _R1,_z),
		ALU_CNDE_INT(_R10,_w, ALU_SRC_PV,_y, KC1(4),_w, _R1,_w)
		ALU_LAST,
	},
	{
		/* 25 */
		ALU_CNDE_INT(_R11,_x, _R0,_x, KC0(1),_x, _R1,_x),
		ALU_CNDE_INT(_R11,_y, _R0,_x, KC0(1),_y, _R1,_y),
		ALU_CNDE_INT(_R11,_z, _R0,_x, KC0(1),_z, _R1,_z)
		ALU_LAST,
		/* 26 */
		ALU_CNDE_INT(_R12,_x, _R0,_w, KC0(2),_x, _R1,_x),
		ALU_CNDE_INT(_R12,_y, _R0,_w, KC0(2),_y, _R1,_y),
		ALU_CNDE_INT(_R12,_z, _R0,_w, KC0(2),_z, _R1,_z)
		ALU_LAST,
	},
	{
		/* 27 */
		ALU_MOV(__,_x, ALU_SRC_0,_x),
		ALU_MOV(__,_y, KC0(3),_z),
		ALU_MOV(__,_z, KC0(3),_y),
		ALU_MOV(__,_w, KC0(3),_x),
		ALU_MOV(_R14,_x, ALU_SRC_0,_x)
		ALU_LAST,
		/* 28 */
		ALU_MULADD(_R13,_x, _R10,_x, KC0(0),_x, ALU_SRC_PV,_w),
		ALU_MULADD(_R13,_y, _R10,_y, KC0(0),_y, ALU_SRC_PV,_z),
		ALU_MULADD(_R13,_z, _R10,_z, KC0(0),_z, ALU_SRC_PV,_y),
		ALU_MULADD(_R13,_w, _R10,_w, KC0(0),_w, ALU_SRC_PV,_x),
		ALU_MOV(_R14,_y, ALU_SRC_0,_x)
		ALU_LAST,
		/* 29 */
		ALU_MOV(_R14,_z, ALU_SRC_0,_x),
		ALU_MOV(_R4,_w, ALU_SRC_0,_x)
		ALU_LAST,
	},
	{
		/* 30 */
		ALU_SETGT_INT(_R0,_x, ALU_SRC_LITERAL,_x, _R4,_w)
		ALU_LAST,
		ALU_LITERAL(0x00000004),
		/* 31 */
		ALU_PRED_SETNE_INT(__,_x, _R0,_x, ALU_SRC_0,_x) UPDATE_EXEC_MASK(DEACTIVATE) UPDATE_PRED
		ALU_LAST,
	},
	{
		/* 32 */
		ALU_ADD_INT(__,_x, _R4,_w, ALU_SRC_1_INT,_x),
		ALU_CNDE_INT(_R0,_y, _R0,_z, ALU_SRC_LITERAL,_x, ALU_SRC_0,_x)
		ALU_LAST,
		ALU_LITERAL(0x3F800000),
		/* 33 */
		ALU_CNDE_INT(_R4,_w, _R0,_z, ALU_SRC_PV,_x, _R4,_w)
		ALU_LAST,
		/* 34 */
		ALU_PRED_SETE_INT(__,_x, _R0,_y, ALU_SRC_0,_x) UPDATE_EXEC_MASK(DEACTIVATE) UPDATE_PRED
		ALU_LAST,
	},
	{
		/* 35 */
		ALU_ADD_INT(_R0,_z, _R4,_w, ALU_SRC_LITERAL,_x),
		ALU_SETE_INT(_R0,_w, _R16,_y, ALU_SRC_0,_x)
		ALU_LAST,
		ALU_LITERAL(0x00000004),
	},
	{
		/* 36 */
		ALU_PRED_SETE_INT(__,_x, _R0,_w, ALU_SRC_0,_x) UPDATE_EXEC_MASK(DEACTIVATE) UPDATE_PRED
		ALU_LAST,
	},
	{
		/* 37 */
		ALU_ADD(_R5,_x, _R6 _NEG,_x, _R8,_x),
		ALU_ADD_INT(_R0,_y, _R4,_w, ALU_SRC_LITERAL,_x),
		ALU_ADD(_R0,_z, _R5 _NEG,_y, _R8,_y),
		ALU_ADD(_R1,_w, _R4 _NEG,_z, _R8,_z),
		ALU_MOV(_R0,_x, ALU_SRC_LITERAL,_y)
		ALU_LAST,
		ALU_LITERAL2(0x0000000C, 0x3F800000),
		/* 38 */
		ALU_DOT4(__,_x, ALU_SRC_PV,_x, ALU_SRC_PV,_x),
		ALU_DOT4(__,_y, ALU_SRC_PV,_z, ALU_SRC_PV,_z),
		ALU_DOT4(__,_z, ALU_SRC_PV,_w, ALU_SRC_PV,_w),
		ALU_DOT4(_R0,_w, ALU_SRC_LITERAL,_x, ALU_SRC_0,_x)
		ALU_LAST,
		ALU_LITERAL(0x80000000),
	},
	{
		/* 39 */
		ALU_SQRT_IEEE(__,_x, _R0,_w) SCL_210
		ALU_LAST,
		/* 40 */
		ALU_MOV(__,_y, ALU_SRC_PS,_x),
		ALU_MUL(__,_z, ALU_SRC_PS,_x, ALU_SRC_PS,_x),
		ALU_RECIP_IEEE(_R127,_w, ALU_SRC_PS,_x) SCL_210
		ALU_LAST,
		/* 41 */
		ALU_DOT4(__,_x, _R0,_x, _R1,_x),
		ALU_DOT4(__,_y, ALU_SRC_PV,_y, _R1,_y),
		ALU_DOT4(__,_z, ALU_SRC_PV,_z, _R1,_z),
		ALU_DOT4(__,_w, ALU_SRC_LITERAL,_x, ALU_SRC_0,_x),
		ALU_MUL_IEEE(_R8,_x, _R5,_x, ALU_SRC_PS,_x)
		ALU_LAST,
		ALU_LITERAL(0x80000000),
		/* 42 */
		ALU_MUL_IEEE(_R8,_y, _R0,_z, _R127,_w),
		ALU_MUL_IEEE(_R8,_z, _R1,_w, _R127,_w),
		ALU_RECIP_IEEE(_R1,_w, ALU_SRC_PV,_x) CLAMP SCL_210
		ALU_LAST,
	},
	{
		/* 43 */
		ALU_MOV(_R1,_w, _R2,_w)
		ALU_LAST,
	},
	{
		/* 44 */
		ALU_PRED_SETGE_INT(__,_x, _R16,_y, ALU_SRC_LITERAL,_x) UPDATE_EXEC_MASK(DEACTIVATE) UPDATE_PRED
		ALU_LAST,
		ALU_LITERAL(0x00000002),
	},
	{
		/* 45 */
		ALU_ADD_INT(_R0,_z, _R4,_w, ALU_SRC_LITERAL,_x),
		ALU_ADD_INT(_R0,_w, _R4,_w, ALU_SRC_LITERAL,_y)
		ALU_LAST,
		ALU_LITERAL2(0x00000010, 0x00000008),
	},
	{
		/* 46 */
		ALU_DOT4_IEEE(__,_x, _R1,_x, _R1,_x),
		ALU_DOT4_IEEE(__,_y, _R1,_y, _R1,_y),
		ALU_DOT4_IEEE(__,_z, _R1,_z, _R1,_z),
		ALU_DOT4_IEEE(__,_w, ALU_SRC_LITERAL,_x, ALU_SRC_0,_x)
		ALU_LAST,
		ALU_LITERAL(0x80000000),
		/* 47 */
		ALU_RECIPSQRT_IEEE(__,_x, ALU_SRC_PV,_x) SCL_210
		ALU_LAST,
		/* 48 */
		ALU_MUL(__,_x, _R1,_x, ALU_SRC_PS,_x),
		ALU_MUL(__,_y, _R1,_y, ALU_SRC_PS,_x),
		ALU_MUL(__,_z, _R1,_z, ALU_SRC_PS,_x)
		ALU_LAST,
		/* 49 */
		ALU_DOT4(__,_x, _R8,_x, ALU_SRC_PV,_x),
		ALU_DOT4(__,_y, _R8,_y, ALU_SRC_PV,_y),
		ALU_DOT4(__,_z, _R8,_z, ALU_SRC_PV,_z),
		ALU_DOT4(__,_w, ALU_SRC_LITERAL,_x, ALU_SRC_0,_x)
		ALU_LAST,
		ALU_LITERAL(0x80000000),
		/* 50 */
		ALU_SETGE_DX10(_R127,_x, ALU_SRC_PV,_x, _R0,_x),
		ALU_LOG_CLAMPED(__,_x, ALU_SRC_PV,_x) SCL_210
		ALU_LAST,
		/* 51 */
		ALU_MUL(__,_z, _R0,_y, ALU_SRC_PS,_x)
		ALU_LAST,
		/* 52 */
		ALU_EXP_IEEE(__,_x, ALU_SRC_PV,_z) SCL_210
		ALU_LAST,
		/* 53 */
		ALU_MUL(__,_x, _R1,_w, ALU_SRC_PS,_x)
		ALU_LAST,
		/* 54 */
		ALU_CNDE_INT(_R1,_w, _R127,_x, ALU_SRC_0,_x, ALU_SRC_PV,_x)
		ALU_LAST,
	},
	{
		/* 55 */
		ALU_SETE_INT(_R5,_x, _R16,_x, ALU_SRC_LITERAL,_x),
		ALU_ADD_INT(_R0,_y, _R4,_w, ALU_SRC_LITERAL,_y),
		ALU_ADD_INT(_R0,_w, _R4,_w, ALU_SRC_LITERAL,_z),
		ALU_MUL(__,_x, _R7,_z, _R8,_z)
		ALU_LAST,
		ALU_LITERAL3(0x00000002, 0x00000014, 0x00000018),
		/* 56 */
		ALU_DOT4(__,_x, _R17,_x, _R8,_x),
		ALU_DOT4(__,_y, _R17,_y, _R8,_y),
		ALU_DOT4(__,_z, ALU_SRC_PS,_x, ALU_SRC_1,_x),
		ALU_DOT4(__,_w, ALU_SRC_LITERAL,_x, ALU_SRC_0,_x)
		ALU_LAST,
		ALU_LITERAL(0x80000000),
		/* 57 */
		ALU_MAX(_R5,_z, ALU_SRC_PV,_x, ALU_SRC_LITERAL,_x)
		ALU_LAST,
		ALU_LITERAL(0x33D6BF95),
	},
	{
		/* 58 */
		ALU_MUL(_R127,_x, _R11,_x, _R1,_x),
		ALU_MUL(_R127,_z, _R11,_z, _R1,_z),
		ALU_MUL(_R127,_w, _R11,_y, _R1,_y),
		ALU_LOG_CLAMPED(__,_x, _R5,_z) SCL_210
		ALU_LAST,
		/* 59 */
		ALU_MUL(_R126,_x, _R10,_x, _R0,_x),
		ALU_MUL(_R127,_y, _R10,_y, _R0,_y),
		ALU_MUL(_R126,_z, _R10,_z, _R0,_z),
		ALU_MUL(__,_w, KC0(2),_w, ALU_SRC_PS,_x)
		ALU_LAST,
		/* 60 */
		ALU_EXP_IEEE(__,_x, ALU_SRC_PV,_w) SCL_210
		ALU_LAST,
		/* 61 */
		ALU_CNDE_INT(_R123,_y, _R5,_x, _R5,_z, ALU_SRC_PS,_x)
		ALU_LAST,
		/* 62 */
		ALU_MULADD(_R123,_x, ALU_SRC_PV,_y, _R127,_w, _R127,_y),
		ALU_MULADD(_R123,_y, ALU_SRC_PV,_y, _R127,_x, _R126,_x),
		ALU_MULADD(_R123,_w, ALU_SRC_PV,_y, _R127,_z, _R126,_z)
		ALU_LAST,
		/* 63 */
		ALU_MULADD(_R13,_x, _R1,_w, ALU_SRC_PV,_y, _R13,_x),
		ALU_MULADD(_R13,_y, _R1,_w, ALU_SRC_PV,_x, _R13,_y),
		ALU_MULADD(_R13,_z, _R1,_w, ALU_SRC_PV,_w, _R13,_z)
		ALU_LAST,
		/* 64 */
		ALU_PRED_SETNE_INT(__,_x, ALU_SRC_0,_x, _R16,_x) UPDATE_EXEC_MASK(DEACTIVATE) UPDATE_PRED
		ALU_LAST,
	},
	{
		/* 65 */
		ALU_ADD(_R127,_x, _R8,_x, ALU_SRC_0,_x),
		ALU_ADD(_R127,_y, _R8,_y, ALU_SRC_0,_x),
		ALU_ADD(_R127,_z, _R8,_z, ALU_SRC_1,_x)
		ALU_LAST,
		/* 66 */
		ALU_DOT4_IEEE(__,_x, ALU_SRC_PV,_x, ALU_SRC_PV,_x),
		ALU_DOT4_IEEE(__,_y, ALU_SRC_PV,_y, ALU_SRC_PV,_y),
		ALU_DOT4_IEEE(__,_z, ALU_SRC_PV,_z, ALU_SRC_PV,_z),
		ALU_DOT4_IEEE(__,_w, ALU_SRC_LITERAL,_x, ALU_SRC_0,_x)
		ALU_LAST,
		ALU_LITERAL(0x80000000),
		/* 67 */
		ALU_RECIPSQRT_IEEE(__,_x, ALU_SRC_PV,_x) SCL_210
		ALU_LAST,
		/* 68 */
		ALU_MUL(__,_x, _R127,_x, ALU_SRC_PS,_x),
		ALU_MUL(__,_y, _R127,_y, ALU_SRC_PS,_x),
		ALU_MUL(__,_z, _R127,_z, ALU_SRC_PS,_x)
		ALU_LAST,
		/* 69 */
		ALU_DOT4(_R0,_x, _R17,_x, ALU_SRC_PV,_x),
		ALU_DOT4(__,_y, _R17,_y, ALU_SRC_PV,_y),
		ALU_DOT4(__,_z, _R7,_z, ALU_SRC_PV,_z),
		ALU_DOT4(__,_w, ALU_SRC_LITERAL,_x, ALU_SRC_0,_x)
		ALU_LAST,
		ALU_LITERAL(0x80000000),
		/* 70 */
		ALU_PRED_SETGT(__,_x, _R0,_x, ALU_SRC_0,_x) UPDATE_EXEC_MASK(DEACTIVATE) UPDATE_PRED
		ALU_LAST,
	},
	{
		/* 71 */
		ALU_ADD_INT(_R0,_z, _R4,_w, ALU_SRC_LITERAL,_x),
		ALU_LOG_CLAMPED(_R1,_z, _R0,_x) SCL_210
		ALU_LAST,
		ALU_LITERAL(0x0000001C),
	},
	{
		/* 72 */
		ALU_MUL(_R127,_x, _R12,_x, _R0,_x),
		ALU_MUL(__,_y, KC0(2),_w, _R1,_z),
		ALU_MUL(_R127,_z, _R12,_y, _R0,_y),
		ALU_MUL(_R127,_w, _R12,_z, _R0,_z) VEC_021
		ALU_LAST,
		/* 73 */
		ALU_EXP_IEEE(__,_x, ALU_SRC_PV,_y) SCL_210
		ALU_LAST,
		/* 74 */
		ALU_MUL(__,_w, _R1,_w, ALU_SRC_PS,_x)
		ALU_LAST,
		/* 75 */
		ALU_MULADD(_R14,_x, ALU_SRC_PV,_w, _R127,_x, _R14,_x),
		ALU_MULADD(_R14,_y, ALU_SRC_PV,_w, _R127,_z, _R14,_y),
		ALU_MULADD(_R14,_z, ALU_SRC_PV,_w, _R127,_w, _R14,_z)
		ALU_LAST,
	},
	{
		/* 76 */
		ALU_ADD_INT(_R4,_w, _R4,_w, ALU_SRC_1_INT,_x)
		ALU_LAST,
	},
	{
		/* 77 */
		ALU_MOV(_R127,_x, _R13,_z) CLAMP,
		ALU_MOV(_R127,_y, _R13,_y) CLAMP,
		ALU_MOV(_R127,_z, _R13,_x) CLAMP,
		ALU_MOV(_R0,_w, ALU_SRC_0,_x),
		ALU_MOV(__,_x, _R13,_w) CLAMP
		ALU_LAST,
		/* 78 */
		ALU_MOV(_R126,_x, _R13,_z) CLAMP,
		ALU_MOV(_R126,_y, _R13,_y) CLAMP,
		ALU_MOV(_R126,_z, _R13,_x) CLAMP,
		ALU_ADD(_R127,_w, ALU_SRC_PV,_w, ALU_SRC_PS,_x) CLAMP,
		ALU_MOV(_R126,_w, _R13,_w) CLAMP
		ALU_LAST,
		/* 79 */
		ALU_MOV(_R125,_x, _R14,_z) CLAMP,
		ALU_MOV(_R127,_y, _R14,_y) CLAMP,
		ALU_MOV(_R127,_z, _R14,_x) CLAMP,
		ALU_ADD(__,_w, _R14,_x, _R127,_z) CLAMP,
		ALU_ADD(__,_x, _R14,_y, _R127,_y) CLAMP
		ALU_LAST,
		/* 80 */
		ALU_ADD(__,_x, _R14,_z, _R127,_x) CLAMP,
		ALU_CNDE_INT(_R5,_y, KC0(4),_x, ALU_SRC_PS,_x, _R126,_y),
		ALU_CNDE_INT(_R5,_x, KC0(4),_x, ALU_SRC_PV,_w, _R126,_z) VEC_021
		ALU_LAST,
		/* 81 */
		ALU_CNDE_INT(_R8,_x, KC0(4),_x, ALU_SRC_0,_x, _R127,_z),
		ALU_CNDE_INT(_R8,_y, KC0(4),_x, ALU_SRC_0,_x, _R127,_y),
		ALU_CNDE_INT(_R5,_z, KC0(4),_x, ALU_SRC_PV,_x, _R126,_x) VEC_021,
		ALU_CNDE_INT(_R5,_w, KC0(4),_x, _R127,_w, _R126,_w),
		ALU_CNDE_INT(_R8,_z, KC0(4),_x, ALU_SRC_0,_x, _R125,_x) VEC_021
		ALU_LAST,
	},
	{
		/* 82 */
		ALU_PRED_SETE_INT(__,_x, KC0(4),_w, ALU_SRC_0,_x) UPDATE_EXEC_MASK(DEACTIVATE) UPDATE_PRED
		ALU_LAST,
	},
	{
		/* 83 */
		ALU_MOV(_R5,_x, KC0(4),_x),
		ALU_MOV(_R5,_y, KC0(4),_y),
		ALU_MOV(_R5,_z, KC0(4),_z),
		ALU_MOV(_R5,_w, KC0(4),_w)
		ALU_LAST,
	},
	{
		/* 84 */
		ALU_MOV(_R5,_x, _R1,_x),
		ALU_MOV(_R5,_y, _R1,_y),
		ALU_MOV(_R5,_z, _R1,_z),
		ALU_MOV(_R5,_w, _R1,_w)
		ALU_LAST,
	},
	{
		/* 85 */
		ALU_MOV(_R8,_x, _R7,_x),
		ALU_MOV(_R8,_y, _R6,_y),
		ALU_MOV(_R8,_z, _R6,_z)
		ALU_LAST,
	},
	{
		/* 86 */
		ALU_PRED_SETNE_INT(__,_x, KC0(5),_x, ALU_SRC_0,_x) UPDATE_EXEC_MASK(DEACTIVATE) UPDATE_PRED
		ALU_LAST,
	},
	{
		/* 87 */
		ALU_SETNE_INT(_R0,_x, KC0(7),_y, ALU_SRC_0,_x)
		ALU_LAST,
		/* 88 */
		ALU_PRED_SETE_INT(__,_x, _R0,_x, ALU_SRC_0,_x) UPDATE_EXEC_MASK(DEACTIVATE) UPDATE_PRED
		ALU_LAST,
	},
	{
		/* 89 */
		ALU_NOT_INT(_R0,_w, KC0(4),_y)
		ALU_LAST,
		/* 90 */
		ALU_PRED_SETNE_INT(__,_x, _R0,_w, ALU_SRC_0,_x) UPDATE_EXEC_MASK(DEACTIVATE) UPDATE_PRED
		ALU_LAST,
	},
	{
		/* 91 */
		ALU_PRED_SETNE_INT(__,_x, KC0(6),_y, ALU_SRC_0,_x) UPDATE_EXEC_MASK(DEACTIVATE) UPDATE_PRED
		ALU_LAST,
	},
	{
		/* 92 */
		ALU_MUL(_R15,_x, _R4,_x, KC0(1),_x),
		ALU_MUL(_R15,_y, _R4,_y, KC0(1),_y),
		ALU_MOV(_R15,_z, ALU_SRC_0,_x)
		ALU_LAST,
	},
	{
		/* 93 */
		ALU_MOV(_R15,_x, _R7,_x),
		ALU_MOV(_R15,_y, _R6,_y),
		ALU_MOV(_R15,_z, _R6,_z)
		ALU_LAST,
	},
	{
		/* 94 */
		ALU_MULADD(_R127,_x, _R4,_x, KC0(1),_x, KC0(1),_z),
		ALU_MOV(_R127,_y, ALU_SRC_0,_x),
		ALU_MOV(_R127,_z, ALU_SRC_0,_x),
		ALU_MULADD(_R127,_w, _R4,_y, KC0(1),_y, KC0(1),_w)
		ALU_LAST,
		/* 95 */
		ALU_MOV(__,_x, KC0(1),_w),
		ALU_MOV(__,_y, KC0(1),_z)
		ALU_LAST,
		/* 96 */
		ALU_CNDE_INT(_R15,_x, KC1(6),_y, ALU_SRC_PV,_y, _R127,_x),
		ALU_CNDE_INT(_R15,_y, KC1(6),_y, ALU_SRC_PV,_x, _R127,_w),
		ALU_CNDE_INT(_R15,_z, KC1(6),_y, _R127,_y, _R127,_z)
		ALU_LAST,
	},
	{
		/* 97 */
		ALU_SETNE_INT(_R0,_z, KC0(7),_y, ALU_SRC_LITERAL,_x)
		ALU_LAST,
		ALU_LITERAL(0x00000003),
		/* 98 */
		ALU_PRED_SETE_INT(__,_x, _R0,_z, ALU_SRC_0,_x) UPDATE_EXEC_MASK(DEACTIVATE) UPDATE_PRED
		ALU_LAST,
	},
	{
		/* 99 */
		ALU_NOT_INT(_R0,_y, KC0(4),_y)
		ALU_LAST,
		/* 100 */
		ALU_PRED_SETNE_INT(__,_x, _R0,_y, ALU_SRC_0,_x) UPDATE_EXEC_MASK(DEACTIVATE) UPDATE_PRED
		ALU_LAST,
	},
	{
		/* 101 */
		ALU_PRED_SETNE_INT(__,_x, KC0(6),_y, ALU_SRC_0,_x) UPDATE_EXEC_MASK(DEACTIVATE) UPDATE_PRED
		ALU_LAST,
	},
	{
		/* 102 */
		ALU_MUL(_R15,_x, _R4,_x, KC0(1),_x),
		ALU_MUL(_R15,_y, _R4,_y, KC0(1),_y),
		ALU_MOV(_R15,_z, ALU_SRC_0,_x)
		ALU_LAST,
	},
	{
		/* 103 */
		ALU_MOV(_R15,_x, _R7,_x),
		ALU_MOV(_R15,_y, _R6,_y),
		ALU_MOV(_R15,_z, _R6,_z)
		ALU_LAST,
	},
	{
		/* 104 */
		ALU_MULADD(_R127,_x, _R4,_x, KC0(1),_x, KC0(1),_z),
		ALU_MOV(_R127,_y, ALU_SRC_0,_x),
		ALU_MOV(_R127,_z, ALU_SRC_0,_x),
		ALU_MULADD(_R127,_w, _R4,_y, KC0(1),_y, KC0(1),_w)
		ALU_LAST,
		/* 105 */
		ALU_MOV(__,_x, KC0(1),_w),
		ALU_MOV(__,_y, KC0(1),_z)
		ALU_LAST,
		/* 106 */
		ALU_CNDE_INT(_R15,_x, KC1(6),_y, ALU_SRC_PV,_y, _R127,_x),
		ALU_CNDE_INT(_R15,_y, KC1(6),_y, ALU_SRC_PV,_x, _R127,_w),
		ALU_CNDE_INT(_R15,_z, KC1(6),_y, _R127,_y, _R127,_z)
		ALU_LAST,
	},
	{
		/* 107 */
		ALU_SETNE_INT(_R0,_x, KC0(7),_y, ALU_SRC_1_INT,_x)
		ALU_LAST,
		/* 108 */
		ALU_PRED_SETE_INT(__,_x, _R0,_x, ALU_SRC_0,_x) UPDATE_EXEC_MASK(DEACTIVATE) UPDATE_PRED
		ALU_LAST,
	},
	{
		/* 109 */
		ALU_PRED_SETNE_INT(__,_x, KC0(7),_z, ALU_SRC_0,_x) UPDATE_EXEC_MASK(DEACTIVATE) UPDATE_PRED
		ALU_LAST,
	},
	{
		/* 110 */
		ALU_SETNE_INT(_R0,_w, KC0(7),_z, ALU_SRC_1_INT,_x)
		ALU_LAST,
		/* 111 */
		ALU_PRED_SETE_INT(__,_x, _R0,_w, ALU_SRC_0,_x) UPDATE_EXEC_MASK(DEACTIVATE) UPDATE_PRED
		ALU_LAST,
	},
	{
		/* 112 */
		ALU_CNDE_INT(_R3,_x, KC0(6),_y, ALU_SRC_0,_x, _R4,_x),
		ALU_CNDE_INT(_R3,_y, KC0(6),_y, ALU_SRC_0,_x, _R4,_y),
		ALU_MOV(_R0,_z, ALU_SRC_0,_x),
		ALU_MOV(_R0,_w, ALU_SRC_LITERAL,_x)
		ALU_LAST,
		ALU_LITERAL(0x3F800000),
		/* 113 */
		ALU_CNDE_INT(_R3,_z, KC0(6),_y, ALU_SRC_0,_x, ALU_SRC_PV,_z),
		ALU_CNDE_INT(_R3,_w, KC0(6),_y, ALU_SRC_LITERAL,_x, ALU_SRC_PV,_w)
		ALU_LAST,
		ALU_LITERAL(0x3F800000),
	},
	{
		/* 114 */
		ALU_SETNE_INT(_R0,_z, KC0(7),_z, ALU_SRC_LITERAL,_x)
		ALU_LAST,
		ALU_LITERAL(0x00000002),
		/* 115 */
		ALU_PRED_SETE_INT(__,_x, _R0,_z, ALU_SRC_0,_x) UPDATE_EXEC_MASK(DEACTIVATE) UPDATE_PRED
		ALU_LAST,
	},
	{
		/* 116 */
		ALU_PRED_SETNE_INT(__,_x, KC0(5),_w, ALU_SRC_0,_x) UPDATE_EXEC_MASK(DEACTIVATE) UPDATE_PRED
		ALU_LAST,
	},
	{
		/* 117 */
		ALU_CNDE_INT(_R127,_x, KC0(6),_x, _R2,_x, _R2 _NEG,_x),
		ALU_CNDE_INT(_R127,_y, KC0(6),_x, _R2,_y, _R2 _NEG,_y),
		ALU_CNDE_INT(_R127,_z, KC0(6),_x, _R2,_z, _R2 _NEG,_z)
		ALU_LAST,
		/* 118 */
		ALU_DOT4_IEEE(__,_x, ALU_SRC_PV,_x, ALU_SRC_PV,_x),
		ALU_DOT4_IEEE(__,_y, ALU_SRC_PV,_y, ALU_SRC_PV,_y),
		ALU_DOT4_IEEE(__,_z, ALU_SRC_PV,_z, ALU_SRC_PV,_z),
		ALU_DOT4_IEEE(__,_w, ALU_SRC_LITERAL,_x, ALU_SRC_0,_x)
		ALU_LAST,
		ALU_LITERAL(0x80000000),
		/* 119 */
		ALU_RECIPSQRT_IEEE(__,_x, ALU_SRC_PV,_x) SCL_210
		ALU_LAST,
		/* 120 */
		ALU_MUL(_R3,_x, _R127,_x, ALU_SRC_PS,_x),
		ALU_MUL(_R3,_y, _R127,_y, ALU_SRC_PS,_x),
		ALU_MUL(_R3,_z, _R127,_z, ALU_SRC_PS,_x),
		ALU_MOV(_R3,_w, ALU_SRC_LITERAL,_x)
		ALU_LAST,
		ALU_LITERAL(0x3F800000),
	},
	{
		/* 121 */
		ALU_MOV(_R3,_x, ALU_SRC_0,_x),
		ALU_MOV(_R3,_y, ALU_SRC_0,_x),
		ALU_MOV(_R3,_z, ALU_SRC_LITERAL,_x),
		ALU_MOV(_R3,_w, ALU_SRC_LITERAL,_x)
		ALU_LAST,
		ALU_LITERAL(0x3F800000),
	},
	{
		/* 122 */
		ALU_MOV(__,_x, ALU_SRC_LITERAL,_x),
		ALU_CNDE_INT(_R123,_y, KC0(6),_x, _R2,_z, _R2 _NEG,_z),
		ALU_CNDE_INT(_R123,_z, KC0(6),_x, _R2,_y, _R2 _NEG,_y),
		ALU_CNDE_INT(_R123,_w, KC0(6),_x, _R2,_x, _R2 _NEG,_x),
		ALU_SETNE_INT(_R127,_x, KC0(7),_z, ALU_SRC_LITERAL,_y)
		ALU_LAST,
		ALU_LITERAL2(0x3F800000, 0x00000003),
		/* 123 */
		ALU_CNDE_INT(_R123,_x, KC0(5),_w, ALU_SRC_LITERAL,_x, ALU_SRC_PV,_x),
		ALU_CNDE_INT(_R123,_y, KC0(5),_w, ALU_SRC_LITERAL,_x, ALU_SRC_PV,_y),
		ALU_CNDE_INT(_R123,_z, KC0(5),_w, ALU_SRC_0,_x, ALU_SRC_PV,_z),
		ALU_CNDE_INT(_R123,_w, KC0(5),_w, ALU_SRC_0,_x, ALU_SRC_PV,_w)
		ALU_LAST,
		ALU_LITERAL(0x3F800000),
		/* 124 */
		ALU_CNDE_INT(_R3,_x, _R127,_x, ALU_SRC_PV,_w, ALU_SRC_0,_x),
		ALU_CNDE_INT(_R3,_y, _R127,_x, ALU_SRC_PV,_z, ALU_SRC_0,_x),
		ALU_CNDE_INT(_R3,_z, _R127,_x, ALU_SRC_PV,_y, ALU_SRC_0,_x),
		ALU_CNDE_INT(_R3,_w, _R127,_x, ALU_SRC_PV,_x, ALU_SRC_0,_x)
		ALU_LAST,
	},
	{
		/* 125 */
		ALU_DOT4(_R127,_x, _R3,_x, KC0(14),_x),
		ALU_DOT4(__,_y, _R3,_y, KC0(14),_y),
		ALU_DOT4(__,_z, _R3,_z, KC0(14),_z),
		ALU_DOT4(__,_w, _R3,_w, KC0(14),_w)
		ALU_LAST,
		/* 126 */
		ALU_DOT4(__,_x, _R3,_x, KC0(15),_x),
		ALU_DOT4(__,_y, _R3,_y, KC0(15),_y),
		ALU_DOT4(__,_z, _R3,_z, KC0(15),_z),
		ALU_DOT4(_R127,_w, _R3,_w, KC0(15),_w)
		ALU_LAST,
		/* 127 */
		ALU_DOT4(__,_x, _R3,_x, KC0(16),_x),
		ALU_DOT4(__,_y, _R3,_y, KC0(16),_y),
		ALU_DOT4(_R15,_z, _R3,_z, KC0(16),_z),
		ALU_DOT4(__,_w, _R3,_w, KC0(16),_w)
		ALU_LAST,
		/* 128 */
		ALU_MUL(_R15,_x, KC0(17),_x, _R127,_x),
		ALU_MUL(_R15,_y, KC0(17),_y, _R127,_w)
		ALU_LAST,
	},
	{
		/* 129 */
		ALU_SETNE_INT(_R0,_z, KC0(7),_y, ALU_SRC_LITERAL,_x)
		ALU_LAST,
		ALU_LITERAL(0x00000002),
		/* 130 */
		ALU_PRED_SETE_INT(__,_x, _R0,_z, ALU_SRC_0,_x) UPDATE_EXEC_MASK(DEACTIVATE) UPDATE_PRED
		ALU_LAST,
	},
	{
		/* 131 */
		ALU_ADD_INT(_R0,_x, KC0(8),_x, ALU_SRC_LITERAL,_x),
		ALU_ADD_INT(_R0,_w, KC0(7),_w, ALU_SRC_LITERAL,_x)
		ALU_LAST,
		ALU_LITERAL(0x00000004),
	},
	{
		/* 132 */
		ALU_DOT4_IEEE(__,_x, _R1,_x, _R1,_x),
		ALU_DOT4_IEEE(__,_y, _R1,_y, _R1,_y),
		ALU_DOT4_IEEE(__,_z, _R1,_z, _R1,_z),
		ALU_DOT4_IEEE(__,_w, ALU_SRC_LITERAL,_x, ALU_SRC_0,_x),
		ALU_MUL_IEEE(__,_x, _R0,_z, _R0,_z)
		ALU_LAST,
		ALU_LITERAL(0x80000000),
		/* 133 */
		ALU_DOT4_IEEE(__,_x, _R0,_x, _R0,_x),
		ALU_DOT4_IEEE(__,_y, _R0,_y, _R0,_y),
		ALU_DOT4_IEEE(__,_z, ALU_SRC_PS,_x, ALU_SRC_1,_x),
		ALU_DOT4_IEEE(__,_w, ALU_SRC_LITERAL,_x, ALU_SRC_0,_x),
		ALU_RECIPSQRT_IEEE(__,_x, ALU_SRC_PV,_x) SCL_210
		ALU_LAST,
		ALU_LITERAL(0x80000000),
		/* 134 */
		ALU_MUL(_R127,_x, _R1,_x, ALU_SRC_PS,_x),
		ALU_MUL(_R127,_y, _R1,_y, ALU_SRC_PS,_x),
		ALU_MUL(__,_z, _R1,_z, ALU_SRC_PS,_x),
		ALU_RECIPSQRT_IEEE(__,_x, ALU_SRC_PV,_x) SCL_210
		ALU_LAST,
		/* 135 */
		ALU_MUL(_R126,_x, _R0,_x, ALU_SRC_PS,_x),
		ALU_MUL(_R126,_y, _R0,_y, ALU_SRC_PS,_x),
		ALU_MUL(__,_z, _R0,_z, ALU_SRC_PS,_x),
		ALU_MUL(__,_x, _R7,_z, ALU_SRC_PV,_z)
		ALU_LAST,
		/* 136 */
		ALU_DOT4(__,_x, _R17,_x, _R127,_x),
		ALU_DOT4(__,_y, _R17,_y, _R127,_y),
		ALU_DOT4(__,_z, ALU_SRC_PS,_x, ALU_SRC_1,_x),
		ALU_DOT4(__,_w, ALU_SRC_LITERAL,_x, ALU_SRC_0,_x),
		ALU_MUL(__,_x, _R7,_z, ALU_SRC_PV,_z)
		ALU_LAST,
		ALU_LITERAL(0x80000000),
		/* 137 */
		ALU_DOT4(__,_x, _R17,_x, _R126,_x),
		ALU_DOT4(__,_y, _R17,_y, _R126,_y),
		ALU_DOT4(__,_z, ALU_SRC_PS,_x, ALU_SRC_1,_x),
		ALU_DOT4(__,_w, ALU_SRC_LITERAL,_x, ALU_SRC_0,_x),
		ALU_ADD_D2(__,_x, ALU_SRC_PV,_x, ALU_SRC_1,_x)
		ALU_LAST,
		ALU_LITERAL(0x80000000),
		/* 138 */
		ALU_MUL(_R15,_x, ALU_SRC_PS,_x, KC0(1),_x),
		ALU_ADD_D2(__,_w, ALU_SRC_PV,_x, ALU_SRC_1,_x)
		ALU_LAST,
		/* 139 */
		ALU_MUL(_R15,_y, ALU_SRC_PV,_w, KC0(1),_y),
		ALU_MOV(_R15,_z, ALU_SRC_LITERAL,_x)
		ALU_LAST,
		ALU_LITERAL(0x3F800000),
	},
	{
		/* 140 */
		ALU_ADD(_R0,_y, _R7,_y, KC0(3),_x)
		ALU_LAST,
		/* 141 */
		ALU_PRED_SETNE_INT(__,_x, KC1(10),_y, ALU_SRC_0,_x) UPDATE_EXEC_MASK(DEACTIVATE) UPDATE_PRED
		ALU_LAST,
	},
	{
		/* 142 */
		ALU_RECIP_IEEE(__,_x, _R9,_w) SCL_210
		ALU_LAST,
		/* 143 */
		ALU_MUL_IEEE(__,_y, _R9,_z, ALU_SRC_PS,_x)
		ALU_LAST,
		/* 144 */
		ALU_MUL(__,_x, ALU_SRC_PV,_y, KC0(2),_x)
		ALU_LAST,
		/* 145 */
		ALU_ADD(__,_z, KC0(2),_y, ALU_SRC_PV,_x)
		ALU_LAST,
		/* 146 */
		ALU_FLOOR(__,_w, ALU_SRC_PV,_z)
		ALU_LAST,
		/* 147 */
		ALU_ADD(__,_y, KC0(2) _NEG,_z, ALU_SRC_PV,_w)
		ALU_LAST,
		/* 148 */
		ALU_MUL(__,_x, KC0(2),_w, ALU_SRC_PV,_y)
		ALU_LAST,
		/* 149 */
		ALU_MUL(_R9,_z, _R9,_w, ALU_SRC_PV,_x)
		ALU_LAST,
	},
	{
		/* 150 */
		ALU_NOP(__,_x),
		ALU_MUL(_R0,_x, KC0(3),_y, _R0,_y)
		ALU_LAST,
	},
	{
		VTX_FETCH(_R0,_m,_m,_z,_m, _R4,_w, (132), FETCH_TYPE(NO_INDEX_OFFSET), MEGA(16), OFFSET(0)),
	},
	{
		VTX_FETCH(_R16,_x,_y,_m,_m, _R4,_w, (132), FETCH_TYPE(NO_INDEX_OFFSET), MEGA(16), OFFSET(0)),
	},
	{
		VTX_FETCH(_R8,_x,_y,_z,_m, _R0,_z, (130), FETCH_TYPE(NO_INDEX_OFFSET), MEGA(16), OFFSET(0)),
	},
	{
		VTX_FETCH(_R1,_x,_y,_z,_m, _R0,_y, (130), FETCH_TYPE(NO_INDEX_OFFSET), MEGA(16), OFFSET(0)),
	},
	{
		VTX_FETCH(_R1,_x,_y,_z,_m, _R0,_w, (130), FETCH_TYPE(NO_INDEX_OFFSET), MEGA(16), OFFSET(0)),
		VTX_FETCH(_R0,_x,_y,_m,_m, _R0,_z, (130), FETCH_TYPE(NO_INDEX_OFFSET), MEGA(16), OFFSET(0)),
	},
	{
		VTX_FETCH(_R1,_x,_y,_z,_m, _R0,_w, (130), FETCH_TYPE(NO_INDEX_OFFSET), MEGA(16), OFFSET(0)),
		VTX_FETCH(_R0,_x,_y,_z,_m, _R0,_y, (130), FETCH_TYPE(NO_INDEX_OFFSET), MEGA(16), OFFSET(0)),
	},
	{
		VTX_FETCH(_R0,_x,_y,_z,_m, _R0,_z, (130), FETCH_TYPE(NO_INDEX_OFFSET), MEGA(16), OFFSET(0)),
	},
	{
		VTX_FETCH(_R1,_x,_y,_z,_m, _R0,_w, (130), FETCH_TYPE(NO_INDEX_OFFSET), MEGA(16), OFFSET(0)),
		VTX_FETCH(_R0,_x,_y,_z,_m, _R0,_x, (130), FETCH_TYPE(NO_INDEX_OFFSET), MEGA(16), OFFSET(0)),
	},
};

static GX2LoopVar VShaderLoopVars[] =
{
	{ 0x00000000, 0xFFFFFFFF },
};

GX2VertexShader VShaderHWNoSkinGX2 = {
	{
		.sq_pgm_resources_vs.num_gprs = 18,
		.sq_pgm_resources_vs.stack_size = 3,
		.spi_vs_out_config.vs_export_count = 3,
		.num_spi_vs_out_id = 1,
		{
			{ .semantic_0 = 0x00, .semantic_1 = 0x01, .semantic_2 = 0x03, .semantic_3 = 0x02 },
			{ .semantic_0 = 0xFF, .semantic_1 = 0xFF, .semantic_2 = 0xFF, .semantic_3 = 0xFF },
			{ .semantic_0 = 0xFF, .semantic_1 = 0xFF, .semantic_2 = 0xFF, .semantic_3 = 0xFF },
			{ .semantic_0 = 0xFF, .semantic_1 = 0xFF, .semantic_2 = 0xFF, .semantic_3 = 0xFF },
			{ .semantic_0 = 0xFF, .semantic_1 = 0xFF, .semantic_2 = 0xFF, .semantic_3 = 0xFF },
			{ .semantic_0 = 0xFF, .semantic_1 = 0xFF, .semantic_2 = 0xFF, .semantic_3 = 0xFF },
			{ .semantic_0 = 0xFF, .semantic_1 = 0xFF, .semantic_2 = 0xFF, .semantic_3 = 0xFF },
			{ .semantic_0 = 0xFF, .semantic_1 = 0xFF, .semantic_2 = 0xFF, .semantic_3 = 0xFF },
			{ .semantic_0 = 0xFF, .semantic_1 = 0xFF, .semantic_2 = 0xFF, .semantic_3 = 0xFF },
			{ .semantic_0 = 0xFF, .semantic_1 = 0xFF, .semantic_2 = 0xFF, .semantic_3 = 0xFF },
		},
		.sq_vtx_semantic_clear = ~0xF,
		.num_sq_vtx_semantic = 4,
		{
			2, 4, 0, 1, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
		},
		.vgt_vertex_reuse_block_cntl.vtx_reuse_depth = 0xE,
		.vgt_hos_reuse_depth.reuse_depth = 0x10,
	}, /* regs */
	.size = sizeof(VShaderHWNoSkinCode),
	.program = (u8 *)&VShaderHWNoSkinCode,
	.mode = GX2_SHADER_MODE_UNIFORM_BLOCK,
	.loopVarCount = countof(VShaderLoopVars), VShaderLoopVars,
	.gx2rBuffer.flags = GX2R_RESOURCE_LOCKED_READ_ONLY,

};

__attribute__((aligned(GX2_SHADER_ALIGNMENT)))
static struct
{
	u64 cf[128];
	u64 alu[39];         /* 128 */
	u64 alu1[3];         /* 167 */
	u64 alu2[8];         /* 170 */
	u64 alu3[25];        /* 178 */
	u64 alu4[91];        /* 203 */
	u64 alu5[12];        /* 294 */
	u64 alu6[1];         /* 306 */
	u64 alu7[8];         /* 307 */
	u64 alu8[6];         /* 315 */
	u64 alu9[12];        /* 321 */
	u64 alu10[3];        /* 333 */
	u64 alu11[5];        /* 336 */
	u64 alu12[3];        /* 341 */
	u64 alu13[1];        /* 344 */
	u64 alu14[11];       /* 345 */
	u64 alu15[13];       /* 356 */
	u64 alu16[1];        /* 369 */
	u64 alu17[2];        /* 370 */
	u64 alu18[3];        /* 372 */
	u64 alu19[20];       /* 375 */
	u64 alu20[13];       /* 395 */
	u64 alu21[17];       /* 408 */
	u64 alu22[18];       /* 425 */
	u64 alu23[3];        /* 443 */
	u64 alu24[9];        /* 446 */
	u64 alu25[1];        /* 455 */
	u64 alu26[23];       /* 456 */
	u64 alu27[1];        /* 479 */
	u64 alu28[4];        /* 480 */
	u64 alu29[4];        /* 484 */
	u64 alu30[3];        /* 488 */
	u64 alu31[1];        /* 491 */
	u64 alu32[2];        /* 492 */
	u64 alu33[2];        /* 494 */
	u64 alu34[1];        /* 496 */
	u64 alu35[3];        /* 497 */
	u64 alu36[3];        /* 500 */
	u64 alu37[9];        /* 503 */
	u64 alu38[3];        /* 512 */
	u64 alu39[2];        /* 515 */
	u64 alu40[1];        /* 517 */
	u64 alu41[3];        /* 518 */
	u64 alu42[3];        /* 521 */
	u64 alu43[9];        /* 524 */
	u64 alu44[2];        /* 533 */
	u64 alu45[1];        /* 535 */
	u64 alu46[2];        /* 536 */
	u64 alu47[8];        /* 538 */
	u64 alu48[3];        /* 546 */
	u64 alu49[1];        /* 549 */
	u64 alu50[14];       /* 550 */
	u64 alu51[5];        /* 564 */
	u64 alu52[15];       /* 569 */
	u64 alu53[14];       /* 584 */
	u64 alu54[3];        /* 598 */
	u64 alu55[3];        /* 601 */
	u64 alu56[37];       /* 604 */
	u64 alu57[2];        /* 641 */
	u64 alu58[8];        /* 643 */
	u64 alu59[5];        /* 651 */
	u64 tex60[1 * 2];    /* 656 */
	u64 tex61[2 * 2];    /* 658 */
	u64 tex62[1 * 2];    /* 662 */
	u64 tex63[1 * 2];    /* 664 */
	u64 tex64[1 * 2];    /* 666 */
	u64 tex65[1 * 2];    /* 668 */
	u64 tex66[2 * 2];    /* 670 */
	u64 tex67[2 * 2];    /* 674 */
	u64 tex68[1 * 2];    /* 678 */
	u64 tex69[2 * 2];    /* 680 */
} VShaderHWSkinCode =
{
	{
		CALL_FS NO_BARRIER,
		ALU(128,39) KCACHE0(CB4, _0_15) KCACHE1(CB3, _0_15),
		LOOP_START_DX10(9),
			ALU_BREAK(167,3) KCACHE0(CB4, _0_15),
			TEX(656,1),
			ALU(170,8),
			TEX(658,2),
			ALU(178,25),
			LOOP_END(3),
		ALU_PUSH_BEFORE(203,91) KCACHE0(CB1, _0_15) KCACHE1(CB4, _0_15),
		JUMP(0,49),
		ALU(294,12) KCACHE0(CB1, _16_31) KCACHE1(CB2, _0_15),
		ALU_PUSH_BEFORE(306,1) KCACHE0(CB4, _0_15),
		JUMP(1, 16),
		ALU(307,8) KCACHE0(CB4, _0_15) KCACHE1(CB1, _16_31),
		ALU_POP_AFTER(315,6) KCACHE0(CB2, _0_15),
		ALU(321,12) KCACHE0(CB2, _0_15),
		LOOP_START_DX10(48),
			ALU_BREAK(333,3),
			TEX(662,1),
			ALU_PUSH_BEFORE(336,5),
			JUMP(1, 47),
			TEX(664,1),
			ALU(341,3),
			TEX(666,1),
			ALU_PUSH_BEFORE(344,1),
			JUMP(0,30),
			ALU(345,11),
			TEX(668,1),
			ALU(356,13),
			ELSE(1, 32),
			ALU_POP_AFTER(369,1),
			ALU_PUSH_BEFORE(370,2),
			JUMP(1, 37),
			ALU(372,3),
			TEX(670,2),
			ALU_POP_AFTER(375,20),
			ALU(395,13),
			TEX(674,2),
			ALU_PUSH_BEFORE(408,17) KCACHE0(CB2, _0_15),
			JUMP(1, 46),
			ALU_PUSH_BEFORE(425,18),
			JUMP(2, 46),
			ALU(443,3),
			TEX(678,1),
			ALU_POP2_AFTER(446,9) KCACHE0(CB2, _0_15),
			ALU_POP_AFTER(455,1),
			LOOP_END(18),
		ALU(456,23) KCACHE0(CB4, _0_15),
		ELSE(1, 56),
		ALU_PUSH_BEFORE(479,1) KCACHE0(CB4, _0_15),
		JUMP(0,53),
		ALU(480,4) KCACHE0(CB1, _16_31),
		ELSE(1, 55),
		ALU_POP_AFTER(484,4),
		ALU_POP_AFTER(488,3),
		ALU_PUSH_BEFORE(491,1) KCACHE0(CB4, _0_15),
		JUMP(1, 109),
		ALU_PUSH_BEFORE(492,2) KCACHE0(CB4, _0_15),
		JUMP(0,69),
		ALU_PUSH_BEFORE(494,2) KCACHE0(CB4, _0_15),
		JUMP(0,67),
		ALU_PUSH_BEFORE(496,1) KCACHE0(CB4, _0_15),
		JUMP(0,65),
		ALU(497,3) KCACHE0(CB1, _16_31),
		ELSE(1, 67),
		ALU_POP_AFTER(500,3),
		ELSE(1, 69),
		ALU_POP_AFTER(503,9) KCACHE0(CB1, _16_31) KCACHE1(CB4, _0_15),
		ELSE(1, 108),
		ALU_PUSH_BEFORE(512,3) KCACHE0(CB4, _0_15),
		JUMP(0,81),
		ALU_PUSH_BEFORE(515,2) KCACHE0(CB4, _0_15),
		JUMP(0,79),
		ALU_PUSH_BEFORE(517,1) KCACHE0(CB4, _0_15),
		JUMP(0,77),
		ALU(518,3) KCACHE0(CB1, _16_31),
		ELSE(1, 79),
		ALU_POP_AFTER(521,3),
		ELSE(1, 81),
		ALU_POP_AFTER(524,9) KCACHE0(CB1, _16_31) KCACHE1(CB4, _0_15),
		ELSE(0,107),
		ALU_PUSH_BEFORE(533,2) KCACHE0(CB4, _0_15),
		JUMP(0,101),
		ALU_PUSH_BEFORE(535,1) KCACHE0(CB4, _0_15),
		JUMP(1, 100),
		ALU_PUSH_BEFORE(536,2) KCACHE0(CB4, _0_15),
		JUMP(0,89),
		ALU(538,8) KCACHE0(CB4, _0_15),
		ELSE(0,99),
		ALU_PUSH_BEFORE(546,3) KCACHE0(CB4, _0_15),
		JUMP(0,97),
		ALU_PUSH_BEFORE(549,1) KCACHE0(CB4, _0_15),
		JUMP(0,95),
		ALU(550,14) KCACHE0(CB4, _0_15),
		ELSE(1, 97),
		ALU_POP_AFTER(564,5),
		ELSE(1, 99),
		ALU_POP_AFTER(569,15) KCACHE0(CB4, _0_15),
		POP(2, 100),
		ALU(584,14) KCACHE0(CB1, _0_31),
		ELSE(1, 107),
		ALU_PUSH_BEFORE(598,3) KCACHE0(CB4, _0_15),
		JUMP(5, 109),
		ALU(601,3) KCACHE0(CB4, _0_15),
		TEX(680,2),
		ALU_POP2_AFTER(604,37) KCACHE0(CB1, _16_31),
		POP(2, 108),
		POP(1, 109),
		ALU_PUSH_BEFORE(641,2) KCACHE0(CB1, _16_31) KCACHE1(CB4, _0_15),
		JUMP(1, 112),
		ALU_POP_AFTER(643,8) KCACHE0(CB1, _16_31),
		ALU(651,2) KCACHE0(CB1, _16_31),
		EXP_DONE(POS0, _R12,_x,_y,_z,_w),
		EXP(PARAM0, _R5,_x,_y,_z,_w) NO_BARRIER,
		EXP(PARAM1, _R7,_x,_y,_z,_w) NO_BARRIER,
		EXP(PARAM2, _R9,_x,_y,_z,_w) NO_BARRIER,
		EXP_DONE(PARAM3, _R0,_x,_y,_y,_y) NO_BARRIER
		END_OF_PROGRAM
	},
	{
		/* 0 */
		ALU_CNDE_INT(_R0,_y, KC0(5),_w, ALU_SRC_0,_x, _R2,_x),
		ALU_MOV(_R0,_z, ALU_SRC_LITERAL,_x),
		ALU_MOV(_R3,_w, ALU_SRC_LITERAL,_y)
		ALU_LAST,
		ALU_LITERAL2(0x00000003, 0x3F800000),
		/* 1 */
		ALU_CNDE_INT(_R7,_y, KC0(5),_w, ALU_SRC_0,_x, _R2,_y),
		ALU_MOV(_R4,_z, ALU_SRC_LITERAL,_x),
		ALU_CNDE_INT(_R0,_w, KC0(5),_w, ALU_SRC_LITERAL,_y, _R2,_z)
		ALU_LAST,
		ALU_LITERAL2(0x00000003, 0x3F800000),
		/* 2 */
		ALU_MOV(_R2,_w, ALU_SRC_LITERAL,_x)
		ALU_LAST,
		ALU_LITERAL(0x3F800000),
		/* 3 */
		ALU_MOV(_R18,_x, _R5,_x)
		ALU_LAST,
		/* 4 */
		ALU_MOV(_R19,_x, _R5,_y)
		ALU_LAST,
		/* 5 */
		ALU_MOV(_R20,_x, _R5,_z)
		ALU_LAST,
		/* 6 */
		ALU_MOV(_R21,_x, _R5,_w)
		ALU_LAST,
		/* 7 */
		ALU_MOV(_R22,_x, _R6,_x)
		ALU_LAST,
		/* 8 */
		ALU_MOV(_R23,_x, _R6,_y)
		ALU_LAST,
		/* 9 */
		ALU_MOV(_R24,_x, _R6,_z)
		ALU_LAST,
		/* 10 */
		ALU_MOV(_R25,_x, _R6,_w)
		ALU_LAST,
		/* 11 */
		ALU_MUL(_R8,_x, _R5,_x, KC1(0),_x),
		ALU_MUL(_R8,_y, _R5,_x, KC1(0),_y),
		ALU_MUL(_R8,_z, _R5,_x, KC1(0),_z),
		ALU_MUL(_R8,_w, _R5,_x, KC1(0),_w),
		ALU_MOV(_R13,_x, ALU_SRC_LITERAL,_x)
		ALU_LAST,
		ALU_LITERAL(0x00000001),
		/* 12 */
		ALU_MUL(_R12,_x, _R5,_x, KC1(1),_x),
		ALU_MUL(_R12,_y, _R5,_x, KC1(1),_y),
		ALU_MUL(_R12,_z, _R5,_x, KC1(1),_z),
		ALU_MUL(_R12,_w, _R5,_x, KC1(1),_w),
		ALU_MOV(_R11,_x, ALU_SRC_0,_x)
		ALU_LAST,
		/* 13 */
		ALU_MUL(_R10,_x, _R5,_x, KC1(2),_x),
		ALU_MUL(_R10,_y, _R5,_x, KC1(2),_y),
		ALU_MUL(_R10,_z, _R5,_x, KC1(2),_z),
		ALU_MUL(_R10,_w, _R5,_x, KC1(2),_w),
		ALU_MOV(_R11,_y, ALU_SRC_0,_x)
		ALU_LAST,
		/* 14 */
		ALU_CNDE_INT(_R14,_x, KC0(6),_x, _R0,_y, _R0 _NEG,_y) VEC_201,
		ALU_NOP(__,_y),
		ALU_MOV(_R11,_z, ALU_SRC_0,_x),
		ALU_CNDE_INT(_R13,_y, KC0(6),_x, _R7,_y, _R7 _NEG,_y) VEC_021
		ALU_LAST,
		/* 15 */
		ALU_CNDE_INT(_R13,_z, KC0(6),_x, _R0,_w, _R0 _NEG,_w)
		ALU_LAST,
	},
	{
		/* 16 */
		ALU_ADD_INT(__,_x, KC0(8),_y, ALU_SRC_1_INT,_x)
		ALU_LAST,
		/* 17 */
		ALU_SETGT_INT(_R0,_y, ALU_SRC_PV,_x, _R13,_x)
		ALU_LAST,
		/* 18 */
		ALU_PRED_SETNE_INT(__,_x, _R0,_y, ALU_SRC_0,_x) UPDATE_EXEC_MASK(DEACTIVATE) UPDATE_PRED
		ALU_LAST,
	},
	{
		/* 19 */
		ALU_MOV(_R0,_x, _R13,_x),
		ALU_ADD_INT(_R0,_y, _R0,_z, ALU_SRC_LITERAL,_x),
		ALU_ADD_INT(_R0,_z, _R4,_z, ALU_SRC_LITERAL,_y) VEC_120,
		ALU_ADD_INT(_R0,_w, _R0,_z, ALU_SRC_1_INT,_x),
		ALU_ADD_INT(_R13,_x, _R13,_x, ALU_SRC_1_INT,_x)
		ALU_LAST,
		ALU_LITERAL2(0x00000002, 0x00000003),
		/* 20 */
		ALU_MOVA_INT(__,_x, _R0,_x)
		ALU_LAST,
		/* 21 */
		ALU_MOV(_R0,_x, _R18 AR,_x)
		ALU_LAST,
	},
	{
		/* 22 */
		ALU_MUL(_R127,_x, _R0,_x, _R5,_w) VEC_201,
		ALU_MUL(_R127,_y, _R0,_x, _R5,_y) VEC_201,
		ALU_MUL(_R127,_z, _R0,_x, _R5,_z) VEC_201,
		ALU_MUL(_R127,_w, _R0,_x, _R5,_x) VEC_201,
		ALU_MUL(_R126,_y, _R0,_x, _R6,_x)
		ALU_LAST,
		/* 23 */
		ALU_MUL(_R126,_x, _R0,_x, _R6,_z),
		ALU_MUL(_R125,_y, _R0,_x, _R7,_x),
		ALU_MUL(_R126,_z, _R0,_x, _R6,_y) VEC_021,
		ALU_MUL(_R126,_w, _R0,_x, _R6,_w),
		ALU_MUL(_R125,_x, _R0,_x, _R7,_y)
		ALU_LAST,
		/* 24 */
		ALU_ADD(_R8,_x, _R8,_x, _R127,_w),
		ALU_MUL(_R127,_y, _R0,_x, _R7,_w) VEC_120,
		ALU_ADD(_R8,_z, _R8,_z, _R127,_z),
		ALU_MUL(_R127,_w, _R0,_x, _R7,_z) VEC_120,
		ALU_ADD(_R8,_y, _R8,_y, _R127,_y)
		ALU_LAST,
		/* 25 */
		ALU_ADD(_R12,_x, _R12,_x, _R126,_y),
		ALU_ADD(_R12,_y, _R12,_y, _R126,_z),
		ALU_ADD(_R12,_z, _R12,_z, _R126,_x),
		ALU_ADD(_R8,_w, _R8,_w, _R127,_x) VEC_021,
		ALU_ADD(_R12,_w, _R12,_w, _R126,_w)
		ALU_LAST,
		/* 26 */
		ALU_ADD(_R10,_x, _R10,_x, _R125,_y),
		ALU_ADD(_R10,_y, _R10,_y, _R125,_x),
		ALU_ADD(_R10,_z, _R10,_z, _R127,_w),
		ALU_ADD(_R10,_w, _R10,_w, _R127,_y) VEC_021
		ALU_LAST,
		/* 27 */
		ALU_MOV(_R4,_z, _R0,_z)
		ALU_LAST,
	},
	{
		/* 28 */
		ALU_DOT4(__,_x, _R3,_x, _R8,_x),
		ALU_DOT4(__,_y, _R3,_y, _R8,_y),
		ALU_DOT4(__,_z, _R3,_z, _R8,_z),
		ALU_DOT4(__,_w, _R3,_w, _R8,_w),
		ALU_MUL(_R127,_w, KC0(3),_x, ALU_SRC_1,_x)
		ALU_LAST,
		/* 29 */
		ALU_DOT4(__,_x, _R3,_x, _R12,_x),
		ALU_DOT4(__,_y, _R3,_y, _R12,_y),
		ALU_DOT4(__,_z, _R3,_z, _R12,_z),
		ALU_DOT4(__,_w, _R3,_w, _R12,_w),
		ALU_MOV(_R0,_x, ALU_SRC_PV,_x)
		ALU_LAST,
		/* 30 */
		ALU_DOT4(__,_x, _R3,_x, _R10,_x),
		ALU_DOT4(__,_y, _R3,_y, _R10,_y),
		ALU_DOT4(__,_z, _R3,_z, _R10,_z),
		ALU_DOT4(__,_w, _R3,_w, _R10,_w),
		ALU_MOV(_R0,_y, ALU_SRC_PV,_x)
		ALU_LAST,
		/* 31 */
		ALU_MUL(_R127,_x, KC0(3),_y, ALU_SRC_1,_x),
		ALU_MUL(_R127,_y, KC0(3),_z, ALU_SRC_1,_x),
		ALU_MOV(_R0,_z, ALU_SRC_PV,_x),
		ALU_MUL(_R126,_w, KC0(3),_w, ALU_SRC_1,_x),
		ALU_MUL(__,_x, _R13,_z, _R8,_z)
		ALU_LAST,
		/* 32 */
		ALU_DOT4(_R6,_x, _R0,_x, KC0(11),_x),
		ALU_DOT4(__,_y, _R0,_y, KC0(11),_y),
		ALU_DOT4(__,_z, ALU_SRC_PV,_z, KC0(11),_z),
		ALU_DOT4(__,_w, _R3,_w, KC0(11),_w),
		ALU_MULADD(_R122,_x, _R13,_y, _R8,_y, ALU_SRC_PS,_x)
		ALU_LAST,
		/* 33 */
		ALU_DOT4(__,_x, _R0,_x, KC0(12),_x),
		ALU_DOT4(_R5,_y, _R0,_y, KC0(12),_y),
		ALU_DOT4(__,_z, _R0,_z, KC0(12),_z),
		ALU_DOT4(__,_w, _R3,_w, KC0(12),_w),
		ALU_MULADD(_R125,_x, _R14,_x, _R8,_x, ALU_SRC_PS,_x)
		ALU_LAST,
		/* 34 */
		ALU_DOT4(__,_x, _R0,_x, KC0(13),_x),
		ALU_DOT4(__,_y, _R0,_y, KC0(13),_y),
		ALU_DOT4(_R4,_z, _R0,_z, KC0(13),_z),
		ALU_DOT4(__,_w, _R3,_w, KC0(13),_w),
		ALU_MOV(_R0,_w, ALU_SRC_LITERAL,_x)
		ALU_LAST,
		ALU_LITERAL(0x3F800000),
		/* 35 */
		ALU_DOT4(__,_x, _R6,_x, KC0(10),_x),
		ALU_DOT4(_R6,_y, _R5,_y, KC0(10),_y),
		ALU_DOT4(__,_z, ALU_SRC_PV,_x, KC0(10),_z),
		ALU_DOT4(__,_w, ALU_SRC_PS,_x, KC0(10),_w),
		ALU_MUL(__,_x, _R13,_z, _R12,_z)
		ALU_LAST,
		/* 36 */
		ALU_DOT4(_R126,_x, _R6,_x, KC0(9),_x),
		ALU_DOT4(__,_y, _R5,_y, KC0(9),_y),
		ALU_DOT4(__,_z, _R4,_z, KC0(9),_z),
		ALU_DOT4(__,_w, _R0,_w, KC0(9),_w),
		ALU_MULADD(_R122,_x, _R13,_y, _R12,_y, ALU_SRC_PS,_x)
		ALU_LAST,
		/* 37 */
		ALU_MULADD(_R127,_x, _R6,_y, KC0(2),_w, _R126,_w),
		ALU_MULADD(_R127,_y, _R6,_y, KC0(2),_z, _R127,_y),
		ALU_MULADD(_R127,_z, _R6,_y, KC0(2),_y, _R127,_x) VEC_120,
		ALU_MULADD(_R127,_w, _R6,_y, KC0(2),_x, _R127,_w) VEC_021,
		ALU_MULADD(_R126,_y, _R14,_x, _R12,_x, ALU_SRC_PS,_x)
		ALU_LAST,
		/* 38 */
		ALU_DOT4(__,_x, _R6,_x, KC0(8),_x),
		ALU_DOT4(__,_y, _R5,_y, KC0(8),_y),
		ALU_DOT4(_R126,_z, _R4,_z, KC0(8),_z),
		ALU_DOT4(__,_w, _R0,_w, KC0(8),_w),
		ALU_MUL(__,_x, _R13,_z, _R10,_z)
		ALU_LAST,
		/* 39 */
		ALU_MULADD(_R123,_x, _R126,_x, KC0(1),_w, _R127,_x),
		ALU_MULADD(_R123,_y, _R126,_x, KC0(1),_z, _R127,_y) VEC_120,
		ALU_MULADD(_R123,_z, _R126,_x, KC0(1),_y, _R127,_z),
		ALU_MULADD(_R123,_w, _R126,_x, KC0(1),_x, _R127,_w),
		ALU_MULADD(_R122,_x, _R13,_y, _R10,_y, ALU_SRC_PS,_x)
		ALU_LAST,
		/* 40 */
		ALU_MULADD(_R12,_x, _R126,_z, KC0(0),_x, ALU_SRC_PV,_w),
		ALU_MULADD(_R12,_y, _R126,_z, KC0(0),_y, ALU_SRC_PV,_z),
		ALU_MULADD(_R12,_z, _R126,_z, KC0(0),_z, ALU_SRC_PV,_y),
		ALU_MULADD(_R12,_w, _R126,_z, KC0(0),_w, ALU_SRC_PV,_x),
		ALU_MULADD(_R126,_z, _R14,_x, _R10,_x, ALU_SRC_PS,_x)
		ALU_LAST,
		/* 41 */
		ALU_DOT4(_R127,_x, _R125,_x, KC0(11),_x),
		ALU_DOT4(__,_y, _R126,_y, KC0(11),_y),
		ALU_DOT4(__,_z, ALU_SRC_PS,_x, KC0(11),_z),
		ALU_DOT4(__,_w, ALU_SRC_LITERAL,_x, ALU_SRC_0,_x)
		ALU_LAST,
		ALU_LITERAL(0x80000000),
		/* 42 */
		ALU_DOT4(__,_x, _R125,_x, KC0(12),_x),
		ALU_DOT4(_R127,_y, _R126,_y, KC0(12),_y),
		ALU_DOT4(__,_z, _R126,_z, KC0(12),_z),
		ALU_DOT4(__,_w, ALU_SRC_LITERAL,_x, ALU_SRC_0,_x)
		ALU_LAST,
		ALU_LITERAL(0x80000000),
		/* 43 */
		ALU_DOT4(__,_x, _R125,_x, KC0(13),_x),
		ALU_DOT4(__,_y, _R126,_y, KC0(13),_y),
		ALU_DOT4(_R126,_z, _R126,_z, KC0(13),_z),
		ALU_DOT4(__,_w, ALU_SRC_LITERAL,_x, ALU_SRC_0,_x)
		ALU_LAST,
		ALU_LITERAL(0x80000000),
		/* 44 */
		ALU_DOT4_IEEE(__,_x, _R127,_x, _R127,_x),
		ALU_DOT4_IEEE(__,_y, _R127,_y, _R127,_y),
		ALU_DOT4_IEEE(__,_z, ALU_SRC_PV,_x, ALU_SRC_PV,_x),
		ALU_DOT4_IEEE(__,_w, ALU_SRC_LITERAL,_x, ALU_SRC_0,_x)
		ALU_LAST,
		ALU_LITERAL(0x80000000),
		/* 45 */
		ALU_RECIPSQRT_IEEE(__,_x, ALU_SRC_PV,_x) SCL_210
		ALU_LAST,
		/* 46 */
		ALU_MUL(_R16,_x, _R127,_x, ALU_SRC_PS,_x),
		ALU_MUL(_R16,_y, _R127,_y, ALU_SRC_PS,_x),
		ALU_MUL(_R6,_z, _R126,_z, ALU_SRC_PS,_x)
		ALU_LAST,
		/* 47 */
		ALU_PRED_SETNE_INT(__,_x, KC1(9),_y, ALU_SRC_0,_x) UPDATE_EXEC_MASK(DEACTIVATE) UPDATE_PRED
		ALU_LAST,
	},
	{
		/* 48 */
		ALU_MOV(_R13,_x, KC0(4),_x),
		ALU_MOV(_R13,_y, KC0(4),_y),
		ALU_MOV(_R13,_z, KC0(4),_z),
		ALU_MOV(_R13,_w, KC0(4),_w)
		ALU_LAST,
		/* 49 */
		ALU_MOV(_R8,_x, KC1(1),_x),
		ALU_MOV(_R8,_y, KC1(1),_y),
		ALU_MOV(_R8,_z, KC1(1),_z),
		ALU_MOV(_R0,_w, KC1(1),_w)
		ALU_LAST,
		/* 50 */
		ALU_MOV(_R7,_x, KC1(2),_x),
		ALU_MOV(_R7,_y, KC1(2),_y),
		ALU_MOV(_R7,_z, KC1(2),_z),
		ALU_MOV(_R0,_w, KC1(2),_w)
		ALU_LAST,
	},
	{
		/* 51 */
		ALU_PRED_SETNE_INT(__,_x, KC0(4),_w, ALU_SRC_0,_x) UPDATE_EXEC_MASK(DEACTIVATE) UPDATE_PRED
		ALU_LAST,
	},
	{
		/* 52 */
		ALU_AND_INT(_R0,_x, KC0(8),_w, ALU_SRC_LITERAL,_x),
		ALU_AND_INT(__,_y, KC0(8),_w, ALU_SRC_1_INT,_x),
		ALU_AND_INT(_R0,_w, KC0(8),_w, ALU_SRC_LITERAL,_y)
		ALU_LAST,
		ALU_LITERAL2(0x00000002, 0x00000004),
		/* 53 */
		ALU_CNDE_INT(_R13,_x, ALU_SRC_PV,_y, KC1(4),_x, _R1,_x),
		ALU_CNDE_INT(_R13,_y, ALU_SRC_PV,_y, KC1(4),_y, _R1,_y),
		ALU_CNDE_INT(_R13,_z, ALU_SRC_PV,_y, KC1(4),_z, _R1,_z),
		ALU_CNDE_INT(_R13,_w, ALU_SRC_PV,_y, KC1(4),_w, _R1,_w)
		ALU_LAST,
	},
	{
		/* 54 */
		ALU_CNDE_INT(_R8,_x, _R0,_x, KC0(1),_x, _R1,_x),
		ALU_CNDE_INT(_R8,_y, _R0,_x, KC0(1),_y, _R1,_y),
		ALU_CNDE_INT(_R8,_z, _R0,_x, KC0(1),_z, _R1,_z)
		ALU_LAST,
		/* 55 */
		ALU_CNDE_INT(_R7,_x, _R0,_w, KC0(2),_x, _R1,_x),
		ALU_CNDE_INT(_R7,_y, _R0,_w, KC0(2),_y, _R1,_y),
		ALU_CNDE_INT(_R7,_z, _R0,_w, KC0(2),_z, _R1,_z)
		ALU_LAST,
	},
	{
		/* 56 */
		ALU_MOV(__,_x, ALU_SRC_0,_x),
		ALU_MOV(__,_y, KC0(3),_z),
		ALU_MOV(__,_z, KC0(3),_y),
		ALU_MOV(__,_w, KC0(3),_x),
		ALU_MOV(_R10,_x, ALU_SRC_0,_x)
		ALU_LAST,
		/* 57 */
		ALU_MULADD(_R14,_x, _R13,_x, KC0(0),_x, ALU_SRC_PV,_w),
		ALU_MULADD(_R14,_y, _R13,_y, KC0(0),_y, ALU_SRC_PV,_z),
		ALU_MULADD(_R14,_z, _R13,_z, KC0(0),_z, ALU_SRC_PV,_y),
		ALU_MULADD(_R14,_w, _R13,_w, KC0(0),_w, ALU_SRC_PV,_x),
		ALU_MOV(_R10,_y, ALU_SRC_0,_x)
		ALU_LAST,
		/* 58 */
		ALU_MOV(_R10,_z, ALU_SRC_0,_x),
		ALU_MOV(_R4,_w, ALU_SRC_0,_x)
		ALU_LAST,
	},
	{
		/* 59 */
		ALU_SETGT_INT(_R0,_w, ALU_SRC_LITERAL,_x, _R4,_w)
		ALU_LAST,
		ALU_LITERAL(0x00000004),
		/* 60 */
		ALU_PRED_SETNE_INT(__,_x, _R0,_w, ALU_SRC_0,_x) UPDATE_EXEC_MASK(DEACTIVATE) UPDATE_PRED
		ALU_LAST,
	},
	{
		/* 61 */
		ALU_CNDE_INT(_R0,_x, _R0,_z, ALU_SRC_LITERAL,_x, ALU_SRC_0,_x),
		ALU_ADD_INT(__,_y, _R4,_w, ALU_SRC_1_INT,_x)
		ALU_LAST,
		ALU_LITERAL(0x3F800000),
		/* 62 */
		ALU_CNDE_INT(_R4,_w, _R0,_z, ALU_SRC_PV,_y, _R4,_w)
		ALU_LAST,
		/* 63 */
		ALU_PRED_SETE_INT(__,_x, _R0,_x, ALU_SRC_0,_x) UPDATE_EXEC_MASK(DEACTIVATE) UPDATE_PRED
		ALU_LAST,
	},
	{
		/* 64 */
		ALU_SETE_INT(_R0,_z, _R17,_y, ALU_SRC_0,_x),
		ALU_ADD_INT(_R0,_w, _R4,_w, ALU_SRC_LITERAL,_x)
		ALU_LAST,
		ALU_LITERAL(0x00000004),
	},
	{
		/* 65 */
		ALU_PRED_SETE_INT(__,_x, _R0,_z, ALU_SRC_0,_x) UPDATE_EXEC_MASK(DEACTIVATE) UPDATE_PRED
		ALU_LAST,
	},
	{
		/* 66 */
		ALU_ADD(_R5,_x, _R6 _NEG,_x, _R15,_x),
		ALU_ADD_INT(_R0,_y, _R4,_w, ALU_SRC_LITERAL,_x),
		ALU_ADD(_R0,_z, _R5 _NEG,_y, _R15,_y),
		ALU_ADD(_R1,_w, _R4 _NEG,_z, _R15,_z),
		ALU_MOV(_R0,_x, ALU_SRC_LITERAL,_y)
		ALU_LAST,
		ALU_LITERAL2(0x0000000C, 0x3F800000),
		/* 67 */
		ALU_DOT4(__,_x, ALU_SRC_PV,_x, ALU_SRC_PV,_x),
		ALU_DOT4(__,_y, ALU_SRC_PV,_z, ALU_SRC_PV,_z),
		ALU_DOT4(__,_z, ALU_SRC_PV,_w, ALU_SRC_PV,_w),
		ALU_DOT4(_R0,_w, ALU_SRC_LITERAL,_x, ALU_SRC_0,_x)
		ALU_LAST,
		ALU_LITERAL(0x80000000),
	},
	{
		/* 68 */
		ALU_SQRT_IEEE(__,_x, _R0,_w) SCL_210
		ALU_LAST,
		/* 69 */
		ALU_MOV(__,_y, ALU_SRC_PS,_x),
		ALU_MUL(__,_z, ALU_SRC_PS,_x, ALU_SRC_PS,_x),
		ALU_RECIP_IEEE(_R127,_w, ALU_SRC_PS,_x) SCL_210
		ALU_LAST,
		/* 70 */
		ALU_DOT4(__,_x, _R0,_x, _R1,_x),
		ALU_DOT4(__,_y, ALU_SRC_PV,_y, _R1,_y),
		ALU_DOT4(__,_z, ALU_SRC_PV,_z, _R1,_z),
		ALU_DOT4(__,_w, ALU_SRC_LITERAL,_x, ALU_SRC_0,_x),
		ALU_MUL_IEEE(_R15,_x, _R5,_x, ALU_SRC_PS,_x)
		ALU_LAST,
		ALU_LITERAL(0x80000000),
		/* 71 */
		ALU_MUL_IEEE(_R15,_y, _R0,_z, _R127,_w),
		ALU_MUL_IEEE(_R15,_z, _R1,_w, _R127,_w),
		ALU_RECIP_IEEE(_R1,_w, ALU_SRC_PV,_x) CLAMP SCL_210
		ALU_LAST,
	},
	{
		/* 72 */
		ALU_MOV(_R1,_w, _R2,_w)
		ALU_LAST,
	},
	{
		/* 73 */
		ALU_PRED_SETGE_INT(__,_x, _R17,_y, ALU_SRC_LITERAL,_x) UPDATE_EXEC_MASK(DEACTIVATE) UPDATE_PRED
		ALU_LAST,
		ALU_LITERAL(0x00000002),
	},
	{
		/* 74 */
		ALU_ADD_INT(_R0,_z, _R4,_w, ALU_SRC_LITERAL,_x),
		ALU_ADD_INT(_R0,_w, _R4,_w, ALU_SRC_LITERAL,_y)
		ALU_LAST,
		ALU_LITERAL2(0x00000010, 0x00000008),
	},
	{
		/* 75 */
		ALU_DOT4_IEEE(__,_x, _R1,_x, _R1,_x),
		ALU_DOT4_IEEE(__,_y, _R1,_y, _R1,_y),
		ALU_DOT4_IEEE(__,_z, _R1,_z, _R1,_z),
		ALU_DOT4_IEEE(__,_w, ALU_SRC_LITERAL,_x, ALU_SRC_0,_x)
		ALU_LAST,
		ALU_LITERAL(0x80000000),
		/* 76 */
		ALU_RECIPSQRT_IEEE(__,_x, ALU_SRC_PV,_x) SCL_210
		ALU_LAST,
		/* 77 */
		ALU_MUL(__,_x, _R1,_x, ALU_SRC_PS,_x),
		ALU_MUL(__,_y, _R1,_y, ALU_SRC_PS,_x),
		ALU_MUL(__,_z, _R1,_z, ALU_SRC_PS,_x)
		ALU_LAST,
		/* 78 */
		ALU_DOT4(__,_x, _R15,_x, ALU_SRC_PV,_x),
		ALU_DOT4(__,_y, _R15,_y, ALU_SRC_PV,_y),
		ALU_DOT4(__,_z, _R15,_z, ALU_SRC_PV,_z),
		ALU_DOT4(__,_w, ALU_SRC_LITERAL,_x, ALU_SRC_0,_x)
		ALU_LAST,
		ALU_LITERAL(0x80000000),
		/* 79 */
		ALU_SETGE_DX10(_R127,_x, ALU_SRC_PV,_x, _R0,_x),
		ALU_LOG_CLAMPED(__,_x, ALU_SRC_PV,_x) SCL_210
		ALU_LAST,
		/* 80 */
		ALU_MUL(__,_z, _R0,_y, ALU_SRC_PS,_x)
		ALU_LAST,
		/* 81 */
		ALU_EXP_IEEE(__,_x, ALU_SRC_PV,_z) SCL_210
		ALU_LAST,
		/* 82 */
		ALU_MUL(__,_x, _R1,_w, ALU_SRC_PS,_x)
		ALU_LAST,
		/* 83 */
		ALU_CNDE_INT(_R1,_w, _R127,_x, ALU_SRC_0,_x, ALU_SRC_PV,_x)
		ALU_LAST,
	},
	{
		/* 84 */
		ALU_SETE_INT(_R5,_x, _R17,_x, ALU_SRC_LITERAL,_x),
		ALU_ADD_INT(_R0,_y, _R4,_w, ALU_SRC_LITERAL,_y),
		ALU_ADD_INT(_R0,_w, _R4,_w, ALU_SRC_LITERAL,_z),
		ALU_MUL(__,_x, _R6,_z, _R15,_z)
		ALU_LAST,
		ALU_LITERAL3(0x00000002, 0x00000014, 0x00000018),
		/* 85 */
		ALU_DOT4(__,_x, _R16,_x, _R15,_x),
		ALU_DOT4(__,_y, _R16,_y, _R15,_y),
		ALU_DOT4(__,_z, ALU_SRC_PS,_x, ALU_SRC_1,_x),
		ALU_DOT4(__,_w, ALU_SRC_LITERAL,_x, ALU_SRC_0,_x)
		ALU_LAST,
		ALU_LITERAL(0x80000000),
		/* 86 */
		ALU_MAX(_R5,_z, ALU_SRC_PV,_x, ALU_SRC_LITERAL,_x)
		ALU_LAST,
		ALU_LITERAL(0x33D6BF95),
	},
	{
		/* 87 */
		ALU_MUL(_R127,_x, _R8,_x, _R1,_x),
		ALU_MUL(_R127,_z, _R8,_z, _R1,_z),
		ALU_MUL(_R127,_w, _R8,_y, _R1,_y),
		ALU_LOG_CLAMPED(__,_x, _R5,_z) SCL_210
		ALU_LAST,
		/* 88 */
		ALU_MUL(_R126,_x, _R13,_x, _R0,_x),
		ALU_MUL(_R127,_y, _R13,_y, _R0,_y),
		ALU_MUL(_R126,_z, _R13,_z, _R0,_z),
		ALU_MUL(__,_w, KC0(2),_w, ALU_SRC_PS,_x)
		ALU_LAST,
		/* 89 */
		ALU_EXP_IEEE(__,_x, ALU_SRC_PV,_w) SCL_210
		ALU_LAST,
		/* 90 */
		ALU_CNDE_INT(_R123,_y, _R5,_x, _R5,_z, ALU_SRC_PS,_x)
		ALU_LAST,
		/* 91 */
		ALU_MULADD(_R123,_x, ALU_SRC_PV,_y, _R127,_w, _R127,_y),
		ALU_MULADD(_R123,_y, ALU_SRC_PV,_y, _R127,_x, _R126,_x),
		ALU_MULADD(_R123,_w, ALU_SRC_PV,_y, _R127,_z, _R126,_z)
		ALU_LAST,
		/* 92 */
		ALU_MULADD(_R14,_x, _R1,_w, ALU_SRC_PV,_y, _R14,_x),
		ALU_MULADD(_R14,_y, _R1,_w, ALU_SRC_PV,_x, _R14,_y),
		ALU_MULADD(_R14,_z, _R1,_w, ALU_SRC_PV,_w, _R14,_z)
		ALU_LAST,
		/* 93 */
		ALU_PRED_SETNE_INT(__,_x, ALU_SRC_0,_x, _R17,_x) UPDATE_EXEC_MASK(DEACTIVATE) UPDATE_PRED
		ALU_LAST,
	},
	{
		/* 94 */
		ALU_ADD(_R127,_x, _R15,_x, ALU_SRC_0,_x),
		ALU_ADD(_R127,_y, _R15,_y, ALU_SRC_0,_x),
		ALU_ADD(_R127,_z, _R15,_z, ALU_SRC_1,_x)
		ALU_LAST,
		/* 95 */
		ALU_DOT4_IEEE(__,_x, ALU_SRC_PV,_x, ALU_SRC_PV,_x),
		ALU_DOT4_IEEE(__,_y, ALU_SRC_PV,_y, ALU_SRC_PV,_y),
		ALU_DOT4_IEEE(__,_z, ALU_SRC_PV,_z, ALU_SRC_PV,_z),
		ALU_DOT4_IEEE(__,_w, ALU_SRC_LITERAL,_x, ALU_SRC_0,_x)
		ALU_LAST,
		ALU_LITERAL(0x80000000),
		/* 96 */
		ALU_RECIPSQRT_IEEE(__,_x, ALU_SRC_PV,_x) SCL_210
		ALU_LAST,
		/* 97 */
		ALU_MUL(__,_x, _R127,_x, ALU_SRC_PS,_x),
		ALU_MUL(__,_y, _R127,_y, ALU_SRC_PS,_x),
		ALU_MUL(__,_z, _R127,_z, ALU_SRC_PS,_x)
		ALU_LAST,
		/* 98 */
		ALU_DOT4(__,_x, _R16,_x, ALU_SRC_PV,_x),
		ALU_DOT4(__,_y, _R16,_y, ALU_SRC_PV,_y),
		ALU_DOT4(__,_z, _R6,_z, ALU_SRC_PV,_z),
		ALU_DOT4(_R0,_w, ALU_SRC_LITERAL,_x, ALU_SRC_0,_x)
		ALU_LAST,
		ALU_LITERAL(0x80000000),
		/* 99 */
		ALU_PRED_SETGT(__,_x, _R0,_w, ALU_SRC_0,_x) UPDATE_EXEC_MASK(DEACTIVATE) UPDATE_PRED
		ALU_LAST,
	},
	{
		/* 100 */
		ALU_ADD_INT(_R0,_z, _R4,_w, ALU_SRC_LITERAL,_x),
		ALU_LOG_CLAMPED(_R1,_z, _R0,_w) SCL_210
		ALU_LAST,
		ALU_LITERAL(0x0000001C),
	},
	{
		/* 101 */
		ALU_MUL(_R127,_x, _R7,_x, _R0,_x),
		ALU_MUL(__,_y, KC0(2),_w, _R1,_z),
		ALU_MUL(_R127,_z, _R7,_y, _R0,_y),
		ALU_MUL(_R127,_w, _R7,_z, _R0,_z) VEC_021
		ALU_LAST,
		/* 102 */
		ALU_EXP_IEEE(__,_x, ALU_SRC_PV,_y) SCL_210
		ALU_LAST,
		/* 103 */
		ALU_MUL(__,_w, _R1,_w, ALU_SRC_PS,_x)
		ALU_LAST,
		/* 104 */
		ALU_MULADD(_R10,_x, ALU_SRC_PV,_w, _R127,_x, _R10,_x),
		ALU_MULADD(_R10,_y, ALU_SRC_PV,_w, _R127,_z, _R10,_y),
		ALU_MULADD(_R10,_z, ALU_SRC_PV,_w, _R127,_w, _R10,_z)
		ALU_LAST,
	},
	{
		/* 105 */
		ALU_ADD_INT(_R4,_w, _R4,_w, ALU_SRC_1_INT,_x)
		ALU_LAST,
	},
	{
		/* 106 */
		ALU_MOV(_R127,_x, _R14,_z) CLAMP,
		ALU_MOV(_R127,_y, _R14,_y) CLAMP,
		ALU_MOV(_R127,_z, _R14,_x) CLAMP,
		ALU_MOV(_R0,_w, ALU_SRC_0,_x),
		ALU_MOV(__,_x, _R14,_w) CLAMP
		ALU_LAST,
		/* 107 */
		ALU_MOV(_R126,_x, _R14,_z) CLAMP,
		ALU_MOV(_R126,_y, _R14,_y) CLAMP,
		ALU_MOV(_R126,_z, _R14,_x) CLAMP,
		ALU_ADD(_R127,_w, ALU_SRC_PV,_w, ALU_SRC_PS,_x) CLAMP,
		ALU_MOV(_R126,_w, _R14,_w) CLAMP
		ALU_LAST,
		/* 108 */
		ALU_MOV(_R125,_x, _R10,_z) CLAMP,
		ALU_MOV(_R127,_y, _R10,_y) CLAMP,
		ALU_MOV(_R127,_z, _R10,_x) CLAMP,
		ALU_ADD(__,_w, _R10,_x, _R127,_z) CLAMP,
		ALU_ADD(__,_x, _R10,_y, _R127,_y) CLAMP
		ALU_LAST,
		/* 109 */
		ALU_ADD(__,_x, _R10,_z, _R127,_x) CLAMP,
		ALU_CNDE_INT(_R5,_y, KC0(4),_x, ALU_SRC_PS,_x, _R126,_y),
		ALU_CNDE_INT(_R5,_x, KC0(4),_x, ALU_SRC_PV,_w, _R126,_z) VEC_021
		ALU_LAST,
		/* 110 */
		ALU_CNDE_INT(_R7,_x, KC0(4),_x, ALU_SRC_0,_x, _R127,_z),
		ALU_CNDE_INT(_R7,_y, KC0(4),_x, ALU_SRC_0,_x, _R127,_y),
		ALU_CNDE_INT(_R5,_z, KC0(4),_x, ALU_SRC_PV,_x, _R126,_x) VEC_021,
		ALU_CNDE_INT(_R5,_w, KC0(4),_x, _R127,_w, _R126,_w),
		ALU_CNDE_INT(_R7,_z, KC0(4),_x, ALU_SRC_0,_x, _R125,_x) VEC_021
		ALU_LAST,
	},
	{
		/* 111 */
		ALU_PRED_SETE_INT(__,_x, KC0(4),_w, ALU_SRC_0,_x) UPDATE_EXEC_MASK(DEACTIVATE) UPDATE_PRED
		ALU_LAST,
	},
	{
		/* 112 */
		ALU_MOV(_R5,_x, KC0(4),_x),
		ALU_MOV(_R5,_y, KC0(4),_y),
		ALU_MOV(_R5,_z, KC0(4),_z),
		ALU_MOV(_R5,_w, KC0(4),_w)
		ALU_LAST,
	},
	{
		/* 113 */
		ALU_MOV(_R5,_x, _R1,_x),
		ALU_MOV(_R5,_y, _R1,_y),
		ALU_MOV(_R5,_z, _R1,_z),
		ALU_MOV(_R5,_w, _R1,_w)
		ALU_LAST,
	},
	{
		/* 114 */
		ALU_MOV(_R7,_x, _R11,_x),
		ALU_MOV(_R7,_y, _R11,_y),
		ALU_MOV(_R7,_z, _R11,_z)
		ALU_LAST,
	},
	{
		/* 115 */
		ALU_PRED_SETNE_INT(__,_x, KC0(5),_x, ALU_SRC_0,_x) UPDATE_EXEC_MASK(DEACTIVATE) UPDATE_PRED
		ALU_LAST,
	},
	{
		/* 116 */
		ALU_SETNE_INT(_R0,_w, KC0(7),_y, ALU_SRC_0,_x)
		ALU_LAST,
		/* 117 */
		ALU_PRED_SETE_INT(__,_x, _R0,_w, ALU_SRC_0,_x) UPDATE_EXEC_MASK(DEACTIVATE) UPDATE_PRED
		ALU_LAST,
	},
	{
		/* 118 */
		ALU_NOT_INT(_R0,_z, KC0(4),_y)
		ALU_LAST,
		/* 119 */
		ALU_PRED_SETNE_INT(__,_x, _R0,_z, ALU_SRC_0,_x) UPDATE_EXEC_MASK(DEACTIVATE) UPDATE_PRED
		ALU_LAST,
	},
	{
		/* 120 */
		ALU_PRED_SETNE_INT(__,_x, KC0(6),_y, ALU_SRC_0,_x) UPDATE_EXEC_MASK(DEACTIVATE) UPDATE_PRED
		ALU_LAST,
	},
	{
		/* 121 */
		ALU_MUL(_R9,_x, _R4,_x, KC0(1),_x),
		ALU_MUL(_R9,_y, _R4,_y, KC0(1),_y),
		ALU_MOV(_R9,_z, ALU_SRC_0,_x)
		ALU_LAST,
	},
	{
		/* 122 */
		ALU_MOV(_R9,_x, _R11,_x),
		ALU_MOV(_R9,_y, _R11,_y),
		ALU_MOV(_R9,_z, _R11,_z)
		ALU_LAST,
	},
	{
		/* 123 */
		ALU_MULADD(_R127,_x, _R4,_x, KC0(1),_x, KC0(1),_z),
		ALU_MOV(_R127,_y, ALU_SRC_0,_x),
		ALU_MOV(_R127,_z, ALU_SRC_0,_x),
		ALU_MULADD(_R127,_w, _R4,_y, KC0(1),_y, KC0(1),_w)
		ALU_LAST,
		/* 124 */
		ALU_MOV(__,_x, KC0(1),_w),
		ALU_MOV(__,_y, KC0(1),_z)
		ALU_LAST,
		/* 125 */
		ALU_CNDE_INT(_R9,_x, KC1(6),_y, ALU_SRC_PV,_y, _R127,_x),
		ALU_CNDE_INT(_R9,_y, KC1(6),_y, ALU_SRC_PV,_x, _R127,_w),
		ALU_CNDE_INT(_R9,_z, KC1(6),_y, _R127,_y, _R127,_z)
		ALU_LAST,
	},
	{
		/* 126 */
		ALU_SETNE_INT(_R0,_y, KC0(7),_y, ALU_SRC_LITERAL,_x)
		ALU_LAST,
		ALU_LITERAL(0x00000003),
		/* 127 */
		ALU_PRED_SETE_INT(__,_x, _R0,_y, ALU_SRC_0,_x) UPDATE_EXEC_MASK(DEACTIVATE) UPDATE_PRED
		ALU_LAST,
	},
	{
		/* 128 */
		ALU_NOT_INT(_R0,_x, KC0(4),_y)
		ALU_LAST,
		/* 129 */
		ALU_PRED_SETNE_INT(__,_x, _R0,_x, ALU_SRC_0,_x) UPDATE_EXEC_MASK(DEACTIVATE) UPDATE_PRED
		ALU_LAST,
	},
	{
		/* 130 */
		ALU_PRED_SETNE_INT(__,_x, KC0(6),_y, ALU_SRC_0,_x) UPDATE_EXEC_MASK(DEACTIVATE) UPDATE_PRED
		ALU_LAST,
	},
	{
		/* 131 */
		ALU_MUL(_R9,_x, _R4,_x, KC0(1),_x),
		ALU_MUL(_R9,_y, _R4,_y, KC0(1),_y),
		ALU_MOV(_R9,_z, ALU_SRC_0,_x)
		ALU_LAST,
	},
	{
		/* 132 */
		ALU_MOV(_R9,_x, _R11,_x),
		ALU_MOV(_R9,_y, _R11,_y),
		ALU_MOV(_R9,_z, _R11,_z)
		ALU_LAST,
	},
	{
		/* 133 */
		ALU_MULADD(_R127,_x, _R4,_x, KC0(1),_x, KC0(1),_z),
		ALU_MOV(_R127,_y, ALU_SRC_0,_x),
		ALU_MOV(_R127,_z, ALU_SRC_0,_x),
		ALU_MULADD(_R127,_w, _R4,_y, KC0(1),_y, KC0(1),_w)
		ALU_LAST,
		/* 134 */
		ALU_MOV(__,_x, KC0(1),_w),
		ALU_MOV(__,_y, KC0(1),_z)
		ALU_LAST,
		/* 135 */
		ALU_CNDE_INT(_R9,_x, KC1(6),_y, ALU_SRC_PV,_y, _R127,_x),
		ALU_CNDE_INT(_R9,_y, KC1(6),_y, ALU_SRC_PV,_x, _R127,_w),
		ALU_CNDE_INT(_R9,_z, KC1(6),_y, _R127,_y, _R127,_z)
		ALU_LAST,
	},
	{
		/* 136 */
		ALU_SETNE_INT(_R0,_w, KC0(7),_y, ALU_SRC_1_INT,_x)
		ALU_LAST,
		/* 137 */
		ALU_PRED_SETE_INT(__,_x, _R0,_w, ALU_SRC_0,_x) UPDATE_EXEC_MASK(DEACTIVATE) UPDATE_PRED
		ALU_LAST,
	},
	{
		/* 138 */
		ALU_PRED_SETNE_INT(__,_x, KC0(7),_z, ALU_SRC_0,_x) UPDATE_EXEC_MASK(DEACTIVATE) UPDATE_PRED
		ALU_LAST,
	},
	{
		/* 139 */
		ALU_SETNE_INT(_R0,_z, KC0(7),_z, ALU_SRC_1_INT,_x)
		ALU_LAST,
		/* 140 */
		ALU_PRED_SETE_INT(__,_x, _R0,_z, ALU_SRC_0,_x) UPDATE_EXEC_MASK(DEACTIVATE) UPDATE_PRED
		ALU_LAST,
	},
	{
		/* 141 */
		ALU_CNDE_INT(_R3,_x, KC0(6),_y, ALU_SRC_0,_x, _R4,_x),
		ALU_CNDE_INT(_R3,_y, KC0(6),_y, ALU_SRC_0,_x, _R4,_y),
		ALU_MOV(_R0,_z, ALU_SRC_0,_x),
		ALU_MOV(_R0,_w, ALU_SRC_LITERAL,_x)
		ALU_LAST,
		ALU_LITERAL(0x3F800000),
		/* 142 */
		ALU_CNDE_INT(_R3,_z, KC0(6),_y, ALU_SRC_0,_x, ALU_SRC_PV,_z),
		ALU_CNDE_INT(_R3,_w, KC0(6),_y, ALU_SRC_LITERAL,_x, ALU_SRC_PV,_w)
		ALU_LAST,
		ALU_LITERAL(0x3F800000),
	},
	{
		/* 143 */
		ALU_SETNE_INT(_R0,_y, KC0(7),_z, ALU_SRC_LITERAL,_x)
		ALU_LAST,
		ALU_LITERAL(0x00000002),
		/* 144 */
		ALU_PRED_SETE_INT(__,_x, _R0,_y, ALU_SRC_0,_x) UPDATE_EXEC_MASK(DEACTIVATE) UPDATE_PRED
		ALU_LAST,
	},
	{
		/* 145 */
		ALU_PRED_SETNE_INT(__,_x, KC0(5),_w, ALU_SRC_0,_x) UPDATE_EXEC_MASK(DEACTIVATE) UPDATE_PRED
		ALU_LAST,
	},
	{
		/* 146 */
		ALU_CNDE_INT(_R127,_x, KC0(6),_x, _R2,_x, _R2 _NEG,_x),
		ALU_CNDE_INT(_R127,_y, KC0(6),_x, _R2,_y, _R2 _NEG,_y),
		ALU_CNDE_INT(_R127,_z, KC0(6),_x, _R2,_z, _R2 _NEG,_z)
		ALU_LAST,
		/* 147 */
		ALU_DOT4_IEEE(__,_x, ALU_SRC_PV,_x, ALU_SRC_PV,_x),
		ALU_DOT4_IEEE(__,_y, ALU_SRC_PV,_y, ALU_SRC_PV,_y),
		ALU_DOT4_IEEE(__,_z, ALU_SRC_PV,_z, ALU_SRC_PV,_z),
		ALU_DOT4_IEEE(__,_w, ALU_SRC_LITERAL,_x, ALU_SRC_0,_x)
		ALU_LAST,
		ALU_LITERAL(0x80000000),
		/* 148 */
		ALU_RECIPSQRT_IEEE(__,_x, ALU_SRC_PV,_x) SCL_210
		ALU_LAST,
		/* 149 */
		ALU_MUL(_R3,_x, _R127,_x, ALU_SRC_PS,_x),
		ALU_MUL(_R3,_y, _R127,_y, ALU_SRC_PS,_x),
		ALU_MUL(_R3,_z, _R127,_z, ALU_SRC_PS,_x),
		ALU_MOV(_R3,_w, ALU_SRC_LITERAL,_x)
		ALU_LAST,
		ALU_LITERAL(0x3F800000),
	},
	{
		/* 150 */
		ALU_MOV(_R3,_x, ALU_SRC_0,_x),
		ALU_MOV(_R3,_y, ALU_SRC_0,_x),
		ALU_MOV(_R3,_z, ALU_SRC_LITERAL,_x),
		ALU_MOV(_R3,_w, ALU_SRC_LITERAL,_x)
		ALU_LAST,
		ALU_LITERAL(0x3F800000),
	},
	{
		/* 151 */
		ALU_MOV(__,_x, ALU_SRC_LITERAL,_x),
		ALU_CNDE_INT(_R123,_y, KC0(6),_x, _R2,_z, _R2 _NEG,_z),
		ALU_CNDE_INT(_R123,_z, KC0(6),_x, _R2,_y, _R2 _NEG,_y),
		ALU_CNDE_INT(_R123,_w, KC0(6),_x, _R2,_x, _R2 _NEG,_x),
		ALU_SETNE_INT(_R127,_x, KC0(7),_z, ALU_SRC_LITERAL,_y)
		ALU_LAST,
		ALU_LITERAL2(0x3F800000, 0x00000003),
		/* 152 */
		ALU_CNDE_INT(_R123,_x, KC0(5),_w, ALU_SRC_LITERAL,_x, ALU_SRC_PV,_x),
		ALU_CNDE_INT(_R123,_y, KC0(5),_w, ALU_SRC_LITERAL,_x, ALU_SRC_PV,_y),
		ALU_CNDE_INT(_R123,_z, KC0(5),_w, ALU_SRC_0,_x, ALU_SRC_PV,_z),
		ALU_CNDE_INT(_R123,_w, KC0(5),_w, ALU_SRC_0,_x, ALU_SRC_PV,_w)
		ALU_LAST,
		ALU_LITERAL(0x3F800000),
		/* 153 */
		ALU_CNDE_INT(_R3,_x, _R127,_x, ALU_SRC_PV,_w, ALU_SRC_0,_x),
		ALU_CNDE_INT(_R3,_y, _R127,_x, ALU_SRC_PV,_z, ALU_SRC_0,_x),
		ALU_CNDE_INT(_R3,_z, _R127,_x, ALU_SRC_PV,_y, ALU_SRC_0,_x),
		ALU_CNDE_INT(_R3,_w, _R127,_x, ALU_SRC_PV,_x, ALU_SRC_0,_x)
		ALU_LAST,
	},
	{
		/* 154 */
		ALU_DOT4(_R127,_x, _R3,_x, KC0(14),_x),
		ALU_DOT4(__,_y, _R3,_y, KC0(14),_y),
		ALU_DOT4(__,_z, _R3,_z, KC0(14),_z),
		ALU_DOT4(__,_w, _R3,_w, KC0(14),_w)
		ALU_LAST,
		/* 155 */
		ALU_DOT4(__,_x, _R3,_x, KC0(15),_x),
		ALU_DOT4(__,_y, _R3,_y, KC0(15),_y),
		ALU_DOT4(__,_z, _R3,_z, KC0(15),_z),
		ALU_DOT4(_R127,_w, _R3,_w, KC0(15),_w)
		ALU_LAST,
		/* 156 */
		ALU_DOT4(__,_x, _R3,_x, KC0(16),_x),
		ALU_DOT4(__,_y, _R3,_y, KC0(16),_y),
		ALU_DOT4(_R9,_z, _R3,_z, KC0(16),_z),
		ALU_DOT4(__,_w, _R3,_w, KC0(16),_w)
		ALU_LAST,
		/* 157 */
		ALU_MUL(_R9,_x, KC0(17),_x, _R127,_x),
		ALU_MUL(_R9,_y, KC0(17),_y, _R127,_w)
		ALU_LAST,
	},
	{
		/* 158 */
		ALU_SETNE_INT(_R0,_y, KC0(7),_y, ALU_SRC_LITERAL,_x)
		ALU_LAST,
		ALU_LITERAL(0x00000002),
		/* 159 */
		ALU_PRED_SETE_INT(__,_x, _R0,_y, ALU_SRC_0,_x) UPDATE_EXEC_MASK(DEACTIVATE) UPDATE_PRED
		ALU_LAST,
	},
	{
		/* 160 */
		ALU_ADD_INT(_R0,_x, KC0(8),_x, ALU_SRC_LITERAL,_x),
		ALU_ADD_INT(_R0,_w, KC0(7),_w, ALU_SRC_LITERAL,_x)
		ALU_LAST,
		ALU_LITERAL(0x00000004),
	},
	{
		/* 161 */
		ALU_DOT4_IEEE(__,_x, _R1,_x, _R1,_x),
		ALU_DOT4_IEEE(__,_y, _R1,_y, _R1,_y),
		ALU_DOT4_IEEE(__,_z, _R1,_z, _R1,_z),
		ALU_DOT4_IEEE(__,_w, ALU_SRC_LITERAL,_x, ALU_SRC_0,_x),
		ALU_MUL_IEEE(__,_x, _R0,_z, _R0,_z)
		ALU_LAST,
		ALU_LITERAL(0x80000000),
		/* 162 */
		ALU_DOT4_IEEE(__,_x, _R0,_x, _R0,_x),
		ALU_DOT4_IEEE(__,_y, _R0,_y, _R0,_y),
		ALU_DOT4_IEEE(__,_z, ALU_SRC_PS,_x, ALU_SRC_1,_x),
		ALU_DOT4_IEEE(__,_w, ALU_SRC_LITERAL,_x, ALU_SRC_0,_x),
		ALU_RECIPSQRT_IEEE(__,_x, ALU_SRC_PV,_x) SCL_210
		ALU_LAST,
		ALU_LITERAL(0x80000000),
		/* 163 */
		ALU_MUL(_R127,_x, _R1,_x, ALU_SRC_PS,_x),
		ALU_MUL(_R127,_y, _R1,_y, ALU_SRC_PS,_x),
		ALU_MUL(__,_z, _R1,_z, ALU_SRC_PS,_x),
		ALU_RECIPSQRT_IEEE(__,_x, ALU_SRC_PV,_x) SCL_210
		ALU_LAST,
		/* 164 */
		ALU_MUL(_R126,_x, _R0,_x, ALU_SRC_PS,_x),
		ALU_MUL(_R126,_y, _R0,_y, ALU_SRC_PS,_x),
		ALU_MUL(__,_z, _R0,_z, ALU_SRC_PS,_x),
		ALU_MUL(__,_x, _R6,_z, ALU_SRC_PV,_z)
		ALU_LAST,
		/* 165 */
		ALU_DOT4(__,_x, _R16,_x, _R127,_x),
		ALU_DOT4(__,_y, _R16,_y, _R127,_y),
		ALU_DOT4(__,_z, ALU_SRC_PS,_x, ALU_SRC_1,_x),
		ALU_DOT4(__,_w, ALU_SRC_LITERAL,_x, ALU_SRC_0,_x),
		ALU_MUL(__,_x, _R6,_z, ALU_SRC_PV,_z)
		ALU_LAST,
		ALU_LITERAL(0x80000000),
		/* 166 */
		ALU_DOT4(__,_x, _R16,_x, _R126,_x),
		ALU_DOT4(__,_y, _R16,_y, _R126,_y),
		ALU_DOT4(__,_z, ALU_SRC_PS,_x, ALU_SRC_1,_x),
		ALU_DOT4(__,_w, ALU_SRC_LITERAL,_x, ALU_SRC_0,_x),
		ALU_ADD_D2(__,_x, ALU_SRC_PV,_x, ALU_SRC_1,_x)
		ALU_LAST,
		ALU_LITERAL(0x80000000),
		/* 167 */
		ALU_MUL(_R9,_x, ALU_SRC_PS,_x, KC0(1),_x),
		ALU_ADD_D2(__,_w, ALU_SRC_PV,_x, ALU_SRC_1,_x)
		ALU_LAST,
		/* 168 */
		ALU_MUL(_R9,_y, ALU_SRC_PV,_w, KC0(1),_y),
		ALU_MOV(_R9,_z, ALU_SRC_LITERAL,_x)
		ALU_LAST,
		ALU_LITERAL(0x3F800000),
	},
	{
		/* 169 */
		ALU_ADD(_R0,_x, _R6,_y, KC0(3),_x)
		ALU_LAST,
		/* 170 */
		ALU_PRED_SETNE_INT(__,_x, KC1(10),_y, ALU_SRC_0,_x) UPDATE_EXEC_MASK(DEACTIVATE) UPDATE_PRED
		ALU_LAST,
	},
	{
		/* 171 */
		ALU_RECIP_IEEE(__,_x, _R12,_w) SCL_210
		ALU_LAST,
		/* 172 */
		ALU_MUL_IEEE(__,_y, _R12,_z, ALU_SRC_PS,_x)
		ALU_LAST,
		/* 173 */
		ALU_MUL(__,_x, ALU_SRC_PV,_y, KC0(2),_x)
		ALU_LAST,
		/* 174 */
		ALU_ADD(__,_z, KC0(2),_y, ALU_SRC_PV,_x)
		ALU_LAST,
		/* 175 */
		ALU_FLOOR(__,_w, ALU_SRC_PV,_z)
		ALU_LAST,
		/* 176 */
		ALU_ADD(__,_y, KC0(2) _NEG,_z, ALU_SRC_PV,_w)
		ALU_LAST,
		/* 177 */
		ALU_MUL(__,_x, KC0(2),_w, ALU_SRC_PV,_y)
		ALU_LAST,
		/* 178 */
		ALU_MUL(_R12,_z, _R12,_w, ALU_SRC_PV,_x)
		ALU_LAST,
	},
	{
		/* 179 */
		ALU_NOP(__,_x),
		ALU_MUL(_R0,_x, KC0(3),_y, _R0,_x)
		ALU_LAST,
	},
	{
		VTX_FETCH(_R5,_x,_y,_z,_w, _R0,_z, (131), FETCH_TYPE(NO_INDEX_OFFSET), MEGA(16), OFFSET(0)),
	},
	{
		VTX_FETCH(_R6,_x,_y,_z,_w, _R0,_w, (131), FETCH_TYPE(NO_INDEX_OFFSET), MEGA(16), OFFSET(0)),
		VTX_FETCH(_R7,_x,_y,_z,_w, _R0,_y, (131), FETCH_TYPE(NO_INDEX_OFFSET), MEGA(16), OFFSET(0)),
	},
	{
		VTX_FETCH(_R0,_m,_m,_z,_m, _R4,_w, (132), FETCH_TYPE(NO_INDEX_OFFSET), MEGA(16), OFFSET(0)),
	},
	{
		VTX_FETCH(_R17,_x,_y,_m,_m, _R4,_w, (132), FETCH_TYPE(NO_INDEX_OFFSET), MEGA(16), OFFSET(0)),
	},
	{
		VTX_FETCH(_R15,_x,_y,_z,_m, _R0,_w, (130), FETCH_TYPE(NO_INDEX_OFFSET), MEGA(16), OFFSET(0)),
	},
	{
		VTX_FETCH(_R1,_x,_y,_z,_m, _R0,_y, (130), FETCH_TYPE(NO_INDEX_OFFSET), MEGA(16), OFFSET(0)),
	},
	{
		VTX_FETCH(_R1,_x,_y,_z,_m, _R0,_w, (130), FETCH_TYPE(NO_INDEX_OFFSET), MEGA(16), OFFSET(0)),
		VTX_FETCH(_R0,_x,_y,_m,_m, _R0,_z, (130), FETCH_TYPE(NO_INDEX_OFFSET), MEGA(16), OFFSET(0)),
	},
	{
		VTX_FETCH(_R1,_x,_y,_z,_m, _R0,_w, (130), FETCH_TYPE(NO_INDEX_OFFSET), MEGA(16), OFFSET(0)),
		VTX_FETCH(_R0,_x,_y,_z,_m, _R0,_y, (130), FETCH_TYPE(NO_INDEX_OFFSET), MEGA(16), OFFSET(0)),
	},
	{
		VTX_FETCH(_R0,_x,_y,_z,_m, _R0,_z, (130), FETCH_TYPE(NO_INDEX_OFFSET), MEGA(16), OFFSET(0)),
	},
	{
		VTX_FETCH(_R1,_x,_y,_z,_m, _R0,_w, (130), FETCH_TYPE(NO_INDEX_OFFSET), MEGA(16), OFFSET(0)),
		VTX_FETCH(_R0,_x,_y,_z,_m, _R0,_x, (130), FETCH_TYPE(NO_INDEX_OFFSET), MEGA(16), OFFSET(0)),
	},
};

GX2VertexShader VShaderHWSkinGX2 = {
	{
		.sq_pgm_resources_vs.num_gprs = 26,
		.sq_pgm_resources_vs.stack_size = 3,
		.spi_vs_out_config.vs_export_count = 3,
		.num_spi_vs_out_id = 1,
		{
			{ .semantic_0 = 0x00, .semantic_1 = 0x01, .semantic_2 = 0x03, .semantic_3 = 0x02 },
			{ .semantic_0 = 0xFF, .semantic_1 = 0xFF, .semantic_2 = 0xFF, .semantic_3 = 0xFF },
			{ .semantic_0 = 0xFF, .semantic_1 = 0xFF, .semantic_2 = 0xFF, .semantic_3 = 0xFF },
			{ .semantic_0 = 0xFF, .semantic_1 = 0xFF, .semantic_2 = 0xFF, .semantic_3 = 0xFF },
			{ .semantic_0 = 0xFF, .semantic_1 = 0xFF, .semantic_2 = 0xFF, .semantic_3 = 0xFF },
			{ .semantic_0 = 0xFF, .semantic_1 = 0xFF, .semantic_2 = 0xFF, .semantic_3 = 0xFF },
			{ .semantic_0 = 0xFF, .semantic_1 = 0xFF, .semantic_2 = 0xFF, .semantic_3 = 0xFF },
			{ .semantic_0 = 0xFF, .semantic_1 = 0xFF, .semantic_2 = 0xFF, .semantic_3 = 0xFF },
			{ .semantic_0 = 0xFF, .semantic_1 = 0xFF, .semantic_2 = 0xFF, .semantic_3 = 0xFF },
			{ .semantic_0 = 0xFF, .semantic_1 = 0xFF, .semantic_2 = 0xFF, .semantic_3 = 0xFF },
		},
		.sq_vtx_semantic_clear = ~0x3F,
		.num_sq_vtx_semantic = 6,
		{
			2, 4, 0, 1, 5, 6, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
		},
		.vgt_vertex_reuse_block_cntl.vtx_reuse_depth = 0xE,
		.vgt_hos_reuse_depth.reuse_depth = 0x10,
	}, /* regs */
	.size = sizeof(VShaderHWSkinCode),
	.program = (u8 *)&VShaderHWSkinCode,
	.mode = GX2_SHADER_MODE_UNIFORM_BLOCK,
	.loopVarCount = countof(VShaderLoopVars), VShaderLoopVars,
	.gx2rBuffer.flags = GX2R_RESOURCE_LOCKED_READ_ONLY,

};
