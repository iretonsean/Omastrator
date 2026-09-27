#include "UI/CharacterSection.h"
#include "UI/NumberField.h"
#include "UI/PanelIcons.h"
#include "UI/ToolHeaderStyle.h"
#include <QEvent>
#include <QFontDatabase>
#include <QGridLayout>
#include <QLineEdit>
#include <QPainter>
#include <QSettings>
#include <QSignalBlocker>
#include <algorithm>
#include <cmath>

namespace {
const QString showMoreKey = QStringLiteral("properties/characterShowsMore");

QLabel *caption(const QString &text, QWidget *parent)
{
    auto *label = new QLabel(text, parent);
    label->setFont(ToolHeaderStyle::controlFont());
    label->setForegroundRole(QPalette::PlaceholderText);
    return label;
}

// One value across every text, or nothing when they differ.
template <typename Value> std::optional<Value> common(const std::vector<TextContent> &texts, const std::function<Value(const TextContent &)> &get)
{
    if (texts.empty())
        return std::nullopt;
    const Value first = get(texts.front());
    for (const TextContent &text : texts) {
        if (!(get(text) == first))
            return std::nullopt;
    }
    return first;
}

// The style the combo shows: the face itself, or the family's nearest when it lacks it.
QString shownStyle(const TextContent &text)
{
    const QStringList styles = QFontDatabase::styles(text.family);
    if (styles.isEmpty() || styles.contains(text.style))
        return text.style;
    return TextContent::styleFor(text.family, text.isBold() ? 700 : 400, text.isItalic());
}
}

QPixmap TypeAlignment::glyph(TextAlignment alignment, const QColor &ink, double ratio)
{
    // Four lines against one side, as text-align glyphs are.
    QPixmap pixmap(QSize(18, 18) * ratio);
    pixmap.setDevicePixelRatio(ratio);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(QPen(ink, 1.6, Qt::SolidLine, Qt::RoundCap));
    const bool justified = alignment == TextAlignment::justify || alignment == TextAlignment::justifyAll;
    for (int row = 0; row < 4; ++row) {
        const double length = justified ? (row == 3 && alignment == TextAlignment::justify ? 8 : 14) : row % 2 == 0 ? 14 : 9;
        const double y = 4 + row * 3.4;
        const double left = alignment == TextAlignment::center ? 9 - length / 2 : alignment == TextAlignment::right ? 16 - length : 2;
        painter.drawLine(QPointF(left, y), QPointF(left + length, y));
    }
    return pixmap;
}

QString TypeAlignment::name(TextAlignment alignment)
{
    switch (alignment) {
    case TextAlignment::center:
        return QStringLiteral("Align center");
    case TextAlignment::right:
        return QStringLiteral("Align right");
    case TextAlignment::justify:
        return QStringLiteral("Justify with last line aligned left");
    case TextAlignment::justifyAll:
        return QStringLiteral("Justify all lines");
    default:
        return QStringLiteral("Align left");
    }
}

CharacterSection::CharacterSection(EditorSession &session, QWidget *parent)
    : PanelSection(QStringLiteral("Character"), QStringLiteral("character"), parent), m_session(session), m_family(new QFontComboBox(this)),
      m_style(new QComboBox(this)), m_more(new QPushButton(this)), m_extra(new QWidget(this)), m_kerning(new QComboBox(m_extra)),
      m_case(new QComboBox(m_extra)), m_kind(new QComboBox(m_extra))
{
    m_family->setObjectName(QStringLiteral("characterFamily"));
    m_family->setToolTip(QStringLiteral("Font family"));
    m_family->setAccessibleName(QStringLiteral("Font family"));
    m_family->setMinimumWidth(60);
    m_family->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    connect(m_family, &QFontComboBox::currentFontChanged, this, [this](const QFont &font) {
        const QString family = font.family();
        // A new family keeps the nearest face to the current one.
        change([&family](TextContent &text) {
            if (text.family == family)
                return;
            const int weight = text.isBold() ? 700 : 400;
            const bool italic = text.isItalic();
            text.family = family;
            text.style = TextContent::styleFor(family, weight, italic);
        }, QStringLiteral("Font"));
    });
    m_style->setObjectName(QStringLiteral("characterStyle"));
    m_style->setToolTip(QStringLiteral("Font style"));
    m_style->setAccessibleName(QStringLiteral("Font style"));
    m_style->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    connect(m_style, &QComboBox::activated, this, [this](int index) {
        const QString style = m_style->itemText(index);
        change([&style](TextContent &text) { text.style = style; }, QStringLiteral("Font Style"));
    });
    m_size = number(QString(), QStringLiteral("pt"), QStringLiteral("characterSize"), QStringLiteral("Font Size"),
                    [](TextContent &text, double value) { text.size = value; }, [](const TextContent &text) { return text.size; });
    m_size->minimum = 0.1;
    m_size->maximum = 1296;
    m_size->setToolTip(QStringLiteral("Font size"));
    m_leading = number(QStringLiteral("Leading"), QString(), QStringLiteral("characterLeading"), QStringLiteral("Leading"),
                       [](TextContent &text, double value) { text.leading = value; }, [](const TextContent &text) { return text.effectiveLeading(); });
    m_leading->minimum = 0;
    m_leading->lengths = true;
    m_leading->setToolTip(QStringLiteral("Leading, in points, baseline to baseline. Empty is Auto: 120 % of the size"));
    m_leading->handle()->setToolTip(QStringLiteral("Leading. Drag to change: Shift for ten times, Alt for a tenth"));
    m_leading->cleared = [this] { change([](TextContent &text) { text.leading.reset(); }, QStringLiteral("Leading")); };
    m_tracking = number(QStringLiteral("Tracking"), QString(), QStringLiteral("characterTracking"), QStringLiteral("Tracking"),
                        [](TextContent &text, double value) { text.tracking = value; }, [](const TextContent &text) { return text.tracking; });
    m_tracking->step = 10;
    m_tracking->minimum = -1000;
    m_tracking->maximum = 10000;
    m_tracking->setToolTip(QStringLiteral("Tracking, in thousandths of an em"));
    m_tracking->handle()->setToolTip(QStringLiteral("Tracking, in thousandths of an em. Drag to change: Shift for ten times, Alt for a tenth"));

    auto *essentials = new QGridLayout;
    essentials->setHorizontalSpacing(8);
    essentials->setVerticalSpacing(6);
    essentials->addWidget(m_family, 0, 0, 1, 2);
    essentials->addWidget(m_style, 1, 0);
    essentials->addWidget(m_size, 1, 1);
    essentials->addWidget(m_leading, 2, 0);
    essentials->addWidget(m_tracking, 2, 1);
    essentials->setColumnStretch(0, 1);
    essentials->setColumnStretch(1, 1);
    body->addLayout(essentials);

    auto *alignments = new QHBoxLayout;
    alignments->setSpacing(2);
    static const std::array<const char *, 5> names{"characterAlignLeft", "characterAlignCenter", "characterAlignRight", "characterJustify",
                                                   "characterJustifyAll"};
    for (size_t index = 0; index < TypeAlignment::all.size(); ++index) {
        const TextAlignment alignment = TypeAlignment::all.at(index);
        m_alignments.at(index) = toggleButton(QString::fromLatin1(names.at(index)), QString(), TypeAlignment::name(alignment));
        connect(m_alignments.at(index), &QToolButton::clicked, this, [this, alignment] {
            change([alignment](TextContent &text) { text.alignment = alignment; }, QStringLiteral("Alignment"));
        });
        alignments->addWidget(m_alignments.at(index));
    }
    alignments->addStretch(1);
    m_more->setObjectName(QStringLiteral("characterMore"));
    m_more->setFlat(true);
    m_more->setFont(ToolHeaderStyle::controlFont());
    m_more->setCursor(Qt::PointingHandCursor);
    connect(m_more, &QPushButton::clicked, this, [this] { setShowsMore(!showsMore()); });
    alignments->addWidget(m_more);
    body->addLayout(alignments);

    // The rest: kerning, baseline shift, scales, case, decoration and area type.
    m_extra->setObjectName(QStringLiteral("characterExtra"));
    auto *extra = new QGridLayout(m_extra);
    extra->setContentsMargins(0, 0, 0, 0);
    extra->setHorizontalSpacing(8);
    extra->setVerticalSpacing(6);
    m_kerning->setObjectName(QStringLiteral("characterKerning"));
    m_kerning->setAccessibleName(QStringLiteral("Kerning"));
    m_kerning->setToolTip(QStringLiteral("Kerning: Auto uses the font's pairs; None sets them to 0. Alt+Left and Alt+Right at a caret kern by hand"));
    m_kerning->addItems({QStringLiteral("Auto"), QStringLiteral("None")});
    connect(m_kerning, &QComboBox::activated, this, [this](int index) {
        change([index](TextContent &text) { text.kerning = index == 1 ? TextKerning::none : TextKerning::metrics; }, QStringLiteral("Kerning"));
    });
    m_baseline = number(QStringLiteral("Shift"), QStringLiteral("pt"), QStringLiteral("characterBaselineShift"), QStringLiteral("Baseline Shift"),
                        [](TextContent &text, double value) { text.baselineShift = value; }, [](const TextContent &text) { return text.baselineShift; });
    m_baseline->setToolTip(QStringLiteral("Baseline shift: up is positive"));
    m_horizontal = number(QStringLiteral("H"), QStringLiteral("%"), QStringLiteral("characterHorizontalScale"), QStringLiteral("Horizontal Scale"),
                          [](TextContent &text, double value) { text.horizontalScale = value; },
                          [](const TextContent &text) { return text.horizontalScale; });
    m_horizontal->setToolTip(QStringLiteral("Horizontal scale"));
    m_vertical = number(QStringLiteral("V"), QStringLiteral("%"), QStringLiteral("characterVerticalScale"), QStringLiteral("Vertical Scale"),
                        [](TextContent &text, double value) { text.verticalScale = value; }, [](const TextContent &text) { return text.verticalScale; });
    m_vertical->setToolTip(QStringLiteral("Vertical scale"));
    for (NumberField *scale : {m_horizontal, m_vertical}) {
        scale->minimum = 1;
        scale->maximum = 10000;
    }
    m_case->setObjectName(QStringLiteral("characterCase"));
    m_case->setAccessibleName(QStringLiteral("Case"));
    m_case->setToolTip(QStringLiteral("Case"));
    m_case->addItems({QStringLiteral("Normal case"), QStringLiteral("All Caps"), QStringLiteral("Small Caps")});
    connect(m_case, &QComboBox::activated, this, [this](int index) {
        change([index](TextContent &text) { text.textCase = std::array{TextCase::normal, TextCase::allCaps, TextCase::smallCaps}.at(size_t(index)); },
               QStringLiteral("Case"));
    });
    m_underline = toggleButton(QStringLiteral("characterUnderline"), QStringLiteral("U"), QStringLiteral("Underline"));
    m_strikethrough = toggleButton(QStringLiteral("characterStrikethrough"), QStringLiteral("S"), QStringLiteral("Strikethrough"));
    for (QToolButton *button : {m_underline, m_strikethrough}) {
        QFont font = ToolHeaderStyle::controlFont();
        font.setUnderline(button == m_underline);
        font.setStrikeOut(button == m_strikethrough);
        button->setFont(font);
    }
    connect(m_underline, &QToolButton::clicked, this, [this](bool on) {
        change([on](TextContent &text) { text.underline = on; }, QStringLiteral("Underline"));
    });
    connect(m_strikethrough, &QToolButton::clicked, this, [this](bool on) {
        change([on](TextContent &text) { text.strikethrough = on; }, QStringLiteral("Strikethrough"));
    });
    m_kind->setObjectName(QStringLiteral("characterKind"));
    m_kind->setAccessibleName(QStringLiteral("Type kind"));
    m_kind->setToolTip(QStringLiteral("Point type runs on one line; area type wraps inside its box"));
    m_kind->addItems({QStringLiteral("Point type"), QStringLiteral("Area type")});
    connect(m_kind, &QComboBox::activated, this, [this](int index) { m_session.convertTextType(index == 1); });
    auto *decoration = new QHBoxLayout;
    decoration->setSpacing(2);
    decoration->addWidget(m_case, 1);
    decoration->addWidget(m_underline);
    decoration->addWidget(m_strikethrough);
    extra->addWidget(caption(QStringLiteral("Kerning"), m_extra), 0, 0);
    extra->addWidget(m_kerning, 0, 1);
    extra->addWidget(m_baseline, 0, 2);
    extra->addWidget(caption(QStringLiteral("Scale"), m_extra), 1, 0);
    extra->addWidget(m_horizontal, 1, 1);
    extra->addWidget(m_vertical, 1, 2);
    extra->addLayout(decoration, 2, 0, 1, 3);
    extra->addWidget(caption(QStringLiteral("Kind"), m_extra), 3, 0);
    extra->addWidget(m_kind, 3, 1, 1, 2);
    extra->setColumnStretch(1, 1);
    extra->setColumnStretch(2, 1);
    body->addWidget(m_extra);

    for (QComboBox *combo : {static_cast<QComboBox *>(m_family), m_style, m_kerning, m_case, m_kind})
        combo->setFont(ToolHeaderStyle::controlFont());
    setShowsMore(QSettings().value(showMoreKey, false).toBool());
    applyGlyphs();
}

NumberField *CharacterSection::number(const QString &label, const QString &suffix, const QString &name, const QString &undo,
                                      const std::function<void(TextContent &, double)> &set,
                                      const std::function<double(const TextContent &)> &get)
{
    auto *made = new NumberField(label, suffix, [this, set, undo](double value) {
        change([&set, value](TextContent &text) { set(text, value); }, undo);
    }, this);
    made->setObjectName(name);
    made->field->setObjectName(name + QStringLiteral("Field"));
    made->field->setAccessibleName(undo);
    // Relative input and steps on several texts keep each one's own value.
    made->changeEach = [this, set, get, undo](const std::function<double(double)> &each) {
        change([&](TextContent &text) { set(text, each(get(text))); }, undo);
    };
    made->gesture = [this, undo](bool starting) {
        if (starting)
            m_session.beginEdit(undo);
        else
            m_session.endEdit();
    };
    m_numbers.push_back({made, get});
    return made;
}

QToolButton *CharacterSection::toggleButton(const QString &name, const QString &text, const QString &tip)
{
    auto *button = new QToolButton(this);
    button->setObjectName(name);
    button->setText(text);
    button->setCheckable(true);
    button->setAutoRaise(true);
    button->setFixedSize(28, 26);
    button->setToolTip(tip);
    button->setAccessibleName(tip);
    return button;
}

void CharacterSection::change(const std::function<void(TextContent &)> &edit, const QString &name)
{
    m_session.updateText(edit, name);
}

bool CharacterSection::showsMore() const
{
    return !m_extra->isHidden();
}

void CharacterSection::setShowsMore(bool shown)
{
    QSettings().setValue(showMoreKey, shown);
    m_extra->setVisible(shown);
    m_more->setText(shown ? QStringLiteral("Show less") : QStringLiteral("Show more"));
    m_more->setToolTip(shown ? QStringLiteral("Hide kerning, baseline shift, scale, case and decoration")
                             : QStringLiteral("Show kerning, baseline shift, scale, case and decoration"));
}

void CharacterSection::applyGlyphs()
{
    // Leading and tracking read as Figma's icons; their names are in tooltips and for screen readers.
    const QColor faint = palette().color(QPalette::PlaceholderText);
    m_leading->handle()->setPixmap(PanelIcons::pixmap(PanelIcon::leading, 16, faint, devicePixelRatio()));
    m_tracking->handle()->setPixmap(PanelIcons::pixmap(PanelIcon::tracking, 16, faint, devicePixelRatio()));
    for (size_t index = 0; index < TypeAlignment::all.size(); ++index)
        m_alignments.at(index)->setIcon(TypeAlignment::glyph(TypeAlignment::all.at(index), palette().color(QPalette::WindowText), devicePixelRatio()));
}

void CharacterSection::changeEvent(QEvent *event)
{
    PanelSection::changeEvent(event);
    if (event->type() == QEvent::PaletteChange)
        applyGlyphs();
}

void CharacterSection::synchronize()
{
    std::vector<TextContent> texts;
    for (const QUuid &id : m_session.selectedTexts())
        texts.push_back(m_session.document()->find(id)->text);
    if (texts.empty())
        texts.push_back(m_session.defaultText);
    const TextContent &first = texts.front();
    const std::optional<QString> family = common<QString>(texts, [](const TextContent &text) { return text.family; });
    {
        const QSignalBlocker quiet(m_family);
        if (family) {
            m_family->setCurrentFont(QFont(*family));
            m_family->lineEdit()->setPlaceholderText(QString());
        } else {
            m_family->setCurrentIndex(-1);
            m_family->lineEdit()->clear();
            m_family->lineEdit()->setPlaceholderText(QStringLiteral("Mixed"));
        }
    }
    {
        const QSignalBlocker quiet(m_style);
        QStringList styles = QFontDatabase::styles(family.value_or(first.family));
        const std::optional<QString> style = common<QString>(texts, shownStyle);
        if (style && !styles.contains(*style))
            styles.prepend(*style);
        QStringList listed;
        for (int index = 0; index < m_style->count(); ++index)
            listed << m_style->itemText(index);
        if (styles != listed) {
            m_style->clear();
            m_style->addItems(styles);
        }
        m_style->setPlaceholderText(QStringLiteral("Mixed"));
        m_style->setCurrentIndex(style ? int(styles.indexOf(*style)) : -1);
    }
    for (const auto &[field, get] : m_numbers) {
        if (const std::optional<double> value = common<double>(texts, get))
            field->sync(*value);
        else
            field->syncMixed();
    }
    // Auto leading shows its value as the placeholder, as Illustrator's parentheses do.
    const std::optional<bool> automatic = common<bool>(texts, [](const TextContent &text) { return !text.leading.has_value(); });
    const std::optional<double> leading = common<double>(texts, [](const TextContent &text) { return text.effectiveLeading(); });
    if (automatic.value_or(false))
        m_leading->syncUnset(first.effectiveLeading(), leading ? QStringLiteral("Auto (%1)").arg(NumberField::formatted(*leading)) : QStringLiteral("Auto"));
    const std::optional<TextAlignment> alignment = common<TextAlignment>(texts, [](const TextContent &text) { return text.alignment; });
    for (size_t index = 0; index < TypeAlignment::all.size(); ++index)
        m_alignments.at(index)->setChecked(alignment == TypeAlignment::all.at(index));
    const auto index = [](QComboBox *combo, std::optional<int> at) {
        const QSignalBlocker quiet(combo);
        combo->setPlaceholderText(QStringLiteral("Mixed"));
        combo->setCurrentIndex(at.value_or(-1));
    };
    index(m_kerning, common<int>(texts, [](const TextContent &text) { return text.kerning == TextKerning::none ? 1 : 0; }));
    index(m_case, common<int>(texts, [](const TextContent &text) { return int(text.textCase); }));
    index(m_kind, common<int>(texts, [](const TextContent &text) { return text.area ? 1 : 0; }));
    m_kind->setEnabled(!m_session.selectedTexts().empty());
    m_underline->setChecked(common<bool>(texts, [](const TextContent &text) { return text.underline; }).value_or(false));
    m_strikethrough->setChecked(common<bool>(texts, [](const TextContent &text) { return text.strikethrough; }).value_or(false));
}
