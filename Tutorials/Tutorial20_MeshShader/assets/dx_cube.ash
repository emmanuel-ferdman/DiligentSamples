#include "structures.h"

RWStructuredBuffer<DrawTask> DrawTasks;

cbuffer cbCubeData
{
    CubeData g_CubeData;
}

cbuffer cbConstants
{
    Constants g_Constants;
}

RWByteAddressBuffer  Statistics;

// Payload will be used in mesh shader.
groupshared Payload s_Payload;


// The sphere is visible when the distance from each plane is greater than or
// equal to the radius of the sphere.
bool IsVisible(float3 cubeCenter, float radius)
{
    float4 center = float4(cubeCenter, 1.0);

    for (int i = 0; i < 6; ++i)
    {
        if (dot(g_Constants.Frustum[i], center) < -radius)
            return false;
    }
    return true;
}

float CalcDetailLevel(float3 cubeCenter, float radius)
{
    // cubeCenter - the center of the sphere 
    // radius     - the radius of circumscribed sphere
    
    // get position in view space
    float3 pos   = mul(float4(cubeCenter, 1.0), g_Constants.ViewMat).xyz;
    
    // square of distance from camera to circumscribed sphere
    float  dist2 = dot(pos, pos);
    
    // calculate sphere size in screen space
    float  size  = g_Constants.CoTanHalfFov * radius / sqrt(dist2 - radius * radius);
    
    // calculate detail level
    float  level = clamp(1.0 - size, 0.0, 1.0);
    return level;
}

// This value is used to calculate tne number of cubes that will be
// rendered after the frustum culling
groupshared uint s_TaskCount;

[numthreads(GROUP_SIZE,1,1)]
void main(in uint I  : SV_GroupIndex,
          in uint wg : SV_GroupID)
{
    // initialize shared variable
    if (I == 0)
    {
        s_TaskCount = 0;
    }

    // flush cache and synchronize
    GroupMemoryBarrierWithGroupSync();

    const uint gid   = wg * GROUP_SIZE + I;
    DrawTask   task  = DrawTasks[gid];
    float3     pos   = float3(task.BasePos, 0.0).xzy;
    float      scale = task.Scale;
    float      time  = task.Time;

    // Simple animation
    pos.y = sin(time);

    if (g_Constants.Animate)
    {
        // write new time to draw task
        DrawTasks[gid].Time = time + g_Constants.ElapsedTime;
    }

    // frustum culling
    if (!g_Constants.FrustumCulling || IsVisible(pos, g_CubeData.SphereRadius.x * scale))
    {
        // Acquire index that will be used to safely access the payload.
        // Each thread will use a unique index.
        uint index = 0;
        InterlockedAdd(s_TaskCount, 1, index);
        
        s_Payload.PosX[index]  = pos.x;
        s_Payload.PosY[index]  = pos.y;
        s_Payload.PosZ[index]  = pos.z;
        s_Payload.Scale[index] = scale;
        s_Payload.LODs[index]  = CalcDetailLevel(pos, g_CubeData.SphereRadius.x * scale);
    }
    
    // all threads must complete their work so that we can read s_TaskCount
    GroupMemoryBarrierWithGroupSync();

    if (I == 0)
    {
        // update statistics
        uint origin;
        Statistics.InterlockedAdd(0, s_TaskCount, origin);
    }
    
    // This function must be called exactly once per amplification shader.
    // The DispatchMesh call implies a GroupMemoryBarrierWithGroupSync(), and ends the amplification shader group's execution.
    DispatchMesh(s_TaskCount, 1, 1, s_Payload);
}