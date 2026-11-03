#include "SwapchainD3D11.h"
#include "Common.h"
#include "Session.h"

#define XR_USE_GRAPHICS_API_D3D11
#include <d3d11.h>
#include <openxr/openxr_platform.h>

ovrTextureSwapChainD3D11::~ovrTextureSwapChainD3D11()
{
	if (m_Proxies)
	{
		for (uint32_t i = 0; i < Length; i++)
		{
			if (m_Proxies[i])
				m_Proxies[i]->Release();
		}
		delete[] m_Proxies;
	}

	if (m_Context)
		m_Context->Release();
}

void* ovrTextureSwapChainD3D11::GetApplicationImage(uint32_t index)
{
	return m_Proxies ? m_Proxies[index] : nullptr;
}

ovrResult ovrTextureSwapChainD3D11::Commit(ovrSession session)
{
	if (m_Proxies)
	{
		// Hand the runtime what the application rendered. Only the mips the runtime image has
		// are copied; the rest exist solely so the application can build its views.
		ID3D11Texture2D* target = ((XrSwapchainImageD3D11KHR*)Images)[CurrentIndex].texture;
		for (uint32_t slice = 0; slice < m_Slices; slice++)
		{
			for (uint32_t mip = 0; mip < m_RuntimeMips; mip++)
			{
				m_Context->CopySubresourceRegion(
					target, D3D11CalcSubresource(mip, slice, m_RuntimeMips), 0, 0, 0,
					m_Proxies[CurrentIndex], D3D11CalcSubresource(mip, slice, Desc.MipLevels), nullptr);
			}
		}
	}

	return ovrTextureSwapChainData::Commit(session);
}

// SteamVR allocates swapchain images with a single mip whatever mipCount asks for. An Oculus
// application that requested a mip chain creates a view per mip (Unreal does this for its
// layer textures), the views past mip 0 fail with E_INVALIDARG and the engine exits on the
// spot. Give the application textures with the chain it asked for instead.
ovrResult ovrTextureSwapChainD3D11::CreateProxies(const D3D11_TEXTURE2D_DESC& runtimeDesc)
{
	ID3D11Texture2D* runtimeTexture = ((XrSwapchainImageD3D11KHR*)Images)[0].texture;
	ID3D11Device* device = nullptr;
	runtimeTexture->GetDevice(&device);
	if (!device)
		return ovrError_RuntimeException;

	D3D11_TEXTURE2D_DESC proxyDesc = runtimeDesc;
	proxyDesc.MipLevels = Desc.MipLevels;
	// The runtime shares its images with the compositor; the proxies stay private to the game.
	proxyDesc.MiscFlags = runtimeDesc.MiscFlags & D3D11_RESOURCE_MISC_TEXTURECUBE;
	if (Desc.MiscFlags & ovrTextureMisc_AllowGenerateMips)
	{
		proxyDesc.MiscFlags |= D3D11_RESOURCE_MISC_GENERATE_MIPS;
		proxyDesc.BindFlags |= D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
	}

	m_Proxies = new ID3D11Texture2D*[Length]();
	m_RuntimeMips = runtimeDesc.MipLevels;
	m_Slices = runtimeDesc.ArraySize;

	HRESULT hr = S_OK;
	for (uint32_t i = 0; i < Length && SUCCEEDED(hr); i++)
	{
		hr = device->CreateTexture2D(&proxyDesc, nullptr, &m_Proxies[i]);
		if (FAILED(hr) && (proxyDesc.MiscFlags & D3D11_RESOURCE_MISC_GENERATE_MIPS))
		{
			// Not every format can generate mips; a chain the game fills itself is still usable.
			RevLog("D3D11 swapchain: proxy with mip generation failed (hr 0x%08lx), retrying without it", (unsigned long)hr);
			proxyDesc.MiscFlags &= ~D3D11_RESOURCE_MISC_GENERATE_MIPS;
			hr = device->CreateTexture2D(&proxyDesc, nullptr, &m_Proxies[i]);
		}
	}

	if (SUCCEEDED(hr))
		device->GetImmediateContext(&m_Context);
	device->Release();

	if (FAILED(hr))
	{
		RevLog("D3D11 swapchain: creating proxy textures failed (hr 0x%08lx)", (unsigned long)hr);
		return ovrError_RuntimeException;
	}

	RevLog("D3D11 swapchain: runtime images have %u mip(s) of the %d requested, rendering into proxy textures",
		runtimeDesc.MipLevels, Desc.MipLevels);
	return ovrSuccess;
}

ovrResult ovrTextureSwapChainD3D11::Create(ovrSession session, const ovrTextureSwapChainDesc* desc, ovrTextureSwapChain* out_TextureSwapChain)
{
	// Do some format compatibility conversions before creating the swapchain
	DXGI_FORMAT format = NegotiateFormat(session, TextureFormatToDXGIFormat(desc->Format));
	assert(session->SupportsFormat(format));

	ovrTextureSwapChainD3D11* chain = new ovrTextureSwapChainD3D11();
	CHK_OVR(chain->Init(session->Session, desc, format));
	CHK_OVR(chain->EnumerateImages<XrSwapchainImageD3D11KHR>(XR_TYPE_SWAPCHAIN_IMAGE_D3D11_KHR));

	D3D11_TEXTURE2D_DESC texDesc = {};
	if (chain->Length > 0)
		((XrSwapchainImageD3D11KHR*)chain->Images)[0].texture->GetDesc(&texDesc);

	// Record what the application asked for next to what the runtime actually allocated. The
	// application creates its own views on these textures, so a format it did not expect
	// (an sRGB substitute, or a typed texture where it asked for a typeless one) surfaces as
	// a failed view creation inside the game rather than as an error here.
	RevLog("D3D11 swapchain: ovr format %d, requested DXGI %d, negotiated %d, texture %d; %dx%d samples %d array %d mips %d (texture has %u) misc 0x%x bind 0x%x (texture bind 0x%x misc 0x%x) static %d, %u image(s)",
		(int)desc->Format, (int)TextureFormatToDXGIFormat(desc->Format), (int)format, (int)texDesc.Format,
		desc->Width, desc->Height, desc->SampleCount, desc->ArraySize, desc->MipLevels, texDesc.MipLevels,
		desc->MiscFlags, desc->BindFlags, texDesc.BindFlags, texDesc.MiscFlags, (int)desc->StaticImage, chain->Length);

	if (chain->Length > 0 && desc->MipLevels > 1 && texDesc.MipLevels < (UINT)desc->MipLevels)
		CHK_OVR(chain->CreateProxies(texDesc));

	// If the app doesn't expect a typeless texture we need to attach the fully qualified format to each texture.
	// Depth formats are always considered typeless in the Oculus SDK, so no need to hook those.
	if (!(desc->MiscFlags & ovrTextureMisc_DX_Typeless) && !IsDepthFormat(desc->Format))
	{
		// Create the SRVs and RTVs with the real format that the application expects
		CD3D11_SHADER_RESOURCE_VIEW_DESC srv(DescToViewDimension(&chain->Desc), format);
		CD3D11_RENDER_TARGET_VIEW_DESC rtv((D3D11_RTV_DIMENSION)DescToViewDimension(&chain->Desc), format);

		for (uint32_t i = 0; i < chain->Length; i++)
		{
			// The views are built on whatever texture the application is handed.
			ID3D11Texture2D* texture = chain->m_Proxies ? chain->m_Proxies[i]
				: ((XrSwapchainImageD3D11KHR*)chain->Images)[i].texture;

			HRESULT hr = texture->SetPrivateData(RXR_SRV_DESC, sizeof(D3D11_SHADER_RESOURCE_VIEW_DESC), &srv);
			if (SUCCEEDED(hr))
				hr = texture->SetPrivateData(RXR_RTV_DESC, sizeof(D3D11_RENDER_TARGET_VIEW_DESC), &rtv);
			if (FAILED(hr))
			{
				RevLog("D3D11 swapchain: SetPrivateData failed (hr 0x%08lx)", (unsigned long)hr);
				return ovrError_RuntimeException;
			}
		}
	}

	*out_TextureSwapChain = chain;
	return ovrSuccess;
}
