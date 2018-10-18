/*     Copyright 2015-2018 Egor Yusov
 *  
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 * 
 *     http://www.apache.org/licenses/LICENSE-2.0
 * 
 *  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 *  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 *  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT OF ANY PROPRIETARY RIGHTS.
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

#include <math.h>
#include <cmath>

#include "Tutorial11_ResourceUpdates.h"
#include "MapHelper.h"
#include "BasicShaderSourceStreamFactory.h"
#include "GraphicsUtilities.h"
#include "TextureUtilities.h"

using namespace Diligent;

SampleBase* CreateSample()
{
    return new Tutorial11_ResourceUpdates();
}

// Layout of this structure matches the one we defined in pipeline state
struct Vertex
{
    float3 pos;
    float2 uv;
};

// Cube vertices

//      (-1,+1,+1)________________(+1,+1,+1) 
//               /|              /|
//              / |             / |
//             /  |            /  |
//            /   |           /   |
//(-1,-1,+1) /____|__________/(+1,-1,+1)
//           |    |__________|____| 
//           |   /(-1,+1,-1) |    /(+1,+1,-1)
//           |  /            |   /
//           | /             |  /
//           |/              | /
//           /_______________|/ 
//        (-1,-1,-1)       (+1,-1,-1)
// 

// This time we have to duplicate verices because texture coordinates cannot
// be shared
static const Vertex CubeVerts[] =
{
    {float3(-1,-1,-1), float2(0,1)},
    {float3(-1,+1,-1), float2(0,0)},
    {float3(+1,+1,-1), float2(1,0)},
    {float3(+1,-1,-1), float2(1,1)},

    {float3(-1,-1,-1), float2(0,1)},
    {float3(-1,-1,+1), float2(0,0)},
    {float3(+1,-1,+1), float2(1,0)},
    {float3(+1,-1,-1), float2(1,1)},

    {float3(+1,-1,-1), float2(0,1)},
    {float3(+1,-1,+1), float2(1,1)},
    {float3(+1,+1,+1), float2(1,0)},
    {float3(+1,+1,-1), float2(0,0)},

    {float3(+1,+1,-1), float2(0,1)},
    {float3(+1,+1,+1), float2(0,0)},
    {float3(-1,+1,+1), float2(1,0)},
    {float3(-1,+1,-1), float2(1,1)},

    {float3(-1,+1,-1), float2(1,0)},
    {float3(-1,+1,+1), float2(0,0)},
    {float3(-1,-1,+1), float2(0,1)},
    {float3(-1,-1,-1), float2(1,1)},

    {float3(-1,-1,+1), float2(1,1)},
    {float3(+1,-1,+1), float2(0,1)},
    {float3(+1,+1,+1), float2(0,0)},
    {float3(-1,+1,+1), float2(1,0)}
};

void Tutorial11_ResourceUpdates::Initialize(IRenderDevice *pDevice, IDeviceContext **ppContexts, Uint32 NumDeferredCtx, ISwapChain *pSwapChain)
{
    SampleBase::Initialize(pDevice, ppContexts, NumDeferredCtx, pSwapChain);

    {
        // Pipeline state object encompasses configuration of all GPU stages

        PipelineStateDesc PSODesc;
        // Pipeline state name is used by the engine to report issues
        // It is always a good idea to give objects descriptive names
        PSODesc.Name = "Cube PSO"; 

        // This is a graphics pipeline
        PSODesc.IsComputePipeline = false; 

        // This tutorial will render to a single render target
        PSODesc.GraphicsPipeline.NumRenderTargets = 1;
        // Set render target format which is the format of the swap chain's color buffer
        PSODesc.GraphicsPipeline.RTVFormats[0] = pSwapChain->GetDesc().ColorBufferFormat;
        // Set depth buffer format which is the format of the swap chain's back buffer
        PSODesc.GraphicsPipeline.DSVFormat = pSwapChain->GetDesc().DepthBufferFormat;
        // Primitive topology defines what kind of primitives will be rendered by this pipeline state
        PSODesc.GraphicsPipeline.PrimitiveTopology = PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        // Cull back faces
        PSODesc.GraphicsPipeline.RasterizerDesc.CullMode = CULL_MODE_BACK;
        // Enable depth testing
        PSODesc.GraphicsPipeline.DepthStencilDesc.DepthEnable = True;

        ShaderCreationAttribs CreationAttribs;
        // Tell the system that the shader source code is in HLSL.
        // For OpenGL, the engine will convert this into GLSL behind the scene
        CreationAttribs.SourceLanguage = SHADER_SOURCE_LANGUAGE_HLSL;

        // We will be using combined texture samplers
        CreationAttribs.UseCombinedTextureSamplers = true;

        // In this tutorial, we will load shaders from file. To be able to do that,
        // we need to create a shader source stream factory
        BasicShaderSourceStreamFactory BasicSSSFactory;
        CreationAttribs.pShaderSourceStreamFactory = &BasicSSSFactory;
        // Define variable type that will be used by default
        CreationAttribs.Desc.DefaultVariableType = SHADER_VARIABLE_TYPE_STATIC;
        // Create vertex shader
        RefCntAutoPtr<IShader> pVS;
        {
            CreationAttribs.Desc.ShaderType = SHADER_TYPE_VERTEX;
            CreationAttribs.EntryPoint = "main";
            CreationAttribs.Desc.Name = "Cube VS";
            CreationAttribs.FilePath = "cube.vsh";
            pDevice->CreateShader(CreationAttribs, &pVS);
            // Create dynamic uniform buffer that will store our transformation matrix
            // Dynamic buffers can be frequently updated by the CPU
            CreateUniformBuffer(pDevice, sizeof(float4x4), "VS constants CB", &m_VSConstants);
            // Since we did not explcitly specify the type for Constants, default type
            // (SHADER_VARIABLE_TYPE_STATIC) will be used. Static variables never change and are bound directly
            // through the shader (http://diligentgraphics.com/2016/03/23/resource-binding-model-in-diligent-engine-2-0/)
            pVS->GetShaderVariable("Constants")->Set(m_VSConstants);
        }

        // Create pixel shader
        RefCntAutoPtr<IShader> pPS;
        {
            CreationAttribs.Desc.ShaderType = SHADER_TYPE_PIXEL;
            CreationAttribs.EntryPoint = "main";
            CreationAttribs.Desc.Name = "Cube PS";
            CreationAttribs.FilePath = "cube.psh";
            // Shader variables should typically be mutable, which means they are expected
            // to change on a per-instance basis
            ShaderVariableDesc Vars[] = 
            {
                {"g_Texture", SHADER_VARIABLE_TYPE_MUTABLE}
            };
            CreationAttribs.Desc.VariableDesc = Vars;
            CreationAttribs.Desc.NumVariables = _countof(Vars);

            // Define static sampler for g_Texture. Static samplers should be used whenever possible
            SamplerDesc SamLinearClampDesc( FILTER_TYPE_LINEAR, FILTER_TYPE_LINEAR, FILTER_TYPE_LINEAR, 
                                            TEXTURE_ADDRESS_CLAMP, TEXTURE_ADDRESS_CLAMP, TEXTURE_ADDRESS_CLAMP);
            StaticSamplerDesc StaticSamplers[] = 
            {
                {"g_Texture", SamLinearClampDesc}
            };
            CreationAttribs.Desc.StaticSamplers = StaticSamplers;
            CreationAttribs.Desc.NumStaticSamplers = _countof(StaticSamplers);

            pDevice->CreateShader(CreationAttribs, &pPS);
        }

        // Define vertex shader input layout
        LayoutElement LayoutElems[] =
        {
            // Attribute 0 - vertex position
            LayoutElement(0, 0, 3, VT_FLOAT32, False),
            // Attribute 1 - texture coordinates
            LayoutElement(1, 0, 2, VT_FLOAT32, False)
        };

        PSODesc.GraphicsPipeline.pVS = pVS;
        PSODesc.GraphicsPipeline.pPS = pPS;
        PSODesc.GraphicsPipeline.InputLayout.LayoutElements = LayoutElems;
        PSODesc.GraphicsPipeline.InputLayout.NumElements = _countof(LayoutElems);

        pDevice->CreatePipelineState(PSODesc, &m_pPSO);

        PSODesc.GraphicsPipeline.RasterizerDesc.CullMode = CULL_MODE_NONE;
        pDevice->CreatePipelineState(PSODesc, &m_pPSO_NoCull);
    }

    for(Uint32 i=0; i < _countof(m_CubeVertexBuffer); ++i)
    {
        auto& VertexBuffer = m_CubeVertexBuffer[i];

        // Create vertex buffer that stores cube vertices
        BufferDesc VertBuffDesc;
        VertBuffDesc.Name = "Cube vertex buffer";
        if(i==0)
            VertBuffDesc.Usage = USAGE_STATIC;
        else if(i==1)
            VertBuffDesc.Usage = USAGE_DEFAULT;
        else
        {
            VertBuffDesc.Usage = USAGE_DYNAMIC;
            VertBuffDesc.CPUAccessFlags = CPU_ACCESS_WRITE;
        }

        VertBuffDesc.BindFlags = BIND_VERTEX_BUFFER;
        VertBuffDesc.uiSizeInBytes = sizeof(CubeVerts);
        BufferData VBData;
        VBData.pData = CubeVerts;
        VBData.DataSize = sizeof(CubeVerts);
        pDevice->CreateBuffer(VertBuffDesc, i < 2 ? VBData : BufferData{}, &VertexBuffer);
    }

    {
        // Indices
        Uint32 Indices[] =
        {
            2,0,1,    2,3,0,
            4,6,5,    4,7,6,
            8,10,9,   8,11,10,
            12,14,13, 12,15,14,
            16,18,17, 16,19,18,
            20,21,22, 20,22,23
        };
        // Create index buffer
        BufferDesc IndBuffDesc;
        IndBuffDesc.Name = "Cube index buffer";
        IndBuffDesc.Usage = USAGE_STATIC;
        IndBuffDesc.BindFlags = BIND_INDEX_BUFFER;
        IndBuffDesc.uiSizeInBytes = sizeof(Indices);
        BufferData IBData;
        IBData.pData = Indices;
        IBData.DataSize = sizeof(Indices);
        pDevice->CreateBuffer(IndBuffDesc, IBData, &m_CubeIndexBuffer);
    }

    for (size_t i=0; i < m_Textures.size(); ++i)
    {
        // Load texture
        TextureLoadInfo loadInfo;
        std::stringstream FileNameSS;
        FileNameSS << "DGLogo" << i << ".png";
        auto FileName = FileNameSS.str();
        loadInfo.IsSRGB = true;
        loadInfo.Usage = USAGE_STATIC;
        if (i==2)
        {
            loadInfo.Usage = USAGE_DEFAULT;
            // Disable mipmapping for simplicity as we will only update mip level 0
            loadInfo.MipLevels = 1;
        }
        else if (i==3)
        {
            // Disable mipmapping
            loadInfo.MipLevels = 1;
            loadInfo.Usage = USAGE_DYNAMIC;
            loadInfo.CPUAccessFlags = CPU_ACCESS_WRITE;
        }

        auto& Tex = m_Textures[i];
        CreateTextureFromFile(FileName.c_str(), loadInfo, m_pDevice, &Tex);
        // Get shader resource view from the texture
        auto TextureSRV = Tex->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE);

        // Since we are using mutable variable, we must create shader resource binding object
        // http://diligentgraphics.com/2016/03/23/resource-binding-model-in-diligent-engine-2-0/
        m_pPSO->CreateShaderResourceBinding(&(m_SRBs[i]));
        // Set texture SRV in the SRB
        m_SRBs[i]->GetVariable(SHADER_TYPE_PIXEL, "g_Texture")->Set(TextureSRV);
    }

    {
        BufferDesc VertBuffDesc;
        VertBuffDesc.Name = "Texture update buffer";
        VertBuffDesc.Usage = USAGE_DYNAMIC;
        VertBuffDesc.BindFlags = BIND_VERTEX_BUFFER; // We do not really bind the buffer, but D3D11 wants at least one bind flag bit
        VertBuffDesc.CPUAccessFlags = CPU_ACCESS_WRITE;
        VertBuffDesc.uiSizeInBytes = MaxUpdateRegionSize * MaxUpdateRegionSize * 4;
        pDevice->CreateBuffer(VertBuffDesc, BufferData(), &m_TextureUpdateBuffer);
    }
}

void Tutorial11_ResourceUpdates::DrawCube(const float4x4& WVPMatrix, Diligent::IBuffer *pVertexBuffer, Diligent::IShaderResourceBinding *pSRB)
{
    // Bind vertex buffer
    Uint32 offset = 0;
    IBuffer *pBuffs[] = {pVertexBuffer};
    m_pImmediateContext->SetVertexBuffers(0, 1, pBuffs, &offset, SET_VERTEX_BUFFERS_FLAG_RESET);
    m_pImmediateContext->SetIndexBuffer(m_CubeIndexBuffer, 0);

    // Commit shader resources. Pass pointer to shader resource binding object
    // COMMIT_SHADER_RESOURCES_FLAG_TRANSITION_RESOURCES flag needs to be specified to make sure
    // that resources are transitioned to proper states
    m_pImmediateContext->CommitShaderResources(pSRB, COMMIT_SHADER_RESOURCES_FLAG_TRANSITION_RESOURCES);

    {
        // Map the buffer and write current world-view-projection matrix
        MapHelper<float4x4> CBConstants(m_pImmediateContext, m_VSConstants, MAP_WRITE, MAP_FLAG_DISCARD);
        *CBConstants = transposeMatrix(WVPMatrix);
    }

    DrawAttribs DrawAttrs;
    DrawAttrs.IsIndexed = true; // This is indexed draw call
    DrawAttrs.IndexType = VT_UINT32; // Index type
    DrawAttrs.NumIndices = 36;
    m_pImmediateContext->Draw(DrawAttrs);
}

// Render a frame
void Tutorial11_ResourceUpdates::Render()
{
    // Clear the back buffer 
    const float ClearColor[] = {  0.350f,  0.350f,  0.350f, 1.0f }; 
    m_pImmediateContext->ClearRenderTarget(nullptr, ClearColor);
    m_pImmediateContext->ClearDepthStencil(nullptr, CLEAR_DEPTH_FLAG, 1.f);

    // Set pipeline state
    m_pImmediateContext->SetPipelineState(m_pPSO);

    // Projection matrix differs between DX and OpenGL
    const bool IsGL = m_pDevice->GetDeviceCaps().IsGLDevice();
    float NearPlane = 0.1f;
    float FarPlane = 100.f;
    float aspectRatio = static_cast<float>(m_pSwapChain->GetDesc().Width) / static_cast<float>(m_pSwapChain->GetDesc().Height);
    auto Proj = Projection(PI_F / 4.f, aspectRatio, NearPlane, FarPlane, IsGL);

    auto CubeRotation = rotationY( -static_cast<float>(m_CurrTime) * 0.5f) * rotationX(PI_F * 0.1f) * translationMatrix(0, 0, 12.0f);

    DrawCube(CubeRotation * translationMatrix(-2.f, -2.f, 0.f) * Proj, m_CubeVertexBuffer[0], m_SRBs[2]);
    DrawCube(CubeRotation * translationMatrix(+2.f, -2.f, 0.f) * Proj, m_CubeVertexBuffer[0], m_SRBs[3]);
    
    DrawCube(CubeRotation * translationMatrix(-4.f, +2.f, 0.f) * Proj, m_CubeVertexBuffer[0], m_SRBs[0]);
    m_pImmediateContext->SetPipelineState(m_pPSO_NoCull);
    DrawCube(CubeRotation * translationMatrix( 0.f, +2.f, 0.f) * Proj, m_CubeVertexBuffer[1], m_SRBs[0]);
    DrawCube(CubeRotation * translationMatrix(+4.f, +2.f, 0.f) * Proj, m_CubeVertexBuffer[2], m_SRBs[1]);
}

void Tutorial11_ResourceUpdates::WriteStripPattern(Uint8* pData, Uint32 Width, Uint32 Height, Uint32 Stride)
{
    auto x_scale = std::uniform_int_distribution<Uint32>{1, 8}(m_gen);
    auto y_scale = std::uniform_int_distribution<Uint32>{1, 8}(m_gen);
    auto c_scale = std::uniform_int_distribution<Uint32>{1, 64}(m_gen);
    for(Uint32 j=0; j < Height; ++j)
    {
        for(Uint32 i=0; i < Width; ++i)
        {
            for(int c=0; c < 4; ++c)
                pData[i*4 + c + j*Stride] = (i * x_scale + j * y_scale + c*c_scale) & 0xFF;
        }
    }
}

void Tutorial11_ResourceUpdates::WriteDiamondPattern(Uint8* pData, Uint32 Width, Uint32 Height, Uint32 Stride)
{
    auto x_scale = std::uniform_int_distribution<Uint32>{1, 8}(m_gen);
    auto y_scale = std::uniform_int_distribution<Uint32>{1, 8}(m_gen);
    auto c_scale = std::uniform_int_distribution<Uint32>{1, 64}(m_gen);
    for(Uint32 j=0; j < Height; ++j)
    {
        for(Uint32 i=0; i < Width; ++i)
        {
            for(int c=0; c < 4; ++c)
                pData[i*4 + c + j*Stride] = ( abs(static_cast<int>(i) - static_cast<int>(Width/2)) * x_scale + abs(static_cast<int>(j) - static_cast<int>(Height/2)) * y_scale + c*c_scale) & 0xFF;
        }
    }
}

void Tutorial11_ResourceUpdates::UpdateTexture(Uint32 TexIndex)
{
    auto& Texture = *m_Textures[TexIndex];
    static constexpr const Uint32 NumUpdates = 3;
    for(Uint32 update = 0; update < NumUpdates; ++update)
    {
        const auto& TexDesc = Texture.GetDesc();
        Uint32 Width  = std::uniform_int_distribution<Uint32>{2, MaxUpdateRegionSize}(m_gen);
        Uint32 Height = std::uniform_int_distribution<Uint32>{2, MaxUpdateRegionSize}(m_gen);

        std::vector<Uint8> Data(Width * Height * 4);
        WriteStripPattern(Data.data(), Width, Height, Width*4);
            
        Box UpdateBox;
        UpdateBox.MinX = std::uniform_int_distribution<Uint32>{0, TexDesc.Width  - Width} (m_gen);
        UpdateBox.MinY = std::uniform_int_distribution<Uint32>{0, TexDesc.Height - Height}(m_gen);
        UpdateBox.MaxX = UpdateBox.MinX + Width;
        UpdateBox.MaxY = UpdateBox.MinY + Height;
        
        TextureSubResData SubresData;
        SubresData.Stride = Width * 4;
        SubresData.pData  = Data.data();
        Uint32 MipLevel   = 0;
        Uint32 ArraySlice = 0;
        Texture.UpdateData(m_pImmediateContext, MipLevel, ArraySlice, UpdateBox, SubresData);
    }
}

void Tutorial11_ResourceUpdates::MapTexture(Uint32 TexIndex, bool MapEntireTexture)
{
    auto& Texture = *m_Textures[TexIndex];
    const auto& TexDesc = m_Textures[2]->GetDesc();
    MappedTextureSubresource MappedSubres;
    Box MapRegion;
    if (MapEntireTexture)
    {
        MapRegion.MaxX = TexDesc.Width;
        MapRegion.MaxY = TexDesc.Height;
    }
    else
    {
        Uint32 Width  = std::uniform_int_distribution<Uint32>{2, MaxMapRegionSize}(m_gen);
        Uint32 Height = std::uniform_int_distribution<Uint32>{2, MaxMapRegionSize}(m_gen);
        MapRegion.MinX = std::uniform_int_distribution<Uint32>{0, TexDesc.Width  - Width} (m_gen);
        MapRegion.MinY = std::uniform_int_distribution<Uint32>{0, TexDesc.Height - Height}(m_gen);
        MapRegion.MaxX = MapRegion.MinX + Width;
        MapRegion.MaxY = MapRegion.MinY + Height;
    }
    Uint32 MipLevel   = 0;
    Uint32 ArraySlice = 0;
    Texture.Map(m_pImmediateContext, MipLevel, ArraySlice, MAP_WRITE, MAP_FLAG_DISCARD, MapEntireTexture ? nullptr : &MapRegion, MappedSubres);
    WriteDiamondPattern( (Uint8*)MappedSubres.pData, MapRegion.MaxX-MapRegion.MinX, MapRegion.MaxY-MapRegion.MinY, MappedSubres.Stride);
    Texture.Unmap(m_pImmediateContext, 0, 0);
}

void Tutorial11_ResourceUpdates::UpdateBuffer(Diligent::Uint32 BufferIndex)
{
    Uint32 NumVertsToUpdate  = std::uniform_int_distribution<Uint32>{2, 8}(m_gen);
    Uint32 FirstVertToUpdate = std::uniform_int_distribution<Uint32>{0, static_cast<Uint32>(_countof(CubeVerts)) - NumVertsToUpdate} (m_gen);
    Vertex Vertices[_countof(CubeVerts)];
    for(Uint32 v=0; v < NumVertsToUpdate; ++v)
    {
        auto SrcInd = FirstVertToUpdate+v;
        const auto& SrcVert = CubeVerts[SrcInd];
        Vertices[v].uv  = SrcVert.uv;
        Vertices[v].pos = SrcVert.pos * static_cast<float>(1 + 0.2*sin(m_CurrTime * (1.0 + SrcInd * 0.2)));
    }
    m_CubeVertexBuffer[BufferIndex]->UpdateData(
        m_pImmediateContext,                // Device context to use for the operation
        FirstVertToUpdate * sizeof(Vertex), // Start offset in bytes
        NumVertsToUpdate  * sizeof(Vertex), // Data size in bytes
        Vertices                            // Data pointer
    );
}

void Tutorial11_ResourceUpdates::MapDynamicBuffer(Diligent::Uint32 BufferIndex)
{
    // Map the buffer and write current world-view-projection matrix
    MapHelper<Vertex> Vertices(m_pImmediateContext, m_CubeVertexBuffer[BufferIndex], MAP_WRITE, MAP_FLAG_DISCARD);
    for(Uint32 v=0; v < _countof(CubeVerts); ++v)
    {
        const auto& SrcVert = CubeVerts[v];
        Vertices[v].uv = SrcVert.uv;
        Vertices[v].pos = SrcVert.pos * static_cast<float>(1 + 0.2*sin(m_CurrTime * (1.0 + v * 0.2)));
    }
}


void Tutorial11_ResourceUpdates::Update(double CurrTime, double ElapsedTime)
{
    SampleBase::Update(CurrTime, ElapsedTime);
    
    m_CurrTime = CurrTime;

    static constexpr const double UpdateBufferPeriod = 0.1;
    if(CurrTime - m_LastBufferUpdateTime > UpdateBufferPeriod)
    {
        m_LastBufferUpdateTime = CurrTime;
        UpdateBuffer(1);
    }

    MapDynamicBuffer(2);

    static constexpr const double UpdateTexturePeriod = 0.5;
    if(CurrTime - m_LastTextureUpdateTime > UpdateTexturePeriod)
    {
        m_LastTextureUpdateTime = CurrTime;
        UpdateTexture(2);
    }

    static constexpr const double MapTexturePeriod = 0.05;
    const auto &deviceType = m_pDevice->GetDeviceCaps().DevType;
    if(CurrTime - m_LastMapTime > MapTexturePeriod * (deviceType == DeviceType::D3D11 ? 10.f : 1.f))
    {
        m_LastMapTime = CurrTime;
        if (deviceType == DeviceType::D3D11 || deviceType == DeviceType::D3D12 || deviceType == DeviceType::Vulkan)
        {
            MapTexture(3, deviceType == DeviceType::D3D11);
        }
    }
}
