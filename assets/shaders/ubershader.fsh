#version 450
#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shading_language_420pack : enable
#extension GL_ARB_conservative_depth : enable
#extension GL_ARB_shader_image_load_store : enable

//#define GPU_SCALE_DEPTH_FROM_24BIT_TO_16BIT
//#define GPU_SUPPORTS_DEPTH_CLAMP
//#define GPU_SUPPORTS_ACCURATE_DEPTH
//#define GPU_ROUND_FRAGMENT_DEPTH_TO_16BIT
//#define FLAT_SHADING

layout(early_fragment_tests) in;
layout(std140, set = 0, binding = 3) uniform baseUBO {
	mat4 proj_mtx;
	mat4 proj_through_mtx;
	mat3x4 view_mtx;
	mat3x4 world_mtx;
	mat3x4 tex_mtx;
	vec4 uvscaleoffset;
	vec4 depthRange;
	vec2 fogcoef;
	float stencilReplace;
	vec4 matambientalpha;
	uint spline_counts;
	uint depal_mask_shift_off_fmt;
	int pad2;
	int pad3;
	vec3 fogcolor;
	vec3 texenv;
	ivec4 alphacolorref;
	ivec4 alphacolormask;
	vec3 blendFixA;
	vec3 blendFixB;
	vec4 texclamp;
	vec2 texclampoff;
	vec4 cullRangeMin;
	vec4 cullRangeMax;
}
base;

layout(std140, set = 0, binding = 7) uniform UB_FSID {
	bool FS_BIT_CLEARMODE;
	bool FS_BIT_DO_TEXTURE;
	int FS_BIT_TEXFUNC;
	bool FS_BIT_TEXALPHA;
	bool FS_BIT_SHADER_DEPAL;
	bool FS_BIT_SHADER_TEX_CLAMP;
	bool FS_BIT_CLAMP_S;
	bool FS_BIT_CLAMP_T;
	bool FS_BIT_TEXTURE_AT_OFFSET;
	bool FS_BIT_LMODE;
	bool FS_BIT_ALPHA_TEST;
	int FS_BIT_ALPHA_TEST_FUNC;
	bool FS_BIT_ALPHA_AGAINST_ZERO;
	bool FS_BIT_COLOR_TEST;
	int FS_BIT_COLOR_TEST_FUNC;
	bool FS_BIT_COLOR_AGAINST_ZERO;
	bool FS_BIT_ENABLE_FOG;
	bool FS_BIT_DO_TEXTURE_PROJ;
	bool FS_BIT_COLOR_DOUBLE;
	int FS_BIT_STENCIL_TO_ALPHA;
	int FS_BIT_REPLACE_ALPHA_WITH_STENCIL_TYPE;
	int FS_BIT_REPLACE_LOGIC_OP_TYPE;
	int FS_BIT_REPLACE_BLEND;
	int FS_BIT_BLENDEQ;
	int FS_BIT_BLENDFUNC_A;
	int FS_BIT_BLENDFUNC_B;
	bool FS_BIT_FLATSHADE;
	bool FS_BIT_BGRA_TEXTURE;
	bool FS_BIT_TEST_DISCARD_TO_ZERO;
};

#define GE_BLENDMODE_MUL_AND_ADD 0
#define GE_BLENDMODE_MUL_AND_SUBTRACT 1
#define GE_BLENDMODE_MUL_AND_SUBTRACT_REVERSE 2
#define GE_BLENDMODE_MIN 3
#define GE_BLENDMODE_MAX 4
#define GE_BLENDMODE_ABSDIFF 5

#define GE_DSTBLEND_SRCCOLOR 0
#define GE_DSTBLEND_INVSRCCOLOR 1
#define GE_DSTBLEND_SRCALPHA 2
#define GE_DSTBLEND_INVSRCALPHA 3
#define GE_DSTBLEND_DSTALPHA 4
#define GE_DSTBLEND_INVDSTALPHA 5
#define GE_DSTBLEND_DOUBLESRCALPHA 6
#define GE_DSTBLEND_DOUBLEINVSRCALPHA 7
#define GE_DSTBLEND_DOUBLEDSTALPHA 8
#define GE_DSTBLEND_DOUBLEINVDSTALPHA 9
#define GE_DSTBLEND_FIXB 10

#define GE_SRCBLEND_DSTCOLOR 0
#define GE_SRCBLEND_INVDSTCOLOR 1
#define GE_SRCBLEND_SRCALPHA 2
#define GE_SRCBLEND_INVSRCALPHA 3
#define GE_SRCBLEND_DSTALPHA 4
#define GE_SRCBLEND_INVDSTALPHA 5
#define GE_SRCBLEND_DOUBLESRCALPHA 6
#define GE_SRCBLEND_DOUBLEINVSRCALPHA 7
#define GE_SRCBLEND_DOUBLEDSTALPHA 8
#define GE_SRCBLEND_DOUBLEINVDSTALPHA 9
#define GE_SRCBLEND_FIXA 10

#define GE_COMP_NEVER 0
#define GE_COMP_ALWAYS 1
#define GE_COMP_EQUAL 2
#define GE_COMP_NOTEQUAL 3
#define GE_COMP_LESS 4
#define GE_COMP_LEQUAL 5
#define GE_COMP_GREATER 6
#define GE_COMP_GEQUAL 7

#define GE_TEXFUNC_MODULATE 0
#define GE_TEXFUNC_DECAL 1
#define GE_TEXFUNC_BLEND 2
#define GE_TEXFUNC_REPLACE 3
#define GE_TEXFUNC_ADD 4
#define GE_TEXFUNC_UNKNOWN1 5
#define GE_TEXFUNC_UNKNOWN2 6
#define GE_TEXFUNC_UNKNOWN3 7

#define REPLACE_BLEND_NO 0
#define REPLACE_BLEND_STANDARD
#define REPLACE_BLEND_PRE_SRC 2
#define REPLACE_BLEND_PRE_SRC_2X_ALPHA 3
#define REPLACE_BLEND_2X_ALPHA 4
#define REPLACE_BLEND_2X_SRC 5
#define REPLACE_BLEND_COPY_FBO 6

#define REPLACE_ALPHA_NO 0
#define REPLACE_ALPHA_YES 1
#define REPLACE_ALPHA_DUALSOURCE 2

#define STENCIL_VALUE_UNIFORM 0
#define STENCIL_VALUE_ZERO 1
#define STENCIL_VALUE_ONE 2
#define STENCIL_VALUE_KEEP 3
#define STENCIL_VALUE_INVERT 4
#define STENCIL_VALUE_INCR_4 5
#define STENCIL_VALUE_INCR_8 6
#define STENCIL_VALUE_DECR_4 7
#define STENCIL_VALUE_DECR_8 8

#define LOGICOPTYPE_NORMAL 0
#define LOGICOPTYPE_ONE 1
#define LOGICOPTYPE_INVERT 2

#define DISCARD \
	do { \
		if (FS_BIT_TEST_DISCARD_TO_ZERO) \
			v.a = 0.0; \
		else \
			discard; \
	} while (false)
#ifdef FLAT_SHADING
#define shading flat
#else
#define shading
#endif

layout(binding = 0) uniform sampler2D tex;
layout(binding = 1) uniform sampler2D fbotex;
layout(binding = 2) uniform sampler2D pal;

layout(location = 0) in vec3 v_texcoord;
layout(location = 1) shading in vec4 v_color0;
layout(location = 2) shading in vec3 v_color1;
layout(location = 3) in float v_fogdepth;

int roundAndScaleTo255i(in float x) { return int(floor(x * 255.0 + 0.5)); }
ivec3 roundAndScaleTo255iv(in vec3 x) { return ivec3(floor(x * 255.0 + 0.5)); }

layout(location = 0, index = 0) out vec4 fragColor0;
layout(location = 0, index = 1) out vec4 fragColor1;

void main() {
	vec4 v;
	if (FS_BIT_CLEARMODE) {
		// Clear mode does not allow any fancy shading.
		v = v_color0;
	} else {
		vec4 secondary = vec4(0.0);
		// Secondary color for specular on top of texture
		if (FS_BIT_LMODE) {
			secondary = vec4(v_color1, 0.0);
		}

		if (FS_BIT_DO_TEXTURE) {
			vec3 texcoord = v_texcoord;
			// TODO: Not sure the right way to do this for projection.
			// This path destroys resolution on older PowerVR no matter what I do, so we disable it on SGX 540 and lesser, and live with the consequences.
			if (FS_BIT_SHADER_TEX_CLAMP) {
				// We may be clamping inside a larger surface (tex = 64x64, buffer=480x272).
				// We may also be wrapping in such a surface, or either one in a too-small surface.
				// Obviously, clamping to a smaller surface won't work.  But better to clamp to something.
				if (FS_BIT_DO_TEXTURE_PROJ) {
					texcoord.x = (v_texcoord.x / v_texcoord.z);
					texcoord.y = (v_texcoord.y / v_texcoord.z);
				}

				if (FS_BIT_CLAMP_S) {
					texcoord.x = clamp(texcoord.x, base.texclamp.z, base.texclamp.x - base.texclamp.z);
				} else {
					texcoord.x = mod(texcoord.x, base.texclamp.x);
				}
				if (FS_BIT_CLAMP_T) {
					texcoord.y = clamp(texcoord.y, base.texclamp.w, base.texclamp.y - base.texclamp.w);
				} else {
					texcoord.y = mod(texcoord.y, base.texclamp.y);
				}
				if (FS_BIT_TEXTURE_AT_OFFSET) {
					texcoord.x = (texcoord.x + base.texclampoff.x);
					texcoord.y = (texcoord.y + base.texclampoff.y);
				}

				texcoord.xy = vec2(texcoord.x, texcoord.y);
			}

			vec4 t;
			if (!FS_BIT_SHADER_DEPAL) {
				if (FS_BIT_DO_TEXTURE_PROJ && !FS_BIT_SHADER_TEX_CLAMP) {
					t = textureProj(tex, texcoord);
				} else {
					t = texture(tex, texcoord.xy);
				}
			} else {
				vec2 tsize = textureSize(tex, 0);
				bool bilinear = (base.depal_mask_shift_off_fmt >> 31) != 0;
				vec2 uv;
				vec2 uv_round;
				vec2 fraction;

				// We don't use textureProj because we need to manually offset from the divided coordinate to do filtering here.
				// On older hardware it has the advantage of higher resolution math, but such old hardware can't run Vulkan.
				if (FS_BIT_DO_TEXTURE_PROJ && !FS_BIT_SHADER_TEX_CLAMP)
					uv = texcoord.xy / texcoord.z;
				else
					uv = texcoord.xy;

				if (bilinear) {
					uv_round = uv * tsize - vec2(0.5, 0.5);
					fraction = fract(uv_round);
					uv_round = (uv_round - fraction + vec2(0.5, 0.5)) / tsize; // We want to take our four point samples at pixel centers.
				} else {
					uv_round = uv;
				}

				t = texture(tex, uv_round);
				vec4 t1 = textureOffset(tex, uv_round, ivec2(1, 0));
				vec4 t2 = textureOffset(tex, uv_round, ivec2(0, 1));
				vec4 t3 = textureOffset(tex, uv_round, ivec2(1, 1));
				uint depalMask = (base.depal_mask_shift_off_fmt & 0xFF);
				uint depalShift = (base.depal_mask_shift_off_fmt >> 8) & 0xFF;
				uint depalOffset = ((base.depal_mask_shift_off_fmt >> 16) & 0xFF) << 4;
				uint depalFmt = (base.depal_mask_shift_off_fmt >> 24) & 0x3;

				uvec4 col;
				uint index0;
				uint index1;
				uint index2;
				uint index3;
				switch (depalFmt) {
				case 0: // 565
					col = uvec4(t.rgb * vec3(31.99, 63.99, 31.99), 0);
					index0 = (col.b << 11) | (col.g << 5) | (col.r);
					if (bilinear) {
						col = uvec4(t1.rgb * vec3(31.99, 63.99, 31.99), 0);
						index1 = (col.b << 11) | (col.g << 5) | (col.r);
						col = uvec4(t2.rgb * vec3(31.99, 63.99, 31.99), 0);
						index2 = (col.b << 11) | (col.g << 5) | (col.r);
						col = uvec4(t3.rgb * vec3(31.99, 63.99, 31.99), 0);
						index3 = (col.b << 11) | (col.g << 5) | (col.r);
					}
					break;
				case 1: // 5551
					col = uvec4(t.rgba * vec4(31.99, 31.99, 31.99, 1.0));
					index0 = (col.a << 15) | (col.b << 10) | (col.g << 5) | (col.r);
					if (bilinear) {
						col = uvec4(t1.rgba * vec4(31.99, 31.99, 31.99, 1.0));
						index1 = (col.a << 15) | (col.b << 10) | (col.g << 5) | (col.r);
						col = uvec4(t2.rgba * vec4(31.99, 31.99, 31.99, 1.0));
						index2 = (col.a << 15) | (col.b << 10) | (col.g << 5) | (col.r);
						col = uvec4(t3.rgba * vec4(31.99, 31.99, 31.99, 1.0));
						index3 = (col.a << 15) | (col.b << 10) | (col.g << 5) | (col.r);
					}
					break;
				case 2: // 4444
					col = uvec4(t.rgba * 15.99);
					index0 = (col.a << 12) | (col.b << 8) | (col.g << 4) | (col.r);
					if (bilinear) {
						col = uvec4(t1.rgba * 15.99);
						index1 = (col.a << 12) | (col.b << 8) | (col.g << 4) | (col.r);
						col = uvec4(t2.rgba * 15.99);
						index2 = (col.a << 12) | (col.b << 8) | (col.g << 4) | (col.r);
						col = uvec4(t3.rgba * 15.99);
						index3 = (col.a << 12) | (col.b << 8) | (col.g << 4) | (col.r);
					}
					break;
				case 3: // 8888
					col = uvec4(t.rgba * 255.99);
					index0 = (col.a << 24) | (col.b << 16) | (col.g << 8) | (col.r);
					if (bilinear) {
						col = uvec4(t1.rgba * 255.99);
						index1 = (col.a << 24) | (col.b << 16) | (col.g << 8) | (col.r);
						col = uvec4(t2.rgba * 255.99);
						index2 = (col.a << 24) | (col.b << 16) | (col.g << 8) | (col.r);
						col = uvec4(t3.rgba * 255.99);
						index3 = (col.a << 24) | (col.b << 16) | (col.g << 8) | (col.r);
					}
					break;
				};
				index0 = ((index0 >> depalShift) & depalMask) | depalOffset;
				t = texelFetch(pal, ivec2(index0, 0), 0);
				if (bilinear && !(index0 == index1 && index1 == index2 && index2 == index3)) {
					index1 = ((index1 >> depalShift) & depalMask) | depalOffset;
					index2 = ((index2 >> depalShift) & depalMask) | depalOffset;
					index3 = ((index3 >> depalShift) & depalMask) | depalOffset;
					t1 = texelFetch(pal, ivec2(index1, 0), 0);
					t2 = texelFetch(pal, ivec2(index2, 0), 0);
					t3 = texelFetch(pal, ivec2(index3, 0), 0);
					t = mix(t, t1, fraction.x);
					t2 = mix(t2, t3, fraction.x);
					t = mix(t, t2, fraction.y);
				}
			}

			vec4 p = v_color0;
			if (FS_BIT_TEXALPHA) { // texfmt == RGBA
				switch (FS_BIT_TEXFUNC) {
				case GE_TEXFUNC_ADD:
				case GE_TEXFUNC_UNKNOWN1:
				case GE_TEXFUNC_UNKNOWN2:
				case GE_TEXFUNC_UNKNOWN3: v = vec4(p.rgb + t.rgb, p.a * t.a) + secondary; break;
				case GE_TEXFUNC_MODULATE: v = p * t + secondary; break;
				case GE_TEXFUNC_REPLACE: v = t + secondary; break;
				case GE_TEXFUNC_DECAL: v = vec4(mix(p.rgb, t.rgb, t.a), p.a) + secondary; break;
				case GE_TEXFUNC_BLEND: v = vec4(mix(p.rgb, base.texenv.rgb, t.rgb), p.a * t.a) + secondary; break;
				default: v = p; break;
				}
			} else { // texfmt == RGB
				switch (FS_BIT_TEXFUNC) {
				case GE_TEXFUNC_ADD:
				case GE_TEXFUNC_UNKNOWN1:
				case GE_TEXFUNC_UNKNOWN2:
				case GE_TEXFUNC_UNKNOWN3: v = vec4(p.rgb + t.rgb, p.a) + secondary; break;
				case GE_TEXFUNC_MODULATE: v = vec4(t.rgb * p.rgb, p.a) + secondary; break;
				case GE_TEXFUNC_REPLACE: v = vec4(t.rgb, p.a) + secondary; break;
				case GE_TEXFUNC_DECAL: v = vec4(t.rgb, p.a) + secondary; break;
				case GE_TEXFUNC_BLEND: v = vec4(mix(p.rgb, base.texenv.rgb, t.rgb), p.a) + secondary; break;
				default: v = p; break;
				}
			}

			if (FS_BIT_COLOR_DOUBLE) {
				// This happens before fog is applied.
				v.rgb = clamp(v.rgb * 2.0, 0.0, 1.0);
			}
		} else {
			// No texture mapping
			v = v_color0 + secondary;
		}

		// Texture access is at half texels [0.5/256, 255.5/256], but colors are normalized [0, 255].
		// So we have to scale to account for the difference.

		if (FS_BIT_ALPHA_TEST) {
			if (FS_BIT_ALPHA_AGAINST_ZERO) {
				// When testing against 0 (extremely common), we can avoid some math.
				// 0.002 is approximately half of 1.0 / 255.0.
				if (FS_BIT_ALPHA_TEST_FUNC == GE_COMP_NOTEQUAL || FS_BIT_ALPHA_TEST_FUNC == GE_COMP_GREATER) {
					if (v.a < 0.002)
						DISCARD;
				} else if (FS_BIT_ALPHA_TEST_FUNC != GE_COMP_NEVER) {
					// Anything else is a test for == 0.  Happens sometimes, actually...
					if (v.a > 0.002)
						DISCARD;
				} else {
					// NEVER has been logged as used by games, although it makes little sense - statically failing.
					// Maybe we could discard the drawcall, but it's pretty rare.  Let's just statically discard here.
					DISCARD;
				}
			} else {
				switch (FS_BIT_ALPHA_TEST_FUNC) {
				case GE_COMP_EQUAL:
					if ((roundAndScaleTo255i(v.a) & base.alphacolormask.a) != base.alphacolorref.a)
						DISCARD;
					break;
				case GE_COMP_NOTEQUAL:
					if ((roundAndScaleTo255i(v.a) & base.alphacolormask.a) == base.alphacolorref.a)
						DISCARD;
					break;
				case GE_COMP_LESS:
					if ((roundAndScaleTo255i(v.a) & base.alphacolormask.a) >= base.alphacolorref.a)
						DISCARD;
					break;
				case GE_COMP_LEQUAL:
					if ((roundAndScaleTo255i(v.a) & base.alphacolormask.a) > base.alphacolorref.a)
						DISCARD;
					break;
				case GE_COMP_GREATER:
					if ((roundAndScaleTo255i(v.a) & base.alphacolormask.a) <= base.alphacolorref.a)
						DISCARD;
					break;
				case GE_COMP_GEQUAL:
					if ((roundAndScaleTo255i(v.a) & base.alphacolormask.a) < base.alphacolorref.a)
						DISCARD;
					break;
				case GE_COMP_NEVER: DISCARD; break;
				default:
				case GE_COMP_ALWAYS: break;
				}
			}
		}

		if (FS_BIT_ENABLE_FOG)
			v = mix(vec4(base.fogcolor, v.a), v, clamp(v_fogdepth, 0.0, 1.0));

		if (FS_BIT_COLOR_TEST) {
			if (FS_BIT_COLOR_AGAINST_ZERO) {
				// When testing against 0 (common), we can avoid some math.
				// Have my doubts that this special case is actually worth it, but whatever.
				// 0.002 is approximately half of 1.0 / 255.0.
				if (FS_BIT_COLOR_TEST_FUNC == GE_COMP_NOTEQUAL) {
					if (v.r + v.g + v.b < 0.002)
						DISCARD;
				} else if (FS_BIT_COLOR_TEST_FUNC != GE_COMP_NEVER) {
					// Anything else is a test for == 0.
					if (v.r + v.g + v.b > 0.002)
						DISCARD;
				} else {
					// NEVER has been logged as used by games, although it makes little sense - statically failing.
					// Maybe we could discard the drawcall, but it's pretty rare.  Let's just statically discard here.
					DISCARD;
				}
			} else {
				switch (FS_BIT_COLOR_TEST_FUNC) {
				case GE_COMP_EQUAL:
					if ((roundAndScaleTo255iv(v.rgb) & base.alphacolormask.rgb) != (base.alphacolorref.rgb & base.alphacolormask.rgb))
						DISCARD;
					break;
				case GE_COMP_NOTEQUAL:
					if ((roundAndScaleTo255iv(v.rgb) & base.alphacolormask.rgb) == (base.alphacolorref.rgb & base.alphacolormask.rgb))
						DISCARD;
					break;
				case GE_COMP_NEVER: DISCARD; break;
				default:
				case GE_COMP_ALWAYS: break;
				}
			}
		}

		if (FS_BIT_REPLACE_BLEND == REPLACE_BLEND_2X_SRC) {
			v.rgb = v.rgb * 2.0;
		}

		if (FS_BIT_REPLACE_BLEND == REPLACE_BLEND_PRE_SRC || FS_BIT_REPLACE_BLEND == REPLACE_BLEND_PRE_SRC_2X_ALPHA) {
			switch (FS_BIT_BLENDFUNC_A) {
			case GE_SRCBLEND_FIXA: v.rgb = v.rgb * base.blendFixA; break;
			case GE_SRCBLEND_SRCALPHA: v.rgb = v.rgb * vec3(v.a); break;
			case GE_SRCBLEND_INVSRCALPHA: v.rgb = v.rgb * vec3(1.0 - v.a); break;
			case GE_SRCBLEND_DOUBLESRCALPHA: v.rgb = v.rgb * vec3(v.a * 2.0); break;
			case GE_SRCBLEND_DOUBLEINVSRCALPHA: v.rgb = v.rgb * vec3(1.0 - v.a * 2.0); break;
			default: break;
			}
		}

		if (FS_BIT_REPLACE_BLEND == REPLACE_BLEND_COPY_FBO) {
			lowp vec4 destColor = texelFetch(fbotex, ivec2(gl_FragCoord.x, gl_FragCoord.y), 0);
			vec3 srcFactor = vec3(1.0);
			vec3 dstFactor = vec3(0.0);

			switch (FS_BIT_BLENDFUNC_A) {
			case GE_SRCBLEND_DSTCOLOR: srcFactor = destColor.rgb; break;
			case GE_SRCBLEND_INVDSTCOLOR: srcFactor = (vec3(1.0) - destColor.rgb); break;
			case GE_SRCBLEND_SRCALPHA: srcFactor = vec3(v.a); break;
			case GE_SRCBLEND_INVSRCALPHA: srcFactor = vec3(1.0 - v.a); break;
			case GE_SRCBLEND_DSTALPHA: srcFactor = vec3(destColor.a); break;
			case GE_SRCBLEND_INVDSTALPHA: srcFactor = vec3(1.0 - destColor.a); break;
			case GE_SRCBLEND_DOUBLESRCALPHA: srcFactor = vec3(v.a * 2.0); break;
			case GE_SRCBLEND_DOUBLEINVSRCALPHA: srcFactor = vec3(1.0 - v.a * 2.0); break;
			case GE_SRCBLEND_DOUBLEDSTALPHA: srcFactor = vec3(destColor.a * 2.0); break;
			case GE_SRCBLEND_DOUBLEINVDSTALPHA: srcFactor = vec3(1.0 - destColor.a * 2.0); break;
			case GE_SRCBLEND_FIXA: srcFactor = base.blendFixA; break;
			}
			switch (FS_BIT_BLENDFUNC_B) {
			case GE_DSTBLEND_SRCCOLOR: dstFactor = v.rgb; break;
			case GE_DSTBLEND_INVSRCCOLOR: dstFactor = (vec3(1.0) - v.rgb); break;
			case GE_DSTBLEND_SRCALPHA: dstFactor = vec3(v.a); break;
			case GE_DSTBLEND_INVSRCALPHA: dstFactor = vec3(1.0 - v.a); break;
			case GE_DSTBLEND_DSTALPHA: dstFactor = vec3(destColor.a); break;
			case GE_DSTBLEND_INVDSTALPHA: dstFactor = vec3(1.0 - destColor.a); break;
			case GE_DSTBLEND_DOUBLESRCALPHA: dstFactor = vec3(v.a * 2.0); break;
			case GE_DSTBLEND_DOUBLEINVSRCALPHA: dstFactor = vec3(1.0 - v.a * 2.0); break;
			case GE_DSTBLEND_DOUBLEDSTALPHA: dstFactor = vec3(destColor.a * 2.0); break;
			case GE_DSTBLEND_DOUBLEINVDSTALPHA: dstFactor = vec3(1.0 - destColor.a * 2.0); break;
			case GE_DSTBLEND_FIXB: dstFactor = base.blendFixB; break;
			}

			switch (FS_BIT_BLENDEQ) {
			case GE_BLENDMODE_MUL_AND_ADD: v.rgb = v.rgb * srcFactor + destColor.rgb * dstFactor; break;
			case GE_BLENDMODE_MUL_AND_SUBTRACT: v.rgb = v.rgb * srcFactor - destColor.rgb * dstFactor; break;
			case GE_BLENDMODE_MUL_AND_SUBTRACT_REVERSE: v.rgb = destColor.rgb * dstFactor - v.rgb * srcFactor; break;
			case GE_BLENDMODE_MIN: v.rgb = min(v.rgb, destColor.rgb); break;
			case GE_BLENDMODE_MAX: v.rgb = max(v.rgb, destColor.rgb); break;
			case GE_BLENDMODE_ABSDIFF: v.rgb = abs(v.rgb - destColor.rgb); break;
			}
		}

		if (FS_BIT_REPLACE_BLEND == REPLACE_BLEND_2X_ALPHA || FS_BIT_REPLACE_BLEND == REPLACE_BLEND_PRE_SRC_2X_ALPHA) {
			v.a = v.a * 2.0;
		}
	}

	float replacedAlpha = 0.0;
	if (FS_BIT_STENCIL_TO_ALPHA != REPLACE_ALPHA_NO) {
		switch (FS_BIT_REPLACE_ALPHA_WITH_STENCIL_TYPE) {
		case STENCIL_VALUE_UNIFORM: replacedAlpha = base.stencilReplace; break;
		case STENCIL_VALUE_ZERO:
			replacedAlpha = 0.0;
			break;
			// In invert, we subtract by one, but we want to output one here.
		case STENCIL_VALUE_ONE:
		case STENCIL_VALUE_INVERT:
			replacedAlpha = 1.0;
			break;
			// We're adding/subtracting, just by the smallest value in 4-bit.
		case STENCIL_VALUE_INCR_4:
		case STENCIL_VALUE_DECR_4:
			replacedAlpha = 1.0 / 15.0;
			break;
			// We're adding/subtracting, just by the smallest value in 8-bit.
		case STENCIL_VALUE_INCR_8:
		case STENCIL_VALUE_DECR_8: replacedAlpha = 1.0 / 255.0; break;
		case STENCIL_VALUE_KEEP:
			// Do nothing. We'll mask out the alpha using color mask.
			break;
		}
	}

	switch (FS_BIT_STENCIL_TO_ALPHA) {
	case REPLACE_ALPHA_DUALSOURCE:
		fragColor0 = vec4(v.rgb, replacedAlpha);
		fragColor1 = vec4(0.0, 0.0, 0.0, v.a);
		break;
	case REPLACE_ALPHA_YES: fragColor0 = vec4(v.rgb, replacedAlpha); break;
	case REPLACE_ALPHA_NO: fragColor0 = v; break;
	default: break; // Bad stencil-to-alpha type, corrupt ID?
	}

	switch (FS_BIT_REPLACE_LOGIC_OP_TYPE) {
	case LOGICOPTYPE_ONE: fragColor0.rgb = vec3(1.0, 1.0, 1.0); break;
	case LOGICOPTYPE_INVERT: fragColor0.rgb = vec3(1.0, 1.0, 1.0) - fragColor0.rgb; break;
	case LOGICOPTYPE_NORMAL: break;
	default: break; // Bad logic op type, corrupt ID?
	}

#if defined(GPU_SCALE_DEPTH_FROM_24BIT_TO_16BIT) && defined(GPU_SUPPORTS_ACCURATE_DEPTH)
#define scale (256.0f * 65535.0)
#define offset 0.5f
#elif defined(GPU_SUPPORTS_DEPTH_CLAMP) || !defined(GPU_SUPPORTS_ACCURATE_DEPTH)
#define scale 65535.0
#define offset 0.0f
#else
#define scale (4.0f * 65535.0)
#define offset 0.5f
#endif

#ifdef GPU_ROUND_FRAGMENT_DEPTH_TO_16BIT
	gl_FragDepth = (floor((gl_FragCoord.z * scale) - offset) + offset) * (1.0 / scale);
#endif
}
