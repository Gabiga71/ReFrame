#include "windowsservices.h"
#include <qt_windows.h>
#include <wincred.h>
#include <wrl.h>
#include <Shlobj.h>
#include <winhttp.h>

#include <QDir>

bool WindowsServices::PostHttps(const QString& host, const QString& path, const QString& body, int* outStatus)
{
	if (outStatus)
		*outStatus = 0;

	HINTERNET session = WinHttpOpen(L"Revive", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
		WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
	if (!session)
		return false;

	bool result = false;
	if (HINTERNET connection = WinHttpConnect(session, qUtf16Printable(host), INTERNET_DEFAULT_HTTPS_PORT, 0))
	{
		if (HINTERNET request = WinHttpOpenRequest(connection, L"POST", qUtf16Printable(path),
			nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE))
		{
			const QByteArray payload = body.toUtf8();
			if (WinHttpSendRequest(request, L"Content-Type: application/x-www-form-urlencoded",
					(DWORD)-1, (LPVOID)payload.constData(), payload.size(), payload.size(), 0) &&
				WinHttpReceiveResponse(request, nullptr))
			{
				DWORD status = 0;
				DWORD size = sizeof(status);
				if (WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
					WINHTTP_HEADER_NAME_BY_INDEX, &status, &size, WINHTTP_NO_HEADER_INDEX))
				{
					if (outStatus)
						*outStatus = (int)status;
					result = status >= 200 && status < 300;
				}
			}
			WinHttpCloseHandle(request);
		}
		WinHttpCloseHandle(connection);
	}
	WinHttpCloseHandle(session);
	return result;
}

bool WindowsServices::CreateShortcut(QString shortcutPath, QString exePath, QString arguments, QString icon)
{
	QString shortcut = QDir::toNativeSeparators(shortcutPath);
	QString exe = QDir::toNativeSeparators(exePath);
	QString args = QDir::toNativeSeparators(arguments);
	QString ico = QDir::toNativeSeparators(icon);

	Microsoft::WRL::ComPtr<IShellLink> shellLink;
	HRESULT hr = CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&shellLink));
	if (SUCCEEDED(hr))
	{
		hr = shellLink->SetPath(qUtf16Printable(exe));
		if (SUCCEEDED(hr) && !args.isEmpty())
			hr = shellLink->SetArguments(qUtf16Printable(args));
		if (SUCCEEDED(hr) && !icon.isEmpty())
			shellLink->SetIconLocation(qUtf16Printable(ico), 0);
		if (SUCCEEDED(hr))
		{
			Microsoft::WRL::ComPtr<IPersistFile> persistFile;
			hr = shellLink.As(&persistFile);
			if (SUCCEEDED(hr))
			{
				hr = persistFile->Save(qUtf16Printable(shortcut), TRUE);
			}
		}
	}
	return SUCCEEDED(hr);
}

bool WindowsServices::CopyFiles(QStringList files, QString destination, QStringList newName)
{
	Microsoft::WRL::ComPtr<IFileOperation> pfo;
	HRESULT hr = CoCreateInstance(CLSID_FileOperation, nullptr, CLSCTX_ALL, IID_PPV_ARGS(&pfo));
	if (SUCCEEDED(hr))
	{
		QString dstDir = QDir::toNativeSeparators(destination);
		Microsoft::WRL::ComPtr<IShellItem> psiTo;
		if (!dstDir.isEmpty())
		{
			hr = SHCreateItemFromParsingName(qUtf16Printable(dstDir),
											 NULL,
											 IID_PPV_ARGS(&psiTo));
		}

		for (QStringList::iterator file = files.begin(), name = newName.begin();
			 SUCCEEDED(hr) && file != files.end() && name != newName.end();
			 file++, name++)
		{
			QString src = QDir::toNativeSeparators(*file);
			Microsoft::WRL::ComPtr<IShellItem> psiFrom;
			hr = SHCreateItemFromParsingName(qUtf16Printable(src), NULL, IID_PPV_ARGS(&psiFrom));
			if (SUCCEEDED(hr))
			{
				hr = pfo->CopyItem(
							psiFrom.Get(),
							psiTo.Get(),
							name->isNull() ? nullptr : qUtf16Printable(*name),
							nullptr);
			}
		}

		if (SUCCEEDED(hr))
		{
			hr = pfo->PerformOperations();
		}
	}
	return SUCCEEDED(hr);
}

const wchar_t* WindowsServices::CredTargetName = L"Revive/OculusPlatform";

bool WindowsServices::DeleteCredentials()
{
	return !!CredDeleteW(CredTargetName, CRED_TYPE_GENERIC, 0);
}
