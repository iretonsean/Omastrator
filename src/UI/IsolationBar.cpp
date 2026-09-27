#include "UI/IsolationBar.h"
#include <QHBoxLayout>
#include <QLabel>
#include <QToolButton>

IsolationBar::IsolationBar(EditorSession &session, QWidget *parent) : QWidget(parent), m_session(session), m_row(new QHBoxLayout(this))
{
    setObjectName(QStringLiteral("isolationBar"));
    setAccessibleName(QStringLiteral("Isolation mode"));
    setAutoFillBackground(true);
    setBackgroundRole(QPalette::AlternateBase);
    m_row->setContentsMargins(8, 2, 8, 2);
    m_row->setSpacing(2);
    connect(&m_session, &EditorSession::changed, this, &IsolationBar::rebuild);
    hide();
    rebuild();
}

void IsolationBar::rebuild()
{
    const std::vector<QUuid> &isolation = m_session.isolation();
    setVisible(!isolation.empty() && m_session.hasDocument());
    if (isolation == m_shown)
        return;
    m_shown = isolation;
    // Later: the crumb clicked may be the one asking.
    while (QLayoutItem *item = m_row->takeAt(0)) {
        if (QWidget *widget = item->widget()) {
            widget->hide();
            widget->deleteLater();
        }
        delete item;
    }
    if (isolation.empty() || !m_session.document())
        return;
    const VectorDocument &document = *m_session.document();
    const auto crumb = [this](const QString &name, const QString &text, int depth, bool current) {
        auto *button = new QToolButton(this);
        button->setObjectName(name);
        button->setText(text);
        button->setAutoRaise(true);
        button->setEnabled(!current);
        if (!current)
            connect(button, &QToolButton::clicked, this, [this, depth] { m_session.exitIsolation(depth); });
        m_row->addWidget(button);
    };
    auto *back = new QToolButton(this);
    back->setObjectName(QStringLiteral("isolationBack"));
    back->setArrowType(Qt::LeftArrow);
    back->setAutoRaise(true);
    back->setToolTip(QStringLiteral("Exit one level (Esc leaves isolation mode)"));
    back->setAccessibleName(QStringLiteral("Exit one level"));
    connect(back, &QToolButton::clicked, this, [this] { m_session.exitIsolation(int(m_session.isolation().size()) - 1); });
    m_row->addWidget(back);
    // The layer, then each group entered.
    const std::optional<QUuid> layer = document.layerOf(isolation.front());
    const VectorObject *layerObject = layer ? document.find(*layer) : nullptr;
    crumb(QStringLiteral("isolationCrumb0"), layerObject ? layerObject->name : QStringLiteral("Layer"), 0, false);
    for (size_t index = 0; index < isolation.size(); ++index) {
        auto *separator = new QLabel(QStringLiteral("›"), this);
        separator->setForegroundRole(QPalette::PlaceholderText);
        m_row->addWidget(separator);
        const VectorObject *group = document.find(isolation[index]);
        const QString name = group && !group->name.isEmpty() ? group->name : QStringLiteral("Group");
        crumb(QStringLiteral("isolationCrumb%1").arg(index + 1), name, int(index) + 1, index + 1 == isolation.size());
    }
    m_row->addStretch(1);
}
