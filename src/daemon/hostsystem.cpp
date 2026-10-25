#include "hostsystem.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

namespace agv {

namespace {

constexpr qint64 MaxConfigSize = 1024 * 1024;

QByteArray readAll(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return file.readAll();
}

bool isConnectCommand(const QByteArray &cmdline)
{
    const QList<QByteArray> args = cmdline.split('\0');
    if (args.isEmpty())
        return false;
    const QByteArray program = args.first().mid(args.first().lastIndexOf('/') + 1);
    return program == "adguardvpn-cli" && args.contains("connect");
}

QString homeOf(const QString &procEntry)
{
    const QByteArray environ = readAll(procEntry + QStringLiteral("/environ"));
    for (const QByteArray &variable : environ.split('\0')) {
        if (variable.startsWith("HOME="))
            return QString::fromLocal8Bit(variable.mid(5));
    }
    return {};
}

uid_t uidOf(const QString &procEntry)
{
    const QByteArray status = readAll(procEntry + QStringLiteral("/status"));
    for (const QByteArray &line : status.split('\n')) {
        if (line.startsWith("Uid:")) {
            const QList<QByteArray> fields = line.mid(4).simplified().split(' ');
            return fields.isEmpty() ? 0 : fields.first().toUInt();
        }
    }
    return 0;
}

} // namespace

QList<ExternalVpn> findExternalVpns(const QString &ownHome, const QString &procDir)
{
    const QString ownCanonical = QFileInfo(ownHome).canonicalFilePath();

    QList<ExternalVpn> result;
    const QStringList entries = QDir(procDir).entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QString &entry : entries) {
        bool ok = false;
        const pid_t pid = entry.toInt(&ok);
        if (!ok || pid <= 0)
            continue;

        const QString path = procDir + QLatin1Char('/') + entry;
        if (!isConnectCommand(readAll(path + QStringLiteral("/cmdline"))))
            continue;

        const QString home = homeOf(path);
        if (!home.isEmpty()) {
            // canonicalFilePath() is empty for paths that do not exist; never match on that.
            const QString canonical = QFileInfo(home).canonicalFilePath();
            if (home == ownHome || (!canonical.isEmpty() && canonical == ownCanonical))
                continue;
        }

        result.append({pid, uidOf(path), home});
    }
    return result;
}

bool isVpnProcess(pid_t pid, const QString &procDir)
{
    return isConnectCommand(readAll(procDir + QLatin1Char('/') + QString::number(pid) + QStringLiteral("/cmdline")));
}

QString cliConfigPath(const QString &home)
{
    return home + QStringLiteral("/.local/share/adguardvpn-cli/adguardvpn-cli.conf");
}

QString importCliConfig(const QString &fromHome, uid_t ownerUid, const QString &toHome)
{
    const QString source = cliConfigPath(fromHome);
    const int fd = ::open(QFile::encodeName(source).constData(), O_RDONLY | O_NOFOLLOW | O_CLOEXEC);
    if (fd < 0)
        return QStringLiteral("No AdGuard VPN CLI login found in %1").arg(fromHome);

    QFile in;
    if (!in.open(fd, QIODevice::ReadOnly, QFileDevice::AutoCloseHandle)) {
        ::close(fd);
        return QStringLiteral("Cannot read %1").arg(source);
    }

    struct stat info {};
    if (::fstat(fd, &info) != 0 || !S_ISREG(info.st_mode))
        return QStringLiteral("%1 is not a regular file").arg(source);
    if (info.st_uid != ownerUid)
        return QStringLiteral("%1 is not owned by the requesting user").arg(source);
    if (info.st_size <= 0 || info.st_size > MaxConfigSize)
        return QStringLiteral("%1 has an unexpected size").arg(source);

    const QByteArray data = in.readAll();
    if (data.size() != info.st_size)
        return QStringLiteral("Cannot read %1").arg(source);

    const QString target = cliConfigPath(toHome);
    const QString targetDir = QFileInfo(target).path();
    if (!QDir().mkpath(targetDir))
        return QStringLiteral("Cannot create %1").arg(targetDir);
    for (QString dir = targetDir; dir.startsWith(toHome) && dir != toHome; dir = QFileInfo(dir).path())
        QFile::setPermissions(dir, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);

    QSaveFile out(target);
    if (!out.open(QIODevice::WriteOnly))
        return QStringLiteral("Cannot write %1").arg(target);
    out.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    out.write(data);
    if (!out.commit())
        return QStringLiteral("Cannot write %1").arg(target);
    return {};
}

} // namespace agv
