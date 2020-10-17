#include <cstdlib>

#include "GPU/ge_constants.h"
#include "GPU/Common/VertexShaderGeneratorCommon.h"

#define WRITE p+=sprintf

char *WriteLights(char *p, const VShaderID &id, DoLightComputation doLight[4], bool *specularIsZero) {
	bool useHWTransform = id.Bit(VS_BIT_USE_HW_TRANSFORM);
	bool hasColor = id.Bit(VS_BIT_HAS_COLOR) || !useHWTransform;
	int matUpdate = id.Bits(VS_BIT_MATERIAL_UPDATE, 3);
	bool enableLighting = id.Bit(VS_BIT_LIGHTING_ENABLE);

	bool doBezier = id.Bit(VS_BIT_BEZIER);
	bool doSpline = id.Bit(VS_BIT_SPLINE);

	const char *ambientStr = ((matUpdate & 1) && hasColor) ? "color0" : "u_matambientalpha";
	const char *diffuseStr = ((matUpdate & 2) && hasColor) ? "color0.rgb" : "u_matdiffuse";
	const char *specularStr = ((matUpdate & 4) && hasColor) ? "color0.rgb" : "u_matspecular.rgb";
	if (doBezier || doSpline) {
		// TODO: Probably, should use hasColorTess but FF4 has a problem with drawing the background.
		ambientStr = (matUpdate & 1) && hasColor ? "tess.col" : "u_matambientalpha";
		diffuseStr = (matUpdate & 2) && hasColor ? "tess.col.rgb" : "u_matdiffuse";
		specularStr = (matUpdate & 4) && hasColor ? "tess.col.rgb" : "u_matspecular.rgb";
	}

	bool diffuseIsZero = true;
	bool distanceNeeded = false;
	*specularIsZero = true;

	if (enableLighting) {
		WRITE(p, "  vec4 lightSum0 = u_ambient * %s + vec4(u_matemissive, 0.0);\n", ambientStr);

		for (int i = 0; i < 4; i++) {
			GELightType type = static_cast<GELightType>(id.Bits(VS_BIT_LIGHT0_TYPE + 4 * i, 2));
			GELightComputation comp = static_cast<GELightComputation>(id.Bits(VS_BIT_LIGHT0_COMP + 4 * i, 2));
			if (doLight[i] != LIGHT_FULL)
				continue;
			diffuseIsZero = false;
			if (comp == GE_LIGHTCOMP_BOTH)
				*specularIsZero = false;
			if (type != GE_LIGHTTYPE_DIRECTIONAL)
				distanceNeeded = true;
		}

		if (!*specularIsZero) {
			WRITE(p, "  vec3 lightSum1 = vec3(0.0);\n");
		}
		if (!diffuseIsZero) {
			WRITE(p, "  vec3 toLight;\n");
			WRITE(p, "  vec3 diffuse;\n");
		}
		if (distanceNeeded) {
			WRITE(p, "  float distance;\n");
			WRITE(p, "  float lightScale;\n");
		}
	}

	// Calculate lights if needed. If shade mapping is enabled, lights may need to be
	// at least partially calculated.
	for (int i = 0; i < 4; i++) {
		if (doLight[i] != LIGHT_FULL)
			continue;

		GELightType type = static_cast<GELightType>(id.Bits(VS_BIT_LIGHT0_TYPE + 4 * i, 2));
		GELightComputation comp = static_cast<GELightComputation>(id.Bits(VS_BIT_LIGHT0_COMP + 4 * i, 2));

		if (type == GE_LIGHTTYPE_DIRECTIONAL) {
			// We prenormalize light positions for directional lights.
			WRITE(p, "  toLight = u_lightpos[%i];\n", i);
		} else {
			WRITE(p, "  toLight = u_lightpos[%i] - worldpos;\n", i);
			WRITE(p, "  distance = length(toLight);\n");
			WRITE(p, "  toLight /= distance;\n");
		}

		bool doSpecular = comp == GE_LIGHTCOMP_BOTH;
		bool poweredDiffuse = comp == GE_LIGHTCOMP_ONLYPOWDIFFUSE;

		WRITE(p, "  mediump float dot%i = dot(toLight, worldnormal);\n", i);
		if (poweredDiffuse) {
			// pow(0.0, 0.0) may be undefined, but the PSP seems to treat it as 1.0.
			// Seen in Tales of the World: Radiant Mythology (#2424.)
			WRITE(p, "  if (u_matspecular.a <= 0.0) {\n");
			WRITE(p, "    dot%i = 1.0;\n", i);
			WRITE(p, "  } else {\n");
			WRITE(p, "    dot%i = pow(max(dot%i, 0.0), u_matspecular.a);\n", i, i);
			WRITE(p, "  }\n");
		}

		const char *timesLightScale = " * lightScale";

		// Attenuation
		switch (type) {
		case GE_LIGHTTYPE_DIRECTIONAL:
			timesLightScale = "";
			break;
		case GE_LIGHTTYPE_POINT:
			WRITE(p, "  lightScale = clamp(1.0 / dot(u_lightatt[%i], vec3(1.0, distance, distance*distance)), 0.0, 1.0);\n", i);
			break;
		case GE_LIGHTTYPE_SPOT:
		case GE_LIGHTTYPE_UNKNOWN:
			WRITE(p, "  float angle%i = length(u_lightdir[%i]) == 0.0 ? 0.0 : dot(normalize(u_lightdir[%i]), toLight);\n", i, i, i);
			WRITE(p, "  if (angle%i >= u_lightangle_spotCoef[%i].x) {\n", i, i);
			WRITE(p, "    lightScale = clamp(1.0 / dot(u_lightatt[%i], vec3(1.0, distance, distance*distance)), 0.0, 1.0) * (u_lightangle_spotCoef[%i].y <= 0.0 ? 1.0 : pow(angle%i, u_lightangle_spotCoef[%i].y));\n", i, i, i, i);
			WRITE(p, "  } else {\n");
			WRITE(p, "    lightScale = 0.0;\n");
			WRITE(p, "  }\n");
			break;
		default:
			// ILLEGAL
			break;
		}

		WRITE(p, "  diffuse = (u_lightdiffuse[%i] * %s) * max(dot%i, 0.0);\n", i, diffuseStr, i);
		if (doSpecular) {
			WRITE(p, "  if (dot%i >= 0.0) {\n", i);
			WRITE(p, "    dot%i = dot(normalize(toLight + vec3(0.0, 0.0, 1.0)), worldnormal);\n", i);
			WRITE(p, "    if (u_matspecular.a <= 0.0) {\n");
			WRITE(p, "      dot%i = 1.0;\n", i);
			WRITE(p, "    } else {\n");
			WRITE(p, "      dot%i = pow(max(dot%i, 0.0), u_matspecular.a);\n", i, i);
			WRITE(p, "    }\n");
			WRITE(p, "    if (dot%i > 0.0)\n", i);
			WRITE(p, "      lightSum1 += u_lightspecular[%i] * %s * dot%i %s;\n", i, specularStr, i, timesLightScale);
			WRITE(p, "  }\n");
		}
		WRITE(p, "  lightSum0.rgb += (u_lightambient[%i] * %s.rgb + diffuse)%s;\n", i, ambientStr, timesLightScale);
	}
	return p;
}
