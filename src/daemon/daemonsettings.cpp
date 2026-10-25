#include "daemonsettings.h"

#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>

namespace agv {

DaemonSettings::DaemonSettings(QString path)
    : m_path(std::move(path))
{
}

void DaemonSettings::load()
{
    QFile file(m_path);
    if (!file.open(QIODevice::ReadOnly))
        return;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll());
    if (document.isObject())
        m_values = document.object().toVariantMap();
}

bool DaemonSettings::save() const
{
    QSaveFile file(m_path);
    if (!file.open(QIODevice::WriteOnly))
        return false;
    file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    file.write(QJsonDocument(QJsonObject::fromVariantMap(m_values)).toJson());
    return file.commit();
}

} // namespace agv
