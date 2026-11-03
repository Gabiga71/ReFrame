#include "OVR_CAPI_D3D.h"
#include "Common.h"
#include "Session.h"
#include "Runtime.h"
#include "SwapchainD3D11.h"
#include "SwapchainD3D12.h"
#include "XR_Math.h"

#include <detours/detours.h>
#include <wrl/client.h>
#include <vector>
#include <algorithm>
#include <atomic>

using Microsoft::WRL::ComPtr;

#define XR_USE_GRAPHICS_API_D3D11
#define XR_USE_GRAPHICS_API_D3D12
#include <d3d11.h>
#include <d3d12.h>
#include <openxr/openxr_platform.h>

LONG DetourVirtual(PVOID pInstance, UINT methodPos, PVOID *ppPointer, PVOID pDetour)
{
	if (!pInstance || !ppPointer)
		return ERROR_INVALID_PARAMETER;

	LPVOID* pVMT = *((LPVOID**)pInstance);
	*ppPointer = pVMT[methodPos];
	return DetourAttach(ppPointer, pDetour);
}

typedef HRESULT(WINAPI* _CreateShaderResourceView)(
	ID3D11Device						  *This,
	ID3D11Resource                        *pResource,
	const D3D11_SHADER_RESOURCE_VIEW_DESC *pDesc,
	ID3D11ShaderResourceView              **ppSRView);
typedef HRESULT(WINAPI* _CreateRenderTargetView)(
	ID3D11Device						*This,
	ID3D11Resource                      *pResource,
	const D3D11_RENDER_TARGET_VIEW_DESC *pDesc,
	ID3D11RenderTargetView              **ppRTView);
typedef HRESULT(WINAPI* _CreateShaderResourceView12)(
	ID3D12Device* This,
	ID3D12Resource* pResource,
	const D3D12_SHADER_RESOURCE_VIEW_DESC* pDesc,
	D3D12_CPU_DESCRIPTOR_HANDLE DestDescriptor);
typedef HRESULT(WINAPI* _CreateRenderTargetView12)(
	ID3D12Device* This,
	ID3D12Resource* pResource,
	const D3D12_RENDER_TARGET_VIEW_DESC* pDesc,
	D3D12_CPU_DESCRIPTOR_HANDLE DestDescriptor);

_CreateShaderResourceView TrueCreateShaderResourceView;
_CreateRenderTargetView TrueCreateRenderTargetView;
_CreateShaderResourceView12 TrueCreateShaderResourceView12;
_CreateRenderTargetView12 TrueCreateRenderTargetView12;

// These hooks see every view the game creates, not only those on swapchain textures, so only
// failures are logged and only a few of them. A failed view on a swapchain texture is how a
// format mismatch shows up: the engine treats it as fatal and exits without an error dialog.
static void LogViewFailure(const char* call, HRESULT hr, ID3D11Resource* pResource, DXGI_FORMAT viewFormat,
	int viewDimension = -1, int mipSlice = -1)
{
	static std::atomic<int> s_logged{ 0 };
	if (SUCCEEDED(hr) || s_logged.fetch_add(1) >= 16)
		return;

	D3D11_TEXTURE2D_DESC desc = {};
	ComPtr<ID3D11Texture2D> texture;
	if (pResource && SUCCEEDED(pResource->QueryInterface(IID_PPV_ARGS(&texture))))
		texture->GetDesc(&desc);
	RevLog("%s failed (hr 0x%08lx): view format %d dimension %d mip %d; texture format %d %ux%u mips %u array %u samples %u bind 0x%x misc 0x%x",
		call, (unsigned long)hr, (int)viewFormat, viewDimension, mipSlice,
		(int)desc.Format, desc.Width, desc.Height, desc.MipLevels, desc.ArraySize, desc.SampleDesc.Count,
		desc.BindFlags, desc.MiscFlags);
}

// The mip a 2D render target view targets, for the dimensions that have one.
static int RtvMipSlice(const D3D11_RENDER_TARGET_VIEW_DESC* desc)
{
	if (!desc)
		return -1;
	switch (desc->ViewDimension)
	{
	case D3D11_RTV_DIMENSION_TEXTURE2D:      return (int)desc->Texture2D.MipSlice;
	case D3D11_RTV_DIMENSION_TEXTURE2DARRAY: return (int)desc->Texture2DArray.MipSlice;
	default:                                 return -1;
	}
}

HRESULT WINAPI HookCreateRenderTargetView(
	ID3D11Device						*This,
	ID3D11Resource                      *pResource,
	const D3D11_RENDER_TARGET_VIEW_DESC *pDesc,
	ID3D11RenderTargetView              **ppRTView)
{
	D3D11_RENDER_TARGET_VIEW_DESC desc;
	UINT size = (UINT)sizeof(desc);
	if (pResource && SUCCEEDED(pResource->GetPrivateData(RXR_RTV_DESC, &size, &desc)))
	{
		if (pDesc)
		{
			DXGI_FORMAT format = desc.Format;
			desc = *pDesc;
			desc.Format = format;
		}
		const HRESULT hr = TrueCreateRenderTargetView(This, pResource, &desc, ppRTView);
		LogViewFailure("CreateRenderTargetView (format forced by Revive)", hr, pResource, desc.Format,
			(int)desc.ViewDimension, RtvMipSlice(&desc));
		return hr;
	}

	const HRESULT hr = TrueCreateRenderTargetView(This, pResource, pDesc, ppRTView);
	LogViewFailure("CreateRenderTargetView", hr, pResource, pDesc ? pDesc->Format : DXGI_FORMAT_UNKNOWN,
		pDesc ? (int)pDesc->ViewDimension : -1, RtvMipSlice(pDesc));
	return hr;
}

HRESULT WINAPI HookCreateShaderResourceView(
	ID3D11Device						  *This,
	ID3D11Resource                        *pResource,
	const D3D11_SHADER_RESOURCE_VIEW_DESC *pDesc,
	ID3D11ShaderResourceView              **ppSRView)
{
	D3D11_SHADER_RESOURCE_VIEW_DESC desc;
	UINT size = (UINT)sizeof(desc);
	if (pResource && SUCCEEDED(pResource->GetPrivateData(RXR_SRV_DESC, &size, &desc)))
	{
		if (pDesc)
		{
			DXGI_FORMAT format = desc.Format;
			desc = *pDesc;
			desc.Format = format;
		}
		const HRESULT hr = TrueCreateShaderResourceView(This, pResource, &desc, ppSRView);
		LogViewFailure("CreateShaderResourceView (format forced by Revive)", hr, pResource, desc.Format);
		return hr;
	}

	const HRESULT hr = TrueCreateShaderResourceView(This, pResource, pDesc, ppSRView);
	LogViewFailure("CreateShaderResourceView", hr, pResource, pDesc ? pDesc->Format : DXGI_FORMAT_UNKNOWN);
	return hr;
}

HRESULT WINAPI HookCreateRenderTargetView12(
	ID3D12Device* This,
	ID3D12Resource* pResource,
	const D3D12_RENDER_TARGET_VIEW_DESC* pDesc,
	D3D12_CPU_DESCRIPTOR_HANDLE DestDescriptor)
{
	D3D12_RENDER_TARGET_VIEW_DESC desc;
	UINT size = (UINT)sizeof(desc);
	if (!pDesc && pResource && SUCCEEDED(pResource->GetPrivateData(RXR_RTV_DESC, &size, &desc)))
		return TrueCreateRenderTargetView12(This, pResource, &desc, DestDescriptor);

	return TrueCreateRenderTargetView12(This, pResource, pDesc, DestDescriptor);
}

HRESULT WINAPI HookCreateShaderResourceView12(
	ID3D12Device* This,
	ID3D12Resource* pResource,
	const D3D12_SHADER_RESOURCE_VIEW_DESC* pDesc,
	D3D12_CPU_DESCRIPTOR_HANDLE DestDescriptor)
{
	D3D12_SHADER_RESOURCE_VIEW_DESC desc;
	UINT size = (UINT)sizeof(desc);
	if (!pDesc && pResource && SUCCEEDED(pResource->GetPrivateData(RXR_SRV_DESC, &size, &desc)))
		return TrueCreateShaderResourceView12(This, pResource, &desc, DestDescriptor);

	return TrueCreateShaderResourceView12(This, pResource, pDesc, DestDescriptor);
}

OVR_PUBLIC_FUNCTION(ovrResult) ovr_CreateTextureSwapChainDX(ovrSession session,
                                                            IUnknown* d3dPtr,
                                                            const ovrTextureSwapChainDesc* desc,
                                                            ovrTextureSwapChain* out_TextureSwapChain)
{
	REV_TRACE(ovr_CreateTextureSwapChainDX);

	if (!session)
		return ovrError_InvalidSession;

	if (!d3dPtr || !desc || !out_TextureSwapChain || desc->Type == ovrTexture_2D_External)
		return ovrError_InvalidParameter;

	ComPtr<ID3D11Device> pDevice = nullptr;
	ComPtr<ID3D12CommandQueue> pQueue = nullptr;
	if (FAILED(d3dPtr->QueryInterface(IID_PPV_ARGS(&pDevice))))
	{
		if (FAILED(d3dPtr->QueryInterface(IID_PPV_ARGS(&pQueue))))
			return ovrError_InvalidParameter;
	}

	if (!session->Session)
	{
		if (pDevice)
		{
			XR_FUNCTION(session->Instance, GetD3D11GraphicsRequirementsKHR);
			XrGraphicsRequirementsD3D11KHR graphicsReq = XR_TYPE(GRAPHICS_REQUIREMENTS_D3D11_KHR);
			CHK_XR(GetD3D11GraphicsRequirementsKHR(session->Instance, session->System, &graphicsReq));

			if (pDevice->GetFeatureLevel() < graphicsReq.minFeatureLevel)
				return ovrError_IncompatibleGPU;

			DetourTransactionBegin();
			DetourUpdateThread(GetCurrentThread());
			// Hook D3D11 functions so we can ensure NULL descriptors keep working on typeless formats
			// This fixes Echo Arena, among others.
			DetourVirtual(pDevice.Get(), 7, (PVOID*)&TrueCreateShaderResourceView, HookCreateShaderResourceView);
			session->HookedFunctions.insert(std::make_pair((PVOID*)&TrueCreateShaderResourceView, HookCreateShaderResourceView));
			DetourVirtual(pDevice.Get(), 9, (PVOID*)&TrueCreateRenderTargetView, HookCreateRenderTargetView);
			session->HookedFunctions.insert(std::make_pair((PVOID*)&TrueCreateRenderTargetView, HookCreateRenderTargetView));
			DetourTransactionCommit();

			XrGraphicsBindingD3D11KHR graphicsBinding = XR_TYPE(GRAPHICS_BINDING_D3D11_KHR);
			graphicsBinding.device = pDevice.Get();
			session->StartSession(&graphicsBinding);
		}
		else if (pQueue)
		{
			if (!Runtime::Get().Supports(XR_KHR_D3D12_ENABLE_EXTENSION_NAME))
				return ovrError_Unsupported;

			ComPtr<ID3D12Device> pDevice12 = nullptr;
			if (FAILED(pQueue->GetDevice(IID_PPV_ARGS(&pDevice12))))
				return ovrError_RuntimeException;

			XR_FUNCTION(session->Instance, GetD3D12GraphicsRequirementsKHR);
			XrGraphicsRequirementsD3D12KHR graphicsReq = XR_TYPE(GRAPHICS_REQUIREMENTS_D3D12_KHR);
			CHK_XR(GetD3D12GraphicsRequirementsKHR(session->Instance, session->System, &graphicsReq));

			static const D3D_FEATURE_LEVEL featureLevels[] = { graphicsReq.minFeatureLevel };
			D3D12_FEATURE_DATA_FEATURE_LEVELS levels = { 1, featureLevels };
			pDevice12->CheckFeatureSupport(D3D12_FEATURE_FEATURE_LEVELS, &levels, sizeof(levels));

			if (levels.MaxSupportedFeatureLevel < graphicsReq.minFeatureLevel)
				return ovrError_IncompatibleGPU;

			// Hook D3D12 functions so we can ensure NULL descriptors keep working on typeless formats
			// This fixes Echo Arena, among others.
			DetourTransactionBegin();
			DetourUpdateThread(GetCurrentThread());
			DetourVirtual(pDevice12.Get(), 18, (PVOID*)&TrueCreateShaderResourceView12, HookCreateShaderResourceView12);
			session->HookedFunctions.insert(std::make_pair((PVOID*)&TrueCreateShaderResourceView12, HookCreateShaderResourceView12));
			DetourVirtual(pDevice12.Get(), 20, (PVOID*)&TrueCreateRenderTargetView12, HookCreateRenderTargetView12);
			session->HookedFunctions.insert(std::make_pair((PVOID*)&TrueCreateRenderTargetView12, HookCreateRenderTargetView12));
			DetourTransactionCommit();

			XrGraphicsBindingD3D12KHR graphicsBinding = XR_TYPE(GRAPHICS_BINDING_D3D12_KHR);
			graphicsBinding.device = pDevice12.Get();
			graphicsBinding.queue = pQueue.Get();
			session->StartSession(&graphicsBinding);
		}
		else
		{
			return ovrError_RuntimeException;
		}
	}

	if (pDevice)
	{
		return ovrTextureSwapChainD3D11::Create(session, desc, out_TextureSwapChain);
	}
	else if (pQueue)
	{
		return ovrTextureSwapChainD3D12::Create(session, pQueue.Get(), desc, out_TextureSwapChain);
	}
	else
	{
		return ovrError_RuntimeException;
	}
}

OVR_PUBLIC_FUNCTION(ovrResult) ovr_GetTextureSwapChainBufferDX(ovrSession session,
                                                               ovrTextureSwapChain chain,
                                                               int index,
                                                               IID iid,
                                                               void** out_Buffer)
{
	REV_TRACE(ovr_GetTextureSwapChainBufferDX);

	if (!session)
		return ovrError_InvalidSession;

	if (!chain || !out_Buffer)
		return ovrError_InvalidParameter;

	if (index < 0)
		index = chain->CurrentIndex;

	if ((uint32_t)index >= chain->Length)
		return ovrError_InvalidParameter;

	// A swapchain may hand the application its own texture in place of the runtime image.
	IUnknown* texture = (IUnknown*)chain->GetApplicationImage(index);
	if (!texture)
		texture = ((XrSwapchainImageD3D11KHR*)chain->Images)[index].texture;

	HRESULT hr = texture->QueryInterface(iid, out_Buffer);
	if (FAILED(hr))
		return ovrError_InvalidParameter;

	return ovrSuccess;
}

OVR_PUBLIC_FUNCTION(ovrResult) ovr_CreateMirrorTextureDX(ovrSession session,
                                                         IUnknown* d3dPtr,
                                                         const ovrMirrorTextureDesc* desc,
                                                         ovrMirrorTexture* out_MirrorTexture)
{
	REV_TRACE(ovr_CreateMirrorTextureDX);

	if (!session)
		return ovrError_InvalidSession;

	if (!d3dPtr || !desc || !out_MirrorTexture)
		return ovrError_InvalidParameter;

	ovrMirrorTexture mirrorTexture = new ovrMirrorTextureData();
	mirrorTexture->Desc = *desc;
	*out_MirrorTexture = mirrorTexture;

	ovrTextureSwapChainDesc swapDesc;
	swapDesc.Type = ovrTexture_2D;
	swapDesc.Format = desc->Format;
	swapDesc.ArraySize = 1;
	swapDesc.Width = desc->Width;
	swapDesc.Height = desc->Height;
	swapDesc.MipLevels = 1;
	swapDesc.SampleCount = 1;
	swapDesc.StaticImage = ovrTrue;
	swapDesc.MiscFlags = desc->MiscFlags;
	swapDesc.BindFlags = 0;
	return ovr_CreateTextureSwapChainDX(session, d3dPtr, &swapDesc, &mirrorTexture->Dummy);
}

OVR_PUBLIC_FUNCTION(ovrResult) ovr_CreateMirrorTextureWithOptionsDX(ovrSession session,
                                                                    IUnknown* d3dPtr,
                                                                    const ovrMirrorTextureDesc* desc,
                                                                    ovrMirrorTexture* out_MirrorTexture)
{
	return ovr_CreateMirrorTextureDX(session, d3dPtr, desc, out_MirrorTexture);
}

OVR_PUBLIC_FUNCTION(ovrResult) ovr_GetMirrorTextureBufferDX(ovrSession session,
                                                            ovrMirrorTexture mirrorTexture,
                                                            IID iid,
                                                            void** out_Buffer)
{
	REV_TRACE(ovr_GetMirrorTextureBufferDX);

	if (!session)
		return ovrError_InvalidSession;

	if (!mirrorTexture || !out_Buffer)
		return ovrError_InvalidParameter;

	return ovr_GetTextureSwapChainBufferDX(session, mirrorTexture->Dummy, 0, iid, out_Buffer);
}
