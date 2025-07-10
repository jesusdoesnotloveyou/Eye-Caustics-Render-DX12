#include "pch.h"

#include <array>

#include "SSAO.h"
#include <DirectXPackedVector.h>


#include "GCommandList.h"
#include "GDevice.h"
#include "GResourceStateTracker.h"
#include "MathHelper.h"
#include "ShaderBuffersData.h"

using namespace Microsoft::WRL;

SSAO::SSAO(
    const std::shared_ptr<GDevice>& device,
    const std::shared_ptr<GCommandList>& cmdList,
    const UINT width, const UINT height): device(device)

{
    normalMapSrvMemory = device->AllocateDescriptors(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 1);
    normalMapRtvMemory = device->AllocateDescriptors(D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 1);

    depthMapSrvMemory = device->AllocateDescriptors(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 1);
    depthMapDSVMemory = device->AllocateDescriptors(D3D12_DESCRIPTOR_HEAP_TYPE_DSV, 1);

    randomVectorSrvMemory = device->AllocateDescriptors(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 1);

    ambientMapMapSrvMemory = device->AllocateDescriptors(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 2);
    ambientMapRtvMemory = device->AllocateDescriptors(D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 2);

    ssaoPrimeRootSignature = std::make_shared<GRootSignature>();

    CD3DX12_DESCRIPTOR_RANGE texTable0;
    texTable0.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 2, 0, 0);

    CD3DX12_DESCRIPTOR_RANGE texTable1;
    texTable1.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 2, 0);

    ssaoPrimeRootSignature->AddConstantBufferParameter(0);
    ssaoPrimeRootSignature->AddConstantParameter(1, 1);
    ssaoPrimeRootSignature->AddDescriptorParameter(&texTable0, 1, D3D12_SHADER_VISIBILITY_PIXEL);
    ssaoPrimeRootSignature->AddDescriptorParameter(&texTable1, 1, D3D12_SHADER_VISIBILITY_PIXEL);

    const CD3DX12_STATIC_SAMPLER_DESC pointClamp(
        0, // shaderRegister
        D3D12_FILTER_MIN_MAG_MIP_POINT, // filter
        D3D12_TEXTURE_ADDRESS_MODE_CLAMP, // addressU
        D3D12_TEXTURE_ADDRESS_MODE_CLAMP, // addressV
        D3D12_TEXTURE_ADDRESS_MODE_CLAMP); // addressW

    const CD3DX12_STATIC_SAMPLER_DESC linearClamp(
        1, // shaderRegister
        D3D12_FILTER_MIN_MAG_MIP_LINEAR, // filter
        D3D12_TEXTURE_ADDRESS_MODE_CLAMP, // addressU
        D3D12_TEXTURE_ADDRESS_MODE_CLAMP, // addressV
        D3D12_TEXTURE_ADDRESS_MODE_CLAMP); // addressW

    const CD3DX12_STATIC_SAMPLER_DESC depthMapSam(
        2, // shaderRegister
        D3D12_FILTER_MIN_MAG_MIP_LINEAR, // filter
        D3D12_TEXTURE_ADDRESS_MODE_BORDER, // addressU
        D3D12_TEXTURE_ADDRESS_MODE_BORDER, // addressV
        D3D12_TEXTURE_ADDRESS_MODE_BORDER, // addressW
        0.0f,
        0,
        D3D12_COMPARISON_FUNC_LESS_EQUAL,
        D3D12_STATIC_BORDER_COLOR_OPAQUE_WHITE);

    const CD3DX12_STATIC_SAMPLER_DESC linearWrap(
        3, // shaderRegister
        D3D12_FILTER_MIN_MAG_MIP_LINEAR, // filter
        D3D12_TEXTURE_ADDRESS_MODE_WRAP, // addressU
        D3D12_TEXTURE_ADDRESS_MODE_WRAP, // addressV
        D3D12_TEXTURE_ADDRESS_MODE_WRAP); // addressW

    std::array<CD3DX12_STATIC_SAMPLER_DESC, 4> staticSamplers =
    {
        pointClamp, linearClamp, depthMapSam, linearWrap
    };

    for (auto&& sampler : staticSamplers)
    {
        ssaoPrimeRootSignature->AddStaticSampler(sampler);
    }

    ssaoPrimeRootSignature->Initialize(device);

    OnResize(width, height);

    BuildOffsetVectors();
    BuildRandomVectorTexture(cmdList);
}

SSAO::~SSAO() = default;

GDescriptor* SSAO::NormalMapRtv()
{
    return &normalMapRtvMemory;
}

GDescriptor* SSAO::NormalMapSrv()
{
    return &normalMapSrvMemory;
}

GDescriptor* SSAO::AmbientMapSrv()
{
    return &ambientMapMapSrvMemory;
}

UINT SSAO::SsaoMapWidth() const
{
    return mRenderTargetWidth / 2;
}

UINT SSAO::SsaoMapHeight() const
{
    return mRenderTargetHeight / 2;
}

void SSAO::GetOffsetVectors(Vector4 offsets[14])
{
    std::copy(&mOffsets[0], &mOffsets[14], &offsets[0]);
}

std::vector<float> SSAO::CalcGaussWeights(const float sigma)
{
    float twoSigma2 = 2.0f * sigma * sigma;

    // Estimate the blur radius based on sigma since sigma controls the "width" of the bell curve.
    // For example, for sigma = 3, the width of the bell curve is 
    int blurRadius = static_cast<int>(ceil(2.0f * sigma));

    assert(blurRadius <= MaxBlurRadius);

    std::vector<float> weights;
    weights.resize(2 * blurRadius + 1);

    float weightSum = 0.0f;

    for (int i = -blurRadius; i <= blurRadius; ++i)
    {
        float x = static_cast<float>(i);

        weights[i + blurRadius] = expf(-x * x / twoSigma2);

        weightSum += weights[i + blurRadius];
    }

    // Divide by the sum so all the weights add up to 1.0.
    for (int i = 0; i < weights.size(); ++i)
    {
        weights[i] /= weightSum;
    }

    return weights;
}

GTexture& SSAO::NormalMap()
{
    return normalMap;
}

GTexture& SSAO::AmbientMap()
{
    return ambientMap0;
}

GTexture& SSAO::NormalDepthMap()
{
    return depthMap;
}

GDescriptor* SSAO::NormalMapDSV()
{
    return &depthMapDSVMemory;
}


void SSAO::BuildDescriptors()
{
    RebuildDescriptors();
}

void SSAO::RebuildDescriptors() const
{
    D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc;
    dsvDesc.Flags = D3D12_DSV_FLAG_NONE;
    dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
    dsvDesc.Format = DXGI_FORMAT_D32_FLOAT;
    dsvDesc.Texture2D.MipSlice = 0;
    depthMap.CreateDepthStencilView(&dsvDesc, &depthMapDSVMemory);


    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Format = NormalMapFormat;
    srvDesc.Texture2D.MostDetailedMip = 0;
    srvDesc.Texture2D.MipLevels = 1;
    normalMap.CreateShaderResourceView(&srvDesc, &normalMapSrvMemory);

    srvDesc.Format = DXGI_FORMAT_R32_TYPELESS;
    depthMap.CreateShaderResourceView(&srvDesc, &depthMapSrvMemory);

    srvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    randomVectorMap.CreateShaderResourceView(&srvDesc, &randomVectorSrvMemory);

    srvDesc.Format = AmbientMapFormat;
    ambientMap0.CreateShaderResourceView(&srvDesc, &ambientMapMapSrvMemory);
    ambientMap1.CreateShaderResourceView(&srvDesc, &ambientMapMapSrvMemory, 1);

    D3D12_RENDER_TARGET_VIEW_DESC rtvDesc = {};
    rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
    rtvDesc.Format = NormalMapFormat;
    rtvDesc.Texture2D.MipSlice = 0;
    rtvDesc.Texture2D.PlaneSlice = 0;
    normalMap.CreateRenderTargetView(&rtvDesc, &normalMapRtvMemory);

    rtvDesc.Format = AmbientMapFormat;
    ambientMap0.CreateRenderTargetView(&rtvDesc, &ambientMapRtvMemory);
    ambientMap1.CreateRenderTargetView(&rtvDesc, &ambientMapRtvMemory, 1);
}

void SSAO::SetPipelineData(GraphicPSO& ssaoPso, GraphicPSO& ssaoBlurPso)
{
    mSsaoPso = ssaoPso;
    mBlurPso = ssaoBlurPso;
}

void SSAO::OnResize(const UINT newWidth, const UINT newHeight)
{
    if (mRenderTargetWidth != newWidth || mRenderTargetHeight != newHeight)
    {
        mRenderTargetWidth = newWidth;
        mRenderTargetHeight = newHeight;

        mViewport.TopLeftX = 0.0f;
        mViewport.TopLeftY = 0.0f;
        mViewport.Width = mRenderTargetWidth;
        mViewport.Height = mRenderTargetHeight;
        mViewport.MinDepth = 0.0f;
        mViewport.MaxDepth = 1.0f;

        mScissorRect = {0, 0, static_cast<int>(mRenderTargetWidth), static_cast<int>(mRenderTargetHeight)};

        BuildResources();
    }
}

void SSAO::ComputeSsao(
    const std::shared_ptr<GCommandList>& cmdList,
    const std::shared_ptr<ConstantUploadBuffer<SsaoConstants>>& currFrame,
    const int blurCount)
{
    cmdList->SetViewports(&mViewport, 1);
    cmdList->SetScissorRects(&mScissorRect, 1);

    cmdList->TransitionBarrier(ambientMap0, D3D12_RESOURCE_STATE_RENDER_TARGET);
    cmdList->FlushResourceBarriers();


    float clearValue[] = {1.0f, 1.0f, 1.0f, 1.0f};
    cmdList->ClearRenderTarget(&ambientMapRtvMemory, 0, clearValue);

    cmdList->SetRenderTargets(1, &ambientMapRtvMemory, 0);

    cmdList->SetPipelineState(mSsaoPso);

    cmdList->SetDescriptorsHeap(&normalMapSrvMemory);
    cmdList->SetDescriptorsHeap(&randomVectorSrvMemory);
    cmdList->SetDescriptorsHeap(&ambientMapMapSrvMemory);
    
    cmdList->SetRootConstantBufferView(0, *currFrame.get());
    cmdList->SetRoot32BitConstant(1, 0, 0);

    cmdList->SetRootDescriptorTable(2, &normalMapSrvMemory);

    cmdList->SetRootDescriptorTable(3, &randomVectorSrvMemory);


    cmdList->SetVBuffer(0, 0, nullptr);
    cmdList->SetIBuffer(nullptr);
    cmdList->SetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    cmdList->Draw(6, 1, 0, 0);


    cmdList->TransitionBarrier(ambientMap0, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    cmdList->FlushResourceBarriers();

    BlurAmbientMap(cmdList, currFrame, blurCount);
}

void SSAO::ClearAmbiantMap(
    const std::shared_ptr<GCommandList>& cmdList) const
{
    cmdList->TransitionBarrier(ambientMap0, D3D12_RESOURCE_STATE_RENDER_TARGET);
    cmdList->FlushResourceBarriers();

    float clearValue[] = {0.0f, 0.0f, 0.0f, 1.0f};
    cmdList->ClearRenderTarget(&ambientMapRtvMemory, 0, clearValue);


    cmdList->TransitionBarrier(ambientMap0, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    cmdList->FlushResourceBarriers();
}

void SSAO::BlurAmbientMap(const std::shared_ptr<GCommandList>& cmdList,
                          const std::shared_ptr<ConstantUploadBuffer<SsaoConstants>>& currFrame, const int blurCount)
{
    cmdList->SetPipelineState(mBlurPso);

    cmdList->SetRootConstantBufferView(0, *currFrame.get());

    for (int i = 0; i < blurCount; ++i)
    {
        BlurAmbientMap(cmdList, true);
        BlurAmbientMap(cmdList, false);
    }
}

void SSAO::BlurAmbientMap(const std::shared_ptr<GCommandList>& cmdList, const bool horzBlur) const
{
    GTexture output;
    size_t inputSrv;
    size_t outputRtv;

    if (horzBlur == true)
    {
        output = ambientMap1;
        inputSrv = 0;
        outputRtv = 1;
        cmdList->SetRoot32BitConstant(1, 1, 0);
    }
    else
    {
        output = ambientMap0;
        inputSrv = 1;
        outputRtv = 0;
        cmdList->SetRoot32BitConstant(1, 0, 0);
    }

    cmdList->TransitionBarrier(output, D3D12_RESOURCE_STATE_RENDER_TARGET);
    cmdList->FlushResourceBarriers();

    float clearValue[] = {1.0f, 1.0f, 1.0f, 1.0f};
    cmdList->ClearRenderTarget(&ambientMapRtvMemory, outputRtv, clearValue);

    cmdList->SetRenderTargets(1, &ambientMapRtvMemory, outputRtv);

    cmdList->SetRootDescriptorTable(2, &normalMapSrvMemory);

    cmdList->SetRootDescriptorTable(3, &ambientMapMapSrvMemory, inputSrv);

    // Draw fullscreen quad.
    cmdList->SetVBuffer(0, 0, nullptr);
    cmdList->SetIBuffer(nullptr);
    cmdList->SetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    cmdList->Draw(6, 1, 0, 0);

    cmdList->TransitionBarrier(output, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    cmdList->FlushResourceBarriers();
}

GTexture SSAO::CreateNormalMap() const
{
    D3D12_RESOURCE_DESC texDesc;
    ZeroMemory(&texDesc, sizeof(D3D12_RESOURCE_DESC));
    texDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    texDesc.Alignment = 0;
    texDesc.Width = mRenderTargetWidth;
    texDesc.Height = mRenderTargetHeight;
    texDesc.DepthOrArraySize = 1;
    texDesc.MipLevels = 1;
    texDesc.Format = NormalMapFormat;
    texDesc.SampleDesc.Count = 1;
    texDesc.SampleDesc.Quality = 0;
    texDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    texDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;


    float normalClearColor[] = {0.0f, 0.0f, 1.0f, 0.0f};
    CD3DX12_CLEAR_VALUE optClear(NormalMapFormat, normalClearColor);

    return GTexture(device, texDesc, L"SSAO NormalMap", TextureUsage::Normalmap, &optClear);
}

GTexture SSAO::CreateAmbientMap() const
{
    D3D12_RESOURCE_DESC texDesc;
    ZeroMemory(&texDesc, sizeof(D3D12_RESOURCE_DESC));
    texDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    texDesc.Alignment = 0;
    texDesc.DepthOrArraySize = 1;
    texDesc.MipLevels = 1;
    texDesc.SampleDesc.Count = 1;
    texDesc.SampleDesc.Quality = 0;
    texDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    texDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    texDesc.Width = mRenderTargetWidth;
    texDesc.Height = mRenderTargetHeight;
    texDesc.Format = AmbientMapFormat;

    float ambientClearColor[] = {1.0f, 1.0f, 1.0f, 1.0f};
    auto optClear = CD3DX12_CLEAR_VALUE(AmbientMapFormat, ambientClearColor);

    return GTexture(device, texDesc, L"SSAO AmbientMap", TextureUsage::Normalmap, &optClear);
}


GTexture SSAO::CreateDepthMap() const
{
    D3D12_RESOURCE_DESC depthStencilDesc;
    depthStencilDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    depthStencilDesc.Alignment = 0;
    depthStencilDesc.Width = mRenderTargetWidth;
    depthStencilDesc.Height = mRenderTargetHeight;
    depthStencilDesc.DepthOrArraySize = 1;
    depthStencilDesc.MipLevels = 1;
    depthStencilDesc.Format = DepthMapFormat;
    depthStencilDesc.SampleDesc.Count = 1;
    depthStencilDesc.SampleDesc.Quality = 0;
    depthStencilDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    depthStencilDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

    D3D12_CLEAR_VALUE optClear;
    optClear.Format = DXGI_FORMAT_D32_FLOAT;
    optClear.DepthStencil.Depth = 1.0f;
    optClear.DepthStencil.Stencil = 0;

    return GTexture(device, depthStencilDesc, L"SSAO Depth Normal Map", TextureUsage::Depth, &optClear);
}


void SSAO::BuildResources()
{
    if (normalMap.GetD3D12Resource() == nullptr)
    {
        normalMap = CreateNormalMap();
    }
    else
    {
        GTexture::Resize(normalMap, mRenderTargetWidth, mRenderTargetHeight, 1);
    }

    if (depthMap.GetD3D12Resource() == nullptr)
    {
        depthMap = CreateDepthMap();
    }
    else
    {
        GTexture::Resize(depthMap, mRenderTargetWidth, mRenderTargetHeight, 1);
    }

    if (ambientMap0.GetD3D12Resource() == nullptr)
    {
        ambientMap0 = CreateAmbientMap();
    }
    else
    {
        GTexture::Resize(ambientMap0, mRenderTargetWidth, mRenderTargetHeight, 1);
    }

    if (ambientMap1.GetD3D12Resource() == nullptr)
    {
        ambientMap1 = CreateAmbientMap();
    }
    else
    {
        GTexture::Resize(ambientMap1, mRenderTargetWidth, mRenderTargetHeight, 1);
    }
}

void SSAO::BuildRandomVectorTexture(const std::shared_ptr<GCommandList>& cmdList)
{
    D3D12_RESOURCE_DESC texDesc;
    ZeroMemory(&texDesc, sizeof(D3D12_RESOURCE_DESC));
    texDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    texDesc.Alignment = 0;
    texDesc.Width = 256;
    texDesc.Height = 256;
    texDesc.DepthOrArraySize = 1;
    texDesc.MipLevels = 1;
    texDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    texDesc.SampleDesc.Count = 1;
    texDesc.SampleDesc.Quality = 0;
    texDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    texDesc.Flags = D3D12_RESOURCE_FLAG_NONE;

    randomVectorMap = GTexture(device, texDesc, L"SSAO Random Vector Map", TextureUsage::Normalmap);

    std::vector<Vector4> data;
    data.resize(256 * 256);

    for (int i = 0; i < 256; ++i)
    {
        for (int j = 0; j < 256; ++j)
        {
            // Random vector in [0,1].  We will decompress in shader to [-1,1].
            Vector3 v(MathHelper::RandF(), MathHelper::RandF(), MathHelper::RandF());
            data[i * 256 + j] = Vector4(v.x, v.y, v.z, 0.0f);
        }
    }

    D3D12_SUBRESOURCE_DATA subResourceData = {};
    subResourceData.pData = data.data();
    subResourceData.RowPitch = 256 * sizeof(Vector4);
    subResourceData.SlicePitch = subResourceData.RowPitch * 256;

    cmdList->TransitionBarrier(randomVectorMap.GetD3D12Resource(), D3D12_RESOURCE_STATE_COPY_DEST);
    cmdList->FlushResourceBarriers();

    cmdList->UpdateSubresource(randomVectorMap, &subResourceData, 1);

    cmdList->TransitionBarrier(randomVectorMap.GetD3D12Resource(), D3D12_RESOURCE_STATE_GENERIC_READ);
    cmdList->FlushResourceBarriers();
}

void SSAO::BuildOffsetVectors()
{
    // Start with 14 uniformly distributed vectors.  We choose the 8 corners of the cube
    // and the 6 center points along each cube face.  We always alternate the points on 
    // opposites sides of the cubes.  This way we still get the vectors spread out even
    // if we choose to use less than 14 samples.

    // 8 cube corners
    mOffsets[0] = Vector4(+1.0f, +1.0f, +1.0f, 0.0f);
    mOffsets[1] = Vector4(-1.0f, -1.0f, -1.0f, 0.0f);

    mOffsets[2] = Vector4(-1.0f, +1.0f, +1.0f, 0.0f);
    mOffsets[3] = Vector4(+1.0f, -1.0f, -1.0f, 0.0f);

    mOffsets[4] = Vector4(+1.0f, +1.0f, -1.0f, 0.0f);
    mOffsets[5] = Vector4(-1.0f, -1.0f, +1.0f, 0.0f);

    mOffsets[6] = Vector4(-1.0f, +1.0f, -1.0f, 0.0f);
    mOffsets[7] = Vector4(+1.0f, -1.0f, +1.0f, 0.0f);

    // 6 centers of cube faces
    mOffsets[8] = Vector4(-1.0f, 0.0f, 0.0f, 0.0f);
    mOffsets[9] = Vector4(+1.0f, 0.0f, 0.0f, 0.0f);

    mOffsets[10] = Vector4(0.0f, -1.0f, 0.0f, 0.0f);
    mOffsets[11] = Vector4(0.0f, +1.0f, 0.0f, 0.0f);

    mOffsets[12] = Vector4(0.0f, 0.0f, -1.0f, 0.0f);
    mOffsets[13] = Vector4(0.0f, 0.0f, +1.0f, 0.0f);

    for (auto& mOffset : mOffsets)
    {
        // Create random lengths in [0.25, 1.0].
        float s = MathHelper::RandF(0.25f, 1.0f);
        mOffset.Normalize();
        mOffset = s * mOffset;
    }
}
