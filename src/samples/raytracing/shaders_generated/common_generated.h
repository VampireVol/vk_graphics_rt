/////////////////////////////////////////////////////////////////////
/////////////  Required  Shader Features ////////////////////////////
/////////////////////////////////////////////////////////////////////

/////////////////////////////////////////////////////////////////////
/////////////////// include files ///////////////////////////////////
/////////////////////////////////////////////////////////////////////

/////////////////////////////////////////////////////////////////////
/////////////////// declarations in class ///////////////////////////
/////////////////////////////////////////////////////////////////////
#ifndef uint32_t
#define uint32_t uint
#endif
struct CRT_Hit 
{
  float    t;         ///< intersection distance from ray origin to object
  uint primId; 
  uint instId;
  uint geomId;    ///< use 4 most significant bits for geometry type; thay are zero for triangles 
  float    coords[4]; ///< custom intersection data; for triangles coords[0] and coords[1] stores baricentric coords (u,v)
};
const uint palette_size = 5;
const uint m_palette[5] = {
    0xffe6194b, 0xff3cb44b, 0xffffe119, 0xff0082c8,
    0xfff58231/*, 0xff911eb4, 0xff46f0f0, 0xfff032e6,
    0xffd2f53c, 0xfffabebe, 0xff008080, 0xffe6beff,
    0xffaa6e28, 0xfffffac8, 0xff800000, 0xffaaffc3,
    0xff808000, 0xffffd8b1, 0xff000080, 0xff808080*/
  };

const float m_reflection[5] = { 0.1f, 0.75f, 0.0f, 0.0f, 0.0f };

struct Light 
{
  vec4 pos;
  uint color;
  float intensity;
};

//For buggy
const Light l1 = { {110.0f,110.0f,75.0f,1.0f}, 0xff000000, 100000.0f };
const Light l2 = { {-60.0f,110.0f,70.0f,1.0f}, 0x0000ff00, 100000.0f };

//buster_drone
//const Light l1 = { {40.0f,10.0f,150.0f,1.0f}, 0xff000000, 100000.0f };
//const Light l2 = { {40.0f,-40.0f,120.0f,1.0f}, 0x0000ff00, 100000.0f };
const Light m_lights[2] = { l1, l2 };

struct MaterialData_pbrMR
{
  vec4 baseColor;

  float metallic;
  float roughness;
  int baseColorTexId;
  int metallicRoughnessTexId;

  vec3 emissionColor;
  int emissionTexId;

  int normalTexId;
  int occlusionTexId;
  float alphaCutoff;
  int alphaMode;
};

struct MeshInfo
{
  uint indexOffset;
  uint vertexOffset;
};

struct Vertex {
  vec4 vertex;
  vec4 tangent;
};

#include "include/RayTracer_ubo.h"

/////////////////////////////////////////////////////////////////////
/////////////////// local functions /////////////////////////////////
/////////////////////////////////////////////////////////////////////

vec3 EyeRayDir(float x, float y, float w, float h, mat4 a_mViewProjInv) {
  vec4 pos = vec4(2.0f * (x + 0.5f) / w - 1.0f, 2.0f * (y + 0.5f) / h - 1.0f, 0.0f, 1.0f);

  pos = a_mViewProjInv * pos;
  pos /= pos.w;

  //  pos.y *= (-1.0f);

  return normalize(pos.xyz);
}

uint fakeOffset(uint x, uint y, uint pitch) { return y*pitch + x; }  // RTV pattern, for 2D threading

#define KGEN_FLAG_RETURN            1
#define KGEN_FLAG_BREAK             2
#define KGEN_FLAG_DONT_SET_EXIT     4
#define KGEN_FLAG_SET_EXIT_NEGATIVE 8
#define KGEN_REDUCTION_LAST_STEP    16

