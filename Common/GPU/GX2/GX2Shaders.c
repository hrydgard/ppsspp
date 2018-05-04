#undef ARRAY_SIZE

#include <wiiu/gx2/common.h>
#include <wiiu/gx2/shaders.h>
#include <wiiu/gx2/shaders_asm.h>

static GX2AttribVar attributes[] =
{
   { "position",  GX2_SHADER_VAR_TYPE_FLOAT3, 0, 0},
   { "color",     GX2_SHADER_VAR_TYPE_FLOAT4, 0, 1},
   { "tex_coord", GX2_SHADER_VAR_TYPE_FLOAT2, 0, 2},
};

static GX2SamplerVar samplers[] =
{
   { "s", GX2_SAMPLER_VAR_TYPE_SAMPLER_2D, 0 },
};

static GX2UniformBlock uniform_blocks[] = {
    {"UBO", 1, 64}
};

static GX2UniformVar uniform_vars[] = {
    {"global.MVP", GX2_SHADER_VAR_TYPE_MATRIX4X4, 1, 0, 0},
};


// clang-format off
__attribute__((aligned(GX2_SHADER_ALIGNMENT)))
static struct
{
   u64 cf[32];
   u64 alu[16];
} vsColCode =
{
   {
      CALL_FS NO_BARRIER,
      ALU(32, 16) KCACHE0(CB1, _0_15),
      EXP_DONE(POS0,   _R1, _x, _y, _z, _w),
      EXP_DONE(PARAM0, _R2, _x, _y, _z, _w) NO_BARRIER
      END_OF_PROGRAM
   },
   {
      ALU_MUL(__,_x, _R1,_w, KC0(3),_y),
      ALU_MUL(__,_y, _R1,_w, KC0(3),_x),
      ALU_MUL(__,_z, _R1,_w, KC0(3),_w),
      ALU_MUL(__,_w, _R1,_w, KC0(3),_z)
      ALU_LAST,
      ALU_MULADD(_R123,_x, _R1,_z, KC0(2),_y, ALU_SRC_PV,_x),
      ALU_MULADD(_R123,_y, _R1,_z, KC0(2),_x, ALU_SRC_PV,_y),
      ALU_MULADD(_R123,_z, _R1,_z, KC0(2),_w, ALU_SRC_PV,_z),
      ALU_MULADD(_R123,_w, _R1,_z, KC0(2),_z, ALU_SRC_PV,_w)
      ALU_LAST,
      ALU_MULADD(_R123,_x, _R1,_y, KC0(1),_y, ALU_SRC_PV,_x),
      ALU_MULADD(_R123,_y, _R1,_y, KC0(1),_x, ALU_SRC_PV,_y),
      ALU_MULADD(_R123,_z, _R1,_y, KC0(1),_w, ALU_SRC_PV,_z),
      ALU_MULADD(_R123,_w, _R1,_y, KC0(1),_z, ALU_SRC_PV,_w)
      ALU_LAST,
      ALU_MULADD(_R1,_x, _R1,_x, KC0(0),_x, ALU_SRC_PV,_y),
      ALU_MULADD(_R1,_y, _R1,_x, KC0(0),_y, ALU_SRC_PV,_x),
      ALU_MULADD(_R1,_z, _R1,_x, KC0(0),_z, ALU_SRC_PV,_w),
      ALU_MULADD(_R1,_w, _R1,_x, KC0(0),_w, ALU_SRC_PV,_z)
      ALU_LAST,
   }
};

__attribute__((aligned(GX2_SHADER_ALIGNMENT)))
static struct
{
   u64 cf[32];
} fsColCode =
{
   {
      EXP_DONE(PIX0, _R0, _x, _y, _z, _w)
      END_OF_PROGRAM
   }
};
// clang-format on
GX2VertexShader GX2_vsCol = {
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
		{
			0, 1, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
		},
		.vgt_vertex_reuse_block_cntl.vtx_reuse_depth = 0xE,
		.vgt_hos_reuse_depth.reuse_depth = 0x10,
	}, /* regs */
	.size = sizeof(vsColCode),
	.program = (uint8_t *)&vsColCode,
	.mode = GX2_SHADER_MODE_UNIFORM_BLOCK,
	.uniformBlockCount = countof(uniform_blocks), uniform_blocks,
	.uniformVarCount = countof(uniform_vars), uniform_vars,
	.attribVarCount = 2, attributes,
	.gx2rBuffer.flags = GX2R_RESOURCE_LOCKED_READ_ONLY,
};

GX2PixelShader GX2_fsCol = {
	{
		.sq_pgm_resources_ps.num_gprs = 1,
		.sq_pgm_exports_ps.export_mode = 0x2,
		.spi_ps_in_control_0.num_interp = 2,
		.spi_ps_in_control_0.persp_gradient_ena = 1,
		.spi_ps_in_control_0.baryc_sample_cntl = spi_baryc_cntl_centers_only,
		.num_spi_ps_input_cntl = 1,
		{ { .semantic = 0, .default_val = 1 }},
		.cb_shader_mask.output0_enable = 0xF,
		.cb_shader_control.rt0_enable = TRUE,
		.db_shader_control.z_order = db_z_order_early_z_then_late_z,
	}, /* regs */
	.size = sizeof(fsColCode),
	.program = (uint8_t *)&fsColCode,
	.mode = GX2_SHADER_MODE_UNIFORM_BLOCK,
	.gx2rBuffer.flags = GX2R_RESOURCE_LOCKED_READ_ONLY,
};
// clang-format off
__attribute__((aligned(GX2_SHADER_ALIGNMENT)))
static struct {
	u64 cf[32];
	u64 alu[16];
} vsTexColCode = {
	{
		CALL_FS NO_BARRIER,
		ALU(32, 16) KCACHE0(CB1, _0_15),
		EXP_DONE(POS0, _R1, _x, _y, _z, _w),
		EXP(PARAM0, _R2, _x, _y, _z, _w) NO_BARRIER,
		EXP_DONE(PARAM1, _R3, _x, _y, _0, _0) NO_BARRIER
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
} fsTexColCode =
{
	{
		TEX(48, 1) VALID_PIX,
		ALU(32, 4),
		EXP_DONE(PIX0, _R0, _x, _y, _z, _w)
		END_OF_PROGRAM
	},
	{
		ALU_MUL(_R0, _x, _R0, _x, _R1, _x),
		ALU_MUL(_R0, _y, _R0, _y, _R1, _y),
		ALU_MUL(_R0, _z, _R0, _z, _R1, _z),
		ALU_MUL(_R0, _w, _R0, _w, _R1, _w)
		ALU_LAST
	},
	{
		TEX_SAMPLE(_R1, _x, _y, _z, _w, _R1, _x, _y, _0, _0, _t0, _s0)
	}
};

__attribute__((aligned(GX2_SHADER_ALIGNMENT))) static struct {
	u64 cf[32];
	u64 alu[16];
	u64 tex[1 * 2];
} fsTexColCode_sw =
{
	{
		TEX(48, 1) VALID_PIX,
		ALU(32, 4),
		EXP_DONE(PIX0, _R0, _z, _y, _x, _w)
		END_OF_PROGRAM
	},
	{
		ALU_MUL(_R0, _x, _R0, _x, _R1, _x),
		ALU_MUL(_R0, _y, _R0, _y, _R1, _y),
		ALU_MUL(_R0, _z, _R0, _z, _R1, _z),
		ALU_MUL(_R0, _w, _R0, _w, _R1, _w)
		ALU_LAST
	},
	{
		TEX_SAMPLE(_R1, _x, _y, _z, _w, _R1, _x, _y, _0, _0, _t0, _s0)
	}
};
// clang-format on

GX2VertexShader GX2_vsTexCol = {
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
	.size = sizeof(vsTexColCode),
	.program = (uint8_t *)&vsTexColCode,
	.mode = GX2_SHADER_MODE_UNIFORM_BLOCK,
	.uniformBlockCount = countof(uniform_blocks), uniform_blocks,
	.uniformVarCount = countof(uniform_vars), uniform_vars,
	.attribVarCount = 3, attributes,
	.gx2rBuffer.flags = GX2R_RESOURCE_LOCKED_READ_ONLY,
};

GX2PixelShader GX2_fsTexCol = {
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
	.size = sizeof(fsTexColCode),
	.program = (uint8_t *)&fsTexColCode,
	.mode = GX2_SHADER_MODE_UNIFORM_BLOCK,
	.samplerVarCount = countof(samplers), samplers,
	.gx2rBuffer.flags = GX2R_RESOURCE_LOCKED_READ_ONLY,
};

GX2PixelShader GX2_fsTexCol_sw = {
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
	.size = sizeof(fsTexColCode_sw),
	.program = (uint8_t *)&fsTexColCode_sw,
	.mode = GX2_SHADER_MODE_UNIFORM_BLOCK,
	.samplerVarCount = countof(samplers), samplers,
	.gx2rBuffer.flags = GX2R_RESOURCE_LOCKED_READ_ONLY,
};
