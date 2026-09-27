#include "UI/DesignSystemPanel.h"
#include "Document/EditorSession.h"
#include "Live/Deploy.h"
#include <QColorDialog>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFontComboBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QMenu>
#include <QPainter>
#include <QPixmap>
#include <QSpinBox>
#include <QTabWidget>
#include <QToolButton>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace {
constexpr int idRole = Qt::UserRole;
constexpr int kindRole = Qt::UserRole + 1;

QIcon swatchIcon(const QColor &color)
{
    QPixmap pixmap(14, 14);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(QColor(128, 128, 128, 160));
    painter.setBrush(color);
    painter.drawRoundedRect(QRectF(0.5, 0.5, 13, 13), 3, 3);
    return QIcon(pixmap);
}

QToolButton *smallButton(const QString &text, const QString &tip, QWidget *parent)
{
    auto *button = new QToolButton(parent);
    button->setText(text);
    button->setToolTip(tip);
    button->setAccessibleName(tip);
    button->setAutoRaise(true);
    return button;
}

// Milestones, once per install (docs/HUMOR.md).
const QStringList firstComponentLines{
    QStringLiteral("Change it once and every copy follows. The client will still ask for a one-off."),
};

// A small form for a type token.
std::optional<TypeValue> askType(const TypeValue &start, QWidget *parent)
{
    QDialog dialog(parent);
    dialog.setWindowTitle(QStringLiteral("Type Token"));
    auto *form = new QFormLayout(&dialog);
    auto *family = new QFontComboBox(&dialog);
    family->setCurrentFont(QFont(start.family));
    auto *size = new QDoubleSpinBox(&dialog);
    size->setRange(1, 999);
    size->setValue(start.size);
    auto *weight = new QSpinBox(&dialog);
    weight->setRange(100, 900);
    weight->setSingleStep(100);
    weight->setValue(start.weight);
    auto *line = new QDoubleSpinBox(&dialog);
    line->setRange(0, 999);
    line->setSpecialValueText(QStringLiteral("Auto"));
    line->setValue(start.lineHeight.value_or(0));
    auto *tracking = new QDoubleSpinBox(&dialog);
    tracking->setRange(-500, 1000);
    tracking->setValue(start.tracking);
    form->addRow(QStringLiteral("Family"), family);
    form->addRow(QStringLiteral("Size"), size);
    form->addRow(QStringLiteral("Weight"), weight);
    form->addRow(QStringLiteral("Line height"), line);
    form->addRow(QStringLiteral("Tracking"), tracking);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    form->addRow(buttons);
    if (dialog.exec() != QDialog::Accepted)
        return std::nullopt;
    TypeValue type{family->currentFont().family(), weight->value(), size->value(), std::nullopt, tracking->value()};
    if (line->value() > 0)
        type.lineHeight = line->value();
    return type;
}

// Asks for a token's value in its kind; nullopt when cancelled.
std::optional<TokenValue> askValue(const DesignToken &token, const TokenValue &start, QWidget *parent)
{
    TokenValue value = start;
    switch (token.kind) {
    case TokenKind::color: {
        const QColor picked = QColorDialog::getColor(start.color, parent, token.name, QColorDialog::ShowAlphaChannel);
        if (!picked.isValid())
            return std::nullopt;
        value.color = picked;
        return value;
    }
    case TokenKind::spacing:
    case TokenKind::radius: {
        bool ok = false;
        const double number = QInputDialog::getDouble(parent, token.name, QStringLiteral("Points"), start.number, 0, 10000, 2, &ok);
        if (!ok)
            return std::nullopt;
        value.number = number;
        return value;
    }
    case TokenKind::type: {
        const auto type = askType(start.type, parent);
        if (!type)
            return std::nullopt;
        value.type = *type;
        return value;
    }
    case TokenKind::shadow: {
        bool ok = false;
        const QString css = QInputDialog::getText(parent, token.name, QStringLiteral("CSS box-shadow"), QLineEdit::Normal, start.shadow.css(), &ok);
        const auto shadow = ShadowValue::fromCss(css);
        if (!ok || !shadow)
            return std::nullopt;
        value.shadow = *shadow;
        return value;
    }
    }
    return std::nullopt;
}
}

DesignSystemPanel::DesignSystemPanel(std::function<EditorSession *()> session, QWidget *parent)
    : QWidget(parent), m_sessionOf(std::move(session))
{
    setObjectName(QStringLiteral("designSystemPanel"));
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(8, 8, 8, 8);
    m_tabs = new QTabWidget(this);
    m_tabs->setDocumentMode(true);
    m_tabs->addTab(buildTokens(), QStringLiteral("Tokens"));
    m_tabs->addTab(buildComponents(), QStringLiteral("Components"));
    m_tabs->addTab(buildSources(), QStringLiteral("Sources"));
    layout->addWidget(m_tabs, 1);
    m_message = new QLabel(this);
    m_message->setObjectName(QStringLiteral("designSystemMessage"));
    m_message->setWordWrap(true);
    layout->addWidget(m_message);
    setMinimumSize(300, 420);
    follow();
}

EditorSession *DesignSystemPanel::session() const
{
    return m_sessionOf ? m_sessionOf() : nullptr;
}

QString DesignSystemPanel::message() const
{
    return m_message->text();
}

QString DesignSystemPanel::say(const QString &text)
{
    m_message->setText(text);
    return text;
}

QString DesignSystemPanel::documentName() const
{
    return QStringLiteral("the front document");
}

void DesignSystemPanel::follow()
{
    EditorSession *now = session();
    if (now != m_session) {
        disconnect(m_watch);
        m_session = now;
        if (now)
            m_watch = connect(now, &EditorSession::changed, this, &DesignSystemPanel::rebuild);
    }
    rebuild();
}

void DesignSystemPanel::rebuild()
{
    rebuildTokens();
    rebuildComponents();
}

QWidget *DesignSystemPanel::buildTokens()
{
    auto *page = new QWidget(this);
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 6, 0, 0);
    m_mode = new QComboBox(page);
    m_mode->setObjectName(QStringLiteral("tokenMode"));
    m_mode->setAccessibleName(QStringLiteral("Mode"));
    connect(m_mode, &QComboBox::currentTextChanged, this, [this](const QString &mode) {
        if (!m_rebuilding && session())
            session()->setTokenMode(mode);
    });
    layout->addWidget(m_mode);
    m_tokens = new QTreeWidget(page);
    m_tokens->setObjectName(QStringLiteral("tokenList"));
    m_tokens->setHeaderLabels({QStringLiteral("Token"), QStringLiteral("Value")});
    m_tokens->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_tokens->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_tokens, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem *item) { editToken(item); });
    connect(m_tokens, &QTreeWidget::customContextMenuRequested, this, &DesignSystemPanel::tokenMenu);
    layout->addWidget(m_tokens, 1);
    auto *row = new QHBoxLayout;
    QToolButton *add = smallButton(QStringLiteral("+"), QStringLiteral("New Token"), page);
    auto *menu = new QMenu(add);
    for (const TokenKind kind : {TokenKind::color, TokenKind::type, TokenKind::spacing, TokenKind::radius, TokenKind::shadow})
        menu->addAction(title(kind), this, [this, kind] { addToken(int(kind)); });
    menu->addSeparator();
    menu->addAction(QStringLiteral("Add Mode…"), this, [this] {
        bool ok = false;
        const QString mode = QInputDialog::getText(this, QStringLiteral("Add Mode"), QStringLiteral("Mode name"), QLineEdit::Normal,
                                                   QStringLiteral("dark"), &ok);
        if (ok && session())
            session()->addTokenMode(mode);
    });
    add->setMenu(menu);
    add->setPopupMode(QToolButton::InstantPopup);
    QToolButton *apply = smallButton(QStringLiteral("Apply"), QStringLiteral("Apply to Selection"), page);
    apply->setObjectName(QStringLiteral("applyToken"));
    connect(apply, &QToolButton::clicked, this, [this] {
        QTreeWidgetItem *item = m_tokens->currentItem();
        if (item && session() && !item->data(0, idRole).toString().isEmpty()) {
            const QString failed = session()->applyToken(item->data(0, idRole).toString());
            say(failed);
        }
    });
    row->addWidget(add);
    row->addStretch();
    row->addWidget(apply);
    layout->addLayout(row);
    return page;
}

void DesignSystemPanel::rebuildTokens()
{
    m_rebuilding = true;
    const QString current = m_tokens->currentItem() ? m_tokens->currentItem()->data(0, idRole).toString() : QString();
    m_tokens->clear();
    EditorSession *s = session();
    const VectorDocument *document = s && s->hasDocument() ? &*s->document() : nullptr;
    m_mode->clear();
    m_mode->setVisible(document && !document->tokenModes.isEmpty());
    if (document) {
        m_mode->addItems(document->tokenModes);
        m_mode->setCurrentText(document->tokenMode);
        for (const TokenKind kind : {TokenKind::color, TokenKind::type, TokenKind::spacing, TokenKind::radius, TokenKind::shadow}) {
            QTreeWidgetItem *group = nullptr;
            for (const DesignToken &token : document->tokens) {
                if (token.kind != kind)
                    continue;
                if (!group) {
                    group = new QTreeWidgetItem(m_tokens, {title(kind)});
                    group->setFlags(Qt::ItemIsEnabled);
                    group->setExpanded(true);
                }
                auto *row = new QTreeWidgetItem(group, {token.name, token.displayValue(document->tokenMode)});
                row->setData(0, idRole, token.id);
                row->setData(0, kindRole, int(kind));
                row->setToolTip(0, token.description.isEmpty() ? token.name : token.description);
                if (kind == TokenKind::color)
                    row->setIcon(0, swatchIcon(token.valueIn(document->tokenMode).color));
                if (token.id == current)
                    m_tokens->setCurrentItem(row);
            }
            if (group)
                group->setExpanded(true);
        }
        if (document->tokens.empty()) {
            auto *empty = new QTreeWidgetItem(m_tokens, {QStringLiteral("No tokens yet. Add one, or pull them from code, a site or a theme.")});
            empty->setFlags(Qt::ItemIsEnabled);
        }
    }
    m_rebuilding = false;
}

void DesignSystemPanel::addToken(int kind)
{
    EditorSession *s = session();
    if (!s || !s->hasDocument())
        return;
    const TokenKind which = TokenKind(kind);
    DesignToken token;
    switch (which) {
    case TokenKind::color: {
        const auto colours = s->selectionColors();
        token = DesignToken::color(QStringLiteral("color/new"), colours.empty() ? s->defaultFill().color : colours.front());
        break;
    }
    case TokenKind::type:
        token = DesignToken::typography(QStringLiteral("text/new"), TypeValue{s->shownText().family, 400, s->shownText().size, s->shownText().leading, s->shownText().tracking});
        break;
    case TokenKind::spacing:
        token = DesignToken::number(which, QStringLiteral("spacing/new"), 8);
        break;
    case TokenKind::radius:
        token = DesignToken::number(which, QStringLiteral("radius/new"), 8);
        break;
    case TokenKind::shadow:
        token = DesignToken::shadowToken(QStringLiteral("shadow/new"), {});
        break;
    }
    bool ok = false;
    const QString name = QInputDialog::getText(this, QStringLiteral("New %1 Token").arg(title(which)), QStringLiteral("Name"), QLineEdit::Normal, token.name, &ok);
    if (!ok || name.trimmed().isEmpty())
        return;
    token.name = name.trimmed();
    s->addToken(token);
}

void DesignSystemPanel::editToken(QTreeWidgetItem *item)
{
    EditorSession *s = session();
    if (!s || !item || item->data(0, idRole).toString().isEmpty())
        return;
    const DesignToken *token = s->token(item->data(0, idRole).toString());
    if (!token)
        return;
    const DesignToken copy = *token;
    const QString mode = s->document()->tokenMode;
    if (const auto value = askValue(copy, copy.valueIn(mode), this))
        s->setTokenValue(copy.id, *value, mode);
}

void DesignSystemPanel::tokenMenu(const QPoint &at)
{
    QTreeWidgetItem *item = m_tokens->itemAt(at);
    EditorSession *s = session();
    if (!item || !s || item->data(0, idRole).toString().isEmpty())
        return;
    const QString id = item->data(0, idRole).toString();
    const TokenKind kind = TokenKind(item->data(0, kindRole).toInt());
    QMenu menu(this);
    const auto target = [&](const QString &label, const QString &key) {
        menu.addAction(label, this, [this, s, id, key] { say(s->applyToken(id, key)); })->setEnabled(s->hasSelection());
    };
    if (kind == TokenKind::color) {
        target(QStringLiteral("Apply to Fill"), QStringLiteral("fill"));
        target(QStringLiteral("Apply to Stroke"), QStringLiteral("stroke"));
    } else if (kind == TokenKind::spacing) {
        target(QStringLiteral("Apply as Horizontal Gap"), TokenRef::gapX);
        target(QStringLiteral("Apply as Vertical Gap"), TokenRef::gapY);
        target(QStringLiteral("Apply to Stroke Weight"), TokenRef::strokeWidth);
    } else if (kind == TokenKind::radius) {
        target(QStringLiteral("Apply to Corners"), TokenRef::radius);
    } else if (kind == TokenKind::type) {
        target(QStringLiteral("Apply to Type"), TokenRef::type);
        QMenu *styles = menu.addMenu(QStringLiteral("Link Text Style"));
        for (const TextStyle &style : s->document()->textStyles)
            styles->addAction(style.name, this, [s, style, id] { s->linkTextStyle(style.id, id); });
        styles->setEnabled(!s->document()->textStyles.empty());
    }
    menu.addSeparator();
    menu.addAction(QStringLiteral("Edit Value…"), this, [this, item] { editToken(item); });
    menu.addAction(QStringLiteral("Rename…"), this, [this, s, id] {
        bool ok = false;
        const QString name = QInputDialog::getText(this, QStringLiteral("Rename Token"), QStringLiteral("Name"), QLineEdit::Normal, s->token(id)->name, &ok);
        if (ok)
            s->renameToken(id, name);
    });
    menu.addAction(QStringLiteral("Delete Token"), this, [s, id] { s->deleteToken(id); });
    menu.exec(m_tokens->viewport()->mapToGlobal(at));
}

QWidget *DesignSystemPanel::buildComponents()
{
    auto *page = new QWidget(this);
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 6, 0, 0);
    m_components = new QTreeWidget(page);
    m_components->setObjectName(QStringLiteral("componentList"));
    m_components->setHeaderLabels({QStringLiteral("Component"), QStringLiteral("Instances")});
    m_components->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_components->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_components, &QTreeWidget::customContextMenuRequested, this, &DesignSystemPanel::componentMenu);
    connect(m_components, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem *item) {
        const QUuid master = QUuid::fromString(item->data(0, idRole).toString());
        if (session() && !master.isNull())
            session()->placeInstance(master);
    });
    layout->addWidget(m_components, 1);
    m_instanceBox = new QWidget(page);
    m_instanceBox->setObjectName(QStringLiteral("instanceBox"));
    new QVBoxLayout(m_instanceBox);
    m_instanceBox->layout()->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(m_instanceBox);
    auto *row = new QHBoxLayout;
    QToolButton *make = smallButton(QStringLiteral("Make Component"), QStringLiteral("Make Component"), page);
    make->setObjectName(QStringLiteral("makeComponent"));
    connect(make, &QToolButton::clicked, this, [this] {
        EditorSession *s = session();
        if (!s || !s->hasSelection()) {
            say(QStringLiteral("Select what the component is made of."));
            return;
        }
        s->makeComponent();
        if (const QString line = Deploy::dryLine(firstComponentLines); !line.isEmpty())
            say(QStringLiteral("Component made. ") + line);
        else
            say(QString());
    });
    QToolButton *place = smallButton(QStringLiteral("Place"), QStringLiteral("Place Instance"), page);
    connect(place, &QToolButton::clicked, this, [this] {
        QTreeWidgetItem *item = m_components->currentItem();
        const QUuid master = item ? QUuid::fromString(item->data(0, idRole).toString()) : QUuid();
        if (session() && !master.isNull())
            session()->placeInstance(master);
    });
    row->addWidget(make);
    row->addStretch();
    row->addWidget(place);
    layout->addLayout(row);
    return page;
}

void DesignSystemPanel::rebuildComponents()
{
    m_components->clear();
    QLayout *box = m_instanceBox->layout();
    while (QLayoutItem *child = box->takeAt(0)) {
        delete child->widget();
        delete child;
    }
    EditorSession *s = session();
    if (!s || !s->hasDocument())
        return;
    const VectorDocument &document = *s->document();
    QStringList sets;
    for (const QUuid &id : Components::masters(document)) {
        if (!sets.contains(document.find(id)->component->set))
            sets.append(document.find(id)->component->set);
    }
    for (const QString &set : sets) {
        const auto variants = Components::variantsOf(document, set);
        auto *group = new QTreeWidgetItem(m_components, {set});
        group->setData(0, idRole, variants.front().toString(QUuid::WithoutBraces));
        int instances = 0;
        for (const QUuid &variant : variants) {
            const int count = int(Components::instancesOf(document, variant).size());
            instances += count;
            if (variants.size() > 1) {
                auto *row = new QTreeWidgetItem(group, {Components::variantLabel(document.find(variant)->component->variant), QString::number(count)});
                row->setData(0, idRole, variant.toString(QUuid::WithoutBraces));
            }
        }
        group->setText(1, QString::number(instances));
        group->setExpanded(true);
    }
    if (sets.isEmpty()) {
        auto *empty = new QTreeWidgetItem(m_components, {QStringLiteral("No components yet. Select some art and make one.")});
        empty->setFlags(Qt::ItemIsEnabled);
    }
    // The selected instance: its variant's properties, Detach and Reset.
    const auto instances = s->selectedInstances();
    if (instances.empty())
        return;
    const VectorObject *master = document.find(document.find(instances.front())->instance->master);
    if (!master || !master->component)
        return;
    for (const auto &[property, values] : Components::properties(document, master->component->set)) {
        auto *line = new QWidget(m_instanceBox);
        auto *lineLayout = new QHBoxLayout(line);
        lineLayout->setContentsMargins(0, 0, 0, 0);
        lineLayout->addWidget(new QLabel(property, line));
        auto *choice = new QComboBox(line);
        choice->setObjectName(QStringLiteral("variant:") + property);
        choice->setAccessibleName(property);
        choice->addItems(values);
        const auto current = master->component->variant.find(property);
        if (current != master->component->variant.end())
            choice->setCurrentText(current->second);
        const QString name = property;
        connect(choice, &QComboBox::textActivated, this, [this, name](const QString &value) {
            if (session())
                say(session()->swapVariant(name, value));
        });
        lineLayout->addWidget(choice, 1);
        box->addWidget(line);
    }
    auto *buttons = new QWidget(m_instanceBox);
    auto *buttonsLayout = new QHBoxLayout(buttons);
    buttonsLayout->setContentsMargins(0, 0, 0, 0);
    QToolButton *detach = smallButton(QStringLiteral("Detach"), QStringLiteral("Detach Instance"), buttons);
    connect(detach, &QToolButton::clicked, this, [this] {
        if (session())
            session()->detachInstances();
    });
    QToolButton *reset = smallButton(QStringLiteral("Reset"), QStringLiteral("Reset Overrides"), buttons);
    connect(reset, &QToolButton::clicked, this, [this] {
        if (session())
            session()->resetOverrides();
    });
    buttonsLayout->addWidget(new QLabel(QStringLiteral("Instance of %1").arg(master->component->set), buttons), 1);
    buttonsLayout->addWidget(reset);
    buttonsLayout->addWidget(detach);
    box->addWidget(buttons);
}

void DesignSystemPanel::componentMenu(const QPoint &at)
{
    QTreeWidgetItem *item = m_components->itemAt(at);
    EditorSession *s = session();
    const QUuid master = item ? QUuid::fromString(item->data(0, idRole).toString()) : QUuid();
    if (!s || master.isNull() || !s->document()->find(master))
        return;
    const QString set = s->document()->find(master)->component->set;
    QMenu menu(this);
    menu.addAction(QStringLiteral("Place Instance"), this, [s, master] { s->placeInstance(master); });
    menu.addAction(QStringLiteral("Select Main Component"), this, [s, master] { s->select({master}); });
    menu.addAction(QStringLiteral("Add Variant…"), this, [this, s, master] {
        bool ok = false;
        const QString text = QInputDialog::getText(this, QStringLiteral("Add Variant"), QStringLiteral("Property = value (size = lg)"), QLineEdit::Normal,
                                                   QStringLiteral("state = hover"), &ok);
        if (!ok || !text.contains(QLatin1Char('=')))
            return;
        s->addVariant(master, text.section(QLatin1Char('='), 0, 0).trimmed(), text.section(QLatin1Char('='), 1).trimmed());
    });
    menu.addAction(QStringLiteral("Rename…"), this, [this, s, set] {
        bool ok = false;
        const QString name = QInputDialog::getText(this, QStringLiteral("Rename Component"), QStringLiteral("Name"), QLineEdit::Normal, set, &ok);
        if (ok)
            s->renameComponent(set, name);
    });
    menu.exec(m_components->viewport()->mapToGlobal(at));
}
