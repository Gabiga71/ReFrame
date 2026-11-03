#include "revivemanifestcontroller.h"
#include "trayiconcontroller.h"
#include "openvroverlaycontroller.h"
#include "oculusoauthtokencontroller.h"
#include "openvr.h"
#include "OVR_CAPI_Keys.h"
#include <qt_windows.h>

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QProcess>
#include <QSettings>
#include <QUrl>

#define MAX_KEY_LENGTH 255
#define MAX_VALUE_NAME 16383

CReviveManifestController *s_pSharedRevController = NULL;
const char* CReviveManifestController::AppKey = "revive.dashboard.overlay";
const char* CReviveManifestController::AppPrefix = "revive.app.";

CReviveManifestController *CReviveManifestController::SharedInstance()
{
	if ( !s_pSharedRevController )
	{
		s_pSharedRevController = new CReviveManifestController();
	}
	return s_pSharedRevController;
}

bool GetLibraryPath(PWCHAR path, DWORD length, PWCHAR guid)
{
	LONG error = ERROR_SUCCESS;

	// Open the libraries key
	WCHAR keyPath[MAX_PATH] = { L"Software\\Oculus VR, LLC\\Oculus\\Libraries\\" };
	HKEY oculusKey;

	// Open the library key
	wcsncat(keyPath, guid, MAX_PATH);
	error = RegOpenKeyExW(HKEY_CURRENT_USER, keyPath, 0, KEY_READ, &oculusKey);
	if (error != ERROR_SUCCESS)
	{
		qDebug("Unable to open Library path key.");
		return false;
	}

	// Get the volume path to this library
	DWORD pathSize;
	error = RegQueryValueExW(oculusKey, L"Path", NULL, NULL, NULL, &pathSize);
	PWCHAR volumePath = (PWCHAR)malloc(pathSize);
	error = RegQueryValueExW(oculusKey, L"Path", NULL, NULL, (PBYTE)volumePath, &pathSize);
	RegCloseKey(oculusKey);
	if (error != ERROR_SUCCESS)
	{
		free(volumePath);
		qDebug("Unable to read Library path.");
		return false;
	}

	// Resolve the volume path to a mount point
	DWORD total;
	WCHAR volume[50] = { L'\0' };
	wcsncpy(volume, volumePath, 49);
	GetVolumePathNamesForVolumeNameW(volume, path, length, &total);
	wcsncat(path, volumePath + 49, MAX_PATH);
	free(volumePath);

	return true;
}

void QueryKey(HKEY hKey, QStringList &id_array, QStringList &path_array)
{
	TCHAR    achKey[MAX_KEY_LENGTH];   // buffer for subkey name
	DWORD    cbName;                   // size of name string 
	DWORD    cSubKeys = 0;               // number of subkeys 
	DWORD i, retCode;

	// Get the class name and the value count. 
	retCode = RegQueryInfoKey(hKey, NULL, NULL, NULL, /* number of subkeys*/ &cSubKeys, NULL, NULL, NULL, NULL, NULL, NULL, NULL); 

	// Enumerate the subkeys, until RegEnumKeyEx fails.
	if (cSubKeys)
	{
		WCHAR path[MAX_PATH] = { 0 };
		for (i = 0; i < cSubKeys; i++)
		{
			cbName = MAX_KEY_LENGTH;
			retCode = RegEnumKeyEx(hKey, i,
				achKey,
				&cbName,
				NULL,
				NULL,
				NULL,
				NULL);
			if (retCode == ERROR_SUCCESS && GetLibraryPath(path, MAX_PATH, achKey))
			{
				id_array << QString::fromWCharArray(achKey);
				path_array << QString::fromWCharArray(path);
			}
		}
	}
}

bool CReviveManifestController::GetLibraries(QStringList &id_array, QStringList &path_array)
{
	LONG error = ERROR_SUCCESS;
	
	// Open the libraries key
	WCHAR keyPath[MAX_PATH] = { L"Software\\Oculus VR, LLC\\Oculus\\Libraries\\" };
	HKEY oculusKey;
	error = RegOpenKeyExW(HKEY_CURRENT_USER, keyPath, 0, KEY_READ, &oculusKey);
	if (error != ERROR_SUCCESS)
	{
		qDebug("Unable to open Libraries key.");
		return false;
	}
	QueryKey(oculusKey, id_array, path_array);
	RegCloseKey(oculusKey);
	return true;
}

bool CReviveManifestController::GetDefaultLibraryPath(wchar_t* path, uint32_t length)
{
	LONG error = ERROR_SUCCESS;

	// Open the libraries key
	WCHAR keyPath[MAX_PATH] = { L"Software\\Oculus VR, LLC\\Oculus\\Libraries\\" };
	HKEY oculusKey;
	error = RegOpenKeyExW(HKEY_CURRENT_USER, keyPath, 0, KEY_READ, &oculusKey);
	if (error != ERROR_SUCCESS)
	{
		qDebug("Unable to open Libraries key.");
		return false;
	}

	// Get the default library
	WCHAR guid[40] = { L'\0' };
	DWORD guidSize = sizeof(guid);
	error = RegQueryValueExW(oculusKey, L"DefaultLibrary", NULL, NULL, (PBYTE)guid, &guidSize);
	RegCloseKey(oculusKey);
	if (error != ERROR_SUCCESS)
	{
		qDebug("Unable to read DefaultLibrary guid.");
		return false;
	}

	// Open the default library key
	wcsncat(keyPath, guid, MAX_PATH);
	error = RegOpenKeyExW(HKEY_CURRENT_USER, keyPath, 0, KEY_READ, &oculusKey);
	if (error != ERROR_SUCCESS)
	{
		qDebug("Unable to open Library path key.");
		return false;
	}

	// Get the volume path to this library
	DWORD pathSize;
	error = RegQueryValueExW(oculusKey, L"Path", NULL, NULL, NULL, &pathSize);
	PWCHAR volumePath = (PWCHAR)malloc(pathSize);
	error = RegQueryValueExW(oculusKey, L"Path", NULL, NULL, (PBYTE)volumePath, &pathSize);
	RegCloseKey(oculusKey);
	if (error != ERROR_SUCCESS)
	{
		free(volumePath);
		qDebug("Unable to read Library path.");
		return false;
	}

	// Resolve the volume path to a mount point
	DWORD total;
	WCHAR volume[50] = { L'\0' };
	wcsncpy(volume, volumePath, 49);
	GetVolumePathNamesForVolumeNameW(volume, path, length, &total);
	wcsncat(path, volumePath + 49, MAX_PATH);
	free(volumePath);

	return true;
}

bool GetOculusBasePath(wchar_t* path, uint32_t length)
{
	LONG error = ERROR_SUCCESS;

	HKEY oculusKey;
	error = RegOpenKeyEx(HKEY_LOCAL_MACHINE, L"Software\\Oculus VR, LLC\\Oculus", 0, KEY_READ | KEY_WOW64_32KEY, &oculusKey);
	if (error != ERROR_SUCCESS)
	{
		qDebug("Unable to open Oculus key.");
		return false;
	}
	error = RegQueryValueEx(oculusKey, L"Base", NULL, NULL, (PBYTE)path, (PDWORD)&length);
	if (error != ERROR_SUCCESS)
	{
		qDebug("Unable to read Base path.");
		return false;
	}
	RegCloseKey(oculusKey);

	return true;
}

CReviveManifestController::CReviveManifestController()
	: BaseClass()
	, m_appFile(QCoreApplication::applicationDirPath() + "/app.vrmanifest")
	, m_manifestFile(QCoreApplication::applicationDirPath() + "/revive.vrmanifest")
	, m_supportFile(QCoreApplication::applicationDirPath() + "/support.vrmanifest")
	, m_bLibraryFound(false)
	, m_bUseOpenXR(false)
{
	m_supportArgs["revive.app.oculus-dreamdeck-nux"] = R"(/base Support\oculus-dreamdeck-nux\Dreamdeck\Binaries\Win64\Dreamdeck-Win64-Shipping.exe -vr -dreamdeck=NUX)";
	m_supportArgs["revive.app.oculus-touch-tutorial"] = R"(/base Support\oculus-touch-tutorial\WindowsNoEditor\TouchNUX\Binaries\Win64\TouchNUX-Win64-Shipping.exe -gamemode=nux)";
	m_supportArgs["revive.app.oculus-first-contact"] = R"(/base Support\oculus-touch-tutorial\WindowsNoEditor\TouchNUX\Binaries\Win64\TouchNUX-Win64-Shipping.exe -gamemode="experienceonly")";
	m_supportArgs["revive.app.oculus-worlds"] = R"(/base Support/oculus-worlds/Home2/Binaries/Win64/Home2-Win64-Shipping.exe)";
}

CReviveManifestController::~CReviveManifestController()
{
}

// The support manifest lists experiences that the Oculus client used to install next to
// the runtime. Modern Meta Horizon clients no longer ship several of them, and a stale
// entry is worse than a missing one: registering the app key with SteamVR makes
// isApplicationInstalled() report the app as ready, so a store-installed app sharing that
// key never receives a manifest entry of its own and launching it resolves to a dead path.
// Drop every entry whose executable is not actually present.
//
// Note: the manifest file itself is static, so if some entries survive we still register it
// whole and the dead keys in it stay visible to SteamVR. Only the launch path is filtered
// in that case. Pruning the file as well would mean generating it at runtime.
bool CReviveManifestController::PruneSupportApps()
{
	wchar_t basePath[MAX_PATH] = {};
	if (!GetOculusBasePath(basePath, MAX_PATH))
		return false;

	const QDir base(QDir::fromNativeSeparators(QString::fromWCharArray(basePath)));

	for (auto it = m_supportArgs.begin(); it != m_supportArgs.end(); )
	{
		const QString args = it.value();
		const int start = args.indexOf("/base ");
		const int end = start < 0 ? -1 : args.indexOf(".exe", start);
		const QString relative = (start < 0 || end < 0) ? QString()
			: QDir::fromNativeSeparators(args.mid(start + 6, end + 4 - start - 6));

		if (relative.isEmpty() || !base.exists(relative))
		{
			qDebug("Support app is not installed, ignoring: %s", qUtf8Printable(it.key()));
			it = m_supportArgs.erase(it);
		}
		else
		{
			++it;
		}
	}

	return !m_supportArgs.isEmpty();
}

bool CReviveManifestController::Init()
{
	bool bSuccess = LoadDocument();

	if (!bSuccess)
	{
		QJsonArray applications;
		m_manifest["applications"] = applications;

		bSuccess = SaveDocument();
	}

#ifndef DEBUG
	// Add application and support manifest
	AddApplicationManifest(m_appFile);
	if (PruneSupportApps())
	{
		AddApplicationManifest(m_supportFile);
	}
	else if (vr::VRApplications())
	{
		// Unregister it so keys from an earlier run stop shadowing installed applications.
		QFileInfo info(m_supportFile);
		vr::VRApplications()->RemoveApplicationManifest(
			qPrintable(QDir::toNativeSeparators(info.absoluteFilePath())));
	}
#endif

	// Ensure the auto-launch flag is set
	if (vr::VRApplications() && vr::VRApplications()->SetApplicationAutoLaunch(AppKey, true) != vr::VRApplicationError_None)
		CTrayIconController::SharedInstance()->ShowInformation(TrayInfo_AutoLaunchFailed);

	// Get the base path
	wchar_t path[MAX_PATH];
	if (GetOculusBasePath(path, MAX_PATH))
	{
		QString base = QString::fromWCharArray(path);
		if (!base.endsWith('\\'))
			base.append('\\');
		qDebug("Oculus Base found: %s", qUtf8Printable(base));

		m_strBaseURL = QUrl::fromLocalFile(base).url();
		m_strBasePath = QDir::fromNativeSeparators(base);
		emit BaseChanged();
	}

	// Get the library path
	QStringList path_array;
	if (GetLibraries(m_lstLibraries, path_array))
	{
		for (int i = 0; i < path_array.size(); ++i) {
			QString library = path_array.at(i);
			if (!library.endsWith('\\'))
				library.append('\\');
			qDebug("Oculus Library found: %s", qUtf8Printable(library));

			m_bLibraryFound = true;
			m_strLibraryURL = QUrl::fromLocalFile(library).url();
			m_strLibraryPath = QDir::fromNativeSeparators(library);
			m_lstLibrariesURL << m_strLibraryURL;
		}
		emit LibraryChanged();

		if (vr::VRApplications() && !QCoreApplication::arguments().contains("-compositor"))
		{
			if (vr::VRApplications()->IsApplicationInstalled(AppKey) && vr::VRApplications()->GetApplicationAutoLaunch(AppKey))
				CTrayIconController::SharedInstance()->ShowInformation(TrayInfo_AutoLaunchEnabled);
		}
	}
	else
	{
		CTrayIconController::SharedInstance()->ShowInformation(TrayInfo_OculusLibraryNotFound);
	}

	return bSuccess;
}

bool CReviveManifestController::AddApplicationManifest(QFile& file)
{
	if (!vr::VRApplications())
		return false;

	QFileInfo info(file);
	QString filePath = QDir::toNativeSeparators(info.absoluteFilePath());
	vr::EVRApplicationError error = vr::VRApplications()->AddApplicationManifest(qPrintable(filePath));
	if (error != vr::VRApplicationError_None)
	{
		qWarning("Failed to add manifest file to OpenVR: %s (%s)", qUtf8Printable(filePath), vr::VRApplications()->GetApplicationsErrorNameFromEnum(error));
		return false;
	}

	qDebug("Loaded manifest: %s", qUtf8Printable(filePath));

	return true;
}

bool CReviveManifestController::LoadDocument()
{
	if (!m_manifestFile.open(QIODevice::ReadOnly))
	{
		qWarning("Couldn't open manifest file for reading");
		return false;
	}

	QJsonDocument doc = QJsonDocument::fromJson(m_manifestFile.readAll());
	m_manifest = doc.object();
	m_manifestFile.close();

#ifndef DEBUG
	AddApplicationManifest(m_manifestFile);
#endif

	return true;
}

bool CReviveManifestController::SaveDocument()
{
	if (!m_manifestFile.open(QIODevice::WriteOnly))
	{
		qWarning("Couldn't open manifest file for writing");
		return false;
	}

	QJsonDocument doc(m_manifest);
	m_manifestFile.write(doc.toJson());
	m_manifestFile.close();

	AddApplicationManifest(m_manifestFile);

	return true;
}

bool CReviveManifestController::addManifest(const QString &canonicalName, const QString &manifest)
{
	qDebug("Adding manifest: %s", qUtf8Printable(canonicalName));
	QJsonDocument doc = QJsonDocument::fromJson(manifest.toUtf8());
	QJsonArray apps = m_manifest["applications"].toArray();
	QJsonObject obj = doc.object();
	obj["app_key"] = AppPrefix + canonicalName;
	apps.append(obj);
	m_manifest["applications"] = apps;
	return SaveDocument();
}

bool CReviveManifestController::removeManifest(const QString &canonicalName)
{
	qDebug("Removing manifest: %s", qUtf8Printable(canonicalName));
	QString appKey = AppPrefix + canonicalName;
	QJsonArray apps = m_manifest["applications"].toArray();
	for (auto it = apps.begin(); it != apps.end(); ++it)
	{
		QJsonObject obj = it->toObject();
		if (obj["app_key"] == appKey)
		{
			apps.erase(it);
			break;
		}
	}
	m_manifest["applications"] = apps;
	return SaveDocument();
}

bool CReviveManifestController::LaunchInjector(const QString& args)
{
	// Launch the injector with the arguments
	QProcess injector;
	injector.setProgram(QCoreApplication::applicationDirPath() + "/ReviveInjector.exe");
	if (m_bUseOpenXR)
		injector.setNativeArguments("/openxr " + args);
	else
		injector.setNativeArguments(args);
	injector.start();

	if (!injector.waitForFinished())
		return false;
	return injector.exitCode() == 0;
}

bool CReviveManifestController::LaunchSupportApp(const QString& appKey)
{
	if (!m_supportArgs.contains(appKey))
		return false;

	return LaunchInjector(m_supportArgs[appKey]);
}

// SteamVR keeps reporting the process id an application was identified with, so a game that
// crashed or was killed still looks like it is running. Confirm with the operating system
// before refusing to launch, otherwise a single stale identification blocks the
// application until SteamVR itself is restarted.
static bool IsProcessAlive(uint32_t processId)
{
	HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, processId);
	if (!process)
		return false;

	DWORD exitCode = 0;
	const bool alive = GetExitCodeProcess(process, &exitCode) && exitCode == STILL_ACTIVE;
	CloseHandle(process);
	return alive;
}

bool CReviveManifestController::launchApplication(const QString &canonicalName)
{
	qDebug("Launching application: %s", qUtf8Printable(canonicalName));
	QString appKey = AppPrefix + canonicalName;
	QStringList multiplayerAppKeys = {"facebook-vr-facebook-horizon", "gunslinger-games-lls-fos", "harmonix-dance-central-vr",
									  "hidden-path-entertainment-brass-tactics", "hidden-path-entertainment-phoenix", "insomniac-games-i-28",
									  "ready-at-dawn-echo-arena", "twisted-pixel-games-path-of-the-warrior"};

	if (multiplayerAppKeys.indexOf(canonicalName) != -1 && !COculusOauthTokenController::SharedInstance()->Connected())
		CTrayIconController::SharedInstance()->ShowInformation(TrayInfo_OculusAccessTokenNotFound);

	// The injector reports the launched process to SteamVR, so it can tell us whether this
	// application is already up. Starting a second instance is never useful: both compete
	// for the one scene application the runtime allows, and the loser fails in a way that
	// looks exactly like the first instance failing.
	if (vr::VRApplications())
	{
		const uint32_t processId = vr::VRApplications()->GetApplicationProcessId(qPrintable(appKey));
		if (processId != 0 && IsProcessAlive(processId))
		{
			qDebug("Already running as process %u, not launching a second instance", processId);
			CTrayIconController::SharedInstance()->ShowAlreadyRunning(canonicalName);
			return false;
		}

		if (processId != 0)
			qDebug("SteamVR still reports process %u for this application, but it has exited", processId);
	}

	CTrayIconController::SharedInstance()->ShowLaunching(canonicalName);

	if (!m_bUseOpenXR && vr::VRApplications())
	{
		// Refresh the manifest after launching the application to aid application identification
		vr::EVRApplicationError error = vr::VRApplications()->LaunchApplication(qPrintable(appKey));
		if (error == vr::VRApplicationError_None)
			return AddApplicationManifest(m_manifestFile);
		else
			qWarning("Failed to launch application through OpenVR, falling back to injector: %s (%s)", qUtf8Printable(appKey), vr::VRApplications()->GetApplicationsErrorNameFromEnum(error));
	}

	if (LaunchSupportApp(appKey))
		return true;

	// Search for the app in the cached manifest
	for (QJsonValue app : m_manifest["applications"].toArray())
	{
		if (app["app_key"].toString() == appKey)
		{
			if (LaunchInjector(app["arguments"].toString()))
				return true;

			CTrayIconController::SharedInstance()->ShowLaunchFailed(canonicalName);
			return false;
		}
	}
	// No manifest entry matched, so nothing was started.
	CTrayIconController::SharedInstance()->ShowLaunchFailed(canonicalName);
	return false;
}

bool CReviveManifestController::isApplicationInstalled(const QString &canonicalName)
{
	QString appKey = AppPrefix + canonicalName;
	if (vr::VRApplications())
		return vr::VRApplications()->IsApplicationInstalled(qPrintable(appKey));

	for (QJsonValue app : m_manifest["applications"].toArray())
	{
		if (app["app_key"].toString() == appKey)
			return true;
	}
	return false;
}
