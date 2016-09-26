/**************************************************************************
* Otter Browser: Web browser controlled by the user, not vice-versa.
* Copyright (C) 2013 - 2016 Michal Dutkiewicz aka Emdek <michal@emdek.pl>
* Copyright (C) 2015 Jan Bajer aka bajasoft <jbajer@gmail.com>
*
* This program is free software: you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation, either version 3 of the License, or
* (at your option) any later version.
*
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
* GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License
* along with this program.  If not, see <http://www.gnu.org/licenses/>.
*
**************************************************************************/

#include "core/Application.h"
#include "core/SessionsManager.h"
#include "core/SettingsManager.h"
#include "ui/MainWindow.h"
#include "ui/StartupDialog.h"
#ifdef OTTER_ENABLE_CRASHREPORTS
#if defined(Q_OS_WIN32)
#include "../3rdparty/breakpad/src/client/windows/handler/exception_handler.h"
#elif defined(Q_OS_LINUX)
#include "../3rdparty/breakpad/src/client/linux/handler/exception_handler.h"
#endif
#endif

#include <QtCore/QDir>
#include <QtCore/QStandardPaths>
#include <QtCore/QUrl>

using namespace Otter;

#if QT_VERSION >= 0x050400
void otterMessageHander(QtMsgType type, const QMessageLogContext &context, const QString &message)
{
	if (message.trimmed().startsWith(QLatin1String("OpenType support missing")) || message.startsWith(QLatin1String("libpng warning: iCCP:")) || message.startsWith(QLatin1String("OpenType support missing for script")) || message.startsWith(QLatin1String("QNetworkReplyImplPrivate::error: Internal problem, this method must only be called once")) || message.startsWith(QLatin1String("QBasicTimer::start: QBasicTimer can only be used with threads started with QThread")) || message.contains(QLatin1String("::_q_startOperation was called more than once")))
	{
		return;
	}

	fputs(qFormatLogMessage(type, context, message).toLocal8Bit().constData(), stderr);

	if (type == QtFatalMsg)
	{
		abort();
	}
}
#endif

#ifdef OTTER_ENABLE_CRASHREPORTS
#if defined(Q_OS_WIN32)
bool otterCrashDumpHandler(const wchar_t *dumpDirectory, const wchar_t *dumpIdentifier, void *context, EXCEPTION_POINTERS *exceptionInformation, MDRawAssertionInfo *assertionInformation, bool succeeded)
{
	Q_UNUSED(context)
	Q_UNUSED(exceptionInformation)
	Q_UNUSED(assertionInformation)

	if (succeeded)
	{
		qDebug("Crash dump saved to: %s", QDir::toNativeSeparators(QString::fromWCharArray(dumpDirectory) + QDir::separator() + QString::fromWCharArray(dumpIdentifier)).toLocal8Bit().constData());
	}

	return succeeded;
}
#elif defined(Q_OS_LINUX)
bool otterCrashDumpHandler(const google_breakpad::MinidumpDescriptor &descriptor, void *context, bool succeeded)
{
	Q_UNUSED(context)

	if (succeeded)
	{
		qDebug("Crash dump saved to: %s", descriptor.path());
	}

	return succeeded;
}
#endif
#endif

int main(int argc, char *argv[])
{
#if QT_VERSION >= 0x050400
	qSetMessagePattern(QLatin1String("%{if-category}%{category}: %{endif}%{message}\n"));
	qInstallMessageHandler(otterMessageHander);
#endif

#ifdef OTTER_ENABLE_CRASHREPORTS
#if defined(Q_OS_WIN32)
	new google_breakpad::ExceptionHandler(reinterpret_cast<const wchar_t*>(QStandardPaths::writableLocation(QStandardPaths::TempLocation).utf16()), 0, otterCrashDumpHandler, 0, true);
#elif defined(Q_OS_LINUX)
	new google_breakpad::ExceptionHandler(google_breakpad::MinidumpDescriptor(QStandardPaths::writableLocation(QStandardPaths::TempLocation).toStdString()), 0, otterCrashDumpHandler, 0, true, -1);
#endif
#endif

	Application application(argc, argv);
	application.setAttribute(Qt::AA_UseHighDpiPixmaps, true);

	if (application.isRunning() || application.isUpdating() || application.getCommandLineParser()->isSet(QLatin1String("report")))
	{
		return 0;
	}

	const QString session(application.getCommandLineParser()->value(QLatin1String("session")).isEmpty() ? QLatin1String("default") : application.getCommandLineParser()->value(QLatin1String("session")));
	const QString startupBehavior(SettingsManager::getValue(SettingsManager::Browser_StartupBehaviorOption).toString());
	const bool isPrivate(application.getCommandLineParser()->isSet(QLatin1String("privatesession")));

	if (!application.getCommandLineParser()->value(QLatin1String("session")).isEmpty() && SessionsManager::getSession(session).isClean)
	{
		SessionsManager::restoreSession(SessionsManager::getSession(session), NULL, isPrivate);
	}
	else if (startupBehavior == QLatin1String("showDialog") || application.getCommandLineParser()->isSet(QLatin1String("sessionchooser")) || !SessionsManager::getSession(session).isClean)
	{
		StartupDialog dialog(session);

		if (dialog.exec() == QDialog::Rejected)
		{
			return 0;
		}

		SessionsManager::restoreSession(dialog.getSession(), NULL, isPrivate);
	}
	else if (startupBehavior == QLatin1String("continuePrevious"))
	{
		SessionsManager::restoreSession(SessionsManager::getSession(QLatin1String("default")), NULL, isPrivate);
	}
	else if (startupBehavior != QLatin1String("startEmpty"))
	{
		WindowHistoryEntry entry;

		if (startupBehavior == QLatin1String("startHomePage"))
		{
			entry.url = SettingsManager::getValue(SettingsManager::Browser_HomePageOption).toString();
		}
		else if (startupBehavior == QLatin1String("startStartPage"))
		{
			entry.url = QLatin1String("about:start");
		}
		else
		{
			entry.url = QLatin1String("about:blank");
		}

		SessionWindow tab;
		tab.history.append(entry);
		tab.historyIndex = 0;

		SessionMainWindow window;
		window.windows.append(tab);

		SessionInformation sessionData;
		sessionData.path = QLatin1String("default");
		sessionData.title = QCoreApplication::translate("main", "Default");
		sessionData.windows.append(window);
		sessionData.index = 0;

		SessionsManager::restoreSession(sessionData, NULL, isPrivate);
	}

	if (!application.getCommandLineParser()->positionalArguments().isEmpty())
	{
		MainWindow *window(Application::getWindow());

		if (window)
		{
			const QStringList urls(application.getCommandLineParser()->positionalArguments());

			for (int i = 0; i < urls.count(); ++i)
			{
				window->openUrl(urls.at(i));
			}
		}
	}

	if (application.getWindows().isEmpty())
	{
		application.createWindow(isPrivate ? Application::PrivateFlag : Application::NoFlags);
	}

	return application.exec();
}
