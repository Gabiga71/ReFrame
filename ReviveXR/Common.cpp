#include "Common.h"

#include <Windows.h>
#include <ShlObj.h>
#include <mutex>
#include <stdarg.h>
#include <stdio.h>
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>

extern XrInstance g_Instance;

XrResult g_LastResult = XR_SUCCESS;

static FILE* GetLogFile()
{
	static FILE* s_file = nullptr;
	static bool s_opened = false;

	if (!s_opened)
	{
		s_opened = true;

		wchar_t path[MAX_PATH] = {};
		if (SUCCEEDED(SHGetFolderPathW(NULL, CSIDL_LOCAL_APPDATA, NULL, 0, path)))
		{
			// Forward slashes are accepted by the Win32 file APIs and keep this readable.
			wcscat_s(path, MAX_PATH, L"/Revive");
			CreateDirectoryW(path, NULL);
			wcscat_s(path, MAX_PATH, L"/ReviveXR.txt");
			// Append rather than truncate, and tag every line with the process id: a game may
			// spawn child processes that each load this module, and with truncating writes they
			// overwrite each other at their own file offsets, shredding the log into unreadable
			// fragments. The file therefore accumulates across runs; delete it before a session
			// when a clean capture is wanted.
			s_file = _wfopen(path, L"a");
		}
	}

	return s_file;
}

void RevLog(const char* format, ...)
{
	FILE* file = GetLogFile();
	if (!file)
		return;

	char line[1024];
	// Wall clock time, so entries can be lined up with the injector and overlay logs.
	SYSTEMTIME now;
	GetLocalTime(&now);
	const int prefix = snprintf(line, sizeof(line), "[%02u:%02u:%02u.%03u %5lu] ",
		now.wHour, now.wMinute, now.wSecond, now.wMilliseconds, GetCurrentProcessId());
	if (prefix < 0 || prefix >= (int)sizeof(line))
		return;

	va_list args;
	va_start(args, format);
	const int written = vsnprintf(line + prefix, sizeof(line) - prefix - 2, format, args);
	va_end(args);
	if (written < 0)
		return;

	// Assemble the whole line first and emit it in a single write, so concurrent callers
	// cannot interleave fragments of each other inside one line.
	size_t length = prefix + (size_t)written;
	if (length > sizeof(line) - 2)
		length = sizeof(line) - 2;
	line[length++] = 10;

	static std::mutex s_mutex;
	std::lock_guard<std::mutex> lock(s_mutex);
	fwrite(line, 1, length, file);
	fflush(file);
}

void RevLogResult(const char* expression, XrResult result, const char* file, int line)
{
	// Split across calls so the format strings stay free of embedded newlines;
	// RevLog already terminates each line.
	RevLog("FAILED: %s", expression);
	RevLog("  result: %ls", ResultToString(result));
	RevLog("  at: %s:%d", file, line);
}

ovrResult ResultToOvrResult(XrResult error)
{
	switch (error)
	{
	case XR_SUCCESS: return ovrSuccess;
	case XR_TIMEOUT_EXPIRED: return ovrError_Timeout;
	case XR_SESSION_LOSS_PENDING: return ovrSuccess;
	case XR_EVENT_UNAVAILABLE: return ovrSuccess;
	case XR_SPACE_BOUNDS_UNAVAILABLE: return ovrSuccess_BoundaryInvalid;
	case XR_SESSION_NOT_FOCUSED: return ovrSuccess_NotVisible;
	case XR_FRAME_DISCARDED: return ovrSuccess_NotVisible;
	case XR_ERROR_VALIDATION_FAILURE: return ovrError_InvalidOperation;
	case XR_ERROR_RUNTIME_FAILURE: return ovrError_DisplayLost;
	case XR_ERROR_OUT_OF_MEMORY: return ovrError_MemoryAllocationFailure;
	case XR_ERROR_API_VERSION_UNSUPPORTED: return ovrError_ServiceVersion;
	case XR_ERROR_INITIALIZATION_FAILED: return ovrError_Initialize;
	case XR_ERROR_FUNCTION_UNSUPPORTED: return ovrError_Unsupported;
	case XR_ERROR_FEATURE_UNSUPPORTED: return ovrError_Unsupported;
	case XR_ERROR_EXTENSION_NOT_PRESENT: return ovrError_Unsupported;
	case XR_ERROR_LIMIT_REACHED: return ovrError_DisplayLimitReached;
	case XR_ERROR_SIZE_INSUFFICIENT: return ovrError_InsufficientArraySize;
	case XR_ERROR_HANDLE_INVALID: return ovrError_InvalidOperation;
	case XR_ERROR_INSTANCE_LOST: return ovrError_DisplayRemoved;
	case XR_ERROR_SESSION_RUNNING: return ovrError_InvalidOperation;
	case XR_ERROR_SESSION_NOT_RUNNING: return ovrError_InvalidOperation;
	case XR_ERROR_SESSION_LOST: return ovrError_ServiceConnection;
	case XR_ERROR_SYSTEM_INVALID: return ovrError_InvalidParameter;
	case XR_ERROR_PATH_INVALID: return ovrError_InvalidParameter;
	case XR_ERROR_PATH_COUNT_EXCEEDED: return ovrError_InvalidOperation;
	case XR_ERROR_PATH_FORMAT_INVALID: return ovrError_InvalidParameter;
	case XR_ERROR_PATH_UNSUPPORTED: return ovrError_InvalidParameter;
	case XR_ERROR_LAYER_INVALID: return ovrError_TextureSwapChainInvalid;
	case XR_ERROR_LAYER_LIMIT_EXCEEDED: return ovrError_InvalidOperation;
	case XR_ERROR_SWAPCHAIN_RECT_INVALID: return ovrError_InvalidParameter;
	case XR_ERROR_SWAPCHAIN_FORMAT_UNSUPPORTED: return ovrError_InvalidParameter;
	case XR_ERROR_ACTION_TYPE_MISMATCH: return ovrError_InvalidParameter;
	case XR_ERROR_SESSION_NOT_READY: return ovrError_RuntimeException;
	case XR_ERROR_SESSION_NOT_STOPPING: return ovrError_RuntimeException;
	case XR_ERROR_TIME_INVALID: return ovrError_InvalidParameter;
	case XR_ERROR_REFERENCE_SPACE_UNSUPPORTED: return ovrError_InvalidParameter;
	case XR_ERROR_FILE_ACCESS_ERROR: return ovrError_RuntimeException;
	case XR_ERROR_FILE_CONTENTS_INVALID: return ovrError_RuntimeException;
	case XR_ERROR_FORM_FACTOR_UNSUPPORTED: return ovrError_NoHmd;
	case XR_ERROR_FORM_FACTOR_UNAVAILABLE: return ovrError_DeviceUnavailable;
	case XR_ERROR_API_LAYER_NOT_PRESENT: return ovrError_RuntimeException;
	case XR_ERROR_CALL_ORDER_INVALID: return ovrError_TextureSwapChainFull;
	case XR_ERROR_GRAPHICS_DEVICE_INVALID: return ovrError_MismatchedAdapters;
	case XR_ERROR_POSE_INVALID: return ovrError_InvalidHeadsetOrientation;
	case XR_ERROR_INDEX_OUT_OF_RANGE: return ovrError_InvalidParameter;
	case XR_ERROR_VIEW_CONFIGURATION_TYPE_UNSUPPORTED: return ovrError_InvalidParameter;
	case XR_ERROR_ENVIRONMENT_BLEND_MODE_UNSUPPORTED: return ovrError_InvalidParameter;
	case XR_ERROR_NAME_DUPLICATED: return ovrError_InvalidParameter;
	case XR_ERROR_NAME_INVALID: return ovrError_InvalidParameter;
	case XR_ERROR_ACTIONSET_NOT_ATTACHED: return ovrError_InvalidParameter;
	case XR_ERROR_ACTIONSETS_ALREADY_ATTACHED: return ovrError_InvalidParameter;
	case XR_ERROR_LOCALIZED_NAME_DUPLICATED: return ovrError_InvalidParameter;
	case XR_ERROR_LOCALIZED_NAME_INVALID: return ovrError_InvalidParameter;
	case XR_ERROR_GRAPHICS_REQUIREMENTS_CALL_MISSING: return ovrError_NotInitialized;
	case XR_ERROR_COLOR_SPACE_UNSUPPORTED_FB: return ovrError_Unsupported;
	default: return ovrError_RuntimeException;
	}
}

XrTime AbsTimeToXrTime(XrInstance instance, double absTime)
{
	XR_FUNCTION(instance, ConvertWin32PerformanceCounterToTimeKHR);

	// Get back the XrTime
	static double PerfFrequency = 0.0;
	if (PerfFrequency == 0.0)
	{
		LARGE_INTEGER freq;
		QueryPerformanceFrequency(&freq);
		PerfFrequency = (double)freq.QuadPart;
	}

	XrResult rs;
	XrTime time;
	LARGE_INTEGER li;
	li.QuadPart = (LONGLONG)(absTime * PerfFrequency);
	rs = ConvertWin32PerformanceCounterToTimeKHR(instance, &li, &time);
	assert(XR_SUCCEEDED(rs));
	return time;
}

XrPath GetXrPath(const char* path)
{
	XrPath outPath;
	XrResult rs = xrStringToPath(g_Instance, path, &outPath);
	assert(XR_SUCCEEDED(rs));
	if (XR_FAILED(rs))
		return XR_NULL_PATH;
	return outPath;
}

XrPath GetXrPath(std::string path)
{
	return GetXrPath(path.c_str());
}
