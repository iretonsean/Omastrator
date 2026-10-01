#include "UI/PropertiesPanel.h"
#include "Canvas/EditorCanvas.h"
#include "UI/CharacterSection.h"
#include "UI/ColorPaletteControls.h"
#include "UI/ColorPickerSheet.h"
#include "UI/FramePresets.h"
#include "UI/NumberField.h"
#include "UI/ObjectDialogs.h"
#include "UI/PaintStack.h"
#include "UI/ParagraphSection.h"
#include "UI/ToolHeaderStyle.h"
#include <QAction>
#include <QEvent>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QListWidget>
#include <QMenu>
#include <QPushButton>
#include <QVBoxLayout>

namespace {
QFrame *divider(QWidget *parent)
{
    auto *line = new QFrame(parent);
    line->setFrameShape(QFrame::HLine);
    line->setFrameShadow(QFrame::Plain);
    line->setForegroundRole(QPalette::Mid);
    return line;
}

QLabel *caption(const QString &text, QWidget *parent)
{
    auto *label = new QLabel(text, parent);
    label->setFont(ToolHeaderStyle::controlFont());
    label->setForegroundRole(QPalette::PlaceholderText);
    return label;
}
}

PropertiesPanel::PropertiesPanel(EditorSession &session, QWidget *parent) : QScrollArea(parent), m_session(session)
{
    setObjectName(QStringLiteral("propertiesPanel"));
    setAccessibleName(QStringLiteral("Properties"));
    setFrameShape(QFrame::NoFrame);
    setWidgetResizable(true);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    auto *content = new QWidget(this);
    auto *column = new QVBoxLayout(content);
    column->setContentsMargins(0, 4, 0, 0);
    column->setSpacing(0);
    m_character = new CharacterSection(m_session, content);
    m_paragraph = new ParagraphSection(m_session, content);
    m_framePresets = new FramePresetsSection(m_session, content);
    bool first = true;
    for (PanelSection *block : {static_cast<PanelSection *>(m_framePresets), documentSection(), componentSection(), transformSection(), layoutSection(), shapeSection(), static_cast<PanelSection *>(m_character),
                                static_cast<PanelSection *>(m_paragraph), appearanceSection(), strokeSection(), alignSection(), pathfinderSection()}) {
        if (!first) {
            // A section's rule hides with it.
            QFrame *rule = divider(content);
            rule->setObjectName(block->objectName() + QStringLiteral("Rule"));
            column->addWidget(rule);
        }
        first = false;
        column->addWidget(block);
    }
    column->addStretch(1);
    setWidget(content);
    evenControls();
    applyIcons();
    connect(&m_session, &EditorSession::changed, this, &PropertiesPanel::synchronize);
    synchronize();
}

void PropertiesPanel::setEditingText(bool editing)
{
    m_editingText = editing;
    synchronize();
}

// Menus and fields as tall as each other, in the fields' type size.
void PropertiesPanel::evenControls()
{
    for (QComboBox *combo : widget()->findChildren<QComboBox *>()) {
        combo->setFont(ToolHeaderStyle::controlFont());
        combo->setFixedHeight(NumberField::fieldHeight);
        // Paired menus share a row: they may shrink below their longest item and elide it,
        // unless they were given a width of their own.
        if (combo->minimumWidth() != combo->maximumWidth()) {
            combo->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
            combo->setMinimumWidth(56);
        }
    }
    for (QLineEdit *field : widget()->findChildren<QLineEdit *>()) {
        // A number field sizes its own edit, inside its box.
        if (qobject_cast<QComboBox *>(field->parentWidget()) || qobject_cast<NumberField *>(field->parentWidget()))
            continue;
        field->setFont(ToolHeaderStyle::controlFont());
        field->setFixedHeight(NumberField::fieldHeight);
    }
    for (QPushButton *button : widget()->findChildren<QPushButton *>()) {
        button->setFont(ToolHeaderStyle::controlFont());
        button->setFixedHeight(NumberField::fieldHeight);
    }
}

QToolButton *PropertiesPanel::iconButton(const QString &name, const QString &tip, PanelIcon icon, const std::function<void()> &run)
{
    auto *button = new QToolButton(this);
    button->setObjectName(name);
    button->setToolTip(tip);
    button->setAccessibleName(tip.section(QStringLiteral(" ("), 0, 0));
    button->setAutoRaise(true);
    button->setFixedSize(NumberField::fieldHeight, NumberField::fieldHeight);
    button->setIconSize(QSize(16, 16));
    connect(button, &QToolButton::clicked, this, run);
    m_iconButtons.push_back({button, icon});
    return button;
}

// The icons take the palette's ink, again after each theme.
void PropertiesPanel::applyIcons()
{
    const QColor ink = palette().color(QPalette::WindowText);
    for (const auto &[button, icon] : m_iconButtons)
        button->setIcon(PanelIcons::pixmap(icon, 16, ink, devicePixelRatio()));
    if (m_link)
        m_link->setIcon(PanelIcons::pixmap(m_link->isChecked() ? PanelIcon::link : PanelIcon::unlink, 16, ink, devicePixelRatio()));
    if (m_rotation)
        m_rotation->handle()->setPixmap(PanelIcons::pixmap(PanelIcon::rotate, 14, palette().color(QPalette::PlaceholderText), devicePixelRatio()));
}

void PropertiesPanel::changeEvent(QEvent *event)
{
    QScrollArea::changeEvent(event);
    if (event->type() == QEvent::PaletteChange)
        applyIcons();
}

void PropertiesPanel::runWindowAction(const QString &name)
{
    if (QAction *action = window()->findChild<QAction *>(name); action && action->isEnabled())
        action->trigger();
}

// Nothing selected: the document's own settings and a few quick actions.
PanelSection *PropertiesPanel::documentSection()
{
    m_document = new PanelSection(QStringLiteral("Document"), QStringLiteral("artboard"), this);
    QVBoxLayout *body = m_document->body;
    m_artboardWidth = new NumberField(QStringLiteral("W"), QStringLiteral("pt"), [this](double width) {
        if (m_session.document() && width > 0)
            m_session.setArtboardSize(QSizeF(width, m_session.document()->artboard(m_session.activeArtboard()).rect.height()));
    }, m_document);
    m_artboardWidth->field->setObjectName(QStringLiteral("artboardWidth"));
    m_artboardHeight = new NumberField(QStringLiteral("H"), QStringLiteral("pt"), [this](double height) {
        if (m_session.document() && height > 0)
            m_session.setArtboardSize(QSizeF(m_session.document()->artboard(m_session.activeArtboard()).rect.width(), height));
    }, m_document);
    m_artboardHeight->field->setObjectName(QStringLiteral("artboardHeight"));
    for (NumberField *side : {m_artboardWidth, m_artboardHeight}) {
        side->maximum = VectorDocument::maximumArtboardSide;
        side->gesture = [this](bool starting) {
            if (starting)
                m_session.beginEdit(QStringLiteral("Artboard Size"));
            else
                m_session.endEdit();
        };
    }
    m_artboardWidth->setToolTip(QStringLiteral("Artboard width"));
    m_artboardHeight->setToolTip(QStringLiteral("Artboard height"));
    auto *size = new QHBoxLayout;
    size->setSpacing(10);
    size->addWidget(m_artboardWidth);
    size->addWidget(m_artboardHeight);
    body->addLayout(size);
    m_background = new PaintSwatch([this] {
        return Paint::solid(m_session.document() && m_session.document()->artboardCount() > 0 ? m_session.document()->artboard(m_session.activeArtboard()).background : QColor(Qt::white));
    }, false, m_document);
    m_background->setObjectName(QStringLiteral("artboardBackground"));
    m_background->setFixedSize(36, 22);
    m_background->setToolTip(QStringLiteral("Artboard background color"));
    m_background->setAccessibleName(QStringLiteral("Artboard background color"));
    connect(m_background, &QAbstractButton::clicked, this, [this] {
        if (!m_session.document() || m_session.document()->artboardCount() == 0)
            return;
        ColorPickerSheet::showIn(m_picker, QStringLiteral("Artboard Background"), m_session.document()->artboard(m_session.activeArtboard()).background,
                                 [this](const QColor &color) { m_session.setArtboardBackground(color); });
    });
    auto *background = new QHBoxLayout;
    background->setSpacing(6);
    background->addWidget(caption(QStringLiteral("Background"), m_document));
    background->addWidget(m_background);
    background->addStretch(1);
    body->addLayout(background);
    // The Artboards list: the active row selected, double-click renames, right-click for the
    // Artboards menu, and a + button to add one.
    auto *artboardsHeader = new QHBoxLayout;
    artboardsHeader->addWidget(caption(QStringLiteral("Artboards"), m_document));
    artboardsHeader->addStretch(1);
    auto *addArtboard = new QToolButton(m_document);
    addArtboard->setObjectName(QStringLiteral("artboardAdd"));
    addArtboard->setText(QStringLiteral("+"));
    addArtboard->setToolTip(QStringLiteral("New Artboard"));
    connect(addArtboard, &QToolButton::clicked, this, [this] { m_session.addArtboard(); });
    artboardsHeader->addWidget(addArtboard);
    body->addLayout(artboardsHeader);
    m_artboards = new QListWidget(m_document);
    m_artboards->setObjectName(QStringLiteral("artboardsList"));
    m_artboards->setMaximumHeight(120);
    m_artboards->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_artboards, &QListWidget::currentRowChanged, this, [this](int row) {
        if (row >= 0)
            m_session.setActiveArtboard(row);
    });
    connect(m_artboards, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem *item) {
        ObjectDialogs::renameArtboard(m_session, m_artboards->row(item), window());
    });
    connect(m_artboards, &QListWidget::customContextMenuRequested, this, [this](QPoint at) {
        if (QListWidgetItem *item = m_artboards->itemAt(at))
            artboardsMenu(m_artboards->row(item), m_artboards->mapToGlobal(at));
    });
    body->addWidget(m_artboards);
    const auto check = [this](const QString &name, const QString &text, const std::function<void(bool)> &set) {
        auto *box = new QCheckBox(text, m_document);
        box->setObjectName(name);
        box->setFont(ToolHeaderStyle::controlFont());
        connect(box, &QCheckBox::toggled, this, set);
        return box;
    };
    m_artboardExported = check(QStringLiteral("artboardExported"), QStringLiteral("Export this artboard"),
                               [this](bool on) { m_session.setArtboardExported(m_session.activeArtboard(), on); });
    m_artboardExported->setToolTip(QStringLiteral("Off keeps it on the canvas but out of every export and share"));
    body->addWidget(m_artboardExported);
    m_grid = check(QStringLiteral("documentShowGrid"), QStringLiteral("Show grid"), [this](bool on) { m_session.setShowsGrid(on); });
    m_snap = check(QStringLiteral("documentSnapToGrid"), QStringLiteral("Snap to grid"), [this](bool on) { m_session.setSnapsToGrid(on); });
    m_outline = check(QStringLiteral("documentOutline"), QStringLiteral("Outline view"), [this](bool on) { m_session.setShowsOutline(on); });
    auto *toggles = new QGridLayout;
    toggles->setHorizontalSpacing(10);
    toggles->setVerticalSpacing(4);
    toggles->addWidget(m_grid, 0, 0);
    toggles->addWidget(m_snap, 0, 1);
    toggles->addWidget(m_outline, 1, 0);
    body->addLayout(toggles);
    m_increment = new NumberField(QStringLiteral("Nudge"), QStringLiteral("pt"), [](double points) { EditorCanvas::setKeyboardIncrement(points); },
                                  m_document);
    m_increment->setObjectName(QStringLiteral("documentNudge"));
    m_increment->field->setObjectName(QStringLiteral("documentNudgeField"));
    m_increment->step = 0.5;
    m_increment->minimum = 0.01;
    m_increment->setToolTip(QStringLiteral("How far an arrow key moves the selection; Shift moves ten times as far"));
    body->addWidget(m_increment);
    auto *actions = new QHBoxLayout;
    actions->setSpacing(6);
    auto *fit = new QPushButton(QStringLiteral("Fit Artboard"), m_document);
    fit->setObjectName(QStringLiteral("documentFit"));
    fit->setToolTip(QStringLiteral("Fit the artboard in the window (Ctrl+0)"));
    connect(fit, &QPushButton::clicked, this, [this] { m_session.zoomToFit(); });
    auto *exporting = new QPushButton(QStringLiteral("Export…"), m_document);
    exporting->setObjectName(QStringLiteral("documentExport"));
    exporting->setToolTip(QStringLiteral("Export the artboard as PNG, JPEG, SVG or PDF"));
    auto *formats = new QMenu(exporting);
    for (const auto &[text, name] : {std::pair{"PNG…", "exportPNG"}, std::pair{"JPEG…", "exportJPEG"}, std::pair{"SVG…", "exportSVG"},
                                     std::pair{"PDF…", "exportPDF"}}) {
        const QString action = QString::fromLatin1(name);
        formats->addAction(QString::fromUtf8(text), this, [this, action] { runWindowAction(action); });
    }
    exporting->setMenu(formats);
    actions->addWidget(fit);
    actions->addWidget(exporting);
    actions->addStretch(1);
    body->addLayout(actions);
    return m_document;
}

void PropertiesPanel::synchronizeArtboards()
{
    const std::vector<Artboard> boards = m_session.document() ? m_session.document()->allArtboards() : std::vector<Artboard>();
    if (int(boards.size()) != m_artboards->count()) {
        m_artboards->clear();
        for (const Artboard &board : boards)
            m_artboards->addItem(board.name);
    } else {
        for (int row = 0; row < int(boards.size()); ++row) {
            if (m_artboards->item(row)->text() != boards[size_t(row)].name)
                m_artboards->item(row)->setText(boards[size_t(row)].name);
        }
    }
    for (int row = 0; row < int(boards.size()); ++row) {
        // A board that doesn't export reads dimmed in the list.
        QListWidgetItem *item = m_artboards->item(row);
        item->setForeground(boards[size_t(row)].exported ? QBrush() : palette().brush(QPalette::Disabled, QPalette::Text));
        item->setToolTip(boards[size_t(row)].exported ? QString() : QStringLiteral("Not exported"));
    }
    const QSignalBlocker quiet(m_artboards);
    m_artboards->setCurrentRow(m_session.activeArtboard());
}

void PropertiesPanel::artboardsMenu(int index, QPoint at)
{
    QMenu menu(this);
    menu.addAction(QStringLiteral("Rename…"), this, [this, index] { ObjectDialogs::renameArtboard(m_session, index, window()); });
    menu.addAction(QStringLiteral("Duplicate"), this, [this, index] { m_session.duplicateArtboard(index); });
    QAction *deleteOne = menu.addAction(QStringLiteral("Delete"), this, [this, index] { m_session.deleteArtboard(index); });
    deleteOne->setEnabled(m_session.document() && m_session.document()->artboardCount() > 0);
    menu.addSeparator();
    const bool exported = m_session.document() && m_session.document()->artboard(index).exported;
    QAction *toggle = menu.addAction(QStringLiteral("Export Artboard"), this, [this, index, exported] { m_session.setArtboardExported(index, !exported); });
    toggle->setCheckable(true);
    toggle->setChecked(exported);
    menu.addSeparator();
    menu.addAction(QStringLiteral("Fit to Artwork Bounds"), this, [this, index] { m_session.fitArtboardToArtwork(index); });
    menu.addAction(QStringLiteral("Switch Orientation"), this, [this, index] { m_session.switchArtboardOrientation(index); });
    menu.exec(at);
}

void PropertiesPanel::synchronize()
{
    const bool drawn = m_session.document().has_value();
    const bool selected = m_session.hasSelection();
    const bool text = !m_session.selectedTexts().empty() || (!selected && (m_session.tool() == Tool::text || m_editingText));
    widget()->setEnabled(drawn);
    const auto show = [this](PanelSection *section, bool shown) {
        section->setVisible(shown);
        if (QWidget *rule = widget()->findChild<QWidget *>(section->objectName() + QStringLiteral("Rule")))
            rule->setVisible(shown);
    };
    show(m_framePresets, drawn && m_session.tool() == Tool::frame);
    show(m_document, !selected);
    show(m_transform, selected);
    show(m_character, text);
    // Area type has paragraphs to indent and space; point type keeps the panel short.
    const std::vector<QUuid> texts = m_session.selectedTexts();
    const bool area = text && !texts.empty() && std::all_of(texts.begin(), texts.end(), [this](const QUuid &id) {
        return m_session.document()->find(id)->text.area.has_value();
    });
    show(m_paragraph, area);
    if (area)
        m_paragraph->synchronize();
    show(m_align, selected);
    show(m_pathfinder, m_session.canCombine());
    show(m_component, drawn && selected && (!m_session.selectedInstances().empty() || m_session.selectedMaster().has_value()));
    if (!m_component->isHidden())
        synchronizeComponent();
    show(m_layout, drawn && selected && (!m_session.selectedFrames().empty() || std::any_of(m_session.selection().begin(), m_session.selection().end(), [this](const QUuid &id) {
        const VectorObject *object = m_session.document()->find(id);
        const VectorObject *parent = object && object->parentID ? m_session.document()->find(*object->parentID) : nullptr;
        return parent && (parent->autoLayout || parent->kind == ObjectKind::frame);
    })));
    if (!m_layout->isHidden())
        synchronizeLayout();
    show(m_shape, drawn && selected && (!m_session.selectedShapes().empty() || !m_session.selectedCompoundPaths().empty()));
    if (!m_shape->isHidden())
        synchronizeShape();
    if (text)
        m_character->synchronize();

    const QRectF bounds = m_session.selectionBounds();
    const QPointF point = ReferencePointPicker::locate(bounds, m_reference->point());
    m_x->sync(point.x());
    m_y->sync(point.y());
    m_width->sync(bounds.width());
    m_height->sync(bounds.height());
    if (!m_rotationPivot)
        m_rotation->sync(0);
    m_session.scaleStrokes = m_scaleStrokes->isChecked();
    m_session.scaleCorners = m_scaleCorners->isChecked();

    // A page with no artboard has no size or paper to edit.
    const bool hasBoard = drawn && m_session.document()->artboardCount() > 0;
    const QSizeF size = hasBoard ? m_session.document()->artboard(m_session.activeArtboard()).rect.size() : QSizeF(0, 0);
    m_artboardWidth->setEnabled(hasBoard);
    m_artboardHeight->setEnabled(hasBoard);
    m_background->setEnabled(hasBoard);
    m_artboardWidth->sync(size.width());
    m_artboardHeight->sync(size.height());
    m_background->update();
    if (!m_document->isHidden())
        synchronizeArtboards();
    {
        const QSignalBlocker quiet(m_artboardExported);
        m_artboardExported->setChecked(!hasBoard || m_session.document()->artboard(m_session.activeArtboard()).exported);
        m_artboardExported->setEnabled(hasBoard);
    }
    for (const auto &[box, on] : {std::pair{m_grid, m_session.showsGrid}, std::pair{m_snap, m_session.snapsToGrid},
                                  std::pair{m_outline, m_session.showsOutline}}) {
        const QSignalBlocker quiet(box);
        box->setChecked(on);
    }
    m_increment->sync(EditorCanvas::keyboardIncrement());

    synchronizePaint();
    const StrokeStyle stroke = shownStroke();
    if (ShownStyle::strokeWidthMixed(m_session) && m_strokeStack->isHidden())
        m_strokeWidth->syncMixed();
    else
        m_strokeWidth->sync(stroke.width);
    m_widthProfile->setCurrentIndex(int(stroke.widthProfile));
    m_cap->setCurrentIndex(stroke.cap == Qt::RoundCap ? 1 : stroke.cap == Qt::SquareCap ? 2 : 0);
    m_join->setCurrentIndex(stroke.join == Qt::RoundJoin ? 1 : stroke.join == Qt::BevelJoin ? 2 : 0);
    if (!m_dashes->hasFocus()) {
        QStringList numbers;
        for (const double dash : stroke.dashes)
            numbers << NumberField::formatted(dash);
        m_dashes->setText(numbers.join(QLatin1Char(' ')));
    }
    for (QToolButton *button : m_alignButtons)
        button->setEnabled(selected);
    for (QToolButton *button : m_distributeButtons)
        button->setEnabled(m_session.selection().size() >= 3);
    // An exact gap spaces two; an even one needs three.
    for (QToolButton *button : m_spacingButtons)
        button->setEnabled(m_session.selection().size() >= (m_spacing ? 2u : 3u));
    if (m_spacing)
        m_spacingField->sync(*m_spacing);
    else
        m_spacingField->syncUnset(0, QStringLiteral("Auto"));
    // A key object, once clicked, is what Align works to, as in Illustrator.
    if (m_session.keyObject() != m_shownKey) {
        m_shownKey = m_session.keyObject();
        const QSignalBlocker quiet(m_alignTarget);
        if (m_shownKey)
            m_alignTarget->setCurrentIndex(2);
        else if (m_alignTarget->currentIndex() == 2)
            m_alignTarget->setCurrentIndex(0);
    }
    for (QToolButton *button : m_pathfinderButtons)
        button->setEnabled(m_session.canCombine());
    // Folded sections keep their one-line summaries current.
    for (PanelSection *section : widget()->findChildren<PanelSection *>()) {
        if (section->isVisible() && section->isCollapsed())
            section->refreshSummary();
    }
}
