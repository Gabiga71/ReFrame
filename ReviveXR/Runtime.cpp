#include "Runtime.h"
#include "Common.h"
#include "version.h"

#include <Windows.h>
#include <Shlwapi.h>
#include <algorithm>
#include <vector>
#include <memory>

#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>

// ReviveXR is written against OpenXR 1.0 and uses no 1.1 feature. Asking for
// XR_CURRENT_API_VERSION would instead request whatever the build headers happen to
// define, and against a 1.1 SDK that becomes 1.1 -- which SteamVR rejects outright with
// XR_ERROR_API_VERSION_UNSUPPORTED. No instance is created, so no session and no HMD,
// and the application concludes that no headset is connected. Request what we implement.
// Raising this to 1.1 later means auditing for the semantics 1.1 changes, not just
// bumping the number.
#ifndef XR_API_VERSION_1_0
#define XR_API_VERSION_1_0 XR_MAKE_VERSION(1, 0, XR_VERSION_PATCH(XR_CURRENT_API_VERSION))
#endif

const char* Runtime::s_required_extensions[] = {
	"XR_KHR_win32_convert_performance_counter_time",
	"XR_KHR_D3D11_enable"
};

const char* Runtime::s_optional_extensions[] = {
	"XR_KHR_D3D12_enable",
	"XR_KHR_vulkan_enable",
	"XR_KHR_opengl_enable",
	XR_KHR_VISIBILITY_MASK_EXTENSION_NAME,
	XR_KHR_COMPOSITION_LAYER_DEPTH_EXTENSION_NAME,
	XR_KHR_COMPOSITION_LAYER_CUBE_EXTENSION_NAME,
	XR_KHR_COMPOSITION_LAYER_CYLINDER_EXTENSION_NAME,
	XR_EPIC_VIEW_CONFIGURATION_FOV_EXTENSION_NAME,
	XR_OCULUS_AUDIO_DEVICE_GUID_EXTENSION_NAME,
	XR_FB_COLOR_SPACE_EXTENSION_NAME
};

Runtime::HackInfo Runtime::s_known_hacks[] = {
	{ nullptr, "SteamVR/OpenXR", HACK_VALVE_INDEX_PROFILE, 0, 0, true },
	{ nullptr, "SteamVR/OpenXR", HACK_BROKEN_LINE_LOOP, 0, 0x100000000, true },
	{ nullptr, "SteamVR/OpenXR", HACK_MIN_HAPTIC_DURATION, 0, 0, true },
	{ nullptr, "Windows Mixed Reality Runtime", HACK_WAIT_FOR_SESSION_READY, 0, 0, true },
	// SteamVR 2.x also refuses to locate views outside a running session, which is what
	// the specification actually requires. Without this the temporary field-of-view query
	// below fails with XR_ERROR_VALIDATION_FAILURE and no session is ever established.
	// The hack name is a misnomer by now: waiting for the session is correct behaviour on
	// every conformant runtime, so making it unconditional would be the better fix.
	{ nullptr, "SteamVR/OpenXR", HACK_WAIT_FOR_SESSION_READY, 0, 0, true },
	{ "echovr.exe", nullptr, HACK_FORCE_FOV_FALLBACK, 0, 0, true },
	{ "loneecho.exe", nullptr, HACK_FORCE_FOV_FALLBACK, 0, 0, true },
};

Runtime& Runtime::Get()
{
	static Runtime instance;
	return instance;
}

ovrResult Runtime::CreateInstance(XrInstance* out_Instance, const ovrInitParams* params)
{
	MinorVersion = params && params->Flags & ovrInit_RequestVersion ?
		params->RequestedMinorVersion : OVR_MINOR_VERSION;

	uint32_t size;
	std::vector<XrExtensionProperties> properties;
	CHK_XR(xrEnumerateInstanceExtensionProperties(nullptr, 0, &size, nullptr));
	properties.resize(size);
	for (XrExtensionProperties& props : properties)
		props = XR_TYPE(EXTENSION_PROPERTIES);
	CHK_XR(xrEnumerateInstanceExtensionProperties(nullptr, (uint32_t)properties.size(), &size, properties.data()));

	m_extensions.clear();

	for (const char* extension : s_required_extensions)
		m_extensions.push_back(extension);

	for (const char* extension : s_optional_extensions)
	{
		auto findExtension = [extension](XrExtensionProperties props)
		{
			return strcmp(props.extensionName, extension) == 0;
		};

		if (std::any_of(properties.begin(), properties.end(), findExtension))
			m_extensions.push_back(extension);
	}

	VisibilityMask = Supports(XR_KHR_VISIBILITY_MASK_EXTENSION_NAME);
	CompositionDepth = Supports(XR_KHR_COMPOSITION_LAYER_DEPTH_EXTENSION_NAME);
	CompositionCube = Supports(XR_KHR_COMPOSITION_LAYER_CUBE_EXTENSION_NAME);
	CompositionCylinder = Supports(XR_KHR_COMPOSITION_LAYER_CYLINDER_EXTENSION_NAME);
	AudioDevice = Supports(XR_OCULUS_AUDIO_DEVICE_GUID_EXTENSION_NAME);
	ColorSpace = Supports(XR_FB_COLOR_SPACE_EXTENSION_NAME);

	XrInstanceCreateInfo createInfo = XR_TYPE(INSTANCE_CREATE_INFO);
	createInfo.applicationInfo = { "Revive", REV_VERSION_INT, "Revive", REV_VERSION_INT, XR_API_VERSION_1_0 };
	createInfo.enabledExtensionCount = (uint32_t)m_extensions.size();
	createInfo.enabledExtensionNames = m_extensions.data();
	RevLog("xrCreateInstance: requesting OpenXR %u.%u.%u with %u extension(s)",
		(unsigned)XR_VERSION_MAJOR(XR_API_VERSION_1_0),
		(unsigned)XR_VERSION_MINOR(XR_API_VERSION_1_0),
		(unsigned)XR_VERSION_PATCH(XR_API_VERSION_1_0),
		(unsigned)m_extensions.size());
	for (const char* extension : m_extensions)
		RevLog("  enabled extension: %s", extension);

	CHK_XR(xrCreateInstance(&createInfo, out_Instance));

	char filepath[MAX_PATH];
	GetModuleFileNameA(NULL, filepath, MAX_PATH);
	char* filename = PathFindFileNameA(filepath);

	XrInstanceProperties props = XR_TYPE(INSTANCE_PROPERTIES);
	CHK_XR(xrGetInstanceProperties(*out_Instance, &props));

	// The hack table below matches the runtime by exact name, so record what it
	// actually reports: a rename silently disables every runtime specific hack.
	RevLog("runtime: %s (version %u.%u.%u)", props.runtimeName,
		(unsigned)XR_VERSION_MAJOR(props.runtimeVersion),
		(unsigned)XR_VERSION_MINOR(props.runtimeVersion),
		(unsigned)XR_VERSION_PATCH(props.runtimeVersion));

	for (auto& hack : s_known_hacks)
	{
		if ((!hack.m_filename || _stricmp(filename, hack.m_filename) == 0) &&
			(!hack.m_runtime || strcmp(props.runtimeName, hack.m_runtime) == 0) &&
			(!hack.m_versionend || hack.m_versionstart <= props.runtimeVersion &&
				props.runtimeVersion < hack.m_versionend))
		{
			if (hack.m_usehack)
				m_hacks.emplace(hack.m_hack, hack);
		}
		else if (!hack.m_usehack)
		{
			m_hacks.emplace(hack.m_hack, hack);
		}
	}
	return ovrSuccess;
}

bool Runtime::UseHack(Hack hack)
{
	return m_hacks.find(hack) != m_hacks.end();
}

bool Runtime::Supports(const char* extensionName)
{
	auto findExtension = [extensionName](const char* extension)
	{
		return strcmp(extension, extensionName) == 0;
	};

	return std::any_of(m_extensions.begin(), m_extensions.end(), findExtension);
}
