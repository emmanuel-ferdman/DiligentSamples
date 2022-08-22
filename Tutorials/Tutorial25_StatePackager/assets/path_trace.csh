#include "structures.fxh"
#include "scene.fxh"

Texture2D g_Albedo;
Texture2D g_Normal;
Texture2D g_Depth;

RWTexture2D<float4 /*format = rgba32f*/> g_Radiance;

cbuffer cbConstants
{
    ShaderConstants g_Constants;
}

#ifndef THREAD_GROUP_SIZE
#   define THREAD_GROUP_SIZE 8
#endif

[numthreads(THREAD_GROUP_SIZE, THREAD_GROUP_SIZE, 1)]
void main(uint3 ThreadId : SV_DispatchThreadID)
{
    if (ThreadId.x >= g_Constants.uScreenWidth || ThreadId.y >= g_Constants.uScreenHeight)
        return;

    float3 f3Albedo = g_Albedo.Load(int3(ThreadId.xy, 0)).xyz;
    float3 f3Normal = normalize(g_Normal.Load(int3(ThreadId.xy, 0)).xyz * 2.0 - 1.0);
    float  fDepth  = g_Depth.Load(int3(ThreadId.xy, 0)).x;

    float3 LightDir = normalize(float3(0.2, -0.7, 0.3));
    g_Radiance[ThreadId.xy] = float4(f3Albedo * (max(dot(-LightDir, f3Normal), 0.0) * float3(2.0, 2.0, 2.0) + float3(0.05, 0.05, 0.05)), fDepth);
}