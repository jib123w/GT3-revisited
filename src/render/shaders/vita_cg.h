// The model shaders in Cg for the PlayStation Vita (docs/dev/VITA.md). Written by hand from
// model.vert / model.frag / no_color.frag, not generated: vitaGL's GLSL translator is made for
// OpenGL ES 2.0 style GLSL and cannot take the GLSL ES 3.00 those compile to, so on the Vita the
// OpenGL ES device (render/gpu_gles.cpp) creates these as Cg shaders (GL_CG_*_SHADER_EXT), which
// vitaGL hands to Sony's shader compiler (ur0:data/libshacccg.suprx) as they are.
//
// The uniform blocks are float4 arrays named as the blocks: their members are all vec4 / mat4, so
// in std140 a vec4 is one float4 and a mat4 four (its columns). gpu_gles.cpp uploads each draw's
// block with glUniform4fv in place of a uniform buffer binding. Samplers and attributes keep the
// GLSL names (attributes bound to the GLSL locations before the link: *_attributes below).
//
// Differences from the GLSL (all in what the Vita draws, none in its uniforms):
// - The lighting factor, fog amount and colour interpolate perspective-correct; the GLSL makes
//   them linear in screen space like the GS (times the clip w, then gl_FragCoord.w).
// - region_uv clamps to the primitive's region without the half-texel inset (no textureSize).
// - The depth of field's resampling (RESAMPLE) is one bilinear tap, not the tap grid.
// - No GS_FILTER / FX / SPIN_COPIES variants (integer texel arithmetic, the wheel spin blurs, the
//   rim copies): GpuContext::make_shader gives those stages the nearest variant here.
#pragma once

namespace gt3r::render::shaders::vita {

// Uniform block and sampler names in slot order; vertex attributes in location order.
inline constexpr char vertex_blocks[] = "VertexUniforms,VertexLighting";
inline constexpr char fragment_blocks[] = "FragmentUniforms";
inline constexpr char fragment_samplers[] = "u_tex,u_env,u_sphere";
inline constexpr char vertex_attributes[] = "a_pos,a_normal,a_uv,a_color";
inline constexpr char clipped_attributes[] =
    "a_pos0,a_normal0,a_uv0,a_color0,a_pos1,a_normal1,a_uv1,a_color1,a_pos2,a_normal2,a_uv2,a_color2,a_weights";

// model.vert (#define CLIPPED 1 in front: the guard band's clipped corners).
inline constexpr char model_vert[] = R"CG(
uniform float4 VertexUniforms[17];
uniform float4 VertexLighting[25];

#define U_MVP 0
#define U_MODEL 4
#define U_NORMAL 8
#define U_VIEW_Z VertexUniforms[12]
#define U_FOG VertexUniforms[13]
#define U_FAR_CLIP VertexUniforms[14]

#define L_EYE_DIR VertexLighting[0]
#define L_LIGHT_DIR(i) VertexLighting[1 + (i)]
#define L_LIGHT_DIFFUSE(i) VertexLighting[5 + (i)]
#define L_LIGHT_SPECULAR(i) VertexLighting[9 + (i)]
#define L_LIGHT_AMBIENT VertexLighting[13]
#define L_MAT_AMBIENT VertexLighting[14]
#define L_MAT_DIFFUSE VertexLighting[15]
#define L_MAT_SPECULAR VertexLighting[16]
#define L_MAT_EMISSION VertexLighting[17]
#define L_ENV_ROW(i) VertexLighting[18 + (i)]
#define L_ENV_C0 VertexLighting[21]
#define L_ENV_C1 VertexLighting[22]
#define L_ENV_PANO VertexLighting[23]
#define L_VERTEX_COLOR VertexLighting[24]

// A matrix (four float4 columns from b) times (p, 1).
#define XFORM(b, p) (VertexUniforms[(b)] * (p).x + VertexUniforms[(b) + 1] * (p).y + \
                     VertexUniforms[(b) + 2] * (p).z + VertexUniforms[(b) + 3])

struct Vert {
    float4 clip;
    float3 world;
    float3 normal;
    float2 uv;
    float4 color;
    float3 lit;
};

Vert vertex_of(float3 pos, float3 nrm, float2 uv, float4 a_col) {
    Vert o;
    o.clip = XFORM(U_MVP, pos);
    o.world = XFORM(U_MODEL, pos).xyz;
    o.normal = VertexUniforms[U_NORMAL].xyz * nrm.x + VertexUniforms[U_NORMAL + 1].xyz * nrm.y +
               VertexUniforms[U_NORMAL + 2].xyz * nrm.z;
    o.uv = uv;
    float4 color = L_VERTEX_COLOR.x > 0.5 ? float4(a_col.rgb * (255.0 / 128.0), a_col.a) : a_col;
    o.color = color;
    o.lit = float3(1.0, 1.0, 1.0);
    if (L_EYE_DIR.w > 2.5) {
        // GT3's environment programs (course chunk kinds 2 and 3).
        float3 n = o.normal;
        float3 D = o.world - L_EYE_DIR.xyz;
        float p = 1.0 / max(length(D), 1e-6);
        float3 R = D - 2.0 * dot(D, n) * n;
        if (L_EYE_DIR.w < 3.5) {
            float3 e = float3(dot(L_ENV_ROW(0).xyz, R), dot(L_ENV_ROW(1).xyz, R), dot(L_ENV_ROW(2).xyz, R)) * p;
            float m = max(length(e + float3(0.0, 0.0, 1.0)), 1e-6);
            o.uv = float2(0.5 + 0.5 * e.x / m, 0.5 - 0.5 * e.y / m);
        } else {
            float3 r = R.x * L_ENV_ROW(0).xyz + R.y * L_ENV_ROW(1).xyz + R.z * L_ENV_ROW(2).xyz;
            float a = r.x - r.z;
            float b = r.x + r.z;
            float u;
            if (a >= 0.0 && b >= 0.0) u = r.z / r.x;
            else if (a < 0.0 && b >= 0.0) u = 2.0 - r.x / r.z;
            else if (a < 0.0) u = 4.0 + r.z / r.x;
            else u = 6.0 - r.x / r.z;
            float y = r.y * p;
            o.uv = float2(L_ENV_PANO.x - u / 8.0, L_ENV_PANO.z - L_ENV_PANO.y * (y * y * y + 2.0 * y));
        }
        o.color = color * (L_ENV_C0 + L_ENV_C1 * min(dot(D, n) * p, 0.0));
    } else if (L_EYE_DIR.w > 1.5) {
        // The road sheen's VU program (course chunk kind 4).
        float3 n = o.normal;
        float3 D = o.world - L_EYE_DIR.xyz;
        float3 R = D - 2.0 * dot(D, n) * n;
        float c = max(dot(normalize(L_LIGHT_DIR(0).xyz), R) / max(length(D), 1e-6), 0.0);
        float3 bytes = floor(L_LIGHT_SPECULAR(0).rgb * L_MAT_SPECULAR.rgb * (128.0 * L_MAT_DIFFUSE.w) *
                             pow(c, L_MAT_SPECULAR.w));
        o.lit = (bytes - 256.0 * floor(bytes / 256.0)) / 128.0 * L_MAT_EMISSION.w;
    } else if (L_EYE_DIR.w > 0.5) {
        // GT3's VU lighting.
        float3 n = normalize(o.normal);
        float3 E = normalize(L_EYE_DIR.xyz);
        float3 sum = L_MAT_EMISSION.rgb + L_LIGHT_AMBIENT.rgb * L_MAT_AMBIENT.rgb;
        for (int i = 0; i < 4; ++i) {
            if (L_LIGHT_DIR(i).w > 0.5) {
                float3 L = normalize(L_LIGHT_DIR(i).xyz);
                sum += L_LIGHT_DIFFUSE(i).rgb * L_MAT_DIFFUSE.rgb * max(dot(n, L), 0.0);
                if (L_MAT_SPECULAR.w > 0.0)
                    sum += L_LIGHT_SPECULAR(i).rgb * L_MAT_SPECULAR.rgb *
                           pow(max(dot(n, normalize(L + E)), 0.0), L_MAT_SPECULAR.w);
            }
        }
        o.lit = clamp(sum * L_MAT_DIFFUSE.w, float3(0.0, 0.0, 0.0), float3(255.0, 255.0, 255.0) / 128.0) *
                L_MAT_EMISSION.w;
    }
    return o;
}

// 1 - F / 255 from the view depth, as the VU's transform.
float fog_at(float3 world) {
    if (U_FOG.z < 0.5) return 0.0;
    float W = max(-(dot(U_VIEW_Z.xyz, world) + U_VIEW_Z.w), 1e-4);
    float b = clamp(255.0 * (1.0 - U_FOG.y), 0.0, 255.0);
    float F = floor(b + 0.1 + (255.0 - b) * min(W, U_FOG.x) / W);
    return 1.0 - min(F, 255.0) / 255.0;
}

// GT3's guard band (render/guard_band.h).
bool outside(float4 c) {
    return c.w < U_FOG.w || abs(c.x) > 2.0 * c.w || abs(c.y) > 2.0 * c.w || (U_FAR_CLIP.x > 0.0 && c.w > U_FAR_CLIP.x);
}

void main(
#ifdef CLIPPED
    float3 a_pos0, float3 a_normal0, float2 a_uv0, float4 a_color0,
    float3 a_pos1, float3 a_normal1, float2 a_uv1, float4 a_color1,
    float3 a_pos2, float3 a_normal2, float2 a_uv2, float4 a_color2,
    float3 a_weights,
#else
    float3 a_pos, float3 a_normal, float2 a_uv, float4 a_color,
#endif
    out float4 o_position : POSITION,
    out float3 o_normal : TEXCOORD0,
    out float2 o_uv : TEXCOORD1,
    out float4 o_color : TEXCOORD2,
    out float3 o_world : TEXCOORD3,
    out float3 o_lit : TEXCOORD4,
    out float2 o_fog : TEXCOORD5)
{
    // (Not `clip` and `lit`: Cg has built-in functions by those names.)
    float4 cpos;
    float3 world, normal, light;
    float2 uv;
    float4 color;
    float drop;
#ifdef CLIPPED
    Vert a = vertex_of(a_pos0, a_normal0, a_uv0, a_color0);
    Vert b = vertex_of(a_pos1, a_normal1, a_uv1, a_color1);
    Vert c = vertex_of(a_pos2, a_normal2, a_uv2, a_color2);
    float3 k = a_weights;
    cpos = k.x * a.clip + k.y * b.clip + k.z * c.clip;
    world = k.x * a.world + k.y * b.world + k.z * c.world;
    normal = k.x * a.normal + k.y * b.normal + k.z * c.normal;
    uv = k.x * a.uv + k.y * b.uv + k.z * c.uv;
    color = k.x * a.color + k.y * b.color + k.z * c.color;
    light = k.x * a.lit + k.y * b.lit + k.z * c.lit;
    if (max(k.x, max(k.y, k.z)) < 1.0) {
        // A new corner: the VU lerps the GS bytes and rounds them.
        color = floor(color * 128.0 + 0.5) / 128.0;
        float e = L_MAT_EMISSION.w > 0.0 ? L_MAT_EMISSION.w : 1.0;
        light = floor(light / e * 128.0 + 0.5) / 128.0 * e;
    }
    drop = (outside(a.clip) || outside(b.clip) || outside(c.clip)) ? 0.0 : 1.0;
#else
    Vert v = vertex_of(a_pos, a_normal, a_uv, a_color);
    cpos = v.clip;
    world = v.world;
    normal = v.normal;
    uv = v.uv;
    color = v.color;
    light = v.lit;
    drop = U_FOG.w > 0.0 ? (outside(v.clip) ? 1.0 : 0.0) : (U_FAR_CLIP.x > 0.0 ? v.clip.w - U_FAR_CLIP.x : 0.0);
#endif
    o_normal = normal;
    o_uv = uv;
    o_color = color;
    o_world = world;
    o_lit = light;
    o_fog = float2(fog_at(world), drop);
    // As the OpenGL ES build's GLSL (spirv-cross --flip-vert-y --fixup-clipspace): y flipped, z
    // from [0, w] to [-w, w].
    o_position = float4(cpos.x, -cpos.y, 2.0 * cpos.z - cpos.w, cpos.w);
}
)CG";

// model.frag (#define PLAIN 1 in front: the plain draws; #define RESAMPLE 1: the depth of field).
inline constexpr char model_frag[] = R"CG(
uniform float4 FragmentUniforms[36];
uniform sampler2D u_tex;
uniform samplerCUBE u_env;
uniform sampler2D u_sphere;

#define U_LIGHT_DIR FragmentUniforms[0]
#define U_CAMERA_POS FragmentUniforms[1]
#define U_PARAMS FragmentUniforms[2]
#define U_FLAT_COLOR FragmentUniforms[3]
#define U_FOG_COLOR FragmentUniforms[4]
#define U_FOG_PARAMS FragmentUniforms[5]
#define U_TINT FragmentUniforms[6]
#define U_VIEW_ROW0 FragmentUniforms[7]
#define U_VIEW_ROW1 FragmentUniforms[8]
#define U_GRADE FragmentUniforms[9]
#define U_LIGHT_AMBIENT FragmentUniforms[10]
#define U_LIGHT_DIFFUSE FragmentUniforms[11]
#define U_ENV_SKY FragmentUniforms[12]
#define U_ENV_HORIZON FragmentUniforms[13]
#define U_ENV_GROUND FragmentUniforms[14]
#define U_LIGHT_SPECULAR FragmentUniforms[15]
#define U_MAT_AMBIENT FragmentUniforms[16]
#define U_MAT_DIFFUSE FragmentUniforms[17]
#define U_MAT_SPECULAR FragmentUniforms[18]
#define U_MAT_EMISSION FragmentUniforms[19]
#define U_LIGHT2_DIR FragmentUniforms[20]
#define U_LIGHT2_DIFFUSE FragmentUniforms[21]
#define U_LIGHT3_DIR FragmentUniforms[23]
#define U_LIGHT3_DIFFUSE FragmentUniforms[24]
#define U_LIGHT4_DIR FragmentUniforms[26]
#define U_LIGHT4_DIFFUSE FragmentUniforms[27]
#define U_GRADE2 FragmentUniforms[29]
#define U_ENV_C0 FragmentUniforms[30]
#define U_ENV_C1 FragmentUniforms[31]
#define U_OUTPUT_MODE FragmentUniforms[32]
#define U_WHEEL_BLUR FragmentUniforms[33]
#define U_UV_REGION FragmentUniforms[35]

float3 v3(float x) { return float3(x, x, x); }

// GT3's per-vertex reflection weight: C0 + C1 min(V.N, 0).
float3 env_weight(float3 view, float3 n) {
    float vn = min(dot(view, n), 0.0);
    if (U_ENV_C0.w > 0.5) return U_ENV_C0.rgb + U_ENV_C1.rgb * vn;
    return v3(1.0 + U_ENV_SKY.w * vn);
}

// The texture coordinates kept inside the primitive's region of its texture (no inset here).
float2 region_uv(float2 uv) {
    float4 r = U_UV_REGION;
    float2 c = clamp(uv, r.xy, max(r.xy, r.zw));
    return float2(r.z > r.x ? c.x : uv.x, r.w > r.y ? c.y : uv.y);
}

// The car reflections' stand-in environment: a sky / horizon / ground gradient.
float3 env_color(float3 dir) {
    float t = clamp(dir.y * 0.5 + 0.5, 0.0, 1.0);
    return t < 0.5 ? lerp(U_ENV_GROUND.rgb, U_ENV_HORIZON.rgb, t * 2.0)
                   : lerp(U_ENV_HORIZON.rgb, U_ENV_SKY.rgb, t * 2.0 - 1.0);
}

// GT3's grade pass, in its 8-bit arithmetic.
float3 grade_table(float i, float inv) {
    float t = (i / 255.0 - U_GRADE2.x) * inv;
    if (i > 0.5) t = clamp(t, 0.0, 1.0);
    float3 g = saturate(U_GRADE.rgb);
    return floor(clamp(255.0 * (t * t + 2.0 * g * t * (1.0 - t)), v3(0.0), v3(255.0)));
}

float3 gt3_grade(float3 c) {
    float3 b = floor(saturate(c) * 255.0 + 0.5);
    float d = U_GRADE2.y - U_GRADE2.x;
    float inv = d == 0.0 ? 0.0 : 1.0 / d;
    float3 lr = grade_table(b.r, inv);
    float3 lg = grade_table(b.g, inv);
    float3 lb = grade_table(b.b, inv);
    float sp = min(128.0 * U_GRADE.w, 255.0);
    float m = max(128.0 - sp, 0.0);
    float3 w = float3(0.30, 0.59, 0.11);
    float3 off = floor(m * w);
    float3 diag = floor(clamp(sp + m * w, v3(0.0), v3(255.0)));
    float3 r = floor(lr * float3(diag.r, off.r, off.r) / 128.0);
    float3 gg = floor(lg * float3(off.g, diag.g, off.g) / 128.0);
    float3 bb = floor(lb * float3(off.b, off.b, diag.b) / 128.0);
    return min(r + gg + bb, v3(255.0)) / 255.0;
}

// The GS's 8-bit colour arithmetic: texel, MODULATE, fog.
float3 gs_texel(float3 t) { return floor(t * 255.0 + 0.05) / 255.0; }
float3 gs_color(float3 c) { return min(floor(c * 255.0 + 1e-3), v3(255.0)) / 255.0; }
float3 gs_fog(float3 c, float3 fog, float amount) {
    float3 f = fog * 255.0;
    return floor(f + (c * 255.0 - f) * ((255.0 / 256.0) * (1.0 - amount)) + 1e-3) / 255.0;
}

float fog_amount(float3 world, float vertex_fog) {
#ifndef PLAIN
    if (U_FOG_PARAMS.w < 0.5) {
        float d = length(world - U_CAMERA_POS.xyz);
        return clamp((d - U_FOG_PARAMS.x) * U_FOG_PARAMS.y, 0.0, 1.0) * U_FOG_PARAMS.z;
    }
#endif
    return vertex_fog;
}

// DrawItem::fade: the 4 x 4 ordered dither (the Bayer table 0 8 2 10 / 12 4 14 6 / 3 11 1 9 /
// 15 7 13 5 as arithmetic: 4 b2(x mod 2, y mod 2) + b2(x / 2, y / 2), b2 = (2x + 3y) mod 4).
float bayer2(float x, float y) {
    float v = 2.0 * x + 3.0 * y;
    return v - 4.0 * floor(v / 4.0);
}

bool faded_out(float2 frag) {
    if (U_WHEEL_BLUR.z <= 0.0) return false;
    float2 p = floor(frag);
    p = p - 4.0 * floor(p / 4.0);
    float2 lo = p - 2.0 * floor(p / 2.0);
    float2 hi = floor(p / 2.0);
    float d = 4.0 * bayer2(lo.x, lo.y) + bayer2(hi.x, hi.y);
    return (d + 0.5) / 16.0 >= U_WHEEL_BLUR.z;
}

// model.frag's shade(): the colour, or kill for a discarded pixel.
float4 shade(float3 v_normal, float2 v_uv, float4 v_color, float3 v_world, float3 v_lit, float v_fog, bool front,
             out bool kill) {
    kill = false;
#ifndef PLAIN
    if (U_PARAMS.w > 4.5) {
        // The post pass: a finished picture over the frame, graded when asked.
        float3 pc = tex2D(u_tex, v_uv).rgb;
        if (U_GRADE2.z > 0.5) pc = gt3_grade(pc);
        return float4(pc, U_TINT.a);
    }
    if (U_PARAMS.w > 0.5 && U_PARAMS.w < 1.5) return U_FLAT_COLOR;
    if (U_PARAMS.w > 3.5) {
        // Sphere map stored with the model (prize scenes).
        float3 n = normalize(v_normal);
        if (!front) n = -n;
        float3 view = normalize(v_world - U_CAMERA_POS.xyz);
        float3 rw = reflect(view, n);
        float3 r = float3(dot(U_VIEW_ROW0.xyz, rw), dot(U_VIEW_ROW1.xyz, rw),
                          dot(cross(U_VIEW_ROW0.xyz, U_VIEW_ROW1.xyz), rw));
        float m = max(length(r + float3(0.0, 0.0, 1.0)), 1e-4);
        float2 suv = float2(0.5 + 0.5 * r.x / m, 0.5 - 0.5 * r.y / m);
        float4 vc = float4(min(v_color.rgb * env_weight(view, n), v3(255.0 / 128.0)), v_color.a);
        float4 st = tex2D(u_tex, suv);
        float4 sc = float4(gs_color(gs_texel(st.rgb) * vc.rgb), st.a * vc.a) * U_TINT;
        if (U_PARAMS.z > 0.5) sc.a = v_color.a * U_TINT.a;
        if (U_GRADE2.w > 0.5) sc.a = 1.0;
        return sc;
    }
    if (U_PARAMS.w > 2.5) {
        // Car reflection pass (PS2 ALPHA Cs*Ad + Cd).
        float ta = tex2D(u_tex, v_uv).a;
        float under_ref = U_PARAMS.x >= 2.0 ? U_PARAMS.x - 2.0 : U_PARAMS.x;
        if (under_ref >= 0.0 && ta <= under_ref) {
            ta = U_ENV_C1.w;
        } else if (U_PARAMS.x >= 2.0) {
            kill = true;
            return v_color;
        }
        float ad = v_color.a * ta;
        if (U_OUTPUT_MODE.w > 0.5 && ad > 254.0 / 255.0) {
            kill = true;
            return v_color;
        }
        float3 n = normalize(v_normal);
        if (!front) n = -n;
        float3 view = normalize(v_world - U_CAMERA_POS.xyz);
        float3 w = env_weight(view, n);
        float3 r = reflect(view, n);
        if (U_ENV_GROUND.w > 1.5 && U_ENV_GROUND.w < 2.5) {
            // GT3's sphere-map lookup in eye space: compressed angle, 15 degree tilt.
            float3 x = U_VIEW_ROW0.xyz;
            float3 y = U_VIEW_ROW1.xyz;
            float3 z = cross(x, y);
            float3 e = float3(dot(x, r), dot(y, r), dot(z, r));
            float th = acos(clamp(e.z, -1.0, 1.0));
            float ph = atan2(e.y, e.x);
            float th2 = 2.0 * asin(0.9 * sin(0.5 * th));
            float3 d = float3(sin(th2) * cos(ph), sin(th2) * sin(ph), cos(th2));
            d = float3(d.x, 0.966 * d.y + 0.259 * d.z, -0.259 * d.y + 0.966 * d.z);
            r = x * d.x + y * d.y + z * d.z;
        }
        float3 env = U_ENV_GROUND.w > 0.5 ? texCUBE(u_env, r).rgb : env_color(r);
        if (U_ENV_GROUND.w > 2.5) {
            // A fixed sphere map in eye space (menu showrooms).
            float3 x = U_VIEW_ROW0.xyz;
            float3 y = U_VIEW_ROW1.xyz;
            float3 e = float3(dot(x, r), dot(y, r), dot(cross(x, y), r));
            float m = max(length(e + float3(0.0, 0.0, 1.0)), 1e-4);
            env = tex2D(u_sphere, float2(0.5 + 0.5 * e.x / m, 0.5 - 0.5 * e.y / m)).rgb;
        }
        float3 rgb = gs_color(gs_texel(env) * min(v_color.rgb * w, v3(255.0 / 128.0)) * U_PARAMS.z);
        if (U_FOG_COLOR.w > 0.0) rgb = gs_fog(rgb, v3(0.0), fog_amount(v_world, v_fog));
        return float4(rgb, ad * U_TINT.a);
    }
#endif
    float4 t = tex2D(u_tex, region_uv(v_uv));
    float4 c = float4(gs_texel(t.rgb), t.a) * v_color;
    if (U_PARAMS.w > 1.5) {
        // Road sheen: the texel times its VU program's lit colour, fogged to black.
        c.rgb = gs_color(gs_texel(t.rgb) * v_lit);
        if (U_FOG_COLOR.w > 0.0) c.rgb = gs_fog(c.rgb, v3(0.0), fog_amount(v_world, v_fog));
        return c;
    }
    // Alpha test: discard alpha <= x; x >= 2: the inverse.
    if (U_PARAMS.x >= 0.0 && (U_PARAMS.x < 2.0 ? c.a <= U_PARAMS.x : c.a > U_PARAMS.x - 2.0)) {
        kill = true;
        return c;
    }
#ifdef PLAIN
    if (U_PARAMS.y < 0.5) c.rgb *= v_lit;
#else
    if (U_PARAMS.y < 0.5) {
        float3 n = normalize(v_normal);
        if (!front) n = -n;
        float3 L = normalize(U_LIGHT_DIR.xyz);
        float diffuse = max(dot(n, L), 0.0);
        if (U_MAT_AMBIENT.w > 0.5) {
            c.rgb *= v_lit;
        } else if (U_LIGHT_AMBIENT.w > 0.5) {
            float3 sum = U_LIGHT_AMBIENT.rgb + U_LIGHT_DIFFUSE.rgb * diffuse;
            if (U_LIGHT2_DIR.w > 0.5) sum += U_LIGHT2_DIFFUSE.rgb * max(dot(n, normalize(U_LIGHT2_DIR.xyz)), 0.0);
            if (U_LIGHT3_DIR.w > 0.5) sum += U_LIGHT3_DIFFUSE.rgb * max(dot(n, normalize(U_LIGHT3_DIR.xyz)), 0.0);
            if (U_LIGHT4_DIR.w > 0.5) sum += U_LIGHT4_DIFFUSE.rgb * max(dot(n, normalize(U_LIGHT4_DIR.xyz)), 0.0);
            c.rgb *= min(sum, v3(2.0));
        } else {
            c.rgb *= U_LIGHT_DIR.w + (1.0 - U_LIGHT_DIR.w) * diffuse;
        }
        if (U_PARAMS.z > 0.0) {
            float3 view = normalize(v_world - U_CAMERA_POS.xyz);
            float3 r = reflect(view, n);
            c.rgb = lerp(c.rgb, env_color(r), U_PARAMS.z);
        }
    }
#endif
    c.rgb = gs_color(c.rgb);
    if (U_FOG_COLOR.w > 0.0) c.rgb = gs_fog(c.rgb, U_FOG_COLOR.rgb, fog_amount(v_world, v_fog));
    float4 o = c * U_TINT;
    if (U_GRADE2.w > 0.5) o.a = 1.0;
    return o;
}

float4 main(
    float3 v_normal : TEXCOORD0,
    float2 v_uv : TEXCOORD1,
    float4 v_color : TEXCOORD2,
    float3 v_world : TEXCOORD3,
    float3 v_lit : TEXCOORD4,
    float2 v_fog : TEXCOORD5,
    float4 frag : WPOS,
    float face : FACE) : COLOR
{
#ifdef RESAMPLE
    // One bilinear tap of the picture (the GLSL's tap grid is not here), clamped to its used part.
    float2 ruv = clamp(v_uv * U_VIEW_ROW0.xy + U_VIEW_ROW0.zw, U_VIEW_ROW1.xy, U_VIEW_ROW1.zw);
    float3 rc = tex2D(u_tex, ruv).rgb;
    if (U_FLAT_COLOR.z > 0.5) {
        float own = 1.0 - tex2D(u_sphere, ruv).r;
        if (own <= 0.0) return float4(0.0, 0.0, 0.0, 0.0);
        return float4(rc, U_TINT.a * own);
    }
    return float4(rc, U_TINT.a);
#else
#ifndef NO_DISCARD
    if (v_fog.y > 0.0 || faded_out(frag.xy)) discard;
#endif
    bool kill;
    float4 o = shade(v_normal, v_uv, v_color, v_world, v_lit, v_fog.x, face > 0.0, kill);
#ifndef NO_DISCARD
    if (kill) discard;
#endif
    // GT3's reflection map is a PSMCT16 frame buffer: each write keeps c8 >> 3.
    if (U_OUTPUT_MODE.x > 0.5) o.rgb = floor(saturate(o.rgb) * (255.0 / 8.0) + 1e-4) * (8.0 / 255.0);
    return o;
#endif
}
)CG";

// no_color.frag: the draws that only set the depth (their pipelines write no colour).
inline constexpr char no_color_frag[] = R"CG(
float4 main() : COLOR
{
    return float4(0.0, 0.0, 0.0, 0.0);
}
)CG";

} // namespace gt3r::render::shaders::vita
