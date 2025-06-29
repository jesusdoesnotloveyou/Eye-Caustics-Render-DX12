#include "pch.h"

#include <array>

#include "SharedSSAO.h"
#include <DirectXPackedVector.h>


#include "GCommandList.h"
#include "GDevice.h"
#include "GResourceStateTracker.h"
#include "MathHelper.h"
#include "ShaderBuffersData.h"

using namespace Microsoft::WRL;

SharedSSAO::SharedSSAO() {}

SharedSSAO::~SharedSSAO() = default;

GDescriptor* SharedSSAO::PrimeNormalMapRtv()
{
    return &primeNormalMapRtvMemory;
}

GDescriptor* SharedSSAO::PrimeNormalMapSrv()
{
    return &primeNormalMapSrvMemory;
}

GDescriptor* SharedSSAO::PrimeAmbientMapSrv()
{
    return &primeAmbientMapSrvMemory;
}

UINT SharedSSAO::SsaoMapWidth() const
{
    return mRenderTargetWidth / 2;
}

UINT SharedSSAO::SsaoMapHeight() const
{
    return mRenderTargetHeight / 2;
}

void SharedSSAO::GetOffsetVectors(Vector4 offsets[14])
{
    std::copy(&mOffsets[0], &mOffsets[14], &offsets[0]);
}

std::vector<float> SharedSSAO::CalcGaussWeights(const float sigma)
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

GTexture& SharedSSAO::PrimeNormalMap()
{
    return primeNormalMap;
}

GTexture& SharedSSAO::PrimeDepthMap()
{
    return primeDepthMap;
}

GDescriptor* SharedSSAO::PrimeNormalMapDSV()
{
    return &primeDepthMapDsvMemory;
}

GTexture& SharedSSAO::PrimeAmbientMap()
{
    return primeAmbientMap0;
}


void SharedSSAO::BuildDescriptors()
{
    RebuildDescriptors();
}

void SharedSSAO::RebuildDescriptors() const
{
    // Prime GPU
    D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc;
    dsvDesc.Flags = D3D12_DSV_FLAG_NONE;
    dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
    dsvDesc.Format = DXGI_FORMAT_D32_FLOAT;
    dsvDesc.Texture2D.MipSlice = 0;
    primeDepthMap.CreateDepthStencilView(&dsvDesc, &primeDepthMapDsvMemory);
    
    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Format = NormalMapFormat;
    srvDesc.Texture2D.MostDetailedMip = 0;
    srvDesc.Texture2D.MipLevels = 1;
    primeNormalMap.CreateShaderResourceView(&srvDesc, &primeNormalMapSrvMemory);

    srvDesc.Format = DXGI_FORMAT_R32_FLOAT;
    primeDepthMap.CreateShaderResourceView(&srvDesc, &primeDepthMapSrvMemory);

    srvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    primeRandomVectorMap.CreateShaderResourceView(&srvDesc, &primeRandomVectorSrvMemory);

    srvDesc.Format = AmbientMapFormat;
    primeAmbientMap0.CreateShaderResourceView(&srvDesc, &primeAmbientMapSrvMemory);
    primeAmbientMap1.CreateShaderResourceView(&srvDesc, &primeAmbientMapSrvMemory, 1);

    D3D12_RENDER_TARGET_VIEW_DESC rtvDesc = {};
    rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
    rtvDesc.Format = NormalMapFormat;
    rtvDesc.Texture2D.MipSlice = 0;
    rtvDesc.Texture2D.PlaneSlice = 0;
    primeNormalMap.CreateRenderTargetView(&rtvDesc, &primeNormalMapRtvMemory);

    rtvDesc.Format = AmbientMapFormat;
    primeAmbientMap0.CreateRenderTargetView(&rtvDesc, &primeAmbientMapRtvMemory);
    primeAmbientMap1.CreateRenderTargetView(&rtvDesc, &primeAmbientMapRtvMemory, 1);

    // Secondary GPU    
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Format = NormalMapFormat;
    srvDesc.Texture2D.MostDetailedMip = 0;
    srvDesc.Texture2D.MipLevels = 1;
    secondNormalMap.CreateShaderResourceView(&srvDesc, &secondNormalMapSrvMemory);

    srvDesc.Format = DXGI_FORMAT_R32_FLOAT;
    secondDepthMap.CreateShaderResourceView(&srvDesc, &secondDepthMapSrvMemory);

    srvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    secondRandomVectorMap.CreateShaderResourceView(&srvDesc, &secondRandomVectorSrvMemory);

    srvDesc.Format = AmbientMapFormat;
    secondAmbientMap0.CreateShaderResourceView(&srvDesc, &secondAmbientMapSrvMemory);
    secondAmbientMap1.CreateShaderResourceView(&srvDesc, &secondAmbientMapSrvMemory, 1);

    rtvDesc.Format = AmbientMapFormat;
    secondAmbientMap0.CreateRenderTargetView(&rtvDesc, &secondAmbientMapRtvMemory);
    secondAmbientMap1.CreateRenderTargetView(&rtvDesc, &secondAmbientMapRtvMemory, 1);
}

void SharedSSAO::SetPipelineData(GraphicPSO& ssaoPso, GraphicPSO& ssaoBlurPso)
{
    mSsaoPso = ssaoPso;
    mBlurPso = ssaoBlurPso;
}

void SharedSSAO::OnResize(const UINT newWidth, const UINT newHeight)
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

        BuildResources(primeDevice, secondDevice);
    }
}

void SharedSSAO::ComputeSsao(
    const std::shared_ptr<GCommandList>& cmdList,
    const std::shared_ptr<ConstantUploadBuffer<SsaoConstants>>& currFrame,
    const int blurCount)
{
    cmdList->SetViewports(&mViewport, 1);
    cmdList->SetScissorRects(&mScissorRect, 1);

    cmdList->TransitionBarrier(primeAmbientMap0, D3D12_RESOURCE_STATE_RENDER_TARGET);
    cmdList->FlushResourceBarriers();


    float clearValue[] = {1.0f, 1.0f, 1.0f, 1.0f};
    cmdList->ClearRenderTarget(&primeAmbientMapRtvMemory, 0, clearValue);

    cmdList->SetRenderTargets(1, &primeAmbientMapRtvMemory, 0);

    cmdList->SetPipelineState(mSsaoPso);

    cmdList->SetDescriptorsHeap(&primeNormalMapSrvMemory);
    cmdList->SetDescriptorsHeap(&primeRandomVectorSrvMemory);
    cmdList->SetDescriptorsHeap(&primeAmbientMapSrvMemory);
    
    cmdList->SetRootConstantBufferView(0, *currFrame.get());
    cmdList->SetRoot32BitConstant(1, 0, 0);

    cmdList->SetRootDescriptorTable(2, &primeNormalMapSrvMemory);

    cmdList->SetRootDescriptorTable(3, &primeRandomVectorSrvMemory);


    cmdList->SetVBuffer(0, 0, nullptr);
    cmdList->SetIBuffer(nullptr);
    cmdList->SetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    cmdList->Draw(6, 1, 0, 0);


    cmdList->TransitionBarrier(primeAmbientMap0, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    cmdList->FlushResourceBarriers();

    BlurAmbientMap(cmdList, currFrame, blurCount);
}

void SharedSSAO::ClearAmbientMap(
    const std::shared_ptr<GCommandList>& cmdList) const
{
    cmdList->TransitionBarrier(primeAmbientMap0, D3D12_RESOURCE_STATE_RENDER_TARGET);
    cmdList->FlushResourceBarriers();

    float clearValue[] = {0.0f, 0.0f, 0.0f, 1.0f};
    cmdList->ClearRenderTarget(&primeAmbientMapRtvMemory, 0, clearValue);


    cmdList->TransitionBarrier(primeAmbientMap0, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    cmdList->FlushResourceBarriers();
}

void SharedSSAO::BlurAmbientMap(const std::shared_ptr<GCommandList>& cmdList,
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

void SharedSSAO::BlurAmbientMap(const std::shared_ptr<GCommandList>& cmdList, const bool horzBlur) const
{
    GTexture output;
    size_t inputSrv;
    size_t outputRtv;

    if (horzBlur == true)
    {
        output = primeAmbientMap1;
        inputSrv = 0;
        outputRtv = 1;
        cmdList->SetRoot32BitConstant(1, 1, 0);
    }
    else
    {
        output = primeAmbientMap0;
        inputSrv = 1;
        outputRtv = 0;
        cmdList->SetRoot32BitConstant(1, 0, 0);
    }

    cmdList->TransitionBarrier(output, D3D12_RESOURCE_STATE_RENDER_TARGET);
    cmdList->FlushResourceBarriers();

    float clearValue[] = {1.0f, 1.0f, 1.0f, 1.0f};
    cmdList->ClearRenderTarget(&primeAmbientMapRtvMemory, outputRtv, clearValue);

    cmdList->SetRenderTargets(1, &primeAmbientMapRtvMemory, outputRtv);

    cmdList->SetRootDescriptorTable(2, &primeNormalMapSrvMemory);

    cmdList->SetRootDescriptorTable(3, &primeAmbientMapSrvMemory, inputSrv);

    // Draw fullscreen quad.
    cmdList->SetVBuffer(0, 0, nullptr);
    cmdList->SetIBuffer(nullptr);
    cmdList->SetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    cmdList->Draw(6, 1, 0, 0);

    cmdList->TransitionBarrier(output, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    cmdList->FlushResourceBarriers();
}

GTexture SharedSSAO::CreateNormalMap(const std::shared_ptr<GDevice>& device) const
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

GTexture SharedSSAO::CreateAmbientMap(const std::shared_ptr<GDevice>& device) const
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

GTexture SharedSSAO::CreateDepthMap(const std::shared_ptr<GDevice>& device) const
{
    D3D12_RESOURCE_DESC depthStencilDesc;
    depthStencilDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    depthStencilDesc.Alignment = 0;
    depthStencilDesc.Width = mRenderTargetWidth;
    depthStencilDesc.Height = mRenderTargetHeight;
    depthStencilDesc.DepthOrArraySize = 1;
    depthStencilDesc.MipLevels = 1;
    depthStencilDesc.Format = DXGI_FORMAT_R32_TYPELESS;
    depthStencilDesc.SampleDesc.Count = 1;
    depthStencilDesc.SampleDesc.Quality = 0;
    depthStencilDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    depthStencilDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

    D3D12_CLEAR_VALUE optClear;
    optClear.Format = DXGI_FORMAT_D32_FLOAT;
    optClear.DepthStencil.Depth = 1.0f;

    return GTexture(device, depthStencilDesc, L"SSAO Depth Normal Map", TextureUsage::Depth, &optClear);
}


void SharedSSAO::BuildResources(const std::shared_ptr<GDevice>& primeDevice, const std::shared_ptr<GDevice>& secondDevice)
{
    // prime GPU
    if (primeNormalMap.GetD3D12Resource() == nullptr)
    {
        primeNormalMap = CreateNormalMap(primeDevice);
    }
    else
    {
        GTexture::Resize(primeNormalMap, mRenderTargetWidth, mRenderTargetHeight, 1);
    }

    if (primeDepthMap.GetD3D12Resource() == nullptr)
    {
        primeDepthMap = CreateDepthMap(primeDevice);
    }
    else
    {
        GTexture::Resize(primeDepthMap, mRenderTargetWidth, mRenderTargetHeight, 1);
    }

    if (primeAmbientMap0.GetD3D12Resource() == nullptr)
    {
        primeAmbientMap0 = CreateAmbientMap(primeDevice);
    }
    else
    {
        GTexture::Resize(primeAmbientMap0, mRenderTargetWidth, mRenderTargetHeight, 1);
    }

    if (primeAmbientMap1.GetD3D12Resource() == nullptr)
    {
        primeAmbientMap1 = CreateAmbientMap(primeDevice);
    }
    else
    {
        GTexture::Resize(primeAmbientMap1, mRenderTargetWidth, mRenderTargetHeight, 1);
    }

    // shared resources
    if (sharedNormalMap == nullptr)
    {
        sharedNormalMap = std::make_shared<GCrossAdapterResource>(primeNormalMap.GetD3D12ResourceDesc(), primeDevice, secondDevice,
                                                                       L"Cross Adapter Normal Map");
    }
    else
    {
        sharedNormalMap->Resize(mRenderTargetWidth, mRenderTargetHeight);
    }

    if (sharedDepthMap == nullptr)
    {
        sharedDepthMap = std::make_shared<GCrossAdapterResource>(primeDepthMap.GetD3D12ResourceDesc(), primeDevice, secondDevice,
                                                                       L"Cross Adapter Depth Map");
    }
    else
    {
        sharedDepthMap->Resize(mRenderTargetWidth, mRenderTargetHeight);
    }

    if (sharedAmbientMap == nullptr)
    {
        sharedAmbientMap = std::make_shared<GCrossAdapterResource>(primeAmbientMap0.GetD3D12ResourceDesc(), primeDevice, secondDevice,
                                                                       L"Cross Adapter Ambient Map");
    }
    else
    {
        sharedAmbientMap->Resize(mRenderTargetWidth, mRenderTargetHeight);
    }
    
    // secondary GPU resources
    if (secondNormalMap.GetD3D12Resource() == nullptr)
    {
        secondNormalMap = CreateNormalMap(secondDevice);
    }
    else
    {
        GTexture::Resize(secondNormalMap, mRenderTargetWidth, mRenderTargetHeight, 1);
    }

    if (secondDepthMap.GetD3D12Resource() == nullptr)
    {
        secondDepthMap = CreateDepthMap(secondDevice);
    }
    else
    {
        GTexture::Resize(secondDepthMap, mRenderTargetWidth, mRenderTargetHeight, 1);
    }

    if (secondAmbientMap0.GetD3D12Resource() == nullptr)
    {
        secondAmbientMap0 = CreateAmbientMap(secondDevice);
    }
    else
    {
        GTexture::Resize(secondAmbientMap0, mRenderTargetWidth, mRenderTargetHeight, 1);
    }

    if (secondAmbientMap1.GetD3D12Resource() == nullptr)
    {
        secondAmbientMap1 = CreateAmbientMap(secondDevice);
    }
    else
    {
        GTexture::Resize(secondAmbientMap1, mRenderTargetWidth, mRenderTargetHeight, 1);
    }
}

void SharedSSAO::BuildRandomVectorTexture(const GTexture& texture, const std::shared_ptr<GCommandList>& cmdList)
{
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

    cmdList->TransitionBarrier(texture.GetD3D12Resource(), D3D12_RESOURCE_STATE_COPY_DEST);
    cmdList->FlushResourceBarriers();

    cmdList->UpdateSubresource(texture, &subResourceData, 1);

    cmdList->TransitionBarrier(texture.GetD3D12Resource(), D3D12_RESOURCE_STATE_GENERIC_READ);
    cmdList->FlushResourceBarriers();
}

void SharedSSAO::BuildOffsetVectors()
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

void SharedSSAO::Initialize(const std::shared_ptr<GDevice>& primeDevice,
    const std::shared_ptr<GDevice>& secondDevice,
    const UINT width, const UINT height)
{
    this->primeDevice = primeDevice;
    this->secondDevice = secondDevice;

    // prime resources
    primeNormalMapSrvMemory = primeDevice->AllocateDescriptors(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 1);
    primeNormalMapRtvMemory = primeDevice->AllocateDescriptors(D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 1);
    primeDepthMapSrvMemory = primeDevice->AllocateDescriptors(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 1);
    primeDepthMapDsvMemory = primeDevice->AllocateDescriptors(D3D12_DESCRIPTOR_HEAP_TYPE_DSV, 1);
    primeRandomVectorSrvMemory = primeDevice->AllocateDescriptors(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 1);
    primeAmbientMapSrvMemory = primeDevice->AllocateDescriptors(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 2);
    primeAmbientMapRtvMemory = primeDevice->AllocateDescriptors(D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 2);

    // secondary resources
    secondNormalMapSrvMemory = secondDevice->AllocateDescriptors(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 1);
    secondDepthMapSrvMemory = secondDevice->AllocateDescriptors(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 1);
    secondRandomVectorSrvMemory = secondDevice->AllocateDescriptors(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 1);
    secondAmbientMapRtvMemory = secondDevice->AllocateDescriptors(D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 2);
    secondAmbientMapSrvMemory = secondDevice->AllocateDescriptors(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 2);
    
    ssaoPrimeRootSignature = std::make_shared<GRootSignature>();
    ssaoSecondRootSignature = std::make_shared<GRootSignature>();

    CD3DX12_DESCRIPTOR_RANGE texTable0;
    texTable0.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 2, 0, 0);

    CD3DX12_DESCRIPTOR_RANGE texTable1;
    texTable1.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 2, 0);

    ssaoPrimeRootSignature->AddConstantBufferParameter(0);
    ssaoPrimeRootSignature->AddConstantParameter(1, 1);
    ssaoPrimeRootSignature->AddDescriptorParameter(&texTable0, 1, D3D12_SHADER_VISIBILITY_PIXEL);
    ssaoPrimeRootSignature->AddDescriptorParameter(&texTable1, 1, D3D12_SHADER_VISIBILITY_PIXEL);

    ssaoSecondRootSignature->AddConstantBufferParameter(0);
    ssaoSecondRootSignature->AddConstantParameter(1, 1);
    ssaoSecondRootSignature->AddDescriptorParameter(&texTable0, 1, D3D12_SHADER_VISIBILITY_PIXEL);
    ssaoSecondRootSignature->AddDescriptorParameter(&texTable1, 1, D3D12_SHADER_VISIBILITY_PIXEL);

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
        ssaoSecondRootSignature->AddStaticSampler(sampler);
    }

    ssaoPrimeRootSignature->Initialize(primeDevice);
    ssaoSecondRootSignature->Initialize(secondDevice);
    
    OnResize(width, height);
    BuildOffsetVectors();

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

    primeRandomVectorMap = GTexture(primeDevice, texDesc, L"SSAO Random Vector Map", TextureUsage::Normalmap);
    secondRandomVectorMap = GTexture(secondDevice, texDesc, L"SSAO Random Vector Map", TextureUsage::Normalmap);
    
    auto primeCommandQueue = primeDevice->GetCommandQueue();
    auto secondCommandQueue = secondDevice->GetCommandQueue();

    auto primeCommandList = primeCommandQueue->GetCommandList();
    auto secondCommandList = secondCommandQueue->GetCommandList();
    
    BuildRandomVectorTexture(primeRandomVectorMap, primeCommandList);
    BuildRandomVectorTexture(secondRandomVectorMap, secondCommandList);

    primeCommandQueue->ExecuteCommandList(primeCommandList);
    secondCommandQueue->ExecuteCommandList(secondCommandList);

    primeDevice->Flush();
    secondDevice->Flush();
}

/*void SharedSSAO::CopyDataFromPrimeToShared(const std::shared_ptr<GCommandList>& cmdList)
{
    auto& source = primeNormalMap;
    auto& destination = sharedNormalMap;
}*/


// void SSAO::PrimeDrawCopyNormalDepth() -> draw normal+depth & copy to shared
// void SSAO::PrimeCopyAmbientMap() -> copy from shared to prime

// void SSAO::SecondCopyNormalDepth() -> copy from shared to second
// void SSAO::SecondDrawCopyAmbient() -> draw ambient & copy to shared