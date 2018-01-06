cbuffer Constants
{
    float4x4 g_ViewProj;
    float4x4 g_Rotation;
};

cbuffer InstanceData
{
    float4x4 g_InstanceMatr;
};

struct PSInput 
{ 
    float4 Pos : SV_POSITION; 
    float2 uv : TEX_COORD; 
};

// Vertex shader takes two inputs: vertex position and color.
// By convention, Diligent Engine expects vertex shader inputs to 
// be labeled as ATTRIBn, where n is the attribute number
PSInput main(float3 pos : ATTRIB0, 
             float2 uv : ATTRIB1) 
{
    PSInput ps; 
    // Apply rotation
    float4 TransformedPos = mul( float4(pos,1.0),g_Rotation);
    // Apply instance-specific transformation
    TransformedPos = mul(TransformedPos, g_InstanceMatr);
    // Apply view-projection matrix
    ps.Pos = mul( TransformedPos, g_ViewProj);
    ps.uv = uv;
    return ps;
}
