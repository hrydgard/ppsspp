#version 150
#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shading_language_420pack : enable

// DEFAULT   0
// HW_SKIN   1
// HW_NOSKIN 2
// SW        3

#define BUILD_TYPE 0
//#define FLAT_SHADING

struct light_t
{
   int COMP;
   int TYPE;
   int ENABLE;
};

layout(std140, binding = 4) uniform UB_VSID
{
   light_t VS_BIT_LIGHT[4];
   bool VS_BIT_LMODE;
   bool VS_BIT_IS_THROUGH;
   bool VS_BIT_ENABLE_FOG;
   bool VS_BIT_HAS_COLOR;
   bool VS_BIT_DO_TEXTURE;
   bool VS_BIT_DO_TEXTURE_TRANSFORM;
   bool VS_BIT_USE_HW_TRANSFORM;
   bool VS_BIT_HAS_NORMAL;
   bool VS_BIT_NORM_REVERSE;
   bool VS_BIT_HAS_TEXCOORD;
   bool VS_BIT_HAS_COLOR_TESS;
   bool VS_BIT_HAS_TEXCOORD_TESS;
   bool VS_BIT_NORM_REVERSE_TESS;
   int  VS_BIT_UVGEN_MODE;
   int  VS_BIT_UVPROJ_MODE;
   int  VS_BIT_LS0;
   int  VS_BIT_LS1;
   int  VS_BIT_BONES;
   bool VS_BIT_ENABLE_BONES;
   int  VS_BIT_MATERIAL_UPDATE;
   bool VS_BIT_SPLINE;
   bool VS_BIT_LIGHTING_ENABLE;
   int  VS_BIT_WEIGHT_FMTSCALE;
   bool VS_BIT_FLATSHADE;
   bool VS_BIT_BEZIER;
   bool GPU_ROUND_DEPTH_TO_16BIT;
};


layout(std140, binding = 1) uniform baseVars
{
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
} base;

layout(std140, binding = 2) uniform lightVars
{
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

layout(std140, binding = 3) uniform boneVars
{
   mat3x4 m[8];
} bone;

layout(location = 0) in vec4 position;
layout(location = 1) in vec3 texcoord;
layout(location = 2) in vec4 color0;
layout(location = 3) in vec3 color1;
layout(location = 4) in vec3 normal;
layout(location = 5) in vec4 weight0;
layout(location = 6) in vec4 weight1;

out gl_PerVertex { vec4 gl_Position; };

#define GE_LIGHTTYPE_DIRECTIONAL 0
#define GE_LIGHTTYPE_POINT 1
#define GE_LIGHTTYPE_SPOT 2
#define GE_LIGHTTYPE_UNKNOWN 3
#define GE_LIGHTCOMP_ONLYDIFFUSE 0
#define GE_LIGHTCOMP_BOTH 1
#define GE_LIGHTCOMP_BOTHWITHPOWDIFFUSE 2

#define GE_TEXMAP_TEXTURE_COORDS 0
#define GE_TEXMAP_TEXTURE_MATRIX 1
#define GE_TEXMAP_ENVIRONMENT_MAP 2
#define GE_TEXMAP_UNKNOWN 3

#define GE_PROJMAP_POSITION 0
#define GE_PROJMAP_UV 1
#define GE_PROJMAP_NORMALIZED_NORMAL 2
#define GE_PROJMAP_NORMAL 3

#ifdef FLAT_SHADING
#define shading flat
#else
#define shading
#endif

layout(location = 0) out vec3 v_texcoord;
layout(location = 1) shading out vec4 v_color0;
layout(location = 2) shading out vec3 v_color1;
layout(location = 3) out float v_fogdepth;

// DEFAULT   0
// HW_SKIN   1
// HW_NOSKIN 2
// SW        3

#if BUILD_TYPE == 1
#define VS_BIT_USE_HW_TRANSFORM  true
#define VS_BIT_ENABLE_BONES      true
#elif BUILD_TYPE == 2
#define VS_BIT_USE_HW_TRANSFORM  true
#define VS_BIT_ENABLE_BONES      false
#elif BUILD_TYPE == 3
#define VS_BIT_USE_HW_TRANSFORM  false
#endif

void main()
{
   v_color1 = vec3(0.0);
   if (VS_BIT_USE_HW_TRANSFORM)
   {
      vec4 pos = vec4(position.xyz, 1.0);
      vec4 nrm = vec4(0.0, 0.0, 1.0, 0.0);
      if (VS_BIT_HAS_NORMAL)
         nrm.xyz = normal;
      if (VS_BIT_NORM_REVERSE)
         nrm.xyz = -nrm.xyz;
      if (VS_BIT_ENABLE_BONES)
      {
         float weights[8] = float[8](weight0.x, weight0.y, weight0.z, weight0.w, weight1.x, weight1.y, weight1.z, weight1.w);
         mat3x4 skinMatrix = weight0.x * bone.m[0];
         for (int i = 1; i < VS_BIT_BONES + 1; i++)
            skinMatrix += weights[i] * bone.m[i];

         pos.xyz = pos * skinMatrix;
         nrm.xyz = nrm * skinMatrix;
      }
      // Step 1: World Transform
      vec4 worldpos = vec4(pos * base.world_mtx, 1.0);
      mediump vec3 worldnormal = normalize(nrm * base.world_mtx);
      vec4 viewPos = vec4(worldpos * base.view_mtx, 1.0);
      // Final view and projection transforms.
      gl_Position = base.proj_mtx * viewPos;

      // Calculate lights if needed. If shade mapping is enabled, lights may need to be
      // at least partially calculated.
      if (VS_BIT_LIGHTING_ENABLE)
      {
         // TODO: Declare variables for dots for shade mapping if needed
         vec4 ambient = base.matambientalpha;
         vec3 diffuse = light.matdiffuse;
         vec3 specular = light.matspecular.rgb;

         if (VS_BIT_HAS_COLOR)
         {
            if (bool(VS_BIT_MATERIAL_UPDATE & 1))
               ambient = color0;

            if (bool(VS_BIT_MATERIAL_UPDATE & 2))
               diffuse = color0.rgb;

            if (bool(VS_BIT_MATERIAL_UPDATE & 4))
               specular = color0.rgb;
         }

         vec4 lightSum0 = light.u_ambient * ambient + vec4(light.matemissive, 0.0);
         vec3 lightSum1 = vec3(0.0);

         for (int i = 0; i < 4; i++)
         {
            if (!(true && bool(VS_BIT_LIGHT[i].ENABLE)))
               continue;

            vec3 toLight = light.pos[i];
            float lightScale; // Attenuation

            if (VS_BIT_LIGHT[i].TYPE == GE_LIGHTTYPE_DIRECTIONAL)
               lightScale = 1.0;
            else
            {
               // We prenormalize light positions for directional lights.
               float distance;
               toLight -= worldpos.xyz;
               distance = length(toLight);
               toLight /= distance;
               lightScale = clamp(1.0 / dot(light.att[i], vec3(1.0, distance, distance * distance)), 0.0, 1.0);
            }

            if (VS_BIT_LIGHT[i].TYPE >= GE_LIGHTTYPE_SPOT)
            {
               float angle = dot(normalize(light.dir[i]), toLight);
               if (angle >= light.angle_spotCoef[i].x)
                  lightScale *= pow(angle, light.angle_spotCoef[i].y);
               else
                  lightScale = 0.0;
            }

            // pow(0.0, 0.0) may be undefined, but the PSP seems to treat it as 1.0.
            // Seen in Tales of the World: Radiant Mythology (#2424.)
            mediump float doti = max(dot(toLight, worldnormal), 0.0000001); // smallest positive mediump is 0.00000006
            if (VS_BIT_LIGHT[i].COMP == GE_LIGHTCOMP_BOTHWITHPOWDIFFUSE)
               doti = pow(doti, light.matspecular.a); // does this only apply to lightSum0 ?

            lightSum0.rgb += (light.ambient[i] * ambient.rgb + diffuse * light.diffuse[i] * doti) * lightScale;

            // specular
            if (VS_BIT_LIGHT[i].COMP != GE_LIGHTCOMP_ONLYDIFFUSE)
            {
               doti = dot(normalize(toLight + vec3(0.0, 0.0, 1.0)), worldnormal);
               if (doti > 0.0)
                  lightSum1 += light.specular[i] * specular * (pow(doti, light.matspecular.a) * lightScale);
            }
         }

         // Sum up ambient, emissive here.
         if (VS_BIT_LMODE)
         {
            v_color0 = clamp(lightSum0, 0.0, 1.0);
            v_color1 = clamp(lightSum1, 0.0, 1.0);
         }
         else
            v_color0 = clamp(clamp(lightSum0, 0.0, 1.0) + vec4(lightSum1, 0.0), 0.0, 1.0);
      }
      else
      {
         // Lighting doesn't affect color.
         if (VS_BIT_HAS_COLOR)
            v_color0 = color0;
         else
            v_color0 = base.matambientalpha;
      }

      // Step 3: UV generation
      if (VS_BIT_DO_TEXTURE)
      {
         switch (VS_BIT_UVGEN_MODE)
         {
         case GE_TEXMAP_TEXTURE_COORDS:  // Scale-offset. Easy.
         case GE_TEXMAP_UNKNOWN: // Not sure what this is, but Riviera uses it.  Treating as coords works.
            if (!VS_BIT_IS_THROUGH)
            {
               if (VS_BIT_HAS_TEXCOORD)
                  v_texcoord = vec3(texcoord.xy * base.uvscaleoffset.xy, 0.0);
               else
                  v_texcoord = vec3(0.0);
            }
            else
            {
               if (VS_BIT_HAS_TEXCOORD)
                  v_texcoord = vec3(texcoord.xy * base.uvscaleoffset.xy + base.uvscaleoffset.zw, 0.0);
               else
                  v_texcoord = vec3(base.uvscaleoffset.zw, 0.0);
            }
            break;

         case GE_TEXMAP_TEXTURE_MATRIX:  // Projection mapping.
         {
            vec4 temp_tc;
            switch (VS_BIT_UVPROJ_MODE)
            {
            case GE_PROJMAP_POSITION:  // Use model space XYZ as source
               temp_tc = vec4(position.xyz, 1.0);
               break;
            case GE_PROJMAP_UV:  // Use unscaled UV as source
            {
               // scaleUV is false here.
               if (VS_BIT_HAS_TEXCOORD)
                  temp_tc = vec4(texcoord.xy, 0.0, 1.0);
               else
                  temp_tc = vec4(0.0, 0.0, 0.0, 1.0);
            }
            break;
            case GE_PROJMAP_NORMALIZED_NORMAL:  // Use normalized transformed normal as source
               if (VS_BIT_HAS_NORMAL)
                  temp_tc = vec4(normalize((VS_BIT_NORM_REVERSE ? -normal : normal)), 1.0);
               else
                  temp_tc = vec4(0.0, 0.0, 1.0, 1.0);
               break;
            case GE_PROJMAP_NORMAL:  // Use non-normalized transformed normal as source
               if (VS_BIT_HAS_NORMAL)
                  temp_tc = vec4((VS_BIT_NORM_REVERSE ? -normal : normal), 1.0);
               else
                  temp_tc = vec4(0.0, 0.0, 1.0, 1.0);
               break;
            }
            // Transform by texture matrix. XYZ as we are doing projection mapping.
            v_texcoord = (temp_tc * base.tex_mtx).xyz * vec3(base.uvscaleoffset.xy, 1.0);
         }
         break;

         case GE_TEXMAP_ENVIRONMENT_MAP:  // Shade mapping - use dots from light sources.
            v_texcoord = vec3(base.uvscaleoffset.xy * vec2(1.0 + dot(normalize(light.pos[VS_BIT_LS0]), worldnormal),
                              1.0 + dot(normalize(light.pos[VS_BIT_LS1]), worldnormal)) * 0.5, 1.0);
            break;
         default:
            // ILLEGAL
            break;
         }
      }
      // Compute fogdepth
      v_fogdepth = (viewPos.z + base.fogcoef.x) * base.fogcoef.y; //   if (VS_BIT_ENABLE_FOG)
   }
   else
   {
      // Simple pass-through of vertex data to fragment shader
      if (VS_BIT_DO_TEXTURE)
      {
         if (VS_BIT_DO_TEXTURE_TRANSFORM && !VS_BIT_IS_THROUGH)
            v_texcoord = texcoord;
         else
            v_texcoord = vec3(texcoord.xy, 1.0);
      }
      if (VS_BIT_HAS_COLOR)
      {
         v_color0 = color0;
         if (VS_BIT_LMODE)
            v_color1 = color1;
      }
      else
         v_color0 = base.matambientalpha;

      v_fogdepth = position.w; //   if (VS_BIT_ENABLE_FOG)

      if (VS_BIT_IS_THROUGH)
         gl_Position = base.proj_through_mtx * vec4(position.xyz, 1.0);
      else
         gl_Position = base.proj_mtx * vec4(position.xyz, 1.0);
   }
   if (GPU_ROUND_DEPTH_TO_16BIT)
   {
      gl_Position.z /= gl_Position.w;
      gl_Position.z = gl_Position.z * base.depthRange.x + base.depthRange.y;
      gl_Position.z = floor(gl_Position.z);
      gl_Position.z = (gl_Position.z - base.depthRange.z) * base.depthRange.w;
      gl_Position.z *= gl_Position.w;
   }
}

