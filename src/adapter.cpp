// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
#include "pch.hpp"

HRESULT WINAPI OpenAdapter_D3D11On12(_Inout_ D3D10DDIARG_OPENADAPTER* pArgs, _Inout_ D3D11On12::SOpenAdapterArgs* pArgs2)
{
    try
    {
        pArgs->hAdapter.pDrvPrivate = new D3D11On12::Adapter(pArgs, *pArgs2); // throw( _com_error, bad_alloc )
        return S_OK;
    }
    catch (_com_error& hrEx) { return hrEx.Error(); }
    catch (std::bad_alloc&) { return E_OUTOFMEMORY; }
}

static std::mutex s_ResourceCreationArgsMutex;
static std::unordered_map<void*, D3D11DDIARG_CREATERESOURCE> s_ResourceCreationArgs{};

static void RegisterHandleCreationForD3D12(_In_ D3D10DDI_HRESOURCE hResource, _In_ CONST D3D11DDIARG_CREATERESOURCE* createArgs)
{
    std::lock_guard<decltype(s_ResourceCreationArgsMutex)> lg(s_ResourceCreationArgsMutex);

    s_ResourceCreationArgs.emplace(hResource.pDrvPrivate, *createArgs);
}
void RegisterHandleDestructionForD3D12(_In_ D3D10DDI_HRESOURCE hResource)
{
    std::lock_guard<decltype(s_ResourceCreationArgsMutex)> lg(s_ResourceCreationArgsMutex);

    auto it = s_ResourceCreationArgs.find(hResource.pDrvPrivate);
    assert(it != s_ResourceCreationArgs.end());

    s_ResourceCreationArgs.erase(it);
}

static D3D11_RESOURCE_FLAGS GetResourceFlagsForD3D12(_In_ D3D10DDI_HRESOURCE hResource, _Out_ bool* pbAcquireableOnWrite)
{
    std::lock_guard<decltype(s_ResourceCreationArgsMutex)> lg(s_ResourceCreationArgsMutex);

    auto it = s_ResourceCreationArgs.find(hResource.pDrvPrivate);
    assert(it != s_ResourceCreationArgs.end());

    CONST D3D11DDIARG_CREATERESOURCE* createArgs = &it->second;

    D3D11_RESOURCE_FLAGS flags{};
    *pbAcquireableOnWrite = true;

    if (createArgs->Usage == D3D10_DDI_USAGE_DEFAULT)
    {
        // Will have no CPU access
    }
    else if (createArgs->Usage == D3D10_DDI_USAGE_IMMUTABLE)
    {
        // Will have no CPU access
    }
    else if (createArgs->Usage == D3D10_DDI_USAGE_DYNAMIC)
    {
        flags.CPUAccessFlags |= D3D11_CPU_ACCESS_WRITE;
    }
    else if (createArgs->Usage == D3D10_DDI_USAGE_STAGING)
    {
        if ((createArgs->MapFlags & (D3D10_DDI_MAP_WRITE | D3D10_DDI_MAP_WRITE_DISCARD | D3D10_DDI_MAP_WRITE_NOOVERWRITE)) != 0)
        {
            flags.CPUAccessFlags |= D3D11_CPU_ACCESS_WRITE;
        }

        if ((createArgs->MapFlags & D3D10_DDI_MAP_READ) != 0)
        {
            flags.CPUAccessFlags |= D3D11_CPU_ACCESS_READ;
        }
    }

    if ((createArgs->MiscFlags & D3D10_DDI_RESOURCE_MISC_SHARED) != 0)
    {
        flags.MiscFlags |= D3D10_RESOURCE_MISC_SHARED;
    }

    if ((createArgs->MiscFlags & D3D10_DDI_RESOURCE_AUTO_GEN_MIP_MAP) != 0)
    {
        flags.MiscFlags |= D3D10_RESOURCE_MISC_GENERATE_MIPS;
    }

    if ((createArgs->BindFlags & D3D10_DDI_BIND_RENDER_TARGET) != 0)
    {
        flags.BindFlags |= D3D11_BIND_RENDER_TARGET;
    }

    if ((createArgs->BindFlags & D3D10_DDI_BIND_CONSTANT_BUFFER) != 0)
    {
        flags.BindFlags |= D3D11_BIND_CONSTANT_BUFFER;
    }

    if ((createArgs->BindFlags & D3D10_DDI_BIND_DEPTH_STENCIL) != 0)
    {
        flags.BindFlags |= D3D11_BIND_DEPTH_STENCIL;
    }

    if ((createArgs->BindFlags & D3D10_DDI_BIND_INDEX_BUFFER) != 0)
    {
        flags.BindFlags |= D3D11_BIND_INDEX_BUFFER;
    }

    if ((createArgs->BindFlags & D3D10_DDI_BIND_SHADER_RESOURCE) != 0)
    {
        flags.BindFlags |= D3D11_BIND_SHADER_RESOURCE;
    }

    if ((createArgs->BindFlags & D3D10_DDI_BIND_VERTEX_BUFFER) != 0)
    {
        flags.BindFlags |= D3D11_BIND_VERTEX_BUFFER;
    }

    if ((createArgs->BindFlags & D3D11_DDI_BIND_UNORDERED_ACCESS) != 0)
    {
        flags.BindFlags |= D3D11_BIND_UNORDERED_ACCESS;
    }

    if ((createArgs->BindFlags & D3D10_DDI_BIND_STREAM_OUTPUT) != 0)
    {
        flags.BindFlags |= D3D11_BIND_STREAM_OUTPUT;
    }

    if ((createArgs->BindFlags & D3D11_DDI_BIND_DECODER) != 0)
    {
        flags.BindFlags |= D3D11_BIND_DECODER;
    }

    if ((createArgs->BindFlags & D3D11_DDI_BIND_VIDEO_ENCODER) != 0)
    {
        flags.BindFlags |= D3D11_BIND_VIDEO_ENCODER;
    }

    return flags;
}

static bool NotifySharedResourceCreationForD3D12(_In_ HANDLE, _In_ IUnknown*)
{
    return true;
}

static BOOL IsProcessDWM()
{
    BOOL result = FALSE;
    SID_IDENTIFIER_AUTHORITY identifierAuthority{};
    *(unsigned int*)(identifierAuthority.Value + 0) = 0;
    *(unsigned short*)(identifierAuthority.Value + 4) = 0x500;
    PSID psid;
    if (TRUE == AllocateAndInitializeSid(&identifierAuthority, 2, 0x5A, 0, 0, 0, 0, 0, 0, 0, &psid))
    {
        BOOL isMember = FALSE;
        if (TRUE == CheckTokenMembership(nullptr, psid, &isMember))
        {
            result = isMember;
        }
        FreeSid(psid);
    }
    return result;
}

HRESULT WINAPI OpenAdapter10_2(_Inout_ D3D10DDIARG_OPENADAPTER* pArgs)
{
    D3D11On12::SOpenAdapterArgs args{};

    CComPtr<IDXGIFactory4> factory;
    ThrowFailure(CreateDXGIFactory1(IID_PPV_ARGS(&factory)));

    CComPtr<IDXGIAdapter> warpAdapter;
    ThrowFailure(factory->EnumWarpAdapter(IID_PPV_ARGS(&warpAdapter)));

    CComPtr<ID3D12Debug> debugController;
    if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController))))
    {
        debugController->EnableDebugLayer();
    }

    ThrowFailure(D3D12CreateDevice(
        warpAdapter,
        D3D_FEATURE_LEVEL_11_0,
        IID_PPV_ARGS(&args.pDevice)
    ));

    D3D12_COMMAND_QUEUE_DESC queueDesc = {};
    queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
    queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;

    ThrowFailure(args.pDevice->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&args.p3DCommandQueue)));
    args.pAdapter = warpAdapter;

    args.Callbacks.RegisterHandleCreation = RegisterHandleCreationForD3D12;
    args.Callbacks.RegisterHandleDestruction = RegisterHandleDestructionForD3D12;
    args.Callbacks.GetResourceFlags = GetResourceFlagsForD3D12;
    args.Callbacks.NotifySharedResourceCreation = NotifySharedResourceCreationForD3D12;

    return OpenAdapter_D3D11On12(pArgs, &args);
}

namespace D3D11On12
{
    //----------------------------------------------------------------------------------------------------------------------------------
    Adapter::Adapter(D3D10DDIARG_OPENADAPTER *pOpenAdapter, SOpenAdapterArgs& Args) noexcept(false)
        : m_pUnderlyingDevice(Args.pDevice)
        , m_p3DCommandQueue(Args.p3DCommandQueue)
        , m_NodeIndex(Args.NodeIndex)
        , m_Callbacks(Args.Callbacks)
        , m_bAPIDisablesGPUTimeout(Args.bDisableGPUTimeout)
        , m_bSupportDisplayableTextures(Args.D3D11On12InterfaceVersion >= 5 ?
            Args.bSupportDisplayableTextures : false)
        , m_bSupportDeferredContexts(Args.D3D11On12InterfaceVersion >= 5 ?
            Args.bSupportDeferredContexts : true)
    {
        static const D3D10_2DDI_ADAPTERFUNCS AdapterFuncs = {
            CalcPrivateDeviceSize,
            CreateDevice,
            CloseAdapter,
            GetSupportedVersions,
            GetCaps
        };
        *pOpenAdapter->pAdapterFuncs_2 = AdapterFuncs;
        pOpenAdapter->hAdapter = GetHandle();
    
        m_Architecture.NodeIndex = m_NodeIndex;

        assert(m_pUnderlyingDevice);
   
        HRESULT hr = m_pUnderlyingDevice->CheckFeatureSupport(D3D12_FEATURE_ARCHITECTURE1, &m_Architecture, sizeof(m_Architecture));
        assert(SUCCEEDED(hr));
    
        hr = m_pUnderlyingDevice->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS, &m_Caps, sizeof(m_Caps));
        assert(SUCCEEDED(hr));

        D3D_FEATURE_LEVEL FLList[] = {
             D3D_FEATURE_LEVEL_1_0_CORE, 
             D3D_FEATURE_LEVEL_10_0, 
             D3D_FEATURE_LEVEL_10_1,
             D3D_FEATURE_LEVEL_11_0,
             D3D_FEATURE_LEVEL_11_1,
             D3D_FEATURE_LEVEL_12_0,
             D3D_FEATURE_LEVEL_12_1,
             D3D_FEATURE_LEVEL_9_1,
             D3D_FEATURE_LEVEL_9_2,
             D3D_FEATURE_LEVEL_9_3
        };
        D3D12_FEATURE_DATA_FEATURE_LEVELS FLDesc = {};
        FLDesc.NumFeatureLevels = _countof(FLList);
        FLDesc.pFeatureLevelsRequested = FLList;
        hr = m_pUnderlyingDevice->CheckFeatureSupport(D3D12_FEATURE_FEATURE_LEVELS,&FLDesc,sizeof(FLDesc));
        assert(SUCCEEDED(hr));
        m_bComputeOnly = !!(FLDesc.MaxSupportedFeatureLevel == D3D_FEATURE_LEVEL_1_0_CORE);

        m_ShaderModelCaps.HighestShaderModel = D3D_SHADER_MODEL_6_2;
        hr = m_pUnderlyingDevice->CheckFeatureSupport(D3D12_FEATURE_SHADER_MODEL, &m_ShaderModelCaps, sizeof(m_ShaderModelCaps));
        assert(SUCCEEDED(hr));

        Args.D3D11On12InterfaceVersion = std::max(Args.D3D11On12InterfaceVersion, c_CurrentD3D11On12InterfaceVersion);
    }
    
    //----------------------------------------------------------------------------------------------------------------------------------
    SIZE_T APIENTRY Adapter::CalcPrivateDeviceSize(_In_ D3D10DDI_HADAPTER /*hAdapter*/, _In_ const D3D10DDIARG_CALCPRIVATEDEVICESIZE*)
    {
        return sizeof(Device);
    }
    
    //----------------------------------------------------------------------------------------------------------------------------------
    HRESULT APIENTRY Adapter::CreateDevice(_In_ D3D10DDI_HADAPTER hAdapter, _Inout_ D3D10DDIARG_CREATEDEVICE* pArgs)
    {
        D3D11on12_DDI_ENTRYPOINT_START();
        HRESULT hr = DXGI_STATUS_NO_REDIRECTION;

        auto pAdapter = CastFrom(hAdapter);
        new (pArgs->hDrvDevice.pDrvPrivate) Device(pAdapter, pArgs); // throw( _com_error, bad_alloc )

        ThrowFailure(pAdapter->m_pUnderlyingDevice->QueryInterface(&pAdapter->m_pCompatDevice));

        D3D11on12_DDI_ENTRYPOINT_END_AND_RETURN_HR(hr);
    }
    
    //----------------------------------------------------------------------------------------------------------------------------------
    HRESULT APIENTRY Adapter::CloseAdapter(_In_ D3D10DDI_HADAPTER hAdapter)
    {
        auto pAdapter = CastFrom(hAdapter);
        delete pAdapter;
        return S_OK;
    }

    //----------------------------------------------------------------------------------------------------------------------------------
    HRESULT APIENTRY Adapter::GetSupportedVersions(_In_ D3D10DDI_HADAPTER, _Inout_ UINT32 *pNumVersions, _Out_opt_ UINT64 *pSupportedVersions)
    {
        static constexpr UINT64 SupportedVersions[] =
        {
            D3DWDDM2_6_DDI_SUPPORTED,
            D3DWDDM2_7_DDI_SUPPORTED,
        };
        *pNumVersions = _countof(SupportedVersions);
        if (pSupportedVersions)
        {
            std::copy(SupportedVersions, std::end(SupportedVersions), pSupportedVersions);
        }
        return S_OK;
    }
    
    //----------------------------------------------------------------------------------------------------------------------------------
    HRESULT APIENTRY Adapter::GetCaps(_In_ D3D10DDI_HADAPTER hAdapter, _In_ const D3D10_2DDIARG_GETCAPS* pArgs)
    {
        auto pThis = CastFrom(hAdapter);
        HRESULT hr = S_OK;
    
        auto& Caps12 = pThis->m_Caps;
        auto& Architecture = pThis->m_Architecture;
    
        switch (pArgs->Type)
        {
            case D3D10_2DDICAPS_TYPE::D3D11DDICAPS_3DPIPELINESUPPORT:
            {
                auto pCaps = reinterpret_cast<D3D11DDI_3DPIPELINESUPPORT_CAPS*>(pArgs->pData);
                if (pArgs->DataSize != sizeof(*pCaps))
                    return E_INVALIDARG;

                D3D12_FEATURE_DATA_FEATURE_LEVELS FLQuery = {};
                static const D3D_FEATURE_LEVEL UnderstoodFLs[] =
                {
                    D3D_FEATURE_LEVEL_1_0_CORE,
                    D3D_FEATURE_LEVEL_11_0,
                    D3D_FEATURE_LEVEL_11_1,
                    D3D_FEATURE_LEVEL_12_0,
                    D3D_FEATURE_LEVEL_12_1,
                };
                FLQuery.NumFeatureLevels = _countof(UnderstoodFLs);
                FLQuery.pFeatureLevelsRequested = UnderstoodFLs;
                FLQuery.MaxSupportedFeatureLevel = D3D_FEATURE_LEVEL_12_1;
                if (FAILED(pThis->m_pUnderlyingDevice->CheckFeatureSupport(D3D12_FEATURE_FEATURE_LEVELS, &FLQuery, sizeof(FLQuery))))
                    return E_INVALIDARG;

                // VSO 12194692: AMD is not able to support multi-channel min-max filtering without static samplers.
                // This is required in D3D11 tiled resources tier 2, which is required for FL12.
                // In D3D11On12, we will downgrade all tiled resources tier 2 to tier 1, which requires
                // also downgrading FL12.0 and FL12.1 to FL11.1.
                if (Caps12.TiledResourcesTier == D3D12_TILED_RESOURCES_TIER_2)
                {
                    FLQuery.MaxSupportedFeatureLevel = std::min(FLQuery.MaxSupportedFeatureLevel, D3D_FEATURE_LEVEL_11_1);
                }

                pCaps->Caps =
                    D3D11DDI_ENCODE_3DPIPELINESUPPORT_CAP(D3D11DDI_3DPIPELINELEVEL_10_0) |
                    D3D11DDI_ENCODE_3DPIPELINESUPPORT_CAP(D3D11DDI_3DPIPELINELEVEL_10_1) |
                    D3D11DDI_ENCODE_3DPIPELINESUPPORT_CAP(D3D11DDI_3DPIPELINELEVEL_11_0) |
                    (FLQuery.MaxSupportedFeatureLevel >= D3D_FEATURE_LEVEL_11_1 ? D3D11DDI_ENCODE_3DPIPELINESUPPORT_CAP(D3D11_1DDI_3DPIPELINELEVEL_11_1) : 0) |
                    (FLQuery.MaxSupportedFeatureLevel >= D3D_FEATURE_LEVEL_12_0 ? D3D11DDI_ENCODE_3DPIPELINESUPPORT_CAP(D3DWDDM2_0DDI_3DPIPELINELEVEL_12_0) : 0) |
                    (FLQuery.MaxSupportedFeatureLevel >= D3D_FEATURE_LEVEL_12_1 ? D3D11DDI_ENCODE_3DPIPELINESUPPORT_CAP(D3DWDDM2_0DDI_3DPIPELINELEVEL_12_1) : 0);
                break;
            }
            case D3D10_2DDICAPS_TYPE::D3D11DDICAPS_THREADING:
            {
                auto pCaps = reinterpret_cast<D3D11DDI_THREADING_CAPS*>(pArgs->pData);
                if (pArgs->DataSize != sizeof(*pCaps))
                    return E_INVALIDARG;
                pCaps->Caps = D3D11DDICAPS_FREETHREADED;
                if (!pThis->m_bComputeOnly && pThis->m_bSupportDeferredContexts)
                {
                    pCaps->Caps |= D3D11DDICAPS_COMMANDLISTS | D3D11DDICAPS_COMMANDLISTS_BUILD_2;
                }
                break;
            }
            case D3D10_2DDICAPS_TYPE::D3D11DDICAPS_SHADER:
            {
                auto pCaps = reinterpret_cast<D3D11DDI_SHADER_CAPS*>(pArgs->pData);
                if (pArgs->DataSize != sizeof(*pCaps))
                    return E_INVALIDARG;
    
                pCaps->Caps = D3D11DDICAPS_SHADER_COMPUTE_PLUS_RAW_AND_STRUCTURED_BUFFERS_IN_SHADER_4_X;
                if (Caps12.DoublePrecisionFloatShaderOps)
                    pCaps->Caps |= D3D11DDICAPS_SHADER_DOUBLES;
                if (Caps12.PSSpecifiedStencilRefSupported)
                    pCaps->Caps |= D3D11DDICAPS_SHADER_SPECIFIED_STENCIL_REF;
                if (Caps12.ROVsSupported)
                    pCaps->Caps |= D3D11DDICAPS_SHADER_ROVS;
                if (Caps12.TypedUAVLoadAdditionalFormats)
                    pCaps->Caps |= D3D11DDICAPS_SHADER_TYPED_UAV_LOAD_ADDITIONAL_FORMATS;
                break;
            }
            case D3D10_2DDICAPS_TYPE::D3D11_1DDICAPS_D3D11_OPTIONS:
            {
                auto pCaps = reinterpret_cast<D3D11_1DDI_D3D11_OPTIONS_DATA*>(pArgs->pData);
                if (pArgs->DataSize != sizeof(*pCaps))
                    return E_INVALIDARG;
    
                pCaps->OutputMergerLogicOp = Caps12.OutputMergerLogicOp;
                break;
            }
            case D3D10_2DDICAPS_TYPE::D3D11_1DDICAPS_ARCHITECTURE_INFO:
            {
                auto pCaps = reinterpret_cast<D3DDDICAPS_ARCHITECTURE_INFO*>(pArgs->pData);
                if (pArgs->DataSize != sizeof(*pCaps))
                    return E_INVALIDARG;
    
                pCaps->TileBasedDeferredRenderer = Architecture.TileBasedRenderer;
                break;
            }
            case D3D10_2DDICAPS_TYPE::D3D11_1DDICAPS_SHADER_MIN_PRECISION_SUPPORT:
            {
                auto pCaps = reinterpret_cast<D3DDDICAPS_SHADER_MIN_PRECISION_SUPPORT*>(pArgs->pData);
                if (pArgs->DataSize != sizeof(*pCaps))
                    return E_INVALIDARG;
    
                UINT DDIMinPrecision = 
                    ((Caps12.MinPrecisionSupport & D3D12_SHADER_MIN_PRECISION_SUPPORT_10_BIT) ? D3D11_DDI_SHADER_MIN_PRECISION_10_BIT : 0) |
                    ((Caps12.MinPrecisionSupport & D3D12_SHADER_MIN_PRECISION_SUPPORT_16_BIT) ? D3D11_DDI_SHADER_MIN_PRECISION_16_BIT : 0);
                pCaps->PixelShaderMinPrecision = DDIMinPrecision;
                pCaps->VertexShaderMinPrecision = DDIMinPrecision;
                break;
            }
            case D3D10_2DDICAPS_TYPE::D3DWDDM1_3DDICAPS_D3D11_OPTIONS1:
            {
                auto pCaps = reinterpret_cast<D3DWDDM1_3DDI_D3D11_OPTIONS_DATA1*>(pArgs->pData);
                if (pArgs->DataSize != sizeof(*pCaps))
                    return E_INVALIDARG;
    
                if (Caps12.ResourceHeapTier >= D3D12_RESOURCE_HEAP_TIER_2)
                {
                    D3D12_FEATURE_DATA_D3D12_OPTIONS5 D3D12Options5;
                    // Once all the D3D12 heap flags disappear, then tiled resources can be cleanly supported.
                    //
                    // VSO 12194692: AMD is not able to support multi-channel min-max filtering without static samplers.
                    // This is required in D3D11 tiled resources tier 2. In D3D11On12, we will downgrade most tiled resources tier 2 to tier 1.
                    switch (Caps12.TiledResourcesTier)
                    {
                        case D3D12_TILED_RESOURCES_TIER_NOT_SUPPORTED: pCaps->TiledResourcesSupportFlags = 0; break;
                        case D3D12_TILED_RESOURCES_TIER_1: pCaps->TiledResourcesSupportFlags = D3DWDDM1_3DDI_TILED_RESOURCES_TIER_1_SUPPORTED; break;

                        case D3D12_TILED_RESOURCES_TIER_2:
                            // Hardware that supports SRV-Only Tiled Resource Tier 3 needs to support Tier 2, to validate behavior well.
                            pCaps->TiledResourcesSupportFlags
                                = ( SUCCEEDED
                                        ( pThis->m_pUnderlyingDevice->CheckFeatureSupport
                                            ( D3D12_FEATURE_D3D12_OPTIONS5
                                            , &D3D12Options5
                                            , sizeof(D3D12Options5)
                                            )
                                        )
                                    && D3D12Options5.SRVOnlyTiledResourceTier3
                                    )
                                ? D3DWDDM1_3DDI_TILED_RESOURCES_TIER_2_SUPPORTED
                                : D3DWDDM1_3DDI_TILED_RESOURCES_TIER_1_SUPPORTED
                                ;
                            break;

                        case D3D12_TILED_RESOURCES_TIER_3: pCaps->TiledResourcesSupportFlags = D3DWDDM2_0DDI_TILED_RESOURCES_TIER_3_SUPPORTED; break;
                        default: ASSUME(false);
                    }
                }
                else
                {
                    pCaps->TiledResourcesSupportFlags = 0;
                }
                break;
            }
            case D3D10_2DDICAPS_TYPE::D3DWDDM2_0DDICAPS_D3D11_OPTIONS2:
            {
                auto pCaps = reinterpret_cast<D3DWDDM2_0DDI_D3D11_OPTIONS2_DATA*>(pArgs->pData);
                if (pArgs->DataSize != sizeof(*pCaps))
                    return E_INVALIDARG;
    
                C_ASSERT(D3D12_CONSERVATIVE_RASTERIZATION_TIER_NOT_SUPPORTED == D3DWDDM2_0DDI_CONSERVATIVE_RASTERIZATION_NOT_SUPPORTED);
                C_ASSERT(D3D12_CONSERVATIVE_RASTERIZATION_TIER_1        == D3DWDDM2_0DDI_CONSERVATIVE_RASTERIZATION_TIER_1);
                C_ASSERT(D3D12_CONSERVATIVE_RASTERIZATION_TIER_2        == D3DWDDM2_0DDI_CONSERVATIVE_RASTERIZATION_TIER_2);
                C_ASSERT(D3D12_CONSERVATIVE_RASTERIZATION_TIER_3        == D3DWDDM2_0DDI_CONSERVATIVE_RASTERIZATION_TIER_3);
                pCaps->ConservativeRasterizationTier = static_cast<D3DWDDM2_0DDI_CONSERVATIVE_RASTERIZATION_TIER>(Caps12.ConservativeRasterizationTier);
    
#ifdef DX_ASTC_PROTOTYPE_ENABLED
                C_ASSERT(D3D12_ASTC_PROFILE_NOT_SUPPORTED  == D3DWDDM2_0DDI_ASTC_PROFILE_NOT_SUPPORTED);
                C_ASSERT(D3D12_ASTC_PROFILE_LDR            == D3DWDDM2_0DDI_ASTC_PROFILE_LDR_SUPPORTED);
                C_ASSERT(D3D12_ASTC_PROFILE_HDR            == D3DWDDM2_0DDI_ASTC_PROFILE_HDR_SUPPORTED);
                C_ASSERT(D3D12_ASTC_PROFILE_FULL           == D3DWDDM2_0DDI_ASTC_PROFILE_FULL_SUPPORTED);
                pCaps->ASTCProfileSupportFlag = static_cast<D3DWDDM2_0DDI_ASTC_PROFILE_SUPPORT_FLAG>(Caps12.ASTCProfile);
#endif
    
                break;
            }
            case D3D10_2DDICAPS_TYPE::D3DWDDM2_0DDICAPS_D3D11_OPTIONS3:
            {
                auto pCaps = reinterpret_cast<D3DWDDM2_0DDI_D3D11_OPTIONS3_DATA*>(pArgs->pData);
                if (pArgs->DataSize != sizeof(*pCaps))
                    return E_INVALIDARG;
    
                pCaps->VPAndRTArrayIndexFromAnyShaderFeedingRasterizer = TRUE;
                break;
            }
            case D3D10_2DDICAPS_TYPE::D3DWDDM2_0DDICAPS_MEMORY_ARCHITECTURE:
            {
                auto pCaps = reinterpret_cast<D3DWDDM2_0DDI_MEMORY_ARCHITECTURE_CAPS*>(pArgs->pData);
                if (pArgs->DataSize != sizeof(*pCaps))
                    return E_INVALIDARG;
    
                pCaps->UMA = Architecture.UMA;
                pCaps->CacheCoherent = Architecture.CacheCoherentUMA;
                break;
            }
            case D3D10_2DDICAPS_TYPE::D3DWDDM2_2DDICAPS_TEXTURE_LAYOUT:
            {
                if (!pArgs->pInfo)
                {
                    auto pCaps = reinterpret_cast<D3DWDDM2_2DDI_TEXTURE_LAYOUT_CAPS*>(pArgs->pData);
                    if (pArgs->DataSize != sizeof(*pCaps))
                        return E_INVALIDARG;
                        
                    pCaps->DeviceDependentLayoutCount = 0;
                    pCaps->DeviceDependentSwizzleCount  = 0;
                    pCaps->Supports64KStandardSwizzle = Caps12.StandardSwizzle64KBSupported;
                    pCaps->IndexableSwizzlePatterns = FALSE;
                }
                else
                {
                    return E_FAIL;
                }
                break;
            }
            case D3D10_2DDICAPS_TYPE::D3DWDDM2_2DDICAPS_SWIZZLE_PATTERN:
            {
                return E_FAIL;
            }
            case D3D10_2DDICAPS_TYPE::D3DWDDM1_3DDICAPS_MARKER:
            {
                // From MSDN: If the GetCaps(D3D10_2) function is called with this value for Type and the driver does not support markers, the driver should return an error code.
                return E_FAIL;
            }
            case D3D10_2DDICAPS_TYPE::D3DWDDM2_0DDICAPS_GPUVA_CAPS:
            {
                D3D12_FEATURE_DATA_GPU_VIRTUAL_ADDRESS_SUPPORT Support;
                if (FAILED(pThis->m_pUnderlyingDevice->CheckFeatureSupport(
                    D3D12_FEATURE_GPU_VIRTUAL_ADDRESS_SUPPORT, &Support, sizeof(Support))))
                {
                    return E_INVALIDARG; // Mismatched binaries
                }
    
                auto pCaps = reinterpret_cast<D3DWDDM2_0DDI_GPUVA_CAPS_DATA*>(pArgs->pData);
                if (pArgs->DataSize != sizeof(*pCaps))
                    return E_INVALIDARG;
    
                pCaps->MaxGPUVirtualAddressBitsPerResource = Support.MaxGPUVirtualAddressBitsPerResource;
                break;
            }
            case D3D10_2DDICAPS_TYPE::D3DWDDM2_2DDICAPS_SHADERCACHE:
            {
                auto pCaps = reinterpret_cast<D3DWDDM2_2DDICAPS_SHADERCACHE_DATA*>(pArgs->pData);
                if (pArgs->DataSize != sizeof(*pCaps))
                    return E_INVALIDARG;

                pCaps->RequestRuntimeShaderCache = pThis->m_ShaderModelCaps.HighestShaderModel >= D3D_SHADER_MODEL_6_0;
                break;
            }
    
            default:
                return E_FAIL;
        }
        return hr;
    }
};
