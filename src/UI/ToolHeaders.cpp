#include "UI/ToolHeaders.h"
#include "UI/ColorPaletteControls.h"
#include "UI/NumberField.h"
#include <QCheckBox>
#include <QComboBox>
#include <QLabel>
#include <QSignalBlocker>
#include <cmath>

namespace {
// The tool's name and a line of help.
ToolHeaderBar *plainBar(Tool tool, const QString &help, QWidget *parent)
{
    auto *bar = new ToolHeaderBar(title(tool), parent);
    auto *words = new QLabel(help, bar);
    words->setObjectName(QStringLiteral("toolHelp"));
    words->setForegroundRole(QPalette::PlaceholderText);
    bar->row->insertWidget(1, words);
    return bar;
}

NumberField *amount(const QString &label, const QString &suffix, const QString &name, std::function<void(double)> change, QWidget *parent)
{
    auto *field = new NumberField(label, suffix, std::move(change), parent);
    field->setObjectName(name);
    field->field->setObjectName(name + QStringLiteral("Field"));
    field->field->setFixedWidth(52);
    return field;
}
}

Tool ToolHeaders::family(Tool tool)
{
    return tool == Tool::zoom ? Tool::hand : tool;
}

ToolHeaderBar *ToolHeaders::make(EditorSession &session, Tool tool, QWidget *parent)
{
    switch (tool) {
    case Tool::select:
    case Tool::directSelect:
        return plainBar(tool, QStringLiteral("Click or drag to select · Shift adds · Alt-drag duplicates · Arrows nudge"), parent);
    case Tool::pen: return plainBar(tool, QStringLiteral("Click for corners · Drag for curves · Click the first point to close · Return ends"), parent);
    case Tool::pencil: return plainBar(tool, QStringLiteral("Drag to draw a smooth path · End at the start to close"), parent);
    case Tool::text: return new TypeControls(session, parent);
    case Tool::line:
    case Tool::rectangle:
    case Tool::roundedRectangle:
    case Tool::ellipse:
    case Tool::polygon:
    case Tool::star: return new ShapeControls(session, tool, parent);
    case Tool::shapeBuilder: return new ShapeBuilderControls(session, parent);
    case Tool::scissors: return plainBar(tool, QStringLiteral("Click a path to cut it there"), parent);
    case Tool::rotate:
    case Tool::scale: return new TransformToolHeader(session, tool, parent);
    case Tool::eyedropper: return plainBar(tool, QStringLiteral("Click an object to take its fill and stroke. Alt-click gives it the selection's"), parent);
    case Tool::gradient: return plainBar(tool, QStringLiteral("Drag across the selection to set its gradient; drag the ends or the stops to adjust"), parent);
    case Tool::hand:
    case Tool::zoom: return new NavigationToolHeader(session, parent);
    case Tool::artboard: {
        ToolHeaderBar *bar = plainBar(tool, QStringLiteral("Drag to draw · Drag to move or resize · Alt-drag duplicates · Delete removes it"), parent);
        auto *moveArt = new QCheckBox(QStringLiteral("Move art with artboard"), bar);
        moveArt->setObjectName(QStringLiteral("artboardMovesArt"));
        moveArt->setChecked(session.artboardMovesArt);
        QObject::connect(moveArt, &QCheckBox::toggled, bar, [&session](bool on) { session.artboardMovesArt = on; });
        bar->row->addWidget(moveArt);
        return bar;
    }
    }
    return new ToolHeaderBar(title(tool), parent);
}

ShapeControls::ShapeControls(EditorSession &session, Tool tool, QWidget *parent)
    : ToolHeaderBar(::title(tool), parent), m_session(session),
      m_fill(new PaintSwatch([&session] { return ShownStyle::fill(session); }, false, this)),
      m_stroke(new PaintSwatch([&session] { return ShownStyle::stroke(session).paint; }, true, this)),
      m_weight(amount(QStringLiteral("Stroke"), QStringLiteral("pt"), QStringLiteral("shapeStrokeWeight"), [this](double width) {
          StrokeStyle stroke = ShownStyle::stroke(m_session);
          stroke.width = std::max(0.0, width);
          m_session.setStrokeOfSelection(stroke);
      }, this)),
      m_radius(amount(QStringLiteral("Corner radius"), QStringLiteral("pt"), QStringLiteral("cornerRadius"), [this](double radius) {
          m_session.cornerRadius = std::max(0.0, radius);
          synchronize();
      }, this)),
      m_sides(amount(QStringLiteral("Sides"), QString(), QStringLiteral("polygonSides"), [this](double sides) {
          m_session.polygonSides = std::clamp(int(std::lround(sides)), 3, 100);
          synchronize();
      }, this)),
      m_points(amount(QStringLiteral("Points"), QString(), QStringLiteral("starPoints"), [this](double points) {
          m_session.starPoints = std::clamp(int(std::lround(points)), 3, 100);
          synchronize();
      }, this)),
      m_inner(amount(QStringLiteral("Inner radius"), QStringLiteral("%"), QStringLiteral("starInnerRatio"), [this](double percent) {
          m_session.starInnerRatio = std::clamp(percent, 1.0, 100.0) / 100;
          synchronize();
      }, this))
{
    setObjectName(QStringLiteral("shapeControls"));
    m_fill->setObjectName(QStringLiteral("shapeFill"));
    m_fill->setFixedSize(28, 18);
    m_fill->setToolTip(QStringLiteral("New shapes take this fill"));
    m_stroke->setObjectName(QStringLiteral("shapeStroke"));
    m_stroke->setFixedSize(28, 18);
    m_stroke->setToolTip(QStringLiteral("New shapes take this stroke"));
    // A click opens the rail's picker, which knows the target.
    connect(m_fill, &QAbstractButton::clicked, this, [this] {
        if (auto *palette = window()->findChild<ColorPaletteControls *>())
            palette->pickFill();
    });
    connect(m_stroke, &QAbstractButton::clicked, this, [this] {
        if (auto *palette = window()->findChild<ColorPaletteControls *>())
            palette->pickStroke();
    });
    m_weight->step = 0.5;
    m_inner->step = 5;
    auto *fillLabel = new QLabel(QStringLiteral("Fill"), this);
    fillLabel->setForegroundRole(QPalette::PlaceholderText);
    m_radius->setVisible(tool == Tool::roundedRectangle);
    m_sides->setVisible(tool == Tool::polygon);
    m_points->setVisible(tool == Tool::star);
    m_inner->setVisible(tool == Tool::star);
    int at = 1;
    for (QWidget *widget : std::initializer_list<QWidget *>{fillLabel, m_fill, m_stroke, m_weight, m_radius, m_sides, m_points, m_inner})
        row->insertWidget(at++, widget);
    connect(&m_session, &EditorSession::changed, this, &ShapeControls::synchronize);
    synchronize();
}

void ShapeControls::synchronize()
{
    m_fill->update();
    m_stroke->update();
    m_weight->sync(ShownStyle::stroke(m_session).width);
    m_radius->sync(m_session.cornerRadius);
    m_sides->sync(m_session.polygonSides);
    m_points->sync(m_session.starPoints);
    m_inner->sync(m_session.starInnerRatio * 100);
}

ShapeBuilderControls::ShapeBuilderControls(EditorSession &session, QWidget *parent)
    : ToolHeaderBar(::title(Tool::shapeBuilder), parent), m_session(session), m_gaps(new QCheckBox(QStringLiteral("Gap detection"), this)),
      m_gapLength(amount(QStringLiteral("Gap"), QStringLiteral("pt"), QStringLiteral("shapeBuilderGap"), [this](double length) {
          change([length](ShapeBuilderOptions &options) { options.gapLength = std::clamp(length, 0.0, 100.0); });
      }, this)),
      m_strokeSplits(new QCheckBox(QStringLiteral("Click on a stroke splits it"), this)), m_colorFrom(new QComboBox(this)),
      m_selection(new QComboBox(this)), m_highlightFill(new QCheckBox(QStringLiteral("Highlight fill"), this)),
      m_highlightStroke(new QCheckBox(QStringLiteral("Highlight stroke"), this)), m_more(new QToolButton(this))
{
    setObjectName(QStringLiteral("shapeBuilderControls"));
    m_gaps->setObjectName(QStringLiteral("shapeBuilderGapDetection"));
    m_gaps->setToolTip(QStringLiteral("Treat gaps up to this length as closed"));
    m_strokeSplits->setObjectName(QStringLiteral("shapeBuilderStrokeSplits"));
    m_strokeSplits->setToolTip(QStringLiteral("In merge mode, clicking an open path's edge splits the path there"));
    m_colorFrom->setObjectName(QStringLiteral("shapeBuilderColorFrom"));
    m_colorFrom->addItems({QStringLiteral("Colour from artwork"), QStringLiteral("Colour from swatches")});
    m_colorFrom->setToolTip(QStringLiteral("Artwork: a merge takes the style of the object the drag starts in. Swatches: the current fill and stroke."));
    m_selection->setObjectName(QStringLiteral("shapeBuilderSelection"));
    m_selection->addItems({QStringLiteral("Freeform"), QStringLiteral("Straight line")});
    m_selection->setToolTip(QStringLiteral("What a drag touches: the path the pointer takes, or a straight line from the press"));
    m_highlightFill->setObjectName(QStringLiteral("shapeBuilderHighlightFill"));
    m_highlightStroke->setObjectName(QStringLiteral("shapeBuilderHighlightStroke"));
    connect(m_gaps, &QCheckBox::toggled, this, [this](bool on) { change([on](ShapeBuilderOptions &options) { options.gapDetection = on; }); });
    connect(m_strokeSplits, &QCheckBox::toggled, this,
            [this](bool on) { change([on](ShapeBuilderOptions &options) { options.clickingStrokeSplits = on; }); });
    connect(m_colorFrom, &QComboBox::activated, this,
            [this](int index) { change([index](ShapeBuilderOptions &options) { options.colorFromArtwork = index == 0; }); });
    connect(m_selection, &QComboBox::activated, this,
            [this](int index) { change([index](ShapeBuilderOptions &options) { options.freeformSelection = index == 0; }); });
    connect(m_highlightFill, &QCheckBox::toggled, this,
            [this](bool on) { change([on](ShapeBuilderOptions &options) { options.highlightFill = on; }); });
    connect(m_highlightStroke, &QCheckBox::toggled, this,
            [this](bool on) { change([on](ShapeBuilderOptions &options) { options.highlightStroke = on; }); });
    // Colour source is the one choice made often; the rest folds behind Options, as Illustrator keeps them in a dialog.
    m_more->setObjectName(QStringLiteral("shapeBuilderOptions"));
    m_more->setText(QStringLiteral("Options"));
    m_more->setCheckable(true);
    m_more->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    m_more->setArrowType(Qt::RightArrow);
    m_more->setToolTip(QStringLiteral("Gap detection, stroke splitting, the drag's path and the highlight"));
    const std::vector<QWidget *> folded{m_gaps, m_gapLength, m_strokeSplits, m_selection, m_highlightFill, m_highlightStroke};
    connect(m_more, &QToolButton::toggled, this, [this, folded](bool open) {
        m_more->setArrowType(open ? Qt::DownArrow : Qt::RightArrow);
        for (QWidget *widget : folded)
            widget->setVisible(open);
    });
    int at = 1;
    for (QWidget *widget : std::initializer_list<QWidget *>{m_colorFrom, m_more})
        row->insertWidget(at++, widget);
    for (QWidget *widget : folded) {
        row->insertWidget(at++, widget);
        widget->setVisible(false);
    }
    synchronize();
}

void ShapeBuilderControls::change(const std::function<void(ShapeBuilderOptions &)> &edit)
{
    ShapeBuilderOptions options = m_session.shapeBuilder;
    edit(options);
    if (options == m_session.shapeBuilder)
        return;
    m_session.shapeBuilder = options;
    synchronize();
    // Options aren't document edits: tell the canvas to rebuild its regions.
    emit m_session.changed();
}

void ShapeBuilderControls::synchronize()
{
    const ShapeBuilderOptions &options = m_session.shapeBuilder;
    const QSignalBlocker blockers[] = {QSignalBlocker(m_gaps), QSignalBlocker(m_strokeSplits), QSignalBlocker(m_colorFrom),
                                       QSignalBlocker(m_selection), QSignalBlocker(m_highlightFill), QSignalBlocker(m_highlightStroke)};
    m_gaps->setChecked(options.gapDetection);
    m_gapLength->sync(options.gapLength);
    m_gapLength->setEnabled(options.gapDetection);
    m_strokeSplits->setChecked(options.clickingStrokeSplits);
    m_colorFrom->setCurrentIndex(options.colorFromArtwork ? 0 : 1);
    m_selection->setCurrentIndex(options.freeformSelection ? 0 : 1);
    m_highlightFill->setChecked(options.highlightFill);
    m_highlightStroke->setChecked(options.highlightStroke);
}

NavigationToolHeader::NavigationToolHeader(EditorSession &session, QWidget *parent)
    : ToolHeaderBar(QString(), parent), m_session(session),
      m_zoom(amount(QStringLiteral("Zoom"), QStringLiteral("%"), QStringLiteral("zoomPercentage"), [this](double percent) {
          if (!m_session.document() || !(percent > 0))
              return;
          // The session has no zoom setter: viewport, then signal.
          m_session.viewport.setZoom(std::clamp(percent / 100, CanvasViewport::minimumZoom, CanvasViewport::maximumZoom),
                                     m_session.viewport.center(), m_session.document()->size);
          emit m_session.changed();
      }, this))
{
    setObjectName(QStringLiteral("navigationHeader"));
    m_zoom->step = 10;
    m_zoom->setToolTip(QStringLiteral("Zoom percentage (0.1–3200%). Press Return to apply."));
    row->insertWidget(1, m_zoom);
    connect(&m_session, &EditorSession::changed, this, &NavigationToolHeader::synchronize);
    synchronize();
}

void NavigationToolHeader::synchronize()
{
    title->setText(::title(m_session.tool()));
    m_zoom->setEnabled(m_session.document().has_value());
    m_zoom->sync(std::round(m_session.viewport.zoom() * 1000) / 10);
}

TransformToolHeader::TransformToolHeader(EditorSession &session, Tool tool, QWidget *parent)
    : ToolHeaderBar(::title(tool), parent), m_session(session),
      m_amount(tool == Tool::rotate ? amount(QStringLiteral("Angle"), QStringLiteral("°"), QStringLiteral("rotateAngle"), [this](double angle) { m_amount->sync(angle); }, this)
                                    : amount(QStringLiteral("Uniform"), QStringLiteral("%"), QStringLiteral("scalePercent"), [this](double percent) { m_amount->sync(percent); }, this)),
      m_apply(new QToolButton(this))
{
    setObjectName(QStringLiteral("transformToolHeader"));
    // The field keeps what was typed; Apply uses it.
    m_amount->sync(tool == Tool::rotate ? 90 : 100);
    m_apply->setObjectName(QStringLiteral("applyTransform"));
    m_apply->setText(tool == Tool::rotate ? QStringLiteral("Rotate") : QStringLiteral("Scale"));
    connect(m_apply, &QToolButton::clicked, this, [this, tool] {
        m_amount->commit();
        const double typed = m_amount->value();
        if (tool == Tool::rotate)
            m_session.rotateSelection(-typed);
        else if (typed > 0)
            m_session.scaleSelection(typed / 100, typed / 100);
    });
    auto *flipH = new QToolButton(this);
    flipH->setObjectName(QStringLiteral("reflectHorizontal"));
    flipH->setText(QStringLiteral("Reflect ↔"));
    connect(flipH, &QToolButton::clicked, this, [this] { m_session.flipSelection(Qt::Horizontal); });
    auto *flipV = new QToolButton(this);
    flipV->setObjectName(QStringLiteral("reflectVertical"));
    flipV->setText(QStringLiteral("Reflect ↕"));
    connect(flipV, &QToolButton::clicked, this, [this] { m_session.flipSelection(Qt::Vertical); });
    int at = 1;
    for (QWidget *widget : std::initializer_list<QWidget *>{m_amount, m_apply, flipH, flipV})
        row->insertWidget(at++, widget);
    connect(&m_session, &EditorSession::changed, this, &TransformToolHeader::synchronize);
    synchronize();
}

void TransformToolHeader::synchronize()
{
    for (QToolButton *button : findChildren<QToolButton *>())
        button->setEnabled(m_session.hasSelection());
}
