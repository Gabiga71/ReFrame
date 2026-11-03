#include "Swapchain.h"
#include "Common.h"
#include "Session.h"

#include <openxr/openxr.h>
#include <dxgiformat.h>
#include <d3dcommon.h>

ovrResult ovrTextureSwapChainData::Commit(ovrSession session)
{
	XrSwapchainImageReleaseInfo releaseInfo = XR_TYPE(SWAPCHAIN_IMAGE_RELEASE_INFO);
	CHK_XR(xrReleaseSwapchainImage(Swapchain, &releaseInfo));

	if (!Desc.StaticImage)
	{
		XrSwapchainImageAcquireInfo acquireInfo = XR_TYPE(SWAPCHAIN_IMAGE_ACQUIRE_INFO);
		CHK_XR(xrAcquireSwapchainImage(Swapchain, &acquireInfo, &CurrentIndex));

		// TODO: We could move this wait to the end of EndFrame so we submit earlier
		XrSwapchainImageWaitInfo waitInfo = XR_TYPE(SWAPCHAIN_IMAGE_WAIT_INFO);
		waitInfo.timeout = (*session->CurrentFrame).predictedDisplayPeriod;
		CHK_XR(xrWaitSwapchainImage(Swapchain, &waitInfo));
	}
	return ovrSuccess;
}

ovrResult ovrTextureSwapChainData::Init(XrSession session, const ovrTextureSwapChainDesc* desc, int64_t format)
{
	Desc = *desc;

	XrSwapchainCreateInfo createInfo = XR_TYPE(SWAPCHAIN_CREATE_INFO);
	if (desc->MiscFlags & ovrTextureMisc_ProtectedContent)
		createInfo.createFlags |= XR_SWAPCHAIN_CREATE_PROTECTED_CONTENT_BIT;

	if (desc->StaticImage)
		createInfo.createFlags |= XR_SWAPCHAIN_CREATE_STATIC_IMAGE_BIT;

	// All Oculus swapchains allow sampling
	createInfo.usageFlags |= XR_SWAPCHAIN_USAGE_SAMPLED_BIT;

	// All Oculus swapchains allow transfers
	createInfo.usageFlags |= XR_SWAPCHAIN_USAGE_TRANSFER_SRC_BIT | XR_SWAPCHAIN_USAGE_TRANSFER_DST_BIT;

	if (desc->BindFlags & ovrTextureBind_DX_RenderTarget)
		createInfo.usageFlags |= XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT;

	if (desc->BindFlags & ovrTextureBind_DX_UnorderedAccess)
		createInfo.usageFlags |= XR_SWAPCHAIN_USAGE_UNORDERED_ACCESS_BIT;

	if (desc->BindFlags & ovrTextureBind_DX_DepthStencil)
		createInfo.usageFlags |= XR_SWAPCHAIN_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;

	if (desc->MiscFlags & ovrTextureMisc_DX_Typeless)
		createInfo.usageFlags |= XR_SWAPCHAIN_USAGE_MUTABLE_FORMAT_BIT;

	createInfo.format = format;
	createInfo.sampleCount = desc->SampleCount;
	createInfo.width = desc->Width;
	createInfo.height = desc->Height;
	createInfo.faceCount = desc->Type == ovrTexture_Cube ? desc->ArraySize : 1;
	createInfo.arraySize = desc->Type == ovrTexture_Cube ? 1 : desc->ArraySize;
	createInfo.mipCount = desc->MipLevels;
	CHK_XR(xrCreateSwapchain(session, &createInfo, &Swapchain));

	XrSwapchainImageAcquireInfo acqInfo = XR_TYPE(SWAPCHAIN_IMAGE_ACQUIRE_INFO);
	CHK_XR(xrAcquireSwapchainImage(Swapchain, &acqInfo, &CurrentIndex));

	XrSwapchainImageWaitInfo waitInfo = XR_TYPE(SWAPCHAIN_IMAGE_WAIT_INFO);
	waitInfo.timeout = XR_NO_DURATION;
	CHK_XR(xrWaitSwapchainImage(Swapchain, &waitInfo));
	return ovrSuccess;
}

bool ovrTextureSwapChainData::IsDepthFormat(ovrTextureFormat format)
{
	switch (format)
	{
	case OVR_FORMAT_D16_UNORM:            return true;
	case OVR_FORMAT_D24_UNORM_S8_UINT:    return true;
	case OVR_FORMAT_D32_FLOAT:            return true;
	case OVR_FORMAT_D32_FLOAT_S8X24_UINT: return true;
	default: return false;
	}
}

DXGI_FORMAT ovrTextureSwapChainData::TextureFormatToDXGIFormat(ovrTextureFormat format)
{
	// OpenXR always uses typeless formats
	switch (format)
	{
	case OVR_FORMAT_UNKNOWN:              return DXGI_FORMAT_UNKNOWN;
	case OVR_FORMAT_B5G6R5_UNORM:         return DXGI_FORMAT_B5G6R5_UNORM;
	case OVR_FORMAT_B5G5R5A1_UNORM:       return DXGI_FORMAT_B5G5R5A1_UNORM;
	case OVR_FORMAT_B4G4R4A4_UNORM:       return DXGI_FORMAT_B4G4R4A4_UNORM;
	case OVR_FORMAT_R8G8B8A8_UNORM:       return DXGI_FORMAT_R8G8B8A8_UNORM;
	case OVR_FORMAT_R8G8B8A8_UNORM_SRGB:  return DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
	case OVR_FORMAT_B8G8R8A8_UNORM:       return DXGI_FORMAT_B8G8R8A8_UNORM;
	case OVR_FORMAT_B8G8R8A8_UNORM_SRGB:  return DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;
	case OVR_FORMAT_B8G8R8X8_UNORM:       return DXGI_FORMAT_B8G8R8X8_UNORM;
	case OVR_FORMAT_B8G8R8X8_UNORM_SRGB:  return DXGI_FORMAT_B8G8R8X8_UNORM_SRGB;
	case OVR_FORMAT_R16G16B16A16_FLOAT:   return DXGI_FORMAT_R16G16B16A16_FLOAT;
	case OVR_FORMAT_R11G11B10_FLOAT:      return DXGI_FORMAT_R11G11B10_FLOAT;

		// Depth formats
	case OVR_FORMAT_D16_UNORM:            return DXGI_FORMAT_D16_UNORM;
	case OVR_FORMAT_D24_UNORM_S8_UINT:    return DXGI_FORMAT_D24_UNORM_S8_UINT;
	case OVR_FORMAT_D32_FLOAT:            return DXGI_FORMAT_D32_FLOAT;
	case OVR_FORMAT_D32_FLOAT_S8X24_UINT: return DXGI_FORMAT_D32_FLOAT_S8X24_UINT;

		// Added in 1.5 compressed formats can be used for static layers
	case OVR_FORMAT_BC1_UNORM:            return DXGI_FORMAT_BC1_UNORM;
	case OVR_FORMAT_BC1_UNORM_SRGB:       return DXGI_FORMAT_BC1_UNORM;
	case OVR_FORMAT_BC2_UNORM:            return DXGI_FORMAT_BC2_UNORM;
	case OVR_FORMAT_BC2_UNORM_SRGB:       return DXGI_FORMAT_BC2_UNORM;
	case OVR_FORMAT_BC3_UNORM:            return DXGI_FORMAT_BC3_UNORM;
	case OVR_FORMAT_BC3_UNORM_SRGB:       return DXGI_FORMAT_BC3_UNORM;
	case OVR_FORMAT_BC6H_UF16:            return DXGI_FORMAT_BC6H_UF16;
	case OVR_FORMAT_BC6H_SF16:            return DXGI_FORMAT_BC6H_SF16;
	case OVR_FORMAT_BC7_UNORM:            return DXGI_FORMAT_BC7_UNORM;
	case OVR_FORMAT_BC7_UNORM_SRGB:       return DXGI_FORMAT_BC7_UNORM;

	default: return DXGI_FORMAT_UNKNOWN;
	}
}

D3D_SRV_DIMENSION ovrTextureSwapChainData::DescToViewDimension(const ovrTextureSwapChainDesc* desc)
{
	if (desc->ArraySize > 1)
	{
		if (desc->SampleCount > 1)
			return D3D_SRV_DIMENSION_TEXTURE2DMSARRAY;
		else
			return D3D_SRV_DIMENSION_TEXTURE2DARRAY;
	}
	else
	{
		if (desc->SampleCount > 1)
			return D3D_SRV_DIMENSION_TEXTURE2DMS;
		else
			return D3D_SRV_DIMENSION_TEXTURE2D;
	}
}

DXGI_FORMAT ovrTextureSwapChainData::NegotiateFormat(ovrSession session, DXGI_FORMAT format)
{
	if (session->SupportsFormat(format))
		return format;

	// Candidates in preference order, tried until one is supported. A substitution that is
	// itself unsupported must not end the search: SteamVR accepts neither B8G8R8X8_UNORM nor
	// B8G8R8A8_UNORM, only their sRGB variants, so any single step mapping dead-ends there.
	//
	// Substituting an sRGB format for a linear one is not free. The runtime then decodes the
	// texture when it samples it, and Revive builds the application facing views from the
	// negotiated format, so content that was already gamma encoded is encoded a second time
	// and reads back washed out. Keeping the gamma exact needs a mutable format swapchain
	// with views in the originally requested format; that is the next step if colours look
	// wrong, but a visible image beats no image at all.
	static const struct
	{
		DXGI_FORMAT requested;
		DXGI_FORMAT candidates[3];
	} fallbacks[] = {
		{ DXGI_FORMAT_R11G11B10_FLOAT,     { DXGI_FORMAT_R16G16B16A16_FLOAT, DXGI_FORMAT_R10G10B10A2_UNORM,   DXGI_FORMAT_R8G8B8A8_UNORM_SRGB } },
		{ DXGI_FORMAT_R16G16B16A16_FLOAT,  { DXGI_FORMAT_R10G10B10A2_UNORM,  DXGI_FORMAT_R8G8B8A8_UNORM_SRGB, DXGI_FORMAT_UNKNOWN } },
		{ DXGI_FORMAT_R8G8B8A8_UNORM,      { DXGI_FORMAT_R8G8B8A8_UNORM_SRGB, DXGI_FORMAT_B8G8R8A8_UNORM_SRGB, DXGI_FORMAT_UNKNOWN } },
		{ DXGI_FORMAT_B8G8R8A8_UNORM,      { DXGI_FORMAT_B8G8R8A8_UNORM_SRGB, DXGI_FORMAT_R8G8B8A8_UNORM_SRGB, DXGI_FORMAT_UNKNOWN } },
		{ DXGI_FORMAT_B8G8R8X8_UNORM,      { DXGI_FORMAT_B8G8R8A8_UNORM,      DXGI_FORMAT_B8G8R8A8_UNORM_SRGB, DXGI_FORMAT_UNKNOWN } },
		{ DXGI_FORMAT_B8G8R8X8_UNORM_SRGB, { DXGI_FORMAT_B8G8R8A8_UNORM_SRGB, DXGI_FORMAT_UNKNOWN,             DXGI_FORMAT_UNKNOWN } },
	};

	for (const auto& fallback : fallbacks)
	{
		if (fallback.requested != format)
			continue;

		for (DXGI_FORMAT candidate : fallback.candidates)
		{
			if (candidate != DXGI_FORMAT_UNKNOWN && session->SupportsFormat(candidate))
			{
				RevLog("substituting DXGI format %d for unsupported %d", (int)candidate, (int)format);
				return candidate;
			}
		}
	}

	// Nothing could satisfy the request. Say so rather than silently handing the runtime a
	// format it already rejected: xrCreateSwapchain then fails with
	// XR_ERROR_SWAPCHAIN_FORMAT_UNSUPPORTED and the application has no way to tell why.
	RevLog("no supported swapchain format for DXGI format %d, requesting it anyway", (int)format);
	return format;
}
