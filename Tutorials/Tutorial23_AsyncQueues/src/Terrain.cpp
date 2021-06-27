/*
 *  Copyright 2019-2021 Diligent Graphics LLC
 *  
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *  
 *      http://www.apache.org/licenses/LICENSE-2.0
 *  
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 *
 *  In no event and under no legal theory, whether in tort (including negligence), 
 *  contract, or otherwise, unless required by applicable law (such as deliberate 
 *  and grossly negligent acts) or agreed to in writing, shall any Contributor be
 *  liable for any damages, including any direct, indirect, special, incidental, 
 *  or consequential damages of any character arising as a result of this License or 
 *  out of the use or inability to use the software (including but not limited to damages 
 *  for loss of goodwill, work stoppage, computer failure or malfunction, or any and 
 *  all other commercial damages or losses), even if such Contributor has been advised 
 *  of the possibility of such damages.
 */

#include "Terrain.hpp"
#include "TextureUtilities.h"
#include "ShaderMacroHelper.hpp"
#include "MapHelper.hpp"

namespace Diligent
{
namespace HLSL
{
#include "../assets/Structures.fxh"
}

using IndexType = Uint32;

void Terrain::Initialize(IRenderDevice* pDevice, IBuffer* pDrawConstants, Uint64 ImmediateContextMask)
{
    m_Device               = pDevice;
    m_DrawConstants        = pDrawConstants;
    m_ImmediateContextMask = ImmediateContextMask;
}

void Terrain::CreateResources(IDeviceContext* pContext)
{
    m_NoiseScale = TerrainSize <= 10 ? 10.0f : 20.0f;

    std::vector<float2>    Vertices;
    std::vector<IndexType> Indices;

    const auto  GridSize  = ((1u << TerrainSize) / m_ComputeGroupSize) * m_ComputeGroupSize;
    const float GridScale = 1.0f / float(GridSize - 1);

    Vertices.resize(GridSize * GridSize);
    Indices.resize((GridSize - 1) * (GridSize - 1) * 6);

    {
        auto*  pVertices = Vertices.data();
        Uint32 v         = 0;
        for (Uint32 y = 0; y < GridSize; ++y)
        {
            for (Uint32 x = 0; x < GridSize; ++x)
            {
                pVertices[v++] = float2{x * GridScale, y * GridScale};
            }
        }
        VERIFY_EXPR(v == Vertices.size());

        auto*  pIndices = Indices.data();
        Uint32 i        = 0;
        for (Uint32 y = 1; y < GridSize; ++y)
        {
            for (Uint32 x = 1; x < GridSize; ++x)
            {
                pIndices[i++] = (x - 1) + y * GridSize;
                pIndices[i++] = x + (y - 1) * GridSize;
                pIndices[i++] = (x - 1) + (y - 1) * GridSize;

                pIndices[i++] = (x - 1) + y * GridSize;
                pIndices[i++] = x + y * GridSize;
                pIndices[i++] = x + (y - 1) * GridSize;
            }
        }
        VERIFY_EXPR(i == Indices.size());
    }

    // Create vertex & index buffers
    {
        BufferDesc BuffDesc;
        BuffDesc.Name          = "Terrain VB";
        BuffDesc.uiSizeInBytes = static_cast<Uint32>(Vertices.size() * sizeof(Vertices[0]));
        BuffDesc.BindFlags     = BIND_VERTEX_BUFFER;
        BuffDesc.Usage         = USAGE_IMMUTABLE;
        BufferData BuffData{Vertices.data(), BuffDesc.uiSizeInBytes, pContext};
        m_Device->CreateBuffer(BuffDesc, &BuffData, &m_VB);

        BuffDesc.Name          = "Terrain IB";
        BuffDesc.uiSizeInBytes = static_cast<Uint32>(Indices.size() * sizeof(Indices[0]));
        BuffDesc.BindFlags     = BIND_INDEX_BUFFER;
        BuffData               = BufferData{Indices.data(), BuffDesc.uiSizeInBytes, pContext};
        m_Device->CreateBuffer(BuffDesc, &BuffData, &m_IB);

        const StateTransitionDesc Barriers[] = {
            {m_VB, RESOURCE_STATE_COPY_DEST, RESOURCE_STATE_VERTEX_BUFFER, true},
            {m_IB, RESOURCE_STATE_COPY_DEST, RESOURCE_STATE_INDEX_BUFFER, true} //
        };
        pContext->TransitionResourceStates(_countof(Barriers), Barriers);
    }

    // Create height & normal maps
    {
        TextureDesc TexDesc;
        TexDesc.Name                 = "Terrain height map";
        TexDesc.Type                 = RESOURCE_DIM_TEX_2D;
        TexDesc.Format               = TEX_FORMAT_R16_FLOAT;
        TexDesc.Width                = GridSize;
        TexDesc.Height               = GridSize;
        TexDesc.BindFlags            = BIND_SHADER_RESOURCE | BIND_UNORDERED_ACCESS;
        TexDesc.ImmediateContextMask = m_ImmediateContextMask;
        m_Device->CreateTexture(TexDesc, nullptr, &m_HeightMap);

        TexDesc.Name   = "Terrain normal map";
        TexDesc.Format = TEX_FORMAT_RGBA16_FLOAT;
        //TexDesc.Format = TEX_FORMAT_RGBA8_UNORM;
        m_Device->CreateTexture(TexDesc, nullptr, &m_NormalMap);

        const StateTransitionDesc Barriers[] = {
            {m_HeightMap, RESOURCE_STATE_UNKNOWN, RESOURCE_STATE_UNORDERED_ACCESS},
            {m_NormalMap, RESOURCE_STATE_UNKNOWN, RESOURCE_STATE_UNORDERED_ACCESS} //
        };
        pContext->TransitionResourceStates(_countof(Barriers), Barriers);

        // Resources are used in multiple contexts, so disable automatic resource transitions.
        m_HeightMap->SetState(RESOURCE_STATE_UNKNOWN);
        m_NormalMap->SetState(RESOURCE_STATE_UNKNOWN);
    }

    if (m_DiffuseMap == nullptr)
    {
        TextureLoadInfo loadInfo;
        loadInfo.IsSRGB       = true;
        loadInfo.GenerateMips = true;
        RefCntAutoPtr<ITexture> Tex;
        CreateTextureFromFile("Sand.jpg", loadInfo, m_Device, &m_DiffuseMap);

        const StateTransitionDesc Barriers[] = {
            {m_DiffuseMap, RESOURCE_STATE_COPY_DEST, RESOURCE_STATE_SHADER_RESOURCE} //
        };
        pContext->TransitionResourceStates(_countof(Barriers), Barriers);
    }

    if (m_TerrainConstants == nullptr)
    {
        BufferDesc BuffDesc;
        BuffDesc.Name                 = "Terrain constants";
        BuffDesc.BindFlags            = BIND_UNIFORM_BUFFER;
        BuffDesc.Usage                = USAGE_DEFAULT;
        BuffDesc.uiSizeInBytes        = sizeof(HLSL::TerrainConstants);
        BuffDesc.ImmediateContextMask = m_ImmediateContextMask;
        m_Device->CreateBuffer(BuffDesc, nullptr, &m_TerrainConstants);
    }

    // Set terrain generator shader resources
    {
        m_GenPSO->CreateShaderResourceBinding(&m_GenSRB);
        m_GenSRB->GetVariableByName(SHADER_TYPE_COMPUTE, "TerrainConstantsCB")->Set(m_TerrainConstants);
        m_GenSRB->GetVariableByName(SHADER_TYPE_COMPUTE, "g_HeightMapUAV")->Set(m_HeightMap->GetDefaultView(TEXTURE_VIEW_UNORDERED_ACCESS));
        m_GenSRB->GetVariableByName(SHADER_TYPE_COMPUTE, "g_NormalMapUAV")->Set(m_NormalMap->GetDefaultView(TEXTURE_VIEW_UNORDERED_ACCESS));
    }

    // Set draw terrain shader resources
    {
        m_DrawPSO->CreateShaderResourceBinding(&m_DrawSRB);
        m_DrawSRB->GetVariableByName(SHADER_TYPE_VERTEX, "DrawConstantsCB")->Set(m_DrawConstants);
        m_DrawSRB->GetVariableByName(SHADER_TYPE_VERTEX, "TerrainConstantsCB")->Set(m_TerrainConstants);
        m_DrawSRB->GetVariableByName(SHADER_TYPE_VERTEX, "g_TerrainHeightMap")->Set(m_HeightMap->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE));
        m_DrawSRB->GetVariableByName(SHADER_TYPE_PIXEL, "DrawConstantsCB")->Set(m_DrawConstants);
        m_DrawSRB->GetVariableByName(SHADER_TYPE_PIXEL, "TerrainConstantsCB")->Set(m_TerrainConstants);
        m_DrawSRB->GetVariableByName(SHADER_TYPE_PIXEL, "g_TerrainNormalMap")->Set(m_NormalMap->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE));
        m_DrawSRB->GetVariableByName(SHADER_TYPE_PIXEL, "g_TerrainDiffuseMap")->Set(m_DiffuseMap->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE));
    }
}

void Terrain::CreatePSO(const ScenepSOCreateAttribs& Attr)
{
    // Terrain generation PSO
    {
        const auto& CSInfo    = m_Device->GetAdapterInfo().ComputeShader;
        const auto  GroupSize = static_cast<int>(sqrt(static_cast<float>(CSInfo.MaxThreadGroupInvocations)) + 0.5f);
        m_ComputeGroupSize    = GroupSize - m_GroupBorderSize;

        VERIFY_EXPR(GroupSize > 0);
        VERIFY_EXPR(m_ComputeGroupSize * m_ComputeGroupSize <= CSInfo.MaxThreadGroupInvocations);

        ShaderMacroHelper Macros;
        Macros.AddShaderMacro("GROUP_SIZE_WITH_BORDER", GroupSize);

        ShaderCreateInfo ShaderCI;
        ShaderCI.UseCombinedTextureSamplers = true;
        ShaderCI.Desc.ShaderType            = SHADER_TYPE_COMPUTE;
        ShaderCI.pShaderSourceStreamFactory = Attr.pShaderSourceFactory;
        ShaderCI.SourceLanguage             = SHADER_SOURCE_LANGUAGE_HLSL;
        ShaderCI.Macros                     = Macros;
        ShaderCI.Desc.Name                  = "Generate terrain height and normal map CS";
        ShaderCI.FilePath                   = "GenerateTerrain.csh";
        ShaderCI.EntryPoint                 = "CSMain";

        RefCntAutoPtr<IShader> pCS;
        m_Device->CreateShader(ShaderCI, &pCS);

        ComputePipelineStateCreateInfo PSOCreateInfo;

        PSOCreateInfo.PSODesc.PipelineType = PIPELINE_TYPE_COMPUTE;
        PSOCreateInfo.PSODesc.Name         = "Generate terrain height and normal map PSO";

        PSOCreateInfo.PSODesc.ImmediateContextMask               = m_ImmediateContextMask;
        PSOCreateInfo.PSODesc.ResourceLayout.DefaultVariableType = SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE;

        PSOCreateInfo.pCS = pCS;
        m_Device->CreateComputePipelineState(PSOCreateInfo, &m_GenPSO);
    }

    // Draw terrain PSO
    {
        GraphicsPipelineStateCreateInfo PSOCreateInfo;

        PSOCreateInfo.PSODesc.Name         = "Draw terrain PSO";
        PSOCreateInfo.PSODesc.PipelineType = PIPELINE_TYPE_GRAPHICS;

        PSOCreateInfo.GraphicsPipeline.NumRenderTargets             = 1;
        PSOCreateInfo.GraphicsPipeline.RTVFormats[0]                = Attr.ColorTargetFormat;
        PSOCreateInfo.GraphicsPipeline.DSVFormat                    = Attr.DepthTargetFormat;
        PSOCreateInfo.GraphicsPipeline.PrimitiveTopology            = PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        PSOCreateInfo.GraphicsPipeline.RasterizerDesc.CullMode      = CULL_MODE_BACK;
        PSOCreateInfo.GraphicsPipeline.DepthStencilDesc.DepthEnable = True;

        ShaderCreateInfo ShaderCI;
        ShaderCI.SourceLanguage             = SHADER_SOURCE_LANGUAGE_HLSL;
        ShaderCI.pShaderSourceStreamFactory = Attr.pShaderSourceFactory;
        ShaderCI.UseCombinedTextureSamplers = true;

        RefCntAutoPtr<IShader> pVS;
        {
            ShaderCI.Desc.ShaderType = SHADER_TYPE_VERTEX;
            ShaderCI.EntryPoint      = "main";
            ShaderCI.Desc.Name       = "Draw terrain VS";
            ShaderCI.FilePath        = "DrawTerrain.vsh";
            m_Device->CreateShader(ShaderCI, &pVS);
        }

        RefCntAutoPtr<IShader> pPS;
        {
            ShaderCI.Desc.ShaderType = SHADER_TYPE_PIXEL;
            ShaderCI.EntryPoint      = "main";
            ShaderCI.Desc.Name       = "Draw terrain PS";
            ShaderCI.FilePath        = "DrawTerrain.psh";
            m_Device->CreateShader(ShaderCI, &pPS);
        }

        PSOCreateInfo.pVS = pVS;
        PSOCreateInfo.pPS = pPS;

        const LayoutElement LayoutElems[] = {LayoutElement{0, 0, 2, VT_FLOAT32, False}};

        PSOCreateInfo.GraphicsPipeline.InputLayout.LayoutElements = LayoutElems;
        PSOCreateInfo.GraphicsPipeline.InputLayout.NumElements    = _countof(LayoutElems);

        const SamplerDesc SamLinearClampDesc //
            {
                FILTER_TYPE_LINEAR, FILTER_TYPE_LINEAR, FILTER_TYPE_LINEAR,
                TEXTURE_ADDRESS_CLAMP, TEXTURE_ADDRESS_CLAMP, TEXTURE_ADDRESS_CLAMP //
            };
        const SamplerDesc SamLinearWrapDesc //
            {
                FILTER_TYPE_LINEAR, FILTER_TYPE_LINEAR, FILTER_TYPE_LINEAR,
                TEXTURE_ADDRESS_WRAP, TEXTURE_ADDRESS_WRAP, TEXTURE_ADDRESS_WRAP //
            };
        const ImmutableSamplerDesc ImtblSamplers[] = //
            {
                {SHADER_TYPE_PIXEL, "g_TerrainNormalMap", SamLinearClampDesc},
                {SHADER_TYPE_PIXEL, "g_TerrainDiffuseMap", SamLinearWrapDesc} //
            };
        PSOCreateInfo.PSODesc.ResourceLayout.ImmutableSamplers    = ImtblSamplers;
        PSOCreateInfo.PSODesc.ResourceLayout.NumImmutableSamplers = _countof(ImtblSamplers);
        PSOCreateInfo.PSODesc.ResourceLayout.DefaultVariableType  = SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE;

        m_Device->CreateGraphicsPipelineState(PSOCreateInfo, &m_DrawPSO);
    }
}

void Terrain::Update(IDeviceContext* pContext)
{
    pContext->BeginDebugGroup("Update terrain");

    const auto& TexDesc = m_HeightMap->GetDesc();

    // Update constants
    {
        HLSL::TerrainConstants ConstData;
        ConstData.Scale      = float3(m_XZScale, m_TerrainHeightScale, m_XZScale);
        ConstData.UVScale    = m_UVScale;
        ConstData.GroupSize  = m_ComputeGroupSize;
        ConstData.Animation  = Animation;
        ConstData.XOffset    = XOffset;
        ConstData.NoiseScale = m_NoiseScale;

        pContext->UpdateBuffer(m_TerrainConstants, 0, sizeof(ConstData), &ConstData, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    }

    pContext->SetPipelineState(m_GenPSO);

    // m_TerrainHeightMap and m_TerrainNormalMap can not be transitioned here because has UNKNOWN state.
    pContext->CommitShaderResources(m_GenSRB, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

    DispatchComputeAttribs dispatchAttrs;
    dispatchAttrs.ThreadGroupCountX = TexDesc.Width / m_ComputeGroupSize;
    dispatchAttrs.ThreadGroupCountY = TexDesc.Height / m_ComputeGroupSize;

    VERIFY_EXPR(dispatchAttrs.ThreadGroupCountX * m_ComputeGroupSize == TexDesc.Width);
    VERIFY_EXPR(dispatchAttrs.ThreadGroupCountY * m_ComputeGroupSize == TexDesc.Height);

    pContext->DispatchCompute(dispatchAttrs);

    pContext->EndDebugGroup(); // Update terrain
}

void Terrain::Draw(IDeviceContext* pContext, const SceneDrawAttribs& Attr)
{
    pContext->BeginDebugGroup("Draw terrain");

    {
        const float Center = -m_XZScale * 0.5f;

        MapHelper<HLSL::DrawConstants> ConstData(pContext, m_DrawConstants, MAP_WRITE, MAP_FLAG_DISCARD);
        ConstData->ModelViewProj = (float4x4::Translation(Center, 0.f, Center) * Attr.ViewProj).Transpose();
        ConstData->NormalMat     = float4x4::Identity();
        ConstData->LightDir      = Attr.LightDir;
        ConstData->AmbientLight  = Attr.AmbientLight;
    }

    pContext->SetPipelineState(m_DrawPSO);

    // m_TerrainHeightMap and m_TerrainNormalMap can not be transitioned here because has UNKNOWN state.
    // Other resources has constant state and does not require transitions.
    pContext->CommitShaderResources(m_DrawSRB, RESOURCE_STATE_TRANSITION_MODE_NONE);

    // Vertex and index buffers are immutable and does not require transitions.
    IBuffer*     VBs[]     = {m_VB};
    const Uint32 Offsets[] = {0};

    pContext->SetVertexBuffers(0, _countof(VBs), VBs, Offsets, RESOURCE_STATE_TRANSITION_MODE_NONE, SET_VERTEX_BUFFERS_FLAG_RESET);
    pContext->SetIndexBuffer(m_IB, 0, RESOURCE_STATE_TRANSITION_MODE_NONE);

    DrawIndexedAttribs drawAttribs;
    drawAttribs.NumIndices = m_IB->GetDesc().uiSizeInBytes / sizeof(IndexType);
    drawAttribs.IndexType  = VT_UINT32;
    drawAttribs.Flags      = DRAW_FLAG_VERIFY_ALL;
    pContext->DrawIndexed(drawAttribs);

    pContext->EndDebugGroup(); // Draw terrain
}

void Terrain::BeforeDraw(IDeviceContext* pContext)
{
    // Resources must be manualy transitioned to required state.
    // Vulkan:     the correct pipeline barrier must contains vertex and pixel shader stages which is not supported in compute context.
    // DirectX 12: height map used as non-pixel shader resource and can be transitioned in compute context,
    //             but normal map used as pixel shader resource and must be transitioned in graphics context.
    const StateTransitionDesc Barriers[] = {
        {m_HeightMap, RESOURCE_STATE_UNORDERED_ACCESS, RESOURCE_STATE_SHADER_RESOURCE, false},
        {m_NormalMap, RESOURCE_STATE_UNORDERED_ACCESS, RESOURCE_STATE_SHADER_RESOURCE, false} //
    };
    pContext->TransitionResourceStates(_countof(Barriers), Barriers);
}

void Terrain::AfterDraw(IDeviceContext* pContext)
{
    // Resources must be manualy transitioned to required state.
    const StateTransitionDesc Barriers[] = {
        {m_HeightMap, RESOURCE_STATE_SHADER_RESOURCE, RESOURCE_STATE_UNORDERED_ACCESS},
        {m_NormalMap, RESOURCE_STATE_SHADER_RESOURCE, RESOURCE_STATE_UNORDERED_ACCESS} //
    };
    pContext->TransitionResourceStates(_countof(Barriers), Barriers);
}

void Terrain::Recreate(IDeviceContext* pContext)
{
    // Recreate terrain buffers
    m_VB        = nullptr;
    m_IB        = nullptr;
    m_HeightMap = nullptr;
    m_NormalMap = nullptr;
    m_GenSRB    = nullptr;
    m_DrawSRB   = nullptr;

    m_Device->IdleGPU();

    CreateResources(pContext);

    pContext->Flush();
    m_Device->IdleGPU();
}

void Terrain::ReloadShaders()
{
    m_DrawPSO = nullptr;
    m_GenPSO  = nullptr;
    m_GenSRB  = nullptr;
    m_DrawSRB = nullptr;

    m_VB        = nullptr;
    m_IB        = nullptr;
    m_HeightMap = nullptr;
    m_NormalMap = nullptr;
}

} // namespace Diligent