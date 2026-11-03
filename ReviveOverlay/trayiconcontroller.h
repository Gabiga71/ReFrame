#ifndef CTRAYICONCONTROLLER_H
#define CTRAYICONCONTROLLER_H

#include <QObject>
#include <QMenu>
#include <QSystemTrayIcon>
#include <QString>
#include <memory>

enum ETrayInfo
{
	TrayInfo_OculusLibraryNotFound,
	TrayInfo_AutoLaunchEnabled,
	TrayInfo_AutoLaunchFailed,
	TrayInfo_OculusAccessTokenNotFound,
	// Launch status notifications carry their own text and have no click action, so
	// messageClicked() deliberately does not handle this value: a click on one of them
	// must not replay the action of whatever notification came before it.
	TrayInfo_LaunchStatus,
};

class CTrayIconController : public QObject
{
	Q_OBJECT
	typedef QObject BaseClass;

public:
	static CTrayIconController *SharedInstance();

public:
	CTrayIconController();
	~CTrayIconController();

	bool Init();
	void ShowInformation(ETrayInfo info);
	void ShowLaunching(const QString& application);
	void ShowLaunchFailed(const QString& application);
	void ShowAlreadyRunning(const QString& application);

public slots:
	void quit();

protected slots:
	void openxr(bool checked);
	void inject();
	void shortcut();
	void show();
	void showHelp();
	void messageClicked();
	void activated(QSystemTrayIcon::ActivationReason reason);

private:
	std::unique_ptr<QSystemTrayIcon> m_trayIcon;
	QMenu m_trayIconMenu;
	ETrayInfo m_LastInfo;

	QString openDialog();
};

#endif // CTRAYICONCONTROLLER_H
