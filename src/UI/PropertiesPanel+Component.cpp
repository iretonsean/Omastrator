#include "UI/PropertiesPanel.h"
#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QToolButton>
#include <QVBoxLayout>

PanelSection *PropertiesPanel::componentSection()
{
    m_component = new PanelSection(QStringLiteral("Component"), QStringLiteral("component"), this);
    m_componentRows = new QWidget(m_component);
    auto *rows = new QVBoxLayout(m_componentRows);
    rows->setContentsMargins(0, 0, 0, 0);
    m_component->body->addWidget(m_componentRows);
    return m_component;
}

void PropertiesPanel::synchronizeComponent()
{
    QLayout *rows = m_componentRows->layout();
    while (QLayoutItem *child = rows->takeAt(0)) {
        delete child->widget();
        delete child;
    }
    if (!m_session.hasDocument())
        return;
    const VectorDocument &document = *m_session.document();
    const auto instances = m_session.selectedInstances();
    const std::optional<QUuid> masterId = m_session.selectedMaster();
    const VectorObject *master = masterId ? document.find(*masterId) : nullptr;
    if (!master || !master->component)
        return;
    const bool instance = !instances.empty();
    auto *heading = new QLabel(instance ? QStringLiteral("Instance of %1").arg(master->component->set)
                                        : QStringLiteral("%1 · %2 instances").arg(master->component->set).arg(Components::instancesOf(document, *masterId).size()),
                               m_componentRows);
    heading->setObjectName(QStringLiteral("componentHeading"));
    rows->addWidget(heading);
    if (instance) {
        for (const auto &[property, values] : Components::properties(document, master->component->set)) {
            auto *line = new QWidget(m_componentRows);
            auto *layout = new QHBoxLayout(line);
            layout->setContentsMargins(0, 0, 0, 0);
            layout->addWidget(new QLabel(property, line));
            auto *choice = new QComboBox(line);
            choice->setObjectName(QStringLiteral("propertiesVariant:") + property);
            choice->setAccessibleName(property);
            choice->addItems(values);
            const auto current = master->component->variant.find(property);
            if (current != master->component->variant.end())
                choice->setCurrentText(current->second);
            const QString name = property;
            connect(choice, &QComboBox::textActivated, this, [this, name](const QString &value) { m_session.swapVariant(name, value); });
            layout->addWidget(choice, 1);
            rows->addWidget(line);
        }
        auto *buttons = new QWidget(m_componentRows);
        auto *layout = new QHBoxLayout(buttons);
        layout->setContentsMargins(0, 0, 0, 0);
        auto *reset = new QToolButton(buttons);
        reset->setText(QStringLiteral("Reset"));
        reset->setToolTip(QStringLiteral("Reset Overrides"));
        connect(reset, &QToolButton::clicked, this, [this] { m_session.resetOverrides(); });
        auto *detach = new QToolButton(buttons);
        detach->setObjectName(QStringLiteral("propertiesDetach"));
        detach->setText(QStringLiteral("Detach"));
        detach->setToolTip(QStringLiteral("Detach Instance"));
        connect(detach, &QToolButton::clicked, this, [this] { m_session.detachInstances(); });
        layout->addStretch();
        layout->addWidget(reset);
        layout->addWidget(detach);
        rows->addWidget(buttons);
    }
}
