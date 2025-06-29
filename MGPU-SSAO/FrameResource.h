#pragma once

#include "GDescriptor.h"
#include "ShaderBuffersData.h"
#include "GTexture.h"

using namespace PEPEngine;
using namespace Graphics;


struct FrameResource
{
    FrameResource(std::shared_ptr<GDevice> primeDevices, std::shared_ptr<GDevice> secondDevice, UINT passCount,
                  UINT materialCount);
    FrameResource(const FrameResource& rhs) = delete;
    FrameResource& operator=(const FrameResource& rhs) = delete;
    ~FrameResource();


    GDescriptor BackBufferRTVMemory;

    std::shared_ptr<ConstantUploadBuffer<PassConstants>> PrimePassConstantUploadBuffer;
    std::shared_ptr<ConstantUploadBuffer<SsaoConstants>> SsaoConstantUploadBuffer;
    std::shared_ptr<StructuredUploadBuffer<MaterialConstants>> MaterialBuffer;


    UINT64 PrimeRenderFenceValue = 0;
    UINT64 PrimeCopyFenceValue = 0;
    UINT64 SecondRenderFenceValue = 0;
};
