#include "OVR_CAPI.h"
#include "XR_Math.h"
#include "Common.h"
#include "Session.h"
#include "Runtime.h"
#include "InputManager.h"

#define XR_USE_GRAPHICS_API_D3D11
#include <d3d11.h>
#include <dxgi1_2.h>
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>
#include <wrl/client.h>
#include <thread>

using namespace std::chrono_literals;

// What the temporary session below found out, kept for the lifetime of the process.
//
// Unreal calls ovr_Create twice. Every temporary session is a complete scene application start
// and quit as far as SteamVR is concerned (it logs the game as having exited each time), so
// querying once per process avoids a second round of that churn right before the real session.
namespace
{
	struct ViewCache
	{
		bool Valid = false;
		XrSystemId System = XR_NULL_SYSTEM_ID;
		XrView Views[ovrEye_Count];
		XrExtent2Df Bounds;
	};

	ViewCache g_ViewCache;

	// The input actions are created once per instance and shared by every session. OpenXR
	// refuses binding suggestions once any session has attached action sets, so a fresh
	// InputManager for a later session would silently end up without bindings. Sharing also
	// avoids rebuilding identically named action sets each time Unreal calls ovr_Create.
	std::shared_ptr<InputManager> g_SharedInput;
	XrInstance g_SharedInputInstance = XR_NULL_HANDLE;
}

void ReleaseSharedInputManager()
{
	g_SharedInput.reset();
	g_SharedInputInstance = XR_NULL_HANDLE;
}

ovrResult ovrHmdStruct::InitSession(XrInstance instance)
{
	XR_FUNCTION(instance, GetD3D11GraphicsRequirementsKHR);

	memset(FrameStats, 0, sizeof(FrameStats));
	for (int i = 0; i < ovrMaxProvidedFrameStats; i++)
		FrameStats[i].type = XR_TYPE_FRAME_STATE;
	CurrentFrame = FrameStats;
	Instance = instance;
	TrackingOrigin = ovrTrackingOrigin_EyeLevel;
	SystemProperties = XR_TYPE(SYSTEM_PROPERTIES);
	SystemColorSpace = XR_TYPE(SYSTEM_COLOR_SPACE_PROPERTIES_FB);

	// Initialize view structures
	for (int i = 0; i < ovrEye_Count; i++)
	{
		ViewConfigs[i] = XR_TYPE(VIEW_CONFIGURATION_VIEW);
		ViewFov[i] = XR_TYPE(VIEW_CONFIGURATION_VIEW_FOV_EPIC);
		ViewPoses[i] = XR_TYPE(VIEW);
		ViewConfigs[i].next = &ViewFov[i];
	}

	XrSystemGetInfo systemInfo = XR_TYPE(SYSTEM_GET_INFO);
	systemInfo.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
	CHK_XR(xrGetSystem(Instance, &systemInfo, &System));
	if (Runtime::Get().ColorSpace)
		SystemProperties.next = &SystemColorSpace;
	CHK_XR(xrGetSystemProperties(Instance, System, &SystemProperties));

	uint32_t numViews;
	CHK_XR(xrEnumerateViewConfigurationViews(Instance, System, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, ovrEye_Count, &numViews, ViewConfigs));
	assert(numViews == ovrEye_Count);

	XrGraphicsRequirementsD3D11KHR graphicsReq = XR_TYPE(GRAPHICS_REQUIREMENTS_D3D11_KHR);
	CHK_XR(GetD3D11GraphicsRequirementsKHR(Instance, System, &graphicsReq));

	// Copy the LUID into the structure
	static_assert(sizeof(graphicsReq.adapterLuid) == sizeof(ovrGraphicsLuid),
		"The adapter LUID needs to fit in ovrGraphicsLuid");
	memcpy(&Adapter, &graphicsReq.adapterLuid, sizeof(ovrGraphicsLuid));

	// Create a temporary session to retrieve the headset field-of-view
	Microsoft::WRL::ComPtr<IDXGIFactory1> pFactory = NULL;
	if (Runtime::Get().MinorVersion >= 17 && Runtime::Get().Supports(XR_EPIC_VIEW_CONFIGURATION_FOV_EXTENSION_NAME) &&
		!Runtime::Get().UseHack(Runtime::HACK_FORCE_FOV_FALLBACK))
	{
		for (int i = 0; i < ovrEye_Count; i++)
		{
			ViewPoses[i].fov = ViewFov[i].recommendedFov;
			ViewPoses[i].pose = XR::Posef::Identity();
		}
	}
	else if (g_ViewCache.Valid && g_ViewCache.System == System)
	{
		for (int i = 0; i < ovrEye_Count; i++)
		{
			ViewPoses[i] = g_ViewCache.Views[i];
			ViewFov[i].recommendedFov = ViewPoses[i].fov;
			ViewFov[i].maxMutableFov = ViewPoses[i].fov;
		}
		bounds = g_ViewCache.Bounds;
		RevLog("reusing the field of view queried earlier, no temporary session needed");
	}
	else if (SUCCEEDED(CreateDXGIFactory1(__uuidof(IDXGIFactory1), (void**)&pFactory)))
	{
		Microsoft::WRL::ComPtr<IDXGIAdapter1> pAdapter;
		Microsoft::WRL::ComPtr<ID3D11Device> pDevice;

		for (UINT i = 0; pFactory->EnumAdapters1(i, &pAdapter) != DXGI_ERROR_NOT_FOUND; ++i)
		{
			DXGI_ADAPTER_DESC1 adapterDesc;
			if (SUCCEEDED(pAdapter->GetDesc1(&adapterDesc)) &&
				memcmp(&adapterDesc.AdapterLuid, &graphicsReq.adapterLuid, sizeof(graphicsReq.adapterLuid)) == 0)
			{
				break;
			}
		}

		HRESULT hr = D3D11CreateDevice(pAdapter.Get(),
			D3D_DRIVER_TYPE_UNKNOWN, 0, 0,
			NULL, 0, D3D11_SDK_VERSION,
			&pDevice, nullptr, nullptr);
		assert(SUCCEEDED(hr));

		XrGraphicsBindingD3D11KHR graphicsBinding = XR_TYPE(GRAPHICS_BINDING_D3D11_KHR);
		graphicsBinding.device = pDevice.Get();
		CHK_OVR(StartSession(&graphicsBinding));

		// OpenXR permits a single session per instance, so this temporary one has to be
		// destroyed on every path out of the block. The CHK_ macros below return early on
		// failure, which used to leak it: every later xrCreateSession then returned
		// XR_ERROR_LIMIT_REACHED, and an application that retries ovr_Create spins forever
		// instead of reporting an error. Turning a recoverable failure into a hang also
		// hides whatever failed first, so guard it rather than relying on the happy path.
		struct SessionGuard
		{
			ovrHmdStruct* owner;
			~SessionGuard() { if (owner->Session) owner->DestroySession(); }
		} guard{ this };

		if (Runtime::Get().UseHack(Runtime::HACK_WAIT_FOR_SESSION_READY))
		{
			// Synchronously wait for the fake session to become ready.
			XrEventDataBuffer event;
			const XrEventDataSessionStateChanged& stateChanged =
				reinterpret_cast<XrEventDataSessionStateChanged&>(event);
			// Bound the wait: a runtime that never reports READY would otherwise hang the
			// application here with no diagnostic at all.
			const auto deadline = std::chrono::steady_clock::now() + 5s;
			do
			{
				event = XR_TYPE(EVENT_DATA_BUFFER);
				XrResult result = xrPollEvent(Instance, &event);
				if (XR_FAILED(result))
					break;
				if (result == XR_EVENT_UNAVAILABLE)
					std::this_thread::sleep_for(10ms);
				if (std::chrono::steady_clock::now() > deadline)
				{
					RevLog("timed out waiting for the temporary session to become ready");
					break;
				}
			} while (event.type != XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED ||
				stateChanged.state != XR_SESSION_STATE_READY);
			assert(stateChanged.session == Session);

			XrSessionBeginInfo beginInfo = XR_TYPE(SESSION_BEGIN_INFO);
			beginInfo.primaryViewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
			CHK_XR(xrBeginSession(Session, &beginInfo));
		}

		CHK_OVR(LocateViews(ViewPoses));
		for (int i = 0; i < ovrEye_Count; i++)
		{
			ViewFov[i].recommendedFov = ViewPoses[i].fov;
			ViewFov[i].maxMutableFov = ViewPoses[i].fov;
		}

		CHK_XR(xrGetReferenceSpaceBoundsRect(Session, XR_REFERENCE_SPACE_TYPE_STAGE, &bounds));

		g_ViewCache.System = System;
		for (int i = 0; i < ovrEye_Count; i++)
			g_ViewCache.Views[i] = ViewPoses[i];
		g_ViewCache.Bounds = bounds;
		g_ViewCache.Valid = true;

		CHK_OVR(DestroySession());
	}

	// Calculate the pixels per tan angle
	for (int i = 0; i < ovrEye_Count; i++)
	{
		const XR::FovPort fov(ViewFov[i].recommendedFov);
		PixelsPerTan[i] = OVR::Vector2f(
			(float)ViewConfigs[i].recommendedImageRectWidth / (fov.LeftTan + fov.RightTan),
			(float)ViewConfigs[i].recommendedImageRectHeight / (fov.UpTan + fov.DownTan)
		);
	}

	// Initialize input manager
	if (!g_SharedInput || g_SharedInputInstance != Instance)
	{
		g_SharedInput = std::make_shared<InputManager>(Instance);
		g_SharedInputInstance = Instance;
	}
	else
	{
		RevLog("reusing the input actions created earlier");
	}
	Input = g_SharedInput;
	return ovrSuccess;
}

ovrResult ovrHmdStruct::StartSession(void* graphicsBinding)
{
	if (Session)
		return ovrError_InvalidOperation;

	XrSessionCreateInfo createInfo = XR_TYPE(SESSION_CREATE_INFO);
	createInfo.next = graphicsBinding;
	createInfo.systemId = System;
	CHK_XR(xrCreateSession(Instance, &createInfo, &Session));
	RevLog("session %llx created", XR_HANDLE_ID(Session));
	memset(&SessionStatus, 0, sizeof(SessionStatus));

	// Attach it to the InputManager
	if (Input)
		Input->AttachSession(Session);

	// Create reference spaces
	XrReferenceSpaceCreateInfo spaceInfo = XR_TYPE(REFERENCE_SPACE_CREATE_INFO);
	spaceInfo.poseInReferenceSpace = XR::Posef::Identity();
	spaceInfo.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_VIEW;
	CHK_XR(xrCreateReferenceSpace(Session, &spaceInfo, &ViewSpace));
	spaceInfo.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_LOCAL;
	CHK_XR(xrCreateReferenceSpace(Session, &spaceInfo, &OriginSpaces[ovrTrackingOrigin_EyeLevel]));
	CHK_XR(xrCreateReferenceSpace(Session, &spaceInfo, &TrackingSpaces[ovrTrackingOrigin_EyeLevel]));
	spaceInfo.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_STAGE;
	CHK_XR(xrCreateReferenceSpace(Session, &spaceInfo, &OriginSpaces[ovrTrackingOrigin_FloorLevel]));
	CHK_XR(xrCreateReferenceSpace(Session, &spaceInfo, &TrackingSpaces[ovrTrackingOrigin_FloorLevel]));

	// Update the visibility mask for both eyes
	if (Runtime::Get().VisibilityMask)
	{
		for (uint32_t i = 0; i < ovrEye_Count; i++)
		{
			UpdateStencil((ovrEyeType)i, XR_VISIBILITY_MASK_TYPE_HIDDEN_TRIANGLE_MESH_KHR);
			UpdateStencil((ovrEyeType)i, XR_VISIBILITY_MASK_TYPE_VISIBLE_TRIANGLE_MESH_KHR);
			UpdateStencil((ovrEyeType)i, XR_VISIBILITY_MASK_TYPE_LINE_LOOP_KHR);
		}
	}

	// Enumerate formats
	uint32_t formatCount = 0;
	CHK_XR(xrEnumerateSwapchainFormats(Session, 0, &formatCount, nullptr));
	SupportedFormats.resize(formatCount);
	CHK_XR(xrEnumerateSwapchainFormats(Session, (uint32_t)SupportedFormats.size(), &formatCount, SupportedFormats.data()));
	assert(formatCount == SupportedFormats.size());

	RevLog("runtime accepts %u swapchain format(s):", (unsigned)SupportedFormats.size());
	for (int64_t supported : SupportedFormats)
		RevLog("  format %lld", (long long)supported);

	Running.second.notify_all();

	return ovrSuccess;
}

ovrResult ovrHmdStruct::BeginSession()
{
	XrSessionBeginInfo beginInfo = XR_TYPE(SESSION_BEGIN_INFO);
	beginInfo.primaryViewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
	CHK_XR(xrBeginSession(Session, &beginInfo));

	// Start the first frame immediately in case the app uses SubmitFrame().
	long long currentIndex = (*CurrentFrame).frameIndex;
	CHK_OVR(ovr_WaitToBeginFrame(this, currentIndex));
	RecenterSpace(ovrTrackingOrigin_EyeLevel, ViewSpace);
	// The stage space faces wherever the runtime's play area says forward is, and on SteamVR
	// that has nothing to do with where the player stands at launch: on a Steam Frame it was
	// close to the opposite direction. Point the floor level origin the way the headset faces,
	// but leave it at the centre of the play area so the boundary still lines up.
	AlignSpaceYaw(ovrTrackingOrigin_FloorLevel, ViewSpace);
	CHK_OVR(ovr_BeginFrame(this, currentIndex));
	return ovrSuccess;
}

ovrResult ovrHmdStruct::EndSession()
{
	CHK_XR(xrEndSession(Session));
	return ovrSuccess;
}

ovrResult ovrHmdStruct::DestroySession()
{
	if (!Session)
		return ovrError_InvalidOperation;

	if (Input)
		Input->AttachSession(XR_NULL_HANDLE);

	RevLog("session %llx destroyed", XR_HANDLE_ID(Session));
	CHK_XR(xrDestroySession(Session));
	Session = XR_NULL_HANDLE;
	ViewSpace = XR_NULL_HANDLE;
	for (uint32_t i = 0; i < ovrTrackingOrigin_Count; i++)
	{
		OriginSpaces[i] = XR_NULL_HANDLE;
		TrackingSpaces[i] = XR_NULL_HANDLE;
	}
	return ovrSuccess;
}

ovrResult ovrHmdStruct::LocateViews(XrView out_Views[ovrEye_Count], XrViewStateFlags* out_Flags) const
{
	if (!Session)
		return ovrError_InvalidSession;

	uint32_t numViews;
	XrViewLocateInfo locateInfo = XR_TYPE(VIEW_LOCATE_INFO);
	XrViewState viewState = XR_TYPE(VIEW_STATE);
	locateInfo.space = ViewSpace;
	locateInfo.viewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
	locateInfo.displayTime = AbsTimeToXrTime(Instance, ovr_GetTimeInSeconds());
	CHK_XR(xrLocateViews(Session, &locateInfo, &viewState, ovrEye_Count, &numViews, out_Views));
	assert(numViews == ovrEye_Count);
	if (out_Flags)
		*out_Flags = viewState.viewStateFlags;
	return ovrSuccess;
}

ovrResult ovrHmdStruct::UpdateStencil(ovrEyeType view, XrVisibilityMaskTypeKHR type)
{
	if (!Session)
		return ovrError_InvalidSession;

	XR_FUNCTION(Instance, GetVisibilityMaskKHR);

	VisibilityMask& result = VisibilityMasks[view][type];
	XrVisibilityMaskKHR mask = XR_TYPE(VISIBILITY_MASK_KHR);
	CHK_XR(GetVisibilityMaskKHR(Session, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, view, type, &mask));
	if (!mask.vertexCountOutput || !mask.indexCountOutput)
		return ovrError_Unsupported;

	result.first.resize(mask.vertexCountOutput);
	result.second.resize(mask.indexCountOutput);

	mask.vertexCapacityInput = (uint32_t)result.first.size();
	mask.vertices = result.first.data();
	mask.indexCapacityInput = (uint32_t)result.second.size();
	mask.indices = result.second.data();
	CHK_XR(GetVisibilityMaskKHR(Session, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, view, type, &mask));

	if (type == XR_VISIBILITY_MASK_TYPE_LINE_LOOP_KHR && Runtime::Get().UseHack(Runtime::HACK_BROKEN_LINE_LOOP))
	{
		// There are actually only 27 valid vertices in this line loop
		result.first.resize(27);
		result.second.resize(27);
	}
	return ovrSuccess;
}

ovrResult ovrHmdStruct::RecenterSpace(ovrTrackingOrigin origin, XrSpace anchor, ovrPosef offset)
{
	std::lock_guard<std::shared_mutex> lk(TrackingMutex);
	XrSpaceLocation location = XR_TYPE(SPACE_LOCATION);
	CHK_XR(xrLocateSpace(anchor, OriginSpaces[origin], (*CurrentFrame).predictedDisplayTime, &location));

	if (!(location.locationFlags & (XR_SPACE_LOCATION_ORIENTATION_VALID_BIT | XR_SPACE_LOCATION_POSITION_VALID_BIT)))
		return ovrError_InvalidHeadsetOrientation;

	// Get the yaw orientation from the view pose
	float yaw;
	XR::Quatf(location.pose.orientation).GetYawPitchRoll(&yaw, nullptr, nullptr);

	// Construct the new origin pose
	XR::Posef newOrigin(OVR::Quatf(OVR::Axis_Y, yaw), XR::Vector3f(location.pose.position));

	// For floor level spaces we keep the height at the floor
	if (origin == ovrTrackingOrigin_FloorLevel)
		newOrigin.Translation.y = 0.0f;

	// Replace the tracking space with the newly calibrated one
	XrReferenceSpaceCreateInfo spaceInfo = XR_TYPE(REFERENCE_SPACE_CREATE_INFO);
	spaceInfo.referenceSpaceType = static_cast<XrReferenceSpaceType>(XR_REFERENCE_SPACE_TYPE_LOCAL + origin);
	spaceInfo.poseInReferenceSpace = XR::Posef(newOrigin * offset);
	CHK_XR(xrDestroySpace(TrackingSpaces[origin]));
	CHK_XR(xrCreateReferenceSpace(Session, &spaceInfo, &TrackingSpaces[origin]));
	return ovrSuccess;
}

ovrResult ovrHmdStruct::AlignSpaceYaw(ovrTrackingOrigin origin, XrSpace anchor)
{
	std::lock_guard<std::shared_mutex> lk(TrackingMutex);
	XrSpaceLocation location = XR_TYPE(SPACE_LOCATION);
	CHK_XR(xrLocateSpace(anchor, OriginSpaces[origin], (*CurrentFrame).predictedDisplayTime, &location));

	if (!(location.locationFlags & XR_SPACE_LOCATION_ORIENTATION_VALID_BIT))
	{
		RevLog("could not align the tracking origin: headset orientation not valid yet");
		return ovrError_InvalidHeadsetOrientation;
	}

	float yaw;
	XR::Quatf(location.pose.orientation).GetYawPitchRoll(&yaw, nullptr, nullptr);
	RevLog("aligned tracking origin %d with the headset (yaw %.0f degrees)", (int)origin, yaw * 180.0f / 3.14159265f);

	XrReferenceSpaceCreateInfo spaceInfo = XR_TYPE(REFERENCE_SPACE_CREATE_INFO);
	spaceInfo.referenceSpaceType = static_cast<XrReferenceSpaceType>(XR_REFERENCE_SPACE_TYPE_LOCAL + origin);
	const XR::Posef newOrigin(OVR::Quatf(OVR::Axis_Y, yaw), XR::Vector3f());
	spaceInfo.poseInReferenceSpace = newOrigin;
	CHK_XR(xrDestroySpace(TrackingSpaces[origin]));
	CHK_XR(xrCreateReferenceSpace(Session, &spaceInfo, &TrackingSpaces[origin]));
	return ovrSuccess;
}

bool ovrHmdStruct::SupportsFormat(int64_t format) const
{
	return std::find(SupportedFormats.begin(), SupportedFormats.end(), format) != SupportedFormats.end();
}
