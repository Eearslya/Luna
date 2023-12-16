#version 460 core
#extension GL_EXT_shader_atomic_float : require
#extension GL_EXT_scalar_block_layout : require

#include "Common.glsli"
#include "VisBuffer.glsli"

layout(set = 0, binding = 0, scalar) uniform SceneBuffer {
  SceneData Scene;
};

layout(set = 1, binding = 0) uniform ComputeUniforms {
  uint MeshletCount;
} Uniforms;
layout(set = 1, binding = 1, scalar) restrict readonly buffer MeshletBuffer {
  Meshlet Meshlets[];
};
layout(set = 1, binding = 6, scalar) restrict readonly buffer TransformBuffer {
  mat4 Transforms[];
};

layout(set = 1, binding = 7, scalar) restrict writeonly buffer VisibleMeshletIDs {
  uint VisibleMeshlets[];
};
layout(set = 1, binding = 8, scalar) restrict buffer MeshletBatchesBuffer {
  uint MeshletsInBatch[];
};

bool IsAABBInsidePlane(vec3 center, vec3 extent, vec4 plane) {
  vec3 normal = plane.xyz;
  float radius = dot(extent, abs(normal));

  return (dot(normal, center) - plane.w) >= -radius;
}

bool CullMeshletFrustum(uint meshletId) {
  uint instanceId = Meshlets[meshletId].InstanceID;
  mat4 transform = Transforms[instanceId];
  vec3 aabbMin = Meshlets[meshletId].AABBMin.xyz;
  vec3 aabbMax = Meshlets[meshletId].AABBMax.xyz;
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

void main() {
  uint meshletsPerBatch = gl_NumWorkGroups.x;
  uint batchId = gl_GlobalInvocationID.y;
  uint meshletOffset = (batchId * meshletsPerBatch);
  uint meshletId = gl_GlobalInvocationID.x + meshletOffset;
  if (gl_GlobalInvocationID.x >= meshletsPerBatch) { return; }
  if (meshletId >= Uniforms.MeshletCount) { return; }

  bool isVisible = CullMeshletFrustum(meshletId);
  if (isVisible) {
    uint index = atomicAdd(MeshletsInBatch[batchId], 1);
    VisibleMeshlets[index + meshletOffset] = meshletId;
  }
}
