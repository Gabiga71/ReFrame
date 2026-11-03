#pragma once

#include "Swapchain.h"

struct ovrTextureSwapChainD3D11 : public ovrTextureSwapChainData
{
public:
	~ovrTextureSwapChainD3D11();

	virtual ovrResult Commit(ovrSession session) override;
	virtual void* GetApplicationImage(uint32_t index) override;

	static ovrResult Create(ovrSession session, const ovrTextureSwapChainDesc* desc, ovrTextureSwapChain* out_TextureSwapChain);

private:
	ovrResult CreateProxies(const struct D3D11_TEXTURE2D_DESC& runtimeDesc);

	// Textures the application renders into when the runtime's images cannot serve it directly,
	// one per runtime image. Mip 0 of each is copied into the matching runtime image on commit.
	struct ID3D11Texture2D** m_Proxies = nullptr;
	struct ID3D11DeviceContext* m_Context = nullptr;
	uint32_t m_RuntimeMips = 1;
	uint32_t m_Slices = 1;
};
