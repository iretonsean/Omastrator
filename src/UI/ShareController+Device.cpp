#include "UI/ShareController.h"
#include "UI/ProjectWorkspace.h"
#include <QSettings>

namespace {
const QString deviceAddressKey = QStringLiteral("shareDeviceAddress");
const QString deviceNameKey = QStringLiteral("shareDeviceName");
const QString deviceFormatKey = QStringLiteral("shareDeviceFormat");
}

void ShareController::connectDevice()
{
    connect(&m_device, &DeviceSend::stageChanged, this, [this] {
        if (m_device.sending())
            setNotice(Notice::Kind::progress, m_device.stageText(), m_jobDetail);
    });
    connect(&m_device, &DeviceSend::finished, this, [this](bool ok) {
        m_folder.reset();
        if (!ok) {
            setNotice(Notice::Kind::failed, m_device.failure());
            return;
        }
        setNotice(Notice::Kind::shared, QStringLiteral("Sent to %1").arg(m_jobDeviceName), m_jobDetail);
    });
}

std::optional<DeviceSend::Device> ShareController::lastDevice() const
{
    const QSettings settings;
    DeviceSend::Device device;
    device.address = settings.value(deviceAddressKey).toString();
    device.name = settings.value(deviceNameKey).toString();
    if (device.address.isEmpty() && device.name.isEmpty())
        return std::nullopt;
    return device;
}

Share::Format ShareController::deviceFormat() const
{
    return Share::parseFormat(QSettings().value(deviceFormatKey).toString()).value_or(Share::Format::png);
}

void ShareController::setDeviceFormat(Share::Format format)
{
    QSettings().setValue(deviceFormatKey, Share::suffix(format));
}

void ShareController::cancel()
{
    if (m_device.sending())
        m_device.cancel();
    else
        m_job.cancel();
}

QString ShareController::sendToDevice(const DeviceSend::Device &device, Share::Format format)
{
    if (running())
        return QStringLiteral("A share is already under way.");
    if (!hasDocument())
        return QStringLiteral("Open a document to send.");
    if (device.address.isEmpty())
        return QStringLiteral("Choose a device to send to.");
    const ProjectTab &tab = m_workspace.current();
    QString file;
    if (const QString failure = render(format, Share::safeName(tab.title()), &file); !failure.isEmpty())
        return failure;
    QString scope = scopeText();
    scope[0] = scope[0].toUpper();
    m_jobDetail = QStringLiteral("%1 as %2.").arg(scope, Share::label(format));
    m_jobDeviceName = device.label();
    // Remembered on choosing, as omadrop does: it's the device they meant whether or not this one gets through.
    QSettings settings;
    settings.setValue(deviceAddressKey, device.address);
    settings.setValue(deviceNameKey, device.name);
    setDeviceFormat(format);
    m_device.send(file, device);
    if (!m_device.sending()) {
        // It couldn't start (omdrop isn't installed): finished() has already said so.
        return {};
    }
    setNotice(Notice::Kind::progress, m_device.stageText(), m_jobDetail);
    return {};
}
