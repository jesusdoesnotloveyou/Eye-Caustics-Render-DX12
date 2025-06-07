#include "GPUTestApp.h"

#include <array>
#include <filesystem>
#include <fstream>
#include "CameraController.h"
#include "GameObject.h"
#include "GDeviceFactory.h"
#include "GModel.h"
#include "imgui.h"
#include "ModelRenderer.h"
#include "Rotater.h"
#include "SkyBox.h"
#include "Transform.h"
#include "Window.h"

GPUTestApp::GPUTestApp(const HINSTANCE hInstance): D3DApp(hInstance)
{
}

GPUTestApp::~GPUTestApp() = default;

void GPUTestApp::Update(const GameTimer& gt)
{
    const UINT olderIndex = currentFrameResourceIndex - 1 > globalCountFrameResources
                                ? 0
                                : static_cast<UINT>(currentFrameResourceIndex);
    GPURenderingTime = device->GetCommandQueue()->GetTimestamp(olderIndex);

    const auto directQueue = device->GetCommandQueue(GQueueType::Graphics);
    const auto copyQueue = device->GetCommandQueue(GQueueType::Copy);

    currentFrameResource = frameResources[currentFrameResourceIndex];

    if (currentFrameResource->PrimeRenderFenceValue != 0 && !directQueue->IsFinish(
        currentFrameResource->PrimeRenderFenceValue))
    {
        directQueue->WaitForFenceValue(currentFrameResource->PrimeRenderFenceValue);
    }

    if (currentFrameResource->PrimeCopyFenceValue != 0 && !copyQueue->IsFinish(
        currentFrameResource->PrimeCopyFenceValue))
    {
        copyQueue->WaitForFenceValue(currentFrameResource->PrimeCopyFenceValue);
    }
}

void GPUTestApp::Draw(const GameTimer& gt)
{
    if (isResizing) return;

    constexpr int Count = 50;
    const UINT timestampHeapIndex = 2 * currentFrameResourceIndex;

    auto renderQueue = device->GetCommandQueue(GQueueType::Graphics);
    auto renderCmdList = renderQueue->GetCommandList();
    auto copyQueue = device->GetCommandQueue(GQueueType::Copy);
    auto copyCmdList = copyQueue->GetCommandList();
    
    if (currentFrameResource->PrimeRenderFenceValue == 0 || renderQueue->IsFinish(currentFrameResource->PrimeRenderFenceValue))
    {
        renderCmdList->EndQuery(timestampHeapIndex);
        for (UINT i = 0; i < Count; i++)
        {
            renderCmdList->CopyResource(*tempTextureDirect, *leftTextureDirect);
            renderCmdList->CopyResource(*leftTextureDirect, *rightTextureDirect);
            renderCmdList->CopyResource(*rightTextureDirect, *tempTextureDirect);
        }
        renderCmdList->SetViewports(&fullViewport, 1);
        renderCmdList->SetScissorRects(&fullRect, 1);
        renderCmdList->TransitionBarrier(MainWindow->GetCurrentBackBuffer(), D3D12_RESOURCE_STATE_RENDER_TARGET);
        renderCmdList->FlushResourceBarriers();
        renderCmdList->ClearRenderTarget(&currentFrameResource->BackBufferRTVMemory, 0,
            currentFrameResource->PrimeRenderFenceValue % 2 ? DirectX::Colors::Red : DirectX::Colors::Green);
        renderCmdList->TransitionBarrier(MainWindow->GetCurrentBackBuffer(), D3D12_RESOURCE_STATE_PRESENT);
        renderCmdList->FlushResourceBarriers();
        currentFrameResource->PrimeRenderFenceValue = renderQueue->ExecuteCommandList(renderCmdList);
    }

    if (currentFrameResource->PrimeCopyFenceValue == 0 || copyQueue->IsFinish(currentFrameResource->PrimeCopyFenceValue))
    {
        for (UINT i = 0; i < Count; i++)
        {
            copyCmdList->CopyResource(*tempTextureCopy, *leftTextureCopy);
            copyCmdList->CopyResource(*leftTextureCopy, *rightTextureCopy);
            copyCmdList->CopyResource(*rightTextureCopy, *tempTextureCopy);
        }
        currentFrameResource->PrimeCopyFenceValue = copyQueue->ExecuteCommandList(copyCmdList);
    }

    currentFrameResourceIndex = MainWindow->Present();
}

bool GPUTestApp::Initialize()
{
    InitDevices();
    InitMainWindow();
    LoadTexture();
    InitFrameResource();

    OnResize();

    Flush();

    return true;
}

void GPUTestApp::InitDevices()
{
    auto allDevices = GDeviceFactory::GetAllDevices(true);

    const auto firstDevice = allDevices[0];
    const auto otherDevice = allDevices[1];

    device = firstDevice;

    assets = std::make_shared<AssetsLoader>(device);
}

void GPUTestApp::InitFrameResource()
{
    for (int i = 0; i < globalCountFrameResources; ++i)
    {
        frameResources.emplace_back(std::make_unique<FrameResource>(device, device, 2, assets->GetMaterials().size()));
    }
}

void GPUTestApp::LoadTexture()
{
    auto queue = device->GetCommandQueue(GQueueType::Compute);

    const auto cmdList = queue->GetCommandList();

    leftTextureDirect = GTexture::LoadTextureFromFile(L"Data\\Objects\\DesertDragon\\T_DesertDragon_BaseColor.jpg",
                                                      cmdList);
    leftTextureCopy = GTexture::LoadTextureFromFile(L"Data\\Objects\\DesertDragon\\T_DesertDragon_BaseColor.jpg",
                                                    cmdList);
    assets->AddTexture(leftTextureDirect);
    assets->AddTexture(leftTextureCopy);

    rightTextureDirect = GTexture::LoadTextureFromFile(L"Data\\Objects\\DesertDragon\\T_DesertDragon_Normal_Yinv.jpg",
                                                       cmdList);
    rightTextureCopy = GTexture::LoadTextureFromFile(L"Data\\Objects\\DesertDragon\\T_DesertDragon_Normal_Yinv.jpg",
                                                     cmdList);
    assets->AddTexture(rightTextureDirect);
    assets->AddTexture(rightTextureCopy);

    queue->WaitForFenceValue(queue->ExecuteCommandList(cmdList));
    Flush();

    tempTextureDirect = std::make_shared<GTexture>(device, leftTextureDirect->GetD3D12ResourceDesc(), L"TempDirect");
    tempTextureCopy = std::make_shared<GTexture>(device, rightTextureDirect->GetD3D12ResourceDesc(), L"TempCopy");
    Flush();
}


void GPUTestApp::CalculateFrameStats()
{
    static float minFps = std::numeric_limits<float>::max();
    static float minMspf = std::numeric_limits<float>::max();
    static float maxFps = std::numeric_limits<float>::min();
    static float maxMspf = std::numeric_limits<float>::min();
    static UINT writeStaticticCount = 0;
    static UINT64 primeGPUTimeMax = std::numeric_limits<UINT64>::min();
    static UINT64 primeGPUTimeMin = std::numeric_limits<UINT64>::max();
    static UINT64 secondGPUTimeMax = std::numeric_limits<UINT64>::min();
    static UINT64 secondGPUTimeMin = std::numeric_limits<UINT64>::max();
    frameCount++;

    if ((timer.TotalTime() - timeElapsed) >= 1.0f)
    {
        float fps = static_cast<float>(frameCount); // fps = frameCnt / 1
        float mspf = 1000.0f / fps;

        minFps = std::min(fps, minFps);
        minMspf = std::min(mspf, minMspf);
        maxFps = std::max(fps, maxFps);
        maxMspf = std::max(mspf, maxMspf);

        primeGPUTimeMin = std::min(GPURenderingTime, primeGPUTimeMin);
        primeGPUTimeMax = std::max(GPURenderingTime, primeGPUTimeMax);

        if (writeStaticticCount >= 5)
        {
            const std::wstring staticticStr =
                L"Min FPS:" + std::to_wstring(minFps)
                + L" Min MSPF:" + std::to_wstring(minMspf)
                + L" Max FPS:" + std::to_wstring(maxFps)
                + L" Max MSPF:" + std::to_wstring(maxMspf)
                + L" Max GPU Time:" + std::to_wstring(primeGPUTimeMax) +
                +L" Min GPU Time:" + std::to_wstring(primeGPUTimeMin);

            MainWindow->SetWindowTitle(staticticStr.c_str());

            writeStaticticCount = 0;
            minFps = std::numeric_limits<float>::max();
            minMspf = std::numeric_limits<float>::max();
            maxFps = std::numeric_limits<float>::min();
            maxMspf = std::numeric_limits<float>::min();
            primeGPUTimeMax = std::numeric_limits<UINT64>::min();
            primeGPUTimeMin = std::numeric_limits<UINT64>::max();
            secondGPUTimeMax = std::numeric_limits<UINT64>::min();
            secondGPUTimeMin = std::numeric_limits<UINT64>::max();
        }
        else
        {
            writeStaticticCount++;
        }
        frameCount = 0;
        timeElapsed += 1.0f;
    }
}

int GPUTestApp::Run()
{
    MSG msg = {nullptr};

    timer.Reset();

    while (msg.message != WM_QUIT)
    {
        if (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE))
        {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
        else
        {
            timer.Tick();

            CalculateFrameStats();
            Update(timer);
            Draw(timer);

            device->ResetAllocators(frameCount++);
        }
    }

    return static_cast<int>(msg.wParam);
}

bool GPUTestApp::InitMainWindow()
{
    MainWindow = CreateRenderWindow(device, mainWindowCaption, 800, 600, false);
    return true;
}

void GPUTestApp::OnResize()
{
    D3DApp::OnResize();

    fullViewport.Height = static_cast<float>(MainWindow->GetClientHeight());
    fullViewport.Width = static_cast<float>(MainWindow->GetClientWidth());
    fullViewport.MinDepth = 0.0f;
    fullViewport.MaxDepth = 1.0f;
    fullViewport.TopLeftX = 0;
    fullViewport.TopLeftY = 0;
    fullRect = D3D12_RECT{0, 0, MainWindow->GetClientWidth(), MainWindow->GetClientHeight()};


    D3D12_RENDER_TARGET_VIEW_DESC rtvDesc = {};
    rtvDesc.Format = GetSRGBFormat(BackBufferFormat);
    rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;

    for (int i = 0; i < globalCountFrameResources; ++i)
    {
        MainWindow->GetBackBuffer(i).CreateRenderTargetView(&rtvDesc, &frameResources[i]->BackBufferRTVMemory);
    }

    if (camera != nullptr)
    {
        camera->SetAspectRatio(AspectRatio());
    }

    currentFrameResourceIndex = MainWindow->GetCurrentBackBufferIndex();
}

void GPUTestApp::Flush()
{
    device->Flush();
}

LRESULT GPUTestApp::MsgProc(const HWND hwnd, const UINT msg, const WPARAM wParam, const LPARAM lParam)
{
    switch (msg)
    {
    case WM_KEYUP:
        {
            auto keycode = static_cast<char>(wParam);
            keyboard.OnKeyReleased(keycode);
            return 0;
        }

    case WM_KEYDOWN:
        {
            auto keycode = static_cast<char>(wParam);
            if (keyboard.IsKeysAutoRepeat())
            {
                keyboard.OnKeyPressed(keycode);
            }
            else
            {
                const bool wasPressed = lParam & 0x40000000;
                if (!wasPressed)
                {
                    keyboard.OnKeyPressed(keycode);
                }
            }

            return 0;
        }
    }

    return D3DApp::MsgProc(hwnd, msg, wParam, lParam);
}
