#version 460 core
#extension GL_EXT_shader_atomic_float : require
#extension GL_EXT_scalar_block_layout : require

#include "Common.glsli"
#include "VisBuffer.glsli"

struct GetMeshletUvBoundsParams {
  uint MeshletID;
  mat4 ViewProj;
  bool ClampNDC;
  bool ReverseZ;
};

// layout(local_size_x = 64) in;

layout(set = 0, binding = 0, scalar) uniform SceneBuffer {
  SceneData Scene;
};

layout(set = 1, binding = 0) uniform ComputeUniforms {
  CullUniforms Uniforms;
};
layout(set = 1, binding = 1, scalar) restrict readonly buffer MeshletBuffer {
  Meshlet Meshlets[];
};
layout(set = 1, binding = 2, scalar) restrict readonly buffer TransformBuffer {
  mat4 Transforms[];
};
layout(set = 1, binding = 3) uniform sampler2D HZB;

layout(set = 1, binding = 4, scalar) restrict writeonly buffer VisibleMeshletsBuffer {
  uint VisibleMeshlets[];
};
layout(set = 1, binding = 5, scalar) restrict buffer CullTriangleDispatchBuffer {
  DispatchIndirectCommand CullTriangleDispatches[];
};
layout(set = 1, binding = 6, scalar) restrict buffer VisBufferStatsBuffer {
  VisBufferStats Stats;
};

void GetMeshletUvBounds(GetMeshletUvBoundsParams params, out vec2 minXY, out vec2 maxXY, out float nearestZ, out bool intersectsNearPlane) {
  uint meshletId = params.MeshletID;
  uint instanceId = Meshlets[meshletId].InstanceID;
  mat4 transform = Transforms[instanceId];
  vec3 aabbMin = Meshlets[meshletId].AABBMin;
  vec3 aabbMax = Meshlets[meshletId].AABBMax;
  vec4 aabbSize = vec4(aabbMax - aabbMin, 0.0);
  vec3[] aabbCorners = vec3[](
    aabbMin,
    aabbMin + aabbSize.xww,
    aabbMin + aabbSize.wyw,
    aabbMin + aabbSize.wwz,
    aabbMin + aabbSize.xyw,
    aabbMin + aabbSize.wyz,
    aabbMin + aabbSize.xwz,
    aabbMin + aabbSize.xyz
  );
  nearestZ = 0;

  minXY = vec2(1e20);
  maxXY = vec2(-1e20);
  mat4 mvp = params.ViewProj * transform;
  for (uint i = 0; i < 8; ++i) {
    vec4 clip = mvp * vec4(aabbCorners[i], 1.0);

    if (clip.w <= 0) {
      intersectsNearPlane = true;
      return;
    }

    clip.z = max(clip.z, 0.0);
    clip /= clip.w;
    if (params.ClampNDC) {
      clip.xy = clamp(clip.xy, -1.0, 1.0);
    }
    clip.xy = clip.xy * 0.5 + 0.5;
    minXY = min(minXY, clip.xy);
    maxXY = max(maxXY, clip.xy);
    nearestZ = clamp(max(nearestZ, clip.z), 0.0, 1.0);
  }

  intersectsNearPlane = false;
}

bool IsAABBInsidePlane(vec3 center, vec3 extent, vec4 plane) {
  vec3 normal = plane.xyz;
  float radius = dot(extent, abs(normal));

  return (dot(normal, center) - plane.w) >= -radius;
}

bool CullMeshletFrustum(uint meshletId) {
  if ((Uniforms.Flags & CullMeshletFrustumBit) == 0) { return true; }

  uint instanceId = Meshlets[meshletId].InstanceID;
  mat4 transform = Transforms[instanceId];
  vec3 aabbMin = Meshlets[meshletId].AABBMin;
  vec3 aabbMax = Meshlets[meshletId].AABBMax;
  vec3 aabbCenter = (aabbMin + aabbMax) / 2.0;
  vec3 aabbExtent = aabbMax - aabbCenter;
  vec3 worldAABBCenter = vec3(transform * vec4(aabbCenter, 1.0));
  vec3 right = vec3(transform[0]) * aabbExtent.x;
  vec3 up = vec3(transform[1]) * aabbExtent.y;
  vec3 forward = vec3(-transform[2]) * aabbExtent.z;

  vec3 worldExtent = vec3(
    abs(dot(vec3(1, 0, 0), right)) +
    abs(dot(vec3(1, 0, 0), up)) +
    abs(dot(vec3(1, 0, 0), forward)),
    abs(dot(vec3(0, 1, 0), right)) +
    abs(dot(vec3(0, 1, 0), up)) +
    abs(dot(vec3(0, 1, 0), forward)),
    abs(dot(vec3(0, 0, 1), right)) +
    abs(dot(vec3(0, 0, 1), up)) +
    abs(dot(vec3(0, 0, 1), forward))
  );
  for (uint i = 0; i < 6; ++i) {
    if (!IsAABBInsidePlane(worldAABBCenter, worldExtent, Scene.FrustumPlanes[i])) { return false; }
  }

  return true;
}

bool CullQuadHiZ(vec2 minXY, vec2 maxXY, float nearestZ) {
  vec4 boxUvs = vec4(minXY, maxXY);
  boxUvs.y = 1.0 - boxUvs.y;
  boxUvs.w = 1.0 - boxUvs.w;
  vec2 hzbSize = vec2(textureSize(HZB, 0));
  float width = (boxUvs.z - boxUvs.x) * hzbSize.x;
  float height = (boxUvs.w - boxUvs.y) * hzbSize.y;

  float level = ceil(log2(max(width, height))) + 1;
  float depth = textureLod(HZB, (boxUvs.xy + boxUvs.zw) * 0.5, level).r;

  if (nearestZ < depth) { return false; }

  return true;
}

void main() {
  uint meshletsPerBatch = gl_NumWorkGroups.x;
  uint batchId = gl_GlobalInvocationID.y;
  uint meshletOffset = batchId * meshletsPerBatch;
  uint meshletId = gl_WorkGroupID.x + meshletOffset;
  if (meshletId >= Uniforms.MeshletCount) { return; }

  bool isVisible = false;
  if (CullMeshletFrustum(meshletId)) {
    isVisible = true;

    if ((Uniforms.Flags & CullMeshletHiZBit) != 0) {
      GetMeshletUvBoundsParams params;
      params.MeshletID = meshletId;
      params.ViewProj = Scene.ViewProjection;
      params.ClampNDC = true;
      params.ReverseZ = true;

      vec2 minXY;
      vec2 maxXY;
      float nearestZ;
      bool intersectsNearPlane;
      GetMeshletUvBounds(params, minXY, maxXY, nearestZ, intersectsNearPlane);
      isVisible = intersectsNearPlane;
      if (!isVisible) {
        isVisible = CullQuadHiZ(minXY, maxXY, nearestZ + 0.0001);
      }
    }
  }

  if (isVisible) {
    uint index = atomicAdd(CullTriangleDispatches[batchId].x, 1);
    VisibleMeshlets[index + meshletOffset] = meshletId;
    atomicAdd(Stats.VisibleMeshlets, 1);
  }
}
