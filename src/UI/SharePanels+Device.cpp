#include "UI/ShareController.h"
#include "UI/SharePanels.h"
#include <QComboBox>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

DevicePopover::DevicePopover(ShareController &share, QWidget *parent)
    : QFrame(parent), m_share(share), m_scope(new QLabel(this)), m_format(new QComboBox(this)), m_device(new QComboBox(this)),
      m_status(new QLabel(this)), m_refresh(new QPushButton(QStringLiteral("Look Again"), this)), m_go(new QPushButton(QStringLiteral("Send"), this))
{
    setObjectName(QStringLiteral("deviceSendPopover"));
    setWindowFlags(Qt::Popup);
    setAttribute(Qt::WA_DeleteOnClose);
    setFrameShape(QFrame::StyledPanel);
    setAutoFillBackground(true);
    setFixedWidth(340);
    m_scope->setObjectName(QStringLiteral("deviceScope"));
    m_scope->setWordWrap(true);
    m_scope->setTextFormat(Qt::PlainText);
    m_status->setObjectName(QStringLiteral("deviceStatus"));
    m_status->setWordWrap(true);
    m_status->setTextFormat(Qt::PlainText);
    m_status->setForegroundRole(QPalette::PlaceholderText);
    m_format->setObjectName(QStringLiteral("deviceFormat"));
    m_format->setAccessibleName(QStringLiteral("Format"));
    for (Share::Format format : {Share::Format::png, Share::Format::pdf, Share::Format::svg})
        m_format->addItem(Share::label(format), Share::suffix(format));
    m_format->setCurrentIndex(m_format->findData(Share::suffix(m_share.deviceFormat())));
    m_device->setObjectName(QStringLiteral("deviceList"));
    m_device->setAccessibleName(QStringLiteral("Device"));
    m_refresh->setObjectName(QStringLiteral("deviceRefresh"));
    m_refresh->setAutoDefault(false);
    m_go->setObjectName(QStringLiteral("deviceGo"));
    m_go->setAutoDefault(false);
    m_go->setDefault(true);

    auto *column = new QVBoxLayout(this);
    column->setContentsMargins(14, 12, 14, 12);
    column->setSpacing(8);
    auto *title = new QLabel(QStringLiteral("Send to a Device"), this);
    title->setObjectName(QStringLiteral("deviceTitle"));
    QFont bold = title->font();
    bold.setBold(true);
    title->setFont(bold);
    column->addWidget(title);
    column->addWidget(m_scope);
    auto *form = new QGridLayout;
    form->setHorizontalSpacing(10);
    form->addWidget(new QLabel(QStringLiteral("Format"), this), 0, 0);
    form->addWidget(m_format, 0, 1);
    form->addWidget(new QLabel(QStringLiteral("To"), this), 1, 0);
    form->addWidget(m_device, 1, 1);
    form->setColumnStretch(1, 1);
    column->addLayout(form);
    column->addWidget(m_status);
    auto *buttons = new QHBoxLayout;
    buttons->addWidget(m_refresh);
    buttons->addStretch();
    buttons->addWidget(m_go);
    column->addLayout(buttons);

    connect(m_format, &QComboBox::activated, this, [this] { m_share.setDeviceFormat(Share::parseFormat(m_format->currentData().toString()).value_or(Share::Format::png)); });
    connect(m_refresh, &QPushButton::clicked, this, [this] { m_share.device().refresh(); });
    connect(m_go, &QPushButton::clicked, this, [this] {
        const QString address = m_device->currentData().toString();
        for (const DeviceSend::Device &each : m_share.device().devices()) {
            if (each.address != address)
                continue;
            const QString failure = m_share.sendToDevice(each, Share::parseFormat(m_format->currentData().toString()).value_or(Share::Format::png));
            if (!failure.isEmpty()) {
                m_status->setText(failure);
                m_status->show();
                adjustSize();
                return;
            }
            close();
            return;
        }
    });
    DeviceSend &device = m_share.device();
    connect(&device, &DeviceSend::devicesChanged, this, &DevicePopover::synchronize);
    connect(&device, &DeviceSend::searchChanged, this, &DevicePopover::synchronize);
    connect(&m_share, &ShareController::changed, this, &DevicePopover::synchronize);
    synchronize();
}

DevicePopover::~DevicePopover()
{
    // A name lookup transmits for as long as it runs, so closing the popover stops it.
    m_share.device().stopSearching();
}

// The remembered device, matched by its address and else its name, else the first.
void DevicePopover::selectDevice()
{
    const std::vector<DeviceSend::Device> &devices = m_share.device().devices();
    const std::optional<DeviceSend::Device> last = m_share.lastDevice();
    int at = -1;
    for (size_t i = 0; last && i < devices.size() && at < 0; ++i) {
        if ((!last->address.isEmpty() && devices[i].address == last->address) || (!last->name.isEmpty() && devices[i].name == last->name))
            at = int(i);
    }
    m_device->setCurrentIndex(at >= 0 ? at : 0);
}

void DevicePopover::synchronize()
{
    const DeviceSend &device = m_share.device();
    const bool blocked = m_device->blockSignals(true);
    const QString kept = m_device->currentData().toString();
    m_device->clear();
    for (const DeviceSend::Device &each : device.devices())
        m_device->addItem(each.label(), each.address);
    if (const int back = m_device->findData(kept); back >= 0 && !kept.isEmpty())
        m_device->setCurrentIndex(back);
    else
        selectDevice();
    m_device->blockSignals(blocked);
    m_scope->setText(m_share.hasDocument() ? QStringLiteral("Sends %1 by AirDrop.").arg(m_share.scopeText()) : QStringLiteral("Open a document to send it."));
    const QString status = device.searching() ? device.searchText() : device.problem();
    m_status->setText(status);
    m_status->setVisible(!status.isEmpty());
    m_device->setEnabled(m_device->count() > 0);
    m_refresh->setEnabled(!device.searching() && !m_share.running());
    m_go->setEnabled(m_device->count() > 0 && !m_share.running() && m_share.hasDocument());
    adjustSize();
}
