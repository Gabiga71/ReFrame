#include "oculusoauthtokencontroller.h"
#include "trayiconcontroller.h"
#include "windowsservices.h"
#include <stdlib.h>

#include <QCoreApplication>
#include <QUrl>
#include <thread>

COculusOauthTokenController *COculusOauthTokenController::SharedInstance()
{
	static COculusOauthTokenController *instance = nullptr;
	if ( !instance )
	{
		instance = new COculusOauthTokenController();
	}
	return instance;
}

COculusOauthTokenController::COculusOauthTokenController()
	: m_strAccessToken()
	, m_roamingAppDataPath(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation))
	, m_sqliteDb(QSqlDatabase::addDatabase( "QSQLITE" ))
{
}

COculusOauthTokenController::~COculusOauthTokenController()
{
}


// The Oculus client keeps this database in WAL mode and may never checkpoint it, so the
// main .sqlite file can be an empty 4 KiB shell while every row still lives in the -wal
// sidecar. Copying only the main file therefore hands us a database with no tables at all.
// Copy the sidecars alongside it and SQLite replays the log when it opens the copy.
static const char* const s_dbSuffixes[] = { "", "-wal", "-shm" };

static void RemoveDatabaseCopy( const QString &basePath )
{
	for ( const char* suffix : s_dbSuffixes )
		QFile::remove( basePath + QLatin1String( suffix ) );
}

static bool CopyDatabase( const QString &source, const QString &dest )
{
	// Also clears any copy a previous run failed to clean up.
	RemoveDatabaseCopy( dest );

	if ( !QFile::copy( source, dest ) )
		return false;

	// The sidecars only exist while the client holds the database open, so a missing one is
	// not an error. A -wal copied mid-write may end on a partial frame, which SQLite detects
	// by checksum and ignores; the worst case is slightly stale data, not a failed read.
	for ( const char* suffix : s_dbSuffixes )
	{
		if ( !*suffix )
			continue;

		const QString sidecar = source + QLatin1String( suffix );
		if ( QFile::exists( sidecar ) )
			QFile::copy( sidecar, dest + QLatin1String( suffix ) );
	}

	return true;
}

bool COculusOauthTokenController::Init()
{
	//cd up from %APPDATA%/Roaming/APPNAME to %APPDATA%/Roaming
	m_roamingAppDataPath.cdUp();
	QString oculusOfflineDb = m_roamingAppDataPath.filePath("Oculus/Sessions/_oaf/data.sqlite");
	QString tempOculusOfflineDB = m_roamingAppDataPath.filePath("Oculus/Sessions/_oaf/revive_data.sqlite");

	//Clean up saved credentials from previous versions as they're no longer needed
	WindowsServices::DeleteCredentials();

	if (!QFile::exists(oculusOfflineDb))
		return false;

	//The SQLite DB is locked by the oculus service, so we need to make a copy
	if (!CopyDatabase(oculusOfflineDb, tempOculusOfflineDB))
	{
		CTrayIconController::SharedInstance()->ShowInformation(TrayInfo_OculusAccessTokenNotFound);
		return false;
	}
	m_sqliteDb.setDatabaseName(tempOculusOfflineDB);

	if(!m_sqliteDb.open() || !LoadToken())
	{
		m_sqliteDb.close();
		RemoveDatabaseCopy(tempOculusOfflineDB);
		CTrayIconController::SharedInstance()->ShowInformation(TrayInfo_OculusAccessTokenNotFound);
		return false;
	}

	m_sqliteDb.close();
	RemoveDatabaseCopy(tempOculusOfflineDB);
	return true;
}

// Oculus decides entitlement from the session its backend knows about, so this request has
// to actually arrive. Two things used to stop it: Qt 5 has no TLS without OpenSSL, which
// Revive does not ship, and the old caller handed a JavaScript object to
// XMLHttpRequest.send(), which Qt stringifies -- app_id_override and in_vr never arrived.
void COculusOauthTokenController::SendHeartbeat(const QString& appId)
{
	if (m_strAccessToken.isEmpty())
		return;

	const QString path = QStringLiteral("/user_heartbeat?access_token=") +
		QString::fromLatin1(QUrl::toPercentEncoding(m_strAccessToken));
	QString body = QStringLiteral("current_status=ONLINE&in_vr=true");
	if (!appId.isEmpty())
		body += QStringLiteral("&app_id_override=") + QString::fromLatin1(QUrl::toPercentEncoding(appId));

	// The overlay renders the dashboard on this thread, so it must not block on a request.
	std::thread([this, path, body]()
	{
		int status = 0;
		const bool ok = WindowsServices::PostHttps(QStringLiteral("graph.oculus.com"), path, body, &status);

		// Report from the main thread: the message handler is not thread safe. Never log the
		// path, it carries the access token.
		QMetaObject::invokeMethod(this, [ok, status]()
		{
			qDebug("Oculus heartbeat %s (HTTP %d)", ok ? "accepted" : "rejected", status);
		}, Qt::QueuedConnection);
	}).detach();
}

bool COculusOauthTokenController::LoadToken()
{
	QSqlQuery query = QSqlQuery( m_sqliteDb );
	if( !query.exec( "SELECT value FROM 'Objects' WHERE hashkey = '__OAF_OFFLINE_DATA_KEY__'" ))
		return false;

	query.first();
	QByteArray offlineDataByteArray = query.value(0).toByteArray();
	qsizetype token_start_index = offlineDataByteArray.indexOf("last_valid_auth_token") + 31;
	if (token_start_index == -1)
		 return false;

	qsizetype token_stop_index = -1;
	if(offlineDataByteArray.contains("last_valid_fb_access_token"))
		token_stop_index = offlineDataByteArray.indexOf("last_valid_fb_access_token") - 4;

	if (offlineDataByteArray.mid(token_start_index).contains("last_valid_auth_token_type"))
		/*'last_valid_auth_token_type' matches 'last_valid_auth_token' in indexOf, so
		search after the index of 'last_valid_auth_token' */
		token_stop_index = offlineDataByteArray.mid(token_start_index).indexOf("last_valid_auth_token_type") - 4;

	if (token_stop_index == -1)
		return false;

	m_strAccessToken = QString::fromStdString(offlineDataByteArray.mid(token_start_index, token_stop_index).toStdString());
	TokenChanged();
	return true;
}
