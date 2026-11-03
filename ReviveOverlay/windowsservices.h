#ifndef WINDOWSSERVICES_H
#define WINDOWSSERVICES_H

#include <QString>

class WindowsServices
{
public:
	static bool CreateShortcut(QString shortcutPath, QString exePath, QString arguments = QString(), QString icon = QString());
	static bool CopyFiles(QStringList files, QString destination, QStringList newName);
	static bool DeleteCredentials();

	// Posts a form encoded body over HTTPS using the TLS stack Windows already provides.
	// Qt 5 can only do TLS through OpenSSL, which is not shipped with Revive and whose 1.1
	// series went out of support in 2023, so requests that matter do not go through Qt.
	static bool PostHttps(const QString& host, const QString& path, const QString& body, int* outStatus = nullptr);

private:
	static const wchar_t* CredTargetName;
};

#endif // WINDOWSSERVICES_H
