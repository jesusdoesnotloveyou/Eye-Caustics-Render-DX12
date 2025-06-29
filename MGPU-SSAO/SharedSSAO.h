#pragma once

#include "d3dUtil.h"
#include "GCrossAdapterResource.h"
#include "GraphicPSO.h"
#include "GDescriptor.h"
#include "GTexture.h"
#include "ShaderBuffersData.h"

using namespace DirectX::SimpleMath;

using namespace PEPEngine;
using namespace Graphics;
using namespace Allocator;
using namespace Utils;

class SharedSSAO
{
public:
    SharedSSAO();
    SharedSSAO(const SharedSSAO& rhs) = delete;
    SharedSSAO& operator=(const SharedSSAO& rhs) = delete;
    ~SharedSSAO();

    static constexpr DXGI_FORMAT AmbientMapFormat = DXGI_FORMAT_R16_UNORM;
    static constexpr DXGI_FORMAT NormalMapFormat = DXGI_FORMAT_R16G16B16A16_FLOAT;

    static constexpr int MaxBlurRadius = 5;

    UINT SsaoMapWidth() const;
    UINT SsaoMapHeight() const;

    void GetOffsetVectors(Vector4 offsets[]);
    std::vector<float> CalcGaussWeights(float sigma);


    // Getters
    GTexture& PrimeNormalMap();
    GTexture& PrimeAmbientMap();
    GTexture& PrimeDepthMap();

    GDescriptor* PrimeNormalMapDSV();
    GDescriptor* PrimeNormalMapRtv();
    GDescriptor* PrimeNormalMapSrv();
    GDescriptor* PrimeAmbientMapSrv();

    void Initialize(const std::shared_ptr<GDevice>& primeDevice, const std::shared_ptr<GDevice>& secondDevice,
        const UINT width, const UINT height);
    
    void BuildDescriptors();

    void RebuildDescriptors() const;

    void SetPipelineData(GraphicPSO& ssaoPso, GraphicPSO& ssaoBlurPso);

    void OnResize(UINT newWidth, UINT newHeight);

    

    void ComputeSsao(
        const std::shared_ptr<GCommandList>& cmdList,
        const std::shared_ptr<ConstantUploadBuffer<SsaoConstants>>& currFrame,
        int blurCount);
    void ClearAmbientMap(const std::shared_ptr<GCommandList>& cmdList) const;

private:
    std::shared_ptr<GRootSignature> ssaoPrimeRootSignature;
    std::shared_ptr<GRootSignature> ssaoSecondRootSignature;

    void BlurAmbientMap(const std::shared_ptr<GCommandList>& cmdList,
                        const std::shared_ptr<ConstantUploadBuffer<SsaoConstants>>& currFrame, int blurCount);
    void BlurAmbientMap(const std::shared_ptr<GCommandList>& cmdList, bool horzBlur) const;
    GTexture CreateNormalMap(const std::shared_ptr<GDevice>& device) const;
    GTexture CreateAmbientMap(const std::shared_ptr<GDevice>& device) const;
    GTexture CreateDepthMap(const std::shared_ptr<GDevice>& device) const;

    void BuildResources(const std::shared_ptr<GDevice>& primeDevice, const std::shared_ptr<GDevice>& secondDevice);
    void BuildRandomVectorTexture(const GTexture& texture, const std::shared_ptr<GCommandList>& cmdList);

    void BuildOffsetVectors();

    std::shared_ptr<GDevice> primeDevice;
    std::shared_ptr<GDevice> secondDevice;

    
    GraphicPSO mSsaoPso;
    GraphicPSO mBlurPso;

    
    /* Primary GPU resources */
    GTexture primeRandomVectorMap;
    GDescriptor primeRandomVectorSrvMemory;
    
    GTexture primeNormalMap;
    GDescriptor primeNormalMapSrvMemory;
    GDescriptor primeNormalMapRtvMemory;
    
    GTexture primeAmbientMap0;
    GTexture primeAmbientMap1;
    GDescriptor primeAmbientMapSrvMemory;
    GDescriptor primeAmbientMapRtvMemory;
    
    GTexture primeDepthMap;
    GDescriptor primeDepthMapSrvMemory;
    GDescriptor primeDepthMapDsvMemory;

    /* Secondary GPU resources */
    GTexture secondRandomVectorMap;
    GDescriptor secondRandomVectorSrvMemory;
    
    GTexture secondNormalMap;
    GDescriptor secondNormalMapSrvMemory;
    
    GTexture secondAmbientMap0;
    GTexture secondAmbientMap1;
    GDescriptor secondAmbientMapSrvMemory;
    GDescriptor secondAmbientMapRtvMemory;
    
    GTexture secondDepthMap;
    GDescriptor secondDepthMapSrvMemory;

    /* Shared GPU resources*/
    std::shared_ptr<GCrossAdapterResource> sharedNormalMap;
    std::shared_ptr<GCrossAdapterResource> sharedDepthMap;
    std::shared_ptr<GCrossAdapterResource> sharedAmbientMap;

    
    void PrimeDrawCopyNormalDepth(const std::shared_ptr<GDevice>& secondDevice);
    GTexture SharedSSAO::CreateSharedDepthMap(const std::shared_ptr<GDevice>& secondDevice);
    
    UINT mRenderTargetWidth;
    UINT mRenderTargetHeight;

    Vector4 mOffsets[14];

    D3D12_VIEWPORT mViewport;
    D3D12_RECT mScissorRect;
};
