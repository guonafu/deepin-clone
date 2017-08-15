#include "ddevicediskinfo.h"
#include "ddiskinfo_p.h"
#include "helper.h"
#include "ddevicepartinfo.h"

#include <QJsonObject>
#include <QJsonArray>
#include <QJsonDocument>
#include <QProcess>

static QString getPTName(const QString &device)
{
    Helper::processExec(QStringLiteral("/sbin/blkid -p -s PTTYPE -d -i %1").arg(device));

    const QByteArray &data = Helper::lastProcessStandardOutput();

    if (data.isEmpty())
        return QString();

    const QByteArrayList &list = data.split('=');

    if (list.count() != 3)
        return QString();

    return list.last().simplified();
}

class DDeviceDiskInfoPrivate : public DDiskInfoPrivate
{
public:
    DDeviceDiskInfoPrivate(DDeviceDiskInfo *qq);

    void init(const QJsonObject &obj);

    QString filePath() const Q_DECL_OVERRIDE;
    void refresh() Q_DECL_OVERRIDE;

    bool hasScope(DDiskInfo::DataScope scope, DDiskInfo::ScopeMode mode) const Q_DECL_OVERRIDE;
    bool openDataStream(int index) Q_DECL_OVERRIDE;
    void closeDataStream() Q_DECL_OVERRIDE;

    qint64 read(char *data, qint64 maxSize) Q_DECL_OVERRIDE;
    qint64 write(const char *data, qint64 maxSize) Q_DECL_OVERRIDE;

    bool atEnd() const Q_DECL_OVERRIDE;

    QProcess *process = NULL;
};

DDeviceDiskInfoPrivate::DDeviceDiskInfoPrivate(DDeviceDiskInfo *qq)
    : DDiskInfoPrivate(qq)
{

}

void DDeviceDiskInfoPrivate::init(const QJsonObject &obj)
{
    name = obj.value("name").toString();
    kname = obj.value("kname").toString();
    sizeDisplay = obj.value("size").toString();
    typeName = obj.value("type").toString();

    if (typeName == "part") {
        type = DDiskInfo::Part;
    } else if (typeName == "disk") {
        type = DDiskInfo::Disk;
    } else if (typeName == "loop") {
        if (name.length() == 5)
            type = DDiskInfo::Disk;
        else
            type = DDiskInfo::Part;
    }

    const QJsonArray &list = obj.value("children").toArray();

    for (const QJsonValue &part : list) {
        DDevicePartInfo info;

        info.init(part.toObject());
        children << info;
    }

    if (!obj.value("fstype").isNull()) {
        DDevicePartInfo info;

        info.init(obj);
        children << info;
        havePartitionTable = false;
    }

    ptTypeName = getPTName(Helper::getDeviceByName(name));

    if (ptTypeName == "dos")
        ptType = DDiskInfo::MBR;
    else if (ptTypeName == "gpt")
        ptType = DDiskInfo::GPT;
    else ptType = DDiskInfo::Unknow;
}

QString DDeviceDiskInfoPrivate::filePath() const
{
    return Helper::getDeviceByName(name);
}

void DDeviceDiskInfoPrivate::refresh()
{
    *q = DDeviceDiskInfo(name);
}

bool DDeviceDiskInfoPrivate::hasScope(DDiskInfo::DataScope scope, DDiskInfo::ScopeMode mode) const
{
    return (scope == DDiskInfo::Headgear || DDiskInfo::PartitionTable) ? type == DDiskInfo::Disk : true;
}

bool DDeviceDiskInfoPrivate::openDataStream(int index)
{
    if (process) {
        process->deleteLater();
    }

    process = new QProcess();

    if (currentScope == DDiskInfo::Headgear) {
        if (type != DDiskInfo::Disk) {
            dCError("%s not is disk", filePath().toUtf8().constData());

            return false;
        }

        if (!havePartitionTable)
            return true;

        if (Q_LIKELY(!children.isEmpty())) {
            quint64 first_part_start = children.first().sizeStart();

            if (currentMode == DDiskInfo::Read) {
                if (first_part_start >= 1048576) {
                    process->start(QStringLiteral("dd if=%1 bs=512 count=2048").arg(filePath()), QIODevice::ReadOnly);
                }
            } else {
                process->start(QStringLiteral("dd of=%1 bs=512").arg(filePath()), QIODevice::WriteOnly);
            }

            process->waitForStarted();
        }
    } else if (currentScope == DDiskInfo::PartitionTable) {
        if (type != DDiskInfo::Disk) {
            dCError("%s not is disk", filePath().toUtf8().constData());

            return false;
        }

        if (!havePartitionTable)
            return true;

        if (currentMode == DDiskInfo::Read)
            process->start(QStringLiteral("sfdisk -d %1").arg(filePath()), QIODevice::ReadOnly);
        else
            process->start(QStringLiteral("sfdisk %1").arg(filePath()), QIODevice::WriteOnly);

        process->waitForStarted();
    } else {
        return false;
    }

    return true;
}

void DDeviceDiskInfoPrivate::closeDataStream()
{
    if (process)
        process->deleteLater();
}

qint64 DDeviceDiskInfoPrivate::read(char *data, qint64 maxSize)
{
    if (!process)
        return -1;

    process->waitForReadyRead(-1);

    return process->read(data, maxSize);
}

qint64 DDeviceDiskInfoPrivate::write(const char *data, qint64 maxSize)
{
    if (!process)
        return -1;

    process->waitForBytesWritten(-1);

    return process->write(data, maxSize);
}

bool DDeviceDiskInfoPrivate::atEnd() const
{
    if (!process)
        return true;

    process->waitForReadyRead(-1);

    return process->atEnd();
}

DDeviceDiskInfo::DDeviceDiskInfo()
    : DDiskInfo(new DDeviceDiskInfoPrivate(this))
{

}

DDeviceDiskInfo::DDeviceDiskInfo(const QString &name)
    : DDiskInfo(new DDeviceDiskInfoPrivate(this))
{
    const QJsonArray &block_devices = Helper::getBlockDevices(Helper::getDeviceByName(name));

    if (!block_devices.isEmpty())
        d_func()->init(block_devices.first().toObject());
}

QList<DDeviceDiskInfo> DDeviceDiskInfo::localeDiskList()
{
    const QJsonArray &block_devices = Helper::getBlockDevices();

    QList<DDeviceDiskInfo> list;

    for (const QJsonValue &value : block_devices) {
        DDeviceDiskInfo info;

        info.d_func()->init(value.toObject());
        list << info;
    }

    return list;
}
