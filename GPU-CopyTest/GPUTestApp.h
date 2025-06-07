#pragma once
#include "AssetsLoader.h"
#include "d3dApp.h"
#include "Renderer.h"
#include "ShadowMap.h"
#include "FrameResource.h"
#include "GCrossAdapterResource.h"

class GPUTestApp final : public Common::D3DApp
{
public:
    GPUTestApp(HINSTANCE hInstance);
    ~GPUTestApp() override;

    void LoadTexture();
    bool Initialize() override;;

    int Run() override;

protected:
    void Update(const GameTimer& gt) override;
    void Draw(const GameTimer& gt) override;

    void InitDevices();
    void InitFrameResource();
    void CalculateFrameStats() override;
    bool InitMainWindow() override;
    void OnResize() override;
    void Flush() override;
    LRESULT MsgProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) override;

    std::shared_ptr<GDevice> device;
    UINT64 GPURenderingTime = 0;

    D3D12_VIEWPORT fullViewport{};
    D3D12_RECT fullRect;
    std::shared_ptr<AssetsLoader> assets;
    
    std::shared_ptr<GTexture> leftTextureDirect;
    std::shared_ptr<GTexture> rightTextureDirect;
    std::shared_ptr<GTexture> tempTextureDirect;

    std::shared_ptr<GTexture> leftTextureCopy;
    std::shared_ptr<GTexture> rightTextureCopy;
    std::shared_ptr<GTexture> tempTextureCopy;
    
    std::vector<std::shared_ptr<FrameResource>> frameResources;
    std::shared_ptr<FrameResource> currentFrameResource = nullptr;
    std::atomic<UINT> currentFrameResourceIndex = 0;
};
