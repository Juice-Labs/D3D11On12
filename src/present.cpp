// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
#include "pch.hpp"
#include "SwapChainHelper.hpp"

class Timer
{
public:
    Timer()
    {
        LARGE_INTEGER	largeFreq;
        QueryPerformanceFrequency(&largeFreq);
        freq = largeFreq.QuadPart;
    }

    uint64_t GetCount()
    {
        LARGE_INTEGER	count;
        QueryPerformanceCounter(&count);
        return count.QuadPart;
    }

    // returns number of ms between 2 counts
    double CalcTimeDelta(uint64_t count1, uint64_t count2)

    {
        return (((double)count2 / (double)freq - (double)count1 / (double)freq) * 1000.0);
    }


private:
    //num. counts per second
    uint64_t		freq;
};

namespace D3D11On12
{
#if 0
    HRESULT APIENTRY Device::Present1(DXGI1_6_1_DDI_ARG_PRESENT* pArgs)
    {
        D3D11on12_DDI_ENTRYPOINT_START();
        Device *pDevice = Device::CastFrom(pArgs->hDevice);

        assert(pArgs->pDirtyRects == nullptr && pArgs->DirtyRects == 0);
        PresentExtensionData ExtensionData{ };
        if (pArgs->SurfacesToPresent)
        {
            ExtensionData.pSrc = Resource::CastFrom(pArgs->phSurfacesToPresent[0].hSurface);
            ExtensionData.pDXGIContext = pArgs->pDXGIContext;
        }
        
#if 0
        pDevice->GetBatchedContext().BatchExtension(&pDevice->m_PresentExt, ExtensionData);
        pDevice->GetBatchedContext().SubmitBatch();
#endif

#if 0
        pDevice->TransitionResourceForRelease(ExtensionData.pSrc, D3D12_RESOURCE_STATE_PRESENT);
        pDevice->ApplyAllResourceTransitions();

        ExtensionData.pSrc->m_associatedUnderlyingSwapchain->Present(0, 0);

        pDevice->TransitionResourceForRelease(ExtensionData.pSrc, D3D12_RESOURCE_STATE_RENDER_TARGET);
        pDevice->ApplyAllResourceTransitions();
#endif

        pDevice->TransitionResourceForRelease(ExtensionData.pSrc, D3D12_RESOURCE_STATE_COPY_SOURCE);
        pDevice->ApplyAllResourceTransitions();

        static UINT bufferNum = 0;

        CComPtr<ID3D12Resource> backBuffer;
        ExtensionData.pSrc->m_associatedUnderlyingSwapchain->GetBuffer(bufferNum, IID_PPV_ARGS(&backBuffer));

        auto list = pDevice->FlushBatchAndGetImmediateContext().GetGraphicsCommandList();

        list->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(backBuffer, D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_COPY_DEST));
        list->CopyResource(backBuffer, ExtensionData.pSrc->GetUnderlyingResource());
        list->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(backBuffer, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PRESENT));

        ExtensionData.pSrc->m_associatedUnderlyingSwapchain->Present(0, 0);

        pDevice->FlushBatchAndGetImmediateContext();

        pDevice->TransitionResourceForRelease(ExtensionData.pSrc, D3D12_RESOURCE_STATE_RENDER_TARGET);
        pDevice->ApplyAllResourceTransitions();

        DXGIDDICB_PRESENT CBArgs = {};
        CBArgs.pDXGIContext = pArgs->pDXGIContext;
        HRESULT res = pDevice->m_pDXGICallbacks->pfnPresentCb(pDevice->m_hRTDevice.handle, &CBArgs);
        printf("%x\n", res);

        D3D11on12_DDI_ENTRYPOINT_END_AND_RETURN_HR(S_OK);
    }
#endif

    static bool callPresent = true;

    HRESULT APIENTRY Device::Present1(DXGI1_6_1_DDI_ARG_PRESENT* pArgs)
    {
        D3D11on12_DDI_ENTRYPOINT_START();
        Device* pDevice = Device::CastFrom(pArgs->hDevice);

        static uint64_t framesRendered = 0;
        static uint64_t startTime = 0;
        static uint64_t endTime = 0;

        auto t = Timer();
        if (startTime == 0)
        {
            startTime = t.GetCount();
        }

        {
            std::lock_guard lock(pDevice->m_SwapChainManagerMutex);
            if (!pDevice->m_SwapChainManager)
            {
                pDevice->m_SwapChainManager = std::make_shared<D3D12TranslationLayer::SwapChainManager>(pDevice->GetImmediateContextNoFlush());
            }
        }

        if (pArgs->SurfacesToPresent)
        {
            PresentExtensionData ExtensionData{ };
            ExtensionData.pSrc = Resource::CastFrom(pArgs->phSurfacesToPresent[0].hSurface);
            ExtensionData.pDXGIContext = pArgs->pDXGIContext;

            DXGIDDICB_PRESENT CBArgs = {};
            CBArgs.pDXGIContext = pArgs->pDXGIContext;
            CBArgs.hContext = pDevice->m_hContext;
            CBArgs.hSrcAllocation = ExtensionData.pSrc->m_hAllocation;
            HRESULT hr = pDevice->m_pDXGICallbacks->pfnPresentCb(pDevice->m_hRTDevice.handle, &CBArgs);
            assert(SUCCEEDED(hr));

            pDevice->GetBatchedContext().BatchExtension(&pDevice->m_PresentExt, ExtensionData);
            //pDevice->GetBatchedContext().SubmitBatch();
        }

        ++framesRendered;

        endTime = t.GetCount();

        double ticksPerFrame = (double)(endTime - startTime) / (double)framesRendered;
        printf("%f\n", ticksPerFrame);

        D3D11on12_DDI_ENTRYPOINT_END_AND_RETURN_HR(S_OK);
    }

    void Device::PresentExtension::Dispatch(D3D12TranslationLayer::ImmediateContext& context, const void* pData, size_t)
    {
        if (callPresent)
        {
            auto pArgs = reinterpret_cast<PresentExtensionData const*>(pData);

            auto pSwapChain = pArgs->pSrc->m_associatedUnderlyingSwapchain;
            CComPtr<IDXGISwapChain3> pSwapChain3;
            pSwapChain->QueryInterface(IID_PPV_ARGS(&pSwapChain3));
            auto swapChainHelper = D3D12TranslationLayer::SwapChainHelper(pSwapChain3);
            m_Device->m_SwapChainManager->WaitForMaximumFrameLatency();

            swapChainHelper.StandardPresent(context, D3DDDI_FLIPINTERVAL_IMMEDIATE, *pArgs->pSrc->ImmediateResource());
        }
    }

    HRESULT APIENTRY Device::Blt(DXGI_DDI_ARG_BLT* pArgs)
    {
        D3D11on12_DDI_ENTRYPOINT_START();

        D3D12_PLACED_SUBRESOURCE_FOOTPRINT& SrcSubresourceDesc =
            Resource::CastFromAndGetImmediateResource(pArgs->hSrcResource)->GetSubresourcePlacement(pArgs->SrcSubresource);

        DXGI_DDI_ARG_BLT1 Args1 =
        {
            pArgs->hDevice,
            pArgs->hDstResource,
            pArgs->DstSubresource,
            pArgs->DstLeft,
            pArgs->DstTop,
            pArgs->DstRight,
            pArgs->DstBottom,
            pArgs->hSrcResource,
            pArgs->SrcSubresource,
            0, //SrcRect.left,
            0, //SrcRect.top,
            SrcSubresourceDesc.Footprint.Width, //SrcRect.right,
            SrcSubresourceDesc.Footprint.Height, //SrcRect.bottom,
            pArgs->Flags,
            pArgs->Rotate,
        };

        HRESULT hr = BltImpl(&Args1);
        D3D11on12_DDI_ENTRYPOINT_END_AND_RETURN_HR(hr);
    }

    HRESULT APIENTRY Device::Blt1(DXGI_DDI_ARG_BLT1* pArgs)
    {
        D3D11on12_DDI_ENTRYPOINT_START();

        HRESULT hr = BltImpl(pArgs);
        D3D11on12_DDI_ENTRYPOINT_END_AND_RETURN_HR(hr);
    }

    HRESULT APIENTRY Device::RotateResourceIdentities(DXGI_DDI_ARG_ROTATE_RESOURCE_IDENTITIES* pArgs)
    {
        D3D11on12_DDI_ENTRYPOINT_START();
        Device *pDevice = Device::CastFrom(pArgs->hDevice);

        // TODO: Consider making RRI an extension so we can emplace in a batch directly from the DDI args
        // without having to allocate an intermediate array.
        std::vector<D3D12TranslationLayer::Resource*> pUnderlying(pArgs->Resources);

        for (size_t i = 0; i < pArgs->Resources; i++)
        {
            pUnderlying[i] = Resource::CastFromAndGetImmediateResource(pArgs->pResources[i]);
        }

        pDevice->GetBatchedContext().RotateResourceIdentities(pUnderlying.data(), pArgs->Resources);
        D3D11on12_DDI_ENTRYPOINT_END_AND_RETURN_HR(S_OK);
    }

    HRESULT APIENTRY Device::ResolveSharedResource(DXGI_DDI_ARG_RESOLVESHAREDRESOURCE* pArgs)
    {
        D3D11on12_DDI_ENTRYPOINT_START();
        Device *pDevice = Device::CastFrom(pArgs->hDevice);

        HRESULT hr = pDevice->FlushBatchAndGetImmediateContext().ResolveSharedResource(Resource::CastFromAndGetImmediateResource(pArgs->hResource));
        D3D11on12_DDI_ENTRYPOINT_END_AND_RETURN_HR(hr);
    }

    HRESULT APIENTRY Device::TrimResidencySet(DXGI_DDI_ARG_TRIMRESIDENCYSET* /*pArgs*/)
    {
        D3D11on12_DDI_ENTRYPOINT_START();
        D3D11on12_DDI_ENTRYPOINT_END_AND_RETURN_HR(S_OK);
    }

    HRESULT APIENTRY Device::SetResourcePriority(DXGI_DDI_ARG_SETRESOURCEPRIORITY* /*pArgs*/)
    {
        D3D11on12_DDI_ENTRYPOINT_START();
        D3D11on12_DDI_ENTRYPOINT_END_AND_RETURN_HR(S_OK);
    }

    HRESULT APIENTRY Device::CheckMultiplaneOverlayColorSpaceSupport(DXGI_DDI_ARG_CHECKMULTIPLANEOVERLAYCOLORSPACESUPPORT* pArgs)
    {
        D3D11on12_DDI_ENTRYPOINT_START();

        pArgs->Supported = FALSE;

        D3D12TranslationLayer::ImmediateContext& Context = Device::CastFrom(pArgs->hDevice)->GetImmediateContextNoFlush();

        if (Context.IsXbox())
        {
            switch (pArgs->Format)
            {
            case DXGI_FORMAT_B8G8R8A8_UNORM:
            case DXGI_FORMAT_B8G8R8X8_UNORM:
            case DXGI_FORMAT_R8G8B8A8_UNORM:
            case DXGI_FORMAT_R10G10B10A2_UNORM:
            case DXGI_FORMAT_R16G16B16A16_FLOAT:
            case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
            case DXGI_FORMAT_B8G8R8X8_UNORM_SRGB:
            case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
                switch (pArgs->ColorSpace)
                {
                case D3DDDI_COLOR_SPACE_RGB_FULL_G22_NONE_P709:
                case D3DDDI_COLOR_SPACE_RGB_STUDIO_G22_NONE_P709:
                case D3DDDI_COLOR_SPACE_RGB_STUDIO_G22_NONE_P2020:
                case D3DDDI_COLOR_SPACE_YCBCR_STUDIO_G22_LEFT_P709:
                case D3DDDI_COLOR_SPACE_YCBCR_STUDIO_G22_LEFT_P2020:
                case D3DDDI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020:
                case D3DDDI_COLOR_SPACE_YCBCR_STUDIO_G2084_LEFT_P2020:
                case D3DDDI_COLOR_SPACE_RGB_STUDIO_G2084_NONE_P2020:
                case D3DDDI_COLOR_SPACE_YCBCR_STUDIO_G22_TOPLEFT_P2020:
                case D3DDDI_COLOR_SPACE_YCBCR_STUDIO_G2084_TOPLEFT_P2020:
                case D3DDDI_COLOR_SPACE_RGB_FULL_G22_NONE_P2020:
                    pArgs->Supported = TRUE;
                    break;
                }
                break;
            }
        }

        D3D11on12_DDI_ENTRYPOINT_END_AND_RETURN_HR(S_OK);
    }

    HRESULT APIENTRY Device::PresentMultiplaneOverlay1(DXGI1_6_1_DDI_ARG_PRESENTMULTIPLANEOVERLAY* /*pArgs*/)
    {
        D3D11on12_DDI_ENTRYPOINT_START();
        D3D11on12_DDI_ENTRYPOINT_END_AND_RETURN_HR(S_OK);
    }

    HRESULT Device::BltImpl(DXGI_DDI_ARG_BLT1* pArgs)
    {
        D3D12TranslationLayer::ImmediateContext& Context = Device::CastFrom(pArgs->hDevice)->FlushBatchAndGetImmediateContext();

        // TODO: Rotation?
        assert(pArgs->Rotate == DXGI_DDI_MODE_ROTATION_IDENTITY);
        RECT DstRect = { (LONG)pArgs->DstLeft, (LONG)pArgs->DstTop, (LONG)pArgs->DstRight, (LONG)pArgs->DstBottom };
        RECT SrcRect = { (LONG)pArgs->SrcLeft, (LONG)pArgs->SrcTop, (LONG)pArgs->SrcRight, (LONG)pArgs->SrcBottom };
        Context.m_BlitHelper.Blit(
            Resource::CastFromAndGetImmediateResource(pArgs->hSrcResource),
            pArgs->SrcSubresource,
            SrcRect,
            Resource::CastFromAndGetImmediateResource(pArgs->hDstResource),
            pArgs->DstSubresource,
            DstRect,
            true,
            false);

        return S_OK;
    }

    STDMETHODIMP Device::Present(D3DKMT_PRESENT* pKMTPresent) noexcept
    {
        D3D11on12_DDI_ENTRYPOINT_START();
        auto pArgs = m_pPresentArgs;
        m_pPresentArgs = nullptr;
        
        std::lock_guard lock(m_SwapChainManagerMutex);
        if (!m_SwapChainManager)
        {
            m_SwapChainManager = std::make_shared<D3D12TranslationLayer::SwapChainManager>(GetImmediateContextNoFlush());
        }

        auto pSwapChain = m_SwapChainManager->GetSwapChainForWindow(pKMTPresent->hWindow, *pArgs->pSrc->ImmediateResource());
        auto swapChainHelper = D3D12TranslationLayer::SwapChainHelper( pSwapChain );
        m_SwapChainManager->WaitForMaximumFrameLatency();

        HRESULT hr = swapChainHelper.StandardPresent(GetImmediateContextNoFlush(), pKMTPresent->FlipInterval, *pArgs->pSrc->ImmediateResource());
        D3D11on12_DDI_ENTRYPOINT_END_AND_RETURN_HR(hr);
    }

    STDMETHODIMP_(void) Device::SetMaximumFrameLatency(UINT MaxFrameLatency) noexcept
    {
        UNREFERENCED_PARAMETER(MaxFrameLatency);
        D3D11on12_DDI_ENTRYPOINT_START();
        std::lock_guard lock(m_SwapChainManagerMutex);
        if (!m_SwapChainManager)
        {
            m_SwapChainManager = std::make_shared<D3D12TranslationLayer::SwapChainManager>(GetImmediateContextNoFlush());
        }
        m_SwapChainManager->SetMaximumFrameLatency(MaxFrameLatency);
        CLOSE_TRYCATCH_AND_STORE_HRESULT(S_OK);
    }

    STDMETHODIMP_(bool) Device::IsMaximumFrameLatencyReached() noexcept
    {
        bool ret = false;
        D3D11on12_DDI_ENTRYPOINT_START();
        std::lock_guard lock(m_SwapChainManagerMutex);
        if (!m_SwapChainManager)
        {
            m_SwapChainManager = std::make_shared<D3D12TranslationLayer::SwapChainManager>(GetImmediateContextNoFlush());
        }
        ret = m_SwapChainManager->IsMaximumFrameLatencyReached();
        CLOSE_TRYCATCH_AND_STORE_HRESULT(S_OK);
        return ret;
    }

    void Device::AcquireResource(D3D10DDI_HDEVICE hDevice, D3D10DDI_HRESOURCE, HANDLE hSyncToken) noexcept
    {
        auto pThis = CastFrom(hDevice);
        pThis->GetBatchedContext().BatchExtension(&pThis->m_AcquireResourceExt, hSyncToken);
    }

    void Device::ReleaseResource(D3D10DDI_HDEVICE hDevice, D3D10DDI_HRESOURCE, HANDLE hSyncToken) noexcept
    {
        auto pThis = CastFrom(hDevice);
        pThis->GetBatchedContext().BatchExtension(&pThis->m_ReleaseResourceExt, hSyncToken);
        pThis->GetBatchedContext().SubmitBatch();
    }

    void Device::QueryScanoutCaps(D3D10DDI_HDEVICE, D3D10DDI_HRESOURCE, UINT, D3DDDI_VIDEO_PRESENT_SOURCE_ID, UINT, D3DWDDM2_6DDI_SCANOUT_FLAGS* pFlags) noexcept
    {
        // Don't do front buffer rendering
        *pFlags = D3DWDDM2_6DDI_SCANOUT_FLAGS(D3DWDDM2_6DDI_SCANOUT_FLAG_TRANSFORMATION_REQUIRED | D3DWDDM2_6DDI_SCANOUT_FLAG_UNPREDICTABLE_TIMING);
    }

    void Device::PrepareScanoutTransformation(D3D10DDI_HDEVICE, D3D10DDI_HRESOURCE, UINT, D3DDDI_VIDEO_PRESENT_SOURCE_ID, UINT, RECT*) noexcept
    {
        // Do nothing
    }

    STDMETHODIMP_(void) Device::SharingContractPresent(_In_ ID3D11On12DDIResource* pResource) noexcept
    {
        D3D11on12_DDI_ENTRYPOINT_START();
        auto& Context = FlushBatchAndGetImmediateContext();
        Context.SharingContractPresent(static_cast<Resource*>(pResource)->ImmediateResource());
        CLOSE_TRYCATCH_AND_STORE_HRESULT(S_OK);
    }
}
