#version 460 core
#extension GL_EXT_shader_atomic_float : require
#extension GL_EXT_scalar_block_layout : require

#include "Common.glsli"
#include "VisBuffer.glsli"

layout(constant_id = 0) const uint MaxMeshletTriangles = 64;
layout(local_size_x = 64) in;

struct DrawIndexedIndirectCommand {
  uint IndexCount;
  uint InstanceCount;
  uint FirstIndex;
  int VertexOffset;
  uint FirstInstance;
};

layout(push_constant) uniform CullPushConstants {
  uint BatchID;
  uint MeshletsPerBatch;
  uint IndicesPerBatch;
};

layout(set = 0, binding = 0, scalar) uniform SceneBuffer {
  SceneData Scene;
};

layout(set = 1, binding = 0, scalar) uniform ComputeUniforms {
  uint MeshletCount;
} Uniforms;
layout(set = 1, binding = 1, scalar) restrict readonly buffer VisibleMeshletIDs {
  uint Indices[];
} VisibleMeshlets;
layout(set = 1, binding = 2, scalar) restrict readonly buffer MeshletBuffer {
  Meshlet Meshlets[];
};
layout(set = 1, binding = 3, scalar) restrict writeonly buffer DrawIndirect {
  DrawIndexedIndirectCommand DrawCommands[];
};
layout(set = 1, binding = 4, scalar) restrict writeonly buffer MeshletIndicesBuffer {
  uint Indices[];
} MeshletIndices;

shared uint sBaseIndex;
shared uint sPrimitivesPassed;
shared mat4 sMVP;

void main() {
  const uint meshletId = VisibleMeshlets.Indices[gl_WorkGroupID.x + (BatchID * MeshletsPerBatch)];
  const uint localId = gl_LocalInvocationID.x;
  const Meshlet meshlet = Meshlets[meshletId];
  const uint triangleId = localId * 3;

  if (localId == 0) {
    sPrimitivesPassed = 0;
    sMVP = mat4(1.0);
  }

  barrier();

  bool primitivePassed = false;
  uint activePrimitiveId = 0;
  if (localId < meshlet.TriangleCount) {
    primitivePassed = true;
    if (primitivePassed) {
      activePrimitiveId = atomicAdd(sPrimitivesPassed, 1);
    }
  }

  barrier();

  if (localId == 0) {
    sBaseIndex = atomicAdd(DrawCommands[BatchID].IndexCount, sPrimitivesPassed * 3);
  }

  barrier();

  if (primitivePassed) {
    const uint indexOffset = sBaseIndex + (BatchID * IndicesPerBatch) + activePrimitiveId * 3;
    MeshletIndices.Indices[indexOffset + 0] = (meshletId << MeshletPrimitiveBits) | ((triangleId + 0) & MeshletPrimitiveMask);
    MeshletIndices.Indices[indexOffset + 1] = (meshletId << MeshletPrimitiveBits) | ((triangleId + 1) & MeshletPrimitiveMask);
    MeshletIndices.Indices[indexOffset + 2] = (meshletId << MeshletPrimitiveBits) | ((triangleId + 2) & MeshletPrimitiveMask);
  }
}
