#include <Windows.h>
#include <stdio.h>
#include <dxgi.h>
#include <d3d11.h>
#include <Shlwapi.h>
#include <string>
#include <atomic>
#include <intrin.h>
#include <detours/detours.h>

#include "OVR_CAPI.h"
#include "OVR_Version.h"

static HMODULE(WINAPI* TrueLoadLibraryW)(LPCWSTR lpFileName) = LoadLibraryW;
static HMODULE(WINAPI* TrueLoadLibraryExW)(LPCWSTR lpLibFileName, HANDLE hFile, DWORD dwFlags) = LoadLibraryExW;
static HANDLE(WINAPI* TrueOpenEvent)(DWORD dwDesiredAccess, BOOL bInheritHandle, LPCWSTR lpName) = OpenEventW;
static VOID(WINAPI* TrueExitProcess)(UINT uExitCode) = ExitProcess;
static BOOL(WINAPI* TrueTerminateProcess)(HANDLE hProcess, UINT uExitCode) = TerminateProcess;

void RevLog(const char* format, ...);

HMODULE revModule;
WCHAR revModuleName[MAX_PATH];
WCHAR ovrModuleName[MAX_PATH];

HANDLE WINAPI HookOpenEvent(DWORD dwDesiredAccess, BOOL bInheritHandle, LPCWSTR lpName)
{
	// Don't touch this, it heavily affects performance in Unity games.
	if (wcscmp(lpName, OVR_HMD_CONNECTED_EVENT_NAME) == 0)
		return ::CreateEventW(NULL, TRUE, TRUE, NULL);

	return TrueOpenEvent(dwDesiredAccess, bInheritHandle, lpName);
}

HMODULE WINAPI HookLoadLibraryW(LPCWSTR lpFileName)
{
	LPCWSTR name = PathFindFileNameW(lpFileName);
	LPCWSTR ext = PathFindExtensionW(name);
	size_t length = ext - name;

	// Load our own library again so the ref count is incremented.
	if (wcsncmp(name, ovrModuleName, length) == 0)
		return TrueLoadLibraryW(revModuleName);
	
	return TrueLoadLibraryW(lpFileName);
}

HMODULE WINAPI HookLoadLibraryExW(LPCWSTR lpLibFileName, HANDLE hFile, DWORD dwFlags)
{
	LPCWSTR name = PathFindFileNameW(lpLibFileName);
	LPCWSTR ext = PathFindExtensionW(name);
	size_t length = ext - name;

	// Load our own library again so the ref count is incremented.
	if (wcsncmp(name, ovrModuleName, length) == 0) 
		return TrueLoadLibraryExW(revModuleName, hFile, dwFlags);
	
	return TrueLoadLibraryExW(lpLibFileName, hFile, dwFlags);
}

// A game that dies without a crash dialog leaves nothing behind: no Windows error event, no
// ovr_Destroy, no engine log in shipping builds. The exit hooks and the exception logger below
// record how the process ended and which module asked for it, so a silent exit can be told
// apart from a crash and traced to the engine, the runtime or Revive itself.
static void DescribeAddress(const void* address, char* out, size_t size)
{
	HMODULE module = NULL;
	char path[MAX_PATH] = {};
	if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
		(LPCSTR)address, &module) && GetModuleFileNameA(module, path, MAX_PATH))
	{
		snprintf(out, size, "%s+0x%llx", PathFindFileNameA(path),
			(unsigned long long)((ULONG_PTR)address - (ULONG_PTR)module));
	}
	else
	{
		snprintf(out, size, "%p", address);
	}
}

DECLSPEC_NORETURN VOID WINAPI HookExitProcess(UINT uExitCode)
{
	char caller[MAX_PATH + 32];
	DescribeAddress(_ReturnAddress(), caller, sizeof(caller));
	RevLog("ExitProcess(%u) called from %s", uExitCode, caller);
	TrueExitProcess(uExitCode);
}

BOOL WINAPI HookTerminateProcess(HANDLE hProcess, UINT uExitCode)
{
	if (hProcess == GetCurrentProcess() || GetProcessId(hProcess) == GetCurrentProcessId())
	{
		char caller[MAX_PATH + 32];
		DescribeAddress(_ReturnAddress(), caller, sizeof(caller));
		RevLog("TerminateProcess(self, %u) called from %s", uExitCode, caller);
	}
	return TrueTerminateProcess(hProcess, uExitCode);
}

static PVOID g_ExceptionLogger = NULL;

static LONG CALLBACK LogException(PEXCEPTION_POINTERS info)
{
	const EXCEPTION_RECORD* record = info->ExceptionRecord;
	const DWORD code = record->ExceptionCode;

	// Errors, C++ throws and small custom codes (Unreal raises code 1 on a fatal error).
	// Informational codes such as thread naming and debug output are skipped.
	const bool error = (code & 0xC0000000) == 0xC0000000;
	if (!error && code != 0xE06D7363 && code >= 0x100)
		return EXCEPTION_CONTINUE_SEARCH;

	static std::atomic<int> s_logged{ 0 };
	if (s_logged.fetch_add(1) >= 24)
		return EXCEPTION_CONTINUE_SEARCH;

	char where[MAX_PATH + 32];
	DescribeAddress(record->ExceptionAddress, where, sizeof(where));
	if (code == EXCEPTION_ACCESS_VIOLATION && record->NumberParameters >= 2)
		RevLog("exception 0x%08lx at %s (thread %lu): %s of address %p", code, where, GetCurrentThreadId(),
			record->ExceptionInformation[0] == 0 ? "read" : record->ExceptionInformation[0] == 1 ? "write" : "execute",
			(void*)record->ExceptionInformation[1]);
	else
		RevLog("exception 0x%08lx at %s (thread %lu)", code, where, GetCurrentThreadId());

	return EXCEPTION_CONTINUE_SEARCH;
}

void AttachDetours()
{
	DetourTransactionBegin();
	DetourUpdateThread(GetCurrentThread());
	DetourAttach((PVOID*)&TrueLoadLibraryW, HookLoadLibraryW);
	DetourAttach((PVOID*)&TrueLoadLibraryExW, HookLoadLibraryExW);
	DetourAttach(&(PVOID&)TrueOpenEvent, HookOpenEvent);
	DetourAttach(&(PVOID&)TrueExitProcess, HookExitProcess);
	DetourAttach(&(PVOID&)TrueTerminateProcess, HookTerminateProcess);
	DetourTransactionCommit();
}

void DetachDetours()
{
	DetourTransactionBegin();
	DetourUpdateThread(GetCurrentThread());
	DetourDetach((PVOID*)&TrueLoadLibraryW, HookLoadLibraryW);
	DetourDetach((PVOID*)&TrueLoadLibraryExW, HookLoadLibraryExW);
	DetourDetach(&(PVOID&)TrueOpenEvent, HookOpenEvent);
	DetourDetach(&(PVOID&)TrueExitProcess, HookExitProcess);
	DetourDetach(&(PVOID&)TrueTerminateProcess, HookTerminateProcess);
	DetourTransactionCommit();
}

BOOL APIENTRY DllMain(HANDLE hModule, DWORD ul_reason_for_call, LPVOID lpReserved)
{
	if (DetourIsHelperProcess())
		return TRUE;

#if defined(_WIN64)
	const char* pBitDepth = "64";
#else
	const char* pBitDepth = "32";
#endif
	switch (ul_reason_for_call)
	{
		case DLL_PROCESS_ATTACH:
			revModule = (HMODULE)hModule;
			GetModuleFileName(revModule, revModuleName, MAX_PATH);
			swprintf(ovrModuleName, MAX_PATH, L"LibOVRRT%hs_%d.dll", pBitDepth, OVR_MAJOR_VERSION);

			DetourRestoreAfterWith();
			AttachDetours();
			g_ExceptionLogger = AddVectoredExceptionHandler(1, LogException);
			break;
		case DLL_PROCESS_DETACH:
			RevLog("ReviveXR unloading (%s)", lpReserved ? "process exit" : "FreeLibrary");
			if (g_ExceptionLogger)
				RemoveVectoredExceptionHandler(g_ExceptionLogger);
			DetachDetours();
			break;
		default:
			break;
	}
	return TRUE;
}
