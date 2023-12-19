#version 460 core
#extension GL_EXT_shader_atomic_float : require
#extension GL_EXT_scalar_block_layout : require

#include "Common.glsli"
#include "VisBuffer.glsli"

layout(local_size_x = 64) in;

layout(push_constant) uniform PushConstant {
  uint BatchID;
};

layout(set = 0, binding = 0, scalar) uniform SceneBuffer {
  SceneData Scene;
};

layout(set = 1, binding = 0) uniform ComputeUniforms {
  CullUniforms Uniforms;
};
layout(set = 1, binding = 1, scalar) restrict readonly buffer MeshletBuffer {
  Meshlet Meshlets[];
};
layout(set = 1, binding = 2, scalar) restrict readonly buffer ScenePositions {
  vec3 Positions[];
};
layout(set = 1, binding = 3, scalar) restrict readonly buffer SceneAttributes {
  Vertex Attributes[];
};
layout(set = 1, binding = 4, scalar) restrict readonly buffer SceneIndices {
  uint Indices[];
};
layout(set = 1, binding = 5, scalar) restrict readonly buffer SceneTriangles {
  uint8_t Triangles[];
};
layout(set = 1, binding = 6, scalar) restrict readonly buffer TransformBuffer {
  mat4 Transforms[];
};
layout(set = 1, binding = 7) uniform sampler2D HZB;
layout(set = 1, binding = 8, scalar) restrict readonly buffer VisibleMeshletsBuffer {
  uint VisibleMeshlets[];
};

layout(set = 1, binding = 9, scalar) restrict writeonly buffer DrawIndirect {
  DrawIndexedIndirectCommand DrawCommands[];
};
layout(set = 1, binding = 10, scalar) restrict writeonly buffer MeshletIndicesBuffer {
  uint MeshletIndices[];
};
layout(set = 1, binding = 11, scalar) restrict buffer VisBufferStatsBuffer {
  VisBufferStats Stats;
};

shared uint sBaseIndex;
shared uint sPrimitivesPassed;
shared mat4 sMVP;

bool CullSmallTriangle(vec2 vertices[3]) {
  const uint SubpixelBits = 8;
  const uint SubpixelMask = 0xff;
  const uint SubpixelSamples = 1 << SubpixelBits;

  ivec2 minBB = ivec2(1 << 30, 1 << 30);
  ivec2 maxBB = ivec2(-(1 << 30), -(1 << 30));

  for (uint i = 0; i < 3; ++i) {
    vec2 screenSpacePositionFP = vertices[i].xy * Scene.ViewportExtent;
    if (screenSpacePositionFP.x < -(1 << 23) || screenSpacePositionFP.x > (1 << 23) || screenSpacePositionFP.y < -(1 << 23) || screenSpacePositionFP.y > (1 << 23)) { return true; }

    ivec2 screenSpacePosition = ivec2(screenSpacePositionFP * SubpixelSamples);
    minBB = min(screenSpacePosition, minBB);
    maxBB = max(screenSpacePosition, maxBB);
  }

  return !(
    (
      ((minBB.x & SubpixelMask) > SubpixelSamples / 2)
   && ((maxBB.x - ((minBB.x & ~SubpixelMask) + SubpixelSamples / 2)) < (SubpixelSamples - 1)))
   || (
      ((minBB.y & SubpixelMask) > SubpixelSamples / 2)
   && ((maxBB.y - ((minBB.y & ~SubpixelMask) + SubpixelSamples / 2)) < (SubpixelSamples - 1))));

  return true;
}

bool CullTriangle(uint meshletId, uint localId) {
  uint primitiveId = localId * 3;

  uint vertexOffset   = Meshlets[meshletId].VertexOffset;
  uint indexOffset    = Meshlets[meshletId].IndexOffset;
  uint triangleOffset = Meshlets[meshletId].TriangleOffset;

  uint triangle0 = uint(Triangles[triangleOffset + primitiveId + 0]);
  uint triangle1 = uint(Triangles[triangleOffset + primitiveId + 1]);
  uint triangle2 = uint(Triangles[triangleOffset + primitiveId + 2]);

  uint index0 = Indices[indexOffset + triangle0];
  uint index1 = Indices[indexOffset + triangle1];
  uint index2 = Indices[indexOffset + triangle2];

  vec3 position0 = Positions[vertexOffset + index0];
  vec3 position1 = Positions[vertexOffset + index1];
  vec3 position2 = Positions[vertexOffset + index2];

  Vertex vertex0 = Attributes[vertexOffset + index0];
  Vertex vertex1 = Attributes[vertexOffset + index1];
  Vertex vertex2 = Attributes[vertexOffset + index2];

  vec4 posClip0 = sMVP * vec4(position0, 1.0);
  vec4 posClip1 = sMVP * vec4(position1, 1.0);
  vec4 posClip2 = sMVP * vec4(position2, 1.0);

  if ((Uniforms.Flags & CullTriangleBackfaceBit) != 0) {
    float det = determinant(mat3(posClip0.xyw, posClip1.xyw, posClip2.xyw));
    if (det <= 0) { return false; }
  }

  vec3 posNdc0 = posClip0.xyz / posClip0.w;
  vec3 posNdc1 = posClip1.xyz / posClip1.w;
  vec3 posNdc2 = posClip2.xyz / posClip2.w;

  vec2 bboxNdcMin = min(posNdc0.xy, min(posNdc1.xy, posNdc2.xy));
  vec2 bboxNdcMax = max(posNdc0.xy, max(posNdc1.xy, posNdc2.xy));

  bool allBehind = posNdc0.z < 0 && posNdc1.z < 0 && posNdc2.z < 0;
  if (allBehind) { return false; }

  bool anyBehind = posNdc0.z < 0 || posNdc1.z < 0 || posNdc2.z < 0;
  if (anyBehind) { return true; }

  if (!RectIntersectRect(bboxNdcMin, bboxNdcMax, vec2(-1.0), vec2(1.0))) { return false; }

  vec2 posUv0 = posNdc0.xy * 0.5 + 0.5;
  vec2 posUv1 = posNdc1.xy * 0.5 + 0.5;
  vec2 posUv2 = posNdc2.xy * 0.5 + 0.5;
  if (!CullSmallTriangle(vec2[3](posUv0, posUv1, posUv2))) { return false; }

  return true;
}

void main() {
  uint meshletsPerBatch = gl_NumWorkGroups.x;
  uint batchId = BatchID;
  uint meshletOffset = batchId * Uniforms.MeshletsPerBatch;
  uint meshletId = VisibleMeshlets[gl_WorkGroupID.x + meshletOffset];
  if (meshletId >= Uniforms.MeshletCount) { return; }

  uint localId = gl_LocalInvocationID.x;
  if (localId == 0) {
    uint instanceId = Meshlets[meshletId].InstanceID;
    sPrimitivesPassed = 0;
    sMVP = Scene.ViewProjection * Transforms[instanceId];
  }

  barrier();

  uint triangleCount = Meshlets[meshletId].TriangleCount;
  bool primitivePassed = false;
  uint activePrimitiveId = 0;
  if (localId < triangleCount) {
    primitivePassed = CullTriangle(meshletId, localId);
    if (primitivePassed) {
      activePrimitiveId = atomicAdd(sPrimitivesPassed, 1);
    }
  }

  barrier();

  if (localId == 0) {
    sBaseIndex = atomicAdd(DrawCommands[batchId].IndexCount, sPrimitivesPassed * 3);
    atomicAdd(Stats.VisibleTriangles, sPrimitivesPassed);
  }

  barrier();

  if (primitivePassed) {
    uint triangleId = localId * 3;
    uint indexOffset = sBaseIndex + (batchId * Uniforms.IndicesPerBatch) + activePrimitiveId * 3;
    MeshletIndices[indexOffset + 0] = (meshletId << MeshletPrimitiveBits) | ((triangleId + 0) & MeshletPrimitiveMask);
    MeshletIndices[indexOffset + 1] = (meshletId << MeshletPrimitiveBits) | ((triangleId + 1) & MeshletPrimitiveMask);
    MeshletIndices[indexOffset + 2] = (meshletId << MeshletPrimitiveBits) | ((triangleId + 2) & MeshletPrimitiveMask);
  }
}
