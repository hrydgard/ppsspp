#version 450
#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shading_language_420pack : enable

//#define FLAT_SHADE // VS_BIT_FLATSHADE

struct light_t {
	int COMP;
	int TYPE;
	bool ENABLE;
};

struct TessData {
	vec4 pos;
	vec4 uv;
	vec4 color;
};

struct TessWeight {
	vec4 basis;
	vec4 deriv;
};

/* clang-format off */
layout(std140, set = 0, binding = 3) uniform baseVars {
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
} base;

layout(std140, set = 0, binding = 4) uniform lightVars {
	vec4 u_ambient;
	vec3 matdiffuse;
	vec4 matspecular;
	vec3 matemissive;
	vec3 pos[4];
	vec3 dir[4];
	vec3 att[4];
	vec2 angle_spotCoef[4];
	vec3 ambient[4];
	vec3 diffuse[4];
	vec3 specular[4];
} light;

layout(std140, set = 0, binding = 5) uniform boneVars {
	mat3x4 m[8];
} bone;

layout(std140, set = 0, binding = 6) uniform UB_VSID {
	light_t VS_BIT_LIGHT[4];
	bool VS_BIT_LIGHTING_ENABLE;
	int VS_BIT_MATERIAL_UPDATE;
	bool VS_BIT_LMODE;
	int VS_BIT_LS0;
	int VS_BIT_LS1;
	bool VS_BIT_IS_THROUGH;
	bool VS_BIT_USE_HW_TRANSFORM;
	bool VS_BIT_DO_TEXTURE;
	bool VS_BIT_DO_TEXTURE_TRANSFORM;
	int VS_BIT_UVGEN_MODE;
	int VS_BIT_UVPROJ_MODE;
	bool VS_BIT_HAS_TEXCOORD;
	bool VS_BIT_HAS_TEXCOORD_TESS;
	bool VS_BIT_HAS_COLOR;
	bool VS_BIT_HAS_COLOR_TESS;
	bool VS_BIT_HAS_NORMAL;
	bool VS_BIT_HAS_NORMAL_TESS;
	bool VS_BIT_NORM_REVERSE;
	bool VS_BIT_NORM_REVERSE_TESS;
	bool VS_BIT_ENABLE_BONES;
	int VS_BIT_BONES;
	int VS_BIT_WEIGHT_FMTSCALE;
	bool VS_BIT_SPLINE;
	bool VS_BIT_BEZIER;
	bool GPU_ROUND_DEPTH_TO_16BIT;
	bool GPU_SUPPORTS_VS_RANGE_CULLING;
};

layout(std430, set = 0, binding = 8) readonly buffer s_tess_data {
	TessData data[];
} tess_data;

layout(std430, set = 0, binding = 9) readonly buffer s_tess_weights_u {
	TessWeight data[];
} tess_weights_u;

layout(std430, set = 0, binding = 10) readonly buffer s_tess_weights_v {
	TessWeight data[];
} tess_weights_v;

layout(location = 0) in vec4 position;
layout(location = 1) in vec3 texcoord;
layout(location = 2) in vec3 normal;
layout(location = 3) in vec4 w1;
layout(location = 4) in vec4 w2;
layout(location = 5) in vec4 color0;
layout(location = 6) in vec3 color1;

out gl_PerVertex { vec4 gl_Position; };

#define GE_LIGHTTYPE_DIRECTIONAL 0
#define GE_LIGHTTYPE_POINT 1
#define GE_LIGHTTYPE_SPOT 2
#define GE_LIGHTTYPE_UNKNOWN 3
#define GE_LIGHTCOMP_ONLYDIFFUSE 0
#define GE_LIGHTCOMP_BOTH 1
#define GE_LIGHTCOMP_ONLYPOWDIFFUSE 2

#define GE_TEXMAP_TEXTURE_COORDS 0
#define GE_TEXMAP_TEXTURE_MATRIX 1
#define GE_TEXMAP_ENVIRONMENT_MAP 2
#define GE_TEXMAP_UNKNOWN 3

#define GE_PROJMAP_POSITION 0
#define GE_PROJMAP_UV 1
#define GE_PROJMAP_NORMALIZED_NORMAL 2
#define GE_PROJMAP_NORMAL 3

#ifdef FLAT_SHADE
#define shading flat
#else
#define shading
#endif

layout(location = 0) out vec3 v_texcoord;
layout(location = 1) shading out vec4 v_color0;
layout(location = 2) shading out vec3 v_color1;
layout(location = 3) out float v_fogdepth;

/* clang-format on */

vec2 tess_sample(in vec2 points[16], mat4 weights) {
	vec2 pos = vec2(0.0);
	for (int v = 0; v < 4; ++v) {
		for (int u = 0; u < 4; ++u) {
			pos += weights[v][u] * points[v * 4 + u];
		}
	}
	return pos;
}

vec3 tess_sample(in vec3 points[16], mat4 weights) {
	vec3 pos = vec3(0.0);
	for (int v = 0; v < 4; ++v) {
		for (int u = 0; u < 4; ++u) {
			pos += weights[v][u] * points[v * 4 + u];
		}
	}
	return pos;
}
vec4 tess_sample(in vec4 points[16], mat4 weights) {
	vec4 pos = vec4(0.0);
	for (int v = 0; v < 4; ++v) {
		for (int u = 0; u < 4; ++u) {
			pos += weights[v][u] * points[v * 4 + u];
		}
	}
	return pos;
}

struct Tess {
	vec3 pos;
	vec2 tex;
	vec4 col;
	vec3 nrm;
};

void tessellate(out Tess tess) {
	ivec2 point_pos = ivec2(position.z, normal.z) * (VS_BIT_BEZIER ? 3 : 1);
	ivec2 weight_idx = ivec2(position.xy);
	// Load 4x4 control points
	vec3 _pos[16];
	vec2 _tex[16];
	vec4 _col[16];
	int index;
	for (int i = 0; i < 4; i++) {
		for (int j = 0; j < 4; j++) {
			index = (i + point_pos.y) * int(base.spline_counts) + (j + point_pos.x);
			_pos[i * 4 + j] = tess_data.data[index].pos.xyz;
			if (VS_BIT_DO_TEXTURE && VS_BIT_HAS_TEXCOORD_TESS)
				_tex[i * 4 + j] = tess_data.data[index].uv.xy;
			if (VS_BIT_HAS_COLOR_TESS)
				_col[i * 4 + j] = tess_data.data[index].color;
		}
	}

	// Basis polynomials as weight coefficients
	vec4 basis_u = tess_weights_u.data[weight_idx.x].basis;
	vec4 basis_v = tess_weights_v.data[weight_idx.y].basis;
	mat4 basis = outerProduct(basis_u, basis_v);

	// Tessellate
	tess.pos = tess_sample(_pos, basis);
	if (VS_BIT_DO_TEXTURE) {
		if (VS_BIT_HAS_TEXCOORD_TESS)
			tess.tex = tess_sample(_tex, basis);
		else
			tess.tex = normal.xy;
	}
	if (VS_BIT_HAS_COLOR_TESS)
		tess.col = tess_sample(_col, basis);
	else
		tess.col = base.matambientalpha;
	if (VS_BIT_HAS_NORMAL_TESS) {
		// Derivatives as weight coefficients
		vec4 deriv_u = tess_weights_u.data[weight_idx.x].deriv;
		vec4 deriv_v = tess_weights_v.data[weight_idx.y].deriv;

		vec3 du = tess_sample(_pos, outerProduct(deriv_u, basis_v));
		vec3 dv = tess_sample(_pos, outerProduct(basis_u, deriv_v));
		tess.nrm = normalize(cross(du, dv));
	}
}

void main() {
	if (!VS_BIT_USE_HW_TRANSFORM) {
		// Simple pass-through of vertex data to fragment shader
		if (VS_BIT_DO_TEXTURE) {
			if (VS_BIT_DO_TEXTURE_TRANSFORM && !VS_BIT_IS_THROUGH) {
				v_texcoord = texcoord;
			} else {
				v_texcoord = vec3(texcoord.xy, 1.0);
			}
		}
		v_color0 = color0;
		v_color1 = color1;
		v_fogdepth = position.w;
		if (VS_BIT_IS_THROUGH)
			gl_Position = base.proj_through_mtx * vec4(position.xyz, 1.0);
		else
			gl_Position = base.proj_mtx * vec4(position.xyz, 1.0);
	} else {
		// Step 1: World Transform / Skinning
		mediump vec3 worldnormal;
		vec3 worldpos;
		Tess tess;
		if (!VS_BIT_ENABLE_BONES) {
			if (VS_BIT_BEZIER || VS_BIT_SPLINE) {
				// Hardware tessellation
				tessellate(tess);
				worldpos = vec4(tess.pos.xyz, 1.0) * base.world_mtx;
				if (VS_BIT_HAS_NORMAL_TESS) {
					worldnormal = normalize(vec4(VS_BIT_NORM_REVERSE_TESS ? -tess.nrm : tess.nrm, 0.0) * base.world_mtx);
				} else {
					worldnormal = vec3(0.0, 0.0, 1.0);
				}
			} else {
				// No skinning, just standard T&L.
				worldpos = vec4(position.xyz, 1.0) * base.world_mtx;
				if (VS_BIT_HAS_NORMAL)
					worldnormal = normalize(vec4(VS_BIT_NORM_REVERSE ? -normal : normal, 0.0) * base.world_mtx);
				else
					worldnormal = vec3(0.0, 0.0, 1.0);
			}
		} else {
			const float rescale[4] = { 1.0f, 1.9921875f, 1.999969482421875, 1.0f }; // 2*127.5f/128.f, 2*32767.5f/32768.f, 1.0f};
			const float boneWeightAttr[8] = { w1.x, w1.y, w1.z, w1.w, w2.x, w2.y, w2.z, w2.w };

			mat3x4 skinMatrix = w1.x * bone.m[0];
			for (int i = 0; i < VS_BIT_BONES; i++) {
				skinMatrix += boneWeightAttr[i + 1] * bone.m[i + 1];
			}

			// Trying to simplify this results in bugs in LBP...
			const float factor = rescale[VS_BIT_WEIGHT_FMTSCALE];
			vec3 skinnedpos = vec4(position.xyz, 1.0) * skinMatrix * factor;
			worldpos = vec4(skinnedpos, 1.0) * base.world_mtx;

			mediump vec3 skinnednormal;
			if (VS_BIT_HAS_NORMAL) {
				skinnednormal = vec4((VS_BIT_NORM_REVERSE ? -normal : normal), 0.0) * skinMatrix * factor;
			} else {
				skinnednormal = vec4(0.0, 0.0, (VS_BIT_NORM_REVERSE ? -1.0 : 1.0), 0.0) * skinMatrix * factor;
			}
			worldnormal = normalize(vec4(skinnednormal, 0.0) * base.world_mtx);
		}

		vec4 viewPos = vec4(vec4(worldpos, 1.0) * base.view_mtx, 1.0);

		// Final view and projection transforms.
		gl_Position = base.proj_mtx * viewPos;

		if (VS_BIT_LIGHTING_ENABLE) {
			vec4 ambientStr = (bool(VS_BIT_MATERIAL_UPDATE & 1) && VS_BIT_HAS_COLOR) ? color0 : base.matambientalpha;
			vec3 diffuseStr = (bool(VS_BIT_MATERIAL_UPDATE & 2) && VS_BIT_HAS_COLOR) ? color0.rgb : light.matdiffuse;
			vec3 specularStr = (bool(VS_BIT_MATERIAL_UPDATE & 4) && VS_BIT_HAS_COLOR) ? color0.rgb : light.matspecular.rgb;
			if (VS_BIT_BEZIER || VS_BIT_SPLINE) {
				// TODO: Probably, should use hasColorTess but FF4 has a problem with drawing the background.
				ambientStr = bool(VS_BIT_MATERIAL_UPDATE & 1) && VS_BIT_HAS_COLOR ? tess.col : base.matambientalpha;
				diffuseStr = bool(VS_BIT_MATERIAL_UPDATE & 2) && VS_BIT_HAS_COLOR ? tess.col.rgb : light.matdiffuse;
				specularStr = bool(VS_BIT_MATERIAL_UPDATE & 4) && VS_BIT_HAS_COLOR ? tess.col.rgb : light.matspecular.rgb;
			}

			vec4 lightSum0 = light.u_ambient * ambientStr + vec4(light.matemissive, 0.0);
			vec3 lightSum1 = vec3(0.0);

			// Calculate lights if needed.
			for (int i = 0; i < 4; i++) {
				if (!VS_BIT_LIGHT[i].ENABLE)
					continue;

				vec3 toLight = light.pos[i];
				float lightScale;
				if (VS_BIT_LIGHT[i].TYPE == GE_LIGHTTYPE_DIRECTIONAL) {
					// We prenormalize light positions for directional lights.
					lightScale = 1.0f;
				} else {
					toLight -= worldpos;
					float distance = length(toLight);
					toLight /= distance;
					lightScale = clamp(1.0 / dot(light.att[i], vec3(1.0, distance, distance * distance)), 0.0, 1.0);
				}

				if (VS_BIT_LIGHT[i].TYPE >= GE_LIGHTTYPE_SPOT) {
					float anglei = length(light.dir[i]) == 0.0 ? 0.0 : dot(normalize(light.dir[i]), toLight);
					if (anglei < light.angle_spotCoef[i].x)
						lightScale = 0.0;
					else if (light.angle_spotCoef[i].y > 0.0)
						lightScale *= pow(anglei, light.angle_spotCoef[i].y);
				}
				mediump float doti = dot(toLight, worldnormal);

				if (VS_BIT_LIGHT[i].COMP == GE_LIGHTCOMP_ONLYPOWDIFFUSE) {
					// pow(0.0, 0.0) may be undefined, but the PSP seems to treat it as 1.0.
					// Seen in Tales of the World: Radiant Mythology (#2424.)
					if (light.matspecular.a <= 0.0)
						doti = 1.0;
					else
						doti = pow(max(doti, 0.0), light.matspecular.a);
				}

				vec3 diffuse = (light.diffuse[i] * diffuseStr) * max(doti, 0.0);
				if (VS_BIT_LIGHT[i].COMP == GE_LIGHTCOMP_BOTH) {
					if (doti >= 0.0) {
						doti = dot(normalize(toLight + vec3(0.0, 0.0, 1.0)), worldnormal);
						if (light.matspecular.a <= 0.0) {
							doti = 1.0;
						} else {
							doti = pow(max(doti, 0.0), light.matspecular.a);
						}
						if (doti > 0.0)
							lightSum1 += light.specular[i] * specularStr * doti * lightScale;
					}
				}
				lightSum0.rgb += (light.ambient[i] * ambientStr.rgb + diffuse) * lightScale;
			}

			// Sum up ambient, emissive here.
			if (VS_BIT_LMODE) {
				v_color0 = clamp(lightSum0, 0.0, 1.0);
				v_color1 = clamp(lightSum1, 0.0, 1.0);
			} else {
				v_color0 = clamp(clamp(lightSum0, 0.0, 1.0) + vec4(lightSum1, 0.0), 0.0, 1.0);
			}
		} else {
			// Lighting doesn't affect color.
			if (VS_BIT_HAS_COLOR) {
				if (VS_BIT_BEZIER || VS_BIT_SPLINE)
					v_color0 = tess.col;
				else
					v_color0 = color0;
			} else {
				v_color0 = base.matambientalpha;
			}
			if (VS_BIT_LMODE)
				v_color1 = vec3(0.0);
		}

		// Step 3: UV generation
		if (VS_BIT_DO_TEXTURE) {
			switch (VS_BIT_UVGEN_MODE) {
			case GE_TEXMAP_TEXTURE_COORDS: // Scale-offset. Easy.
			case GE_TEXMAP_UNKNOWN:        // Not sure what this is, but Riviera uses it.  Treating as coords works.
				if (!VS_BIT_IS_THROUGH) {
					if (VS_BIT_HAS_TEXCOORD) {
						if (VS_BIT_BEZIER || VS_BIT_SPLINE)
							v_texcoord = vec3(tess.tex.xy * base.uvscaleoffset.xy + base.uvscaleoffset.zw, 0.0);
						else
							v_texcoord = vec3(texcoord.xy * base.uvscaleoffset.xy, 0.0);
					} else {
						v_texcoord = vec3(0.0);
					}
				} else {
					// should actually never be hit since VS_BIT_USE_HW_TRANSFORM == true.
					if (VS_BIT_HAS_TEXCOORD)
						v_texcoord = vec3(texcoord.xy * base.uvscaleoffset.xy + base.uvscaleoffset.zw, 0.0);
					else
						v_texcoord = vec3(base.uvscaleoffset.zw, 0.0);
				}
				break;

			case GE_TEXMAP_TEXTURE_MATRIX: // Projection mapping.
			{
				vec4 temp_tc;
				switch (VS_BIT_UVPROJ_MODE) {
				case GE_PROJMAP_POSITION: // Use model space XYZ as source
					temp_tc = vec4(position.xyz, 1.0);
					break;
				case GE_PROJMAP_UV: // Use unscaled UV as source
					if (VS_BIT_HAS_TEXCOORD)
						temp_tc = vec4(texcoord.xy, 0.0, 1.0);
					else
						temp_tc = vec4(0.0, 0.0, 0.0, 1.0);
					break;
				case GE_PROJMAP_NORMALIZED_NORMAL: // Use normalized transformed normal as source
					if (VS_BIT_HAS_NORMAL)
						temp_tc = VS_BIT_NORM_REVERSE ? vec4(normalize(-normal), 1.0) : vec4(normalize(normal), 1.0);
					else
						temp_tc = vec4(0.0, 0.0, 1.0, 1.0);
					break;
				case GE_PROJMAP_NORMAL: // Use non-normalized transformed normal as source
					if (VS_BIT_HAS_NORMAL)
						temp_tc = VS_BIT_NORM_REVERSE ? vec4(-normal, 1.0) : vec4(normal, 1.0);
					else
						temp_tc = vec4(0.0, 0.0, 1.0, 1.0);
					break;
				}
				// Transform by texture matrix. XYZ as we are doing projection mapping.
				v_texcoord = (temp_tc * base.tex_mtx).xyz * vec3(base.uvscaleoffset.xy, 1.0);
			} break;

			case GE_TEXMAP_ENVIRONMENT_MAP: // Shade mapping - use dots from light sources.
			{
				float lightFactor0 = length(light.pos[VS_BIT_LS0]) == 0.0 ? worldnormal.z : dot(normalize(light.pos[VS_BIT_LS0]), worldnormal);
				float lightFactor1 = length(light.pos[VS_BIT_LS1]) == 0.0 ? worldnormal.z : dot(normalize(light.pos[VS_BIT_LS1]), worldnormal);
				v_texcoord = vec3(base.uvscaleoffset.xy * vec2(1.0 + lightFactor0, 1.0 + lightFactor1) * 0.5, 1.0);
			} break;

			default:
				// ILLEGAL
				break;
			}
		}

		// Compute fogdepth
		v_fogdepth = (viewPos.z + base.fogcoef.x) * base.fogcoef.y;
	}

	if (!VS_BIT_IS_THROUGH) {
		if (GPU_ROUND_DEPTH_TO_16BIT) {
			// Apply the projection and viewport to get the Z buffer value, floor to integer, undo the viewport and projection.
			gl_Position.z /= gl_Position.w;
			gl_Position.z = gl_Position.z * base.depthRange.x + base.depthRange.y;
			gl_Position.z = floor(gl_Position.z);
			gl_Position.z = (gl_Position.z - base.depthRange.z) * base.depthRange.w;
			gl_Position.z *= gl_Position.w;
		}
		if (GPU_SUPPORTS_VS_RANGE_CULLING) {
			vec3 projPos = gl_Position.xyz / gl_Position.w;
			// Vertex range culling doesn't happen when depth is clamped, so only do this if in range.
			if (base.cullRangeMin.w <= 0.0 || (projPos.z >= base.cullRangeMin.z && projPos.z <= base.cullRangeMax.z)) {
				bool outMin = projPos.x < base.cullRangeMin.x || projPos.y < base.cullRangeMin.y || projPos.z < base.cullRangeMin.z;
				bool outMax = projPos.x > base.cullRangeMax.x || projPos.y > base.cullRangeMax.y || projPos.z > base.cullRangeMax.z;
				if (outMin || outMax) {
					gl_Position = vec4(base.cullRangeMax.w);
				}
			}
		}
	}
// gl_PointSize = 1.0;
}
