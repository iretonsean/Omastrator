#include "UI/ExportSheet.h"
#include "Rendering/VectorRenderer.h"
#include "UI/KeyboardShortcuts.h"
#include <QBuffer>
#include <QCheckBox>
#include <QComboBox>
#include <QHBoxLayout>
#include <QImageReader>
#include <QImageWriter>
#include <QLabel>
#include <QLocale>
#include <QPainter>
#include <QPushButton>
#include <QSettings>
#include <QSlider>
#include <QTimer>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>

const QString ExportSheet::qualityKey = QStringLiteral("jpegExportQuality");

namespace {
QLabel *text(const QString &words, int pixels, QFont::Weight weight, QPalette::ColorRole role, QWidget *parent)
{
    auto *label = new QLabel(words, parent);
    label->setTextFormat(Qt::PlainText);
    QFont font = label->font();
    font.setPixelSize(pixels);
    font.setWeight(weight);
    label->setFont(font);
    label->setForegroundRole(role);
    return label;
}

QString fileSize(qint64 bytes)
{
    return QLocale(QLocale::English, QLocale::UnitedStates).formattedDataSize(bytes, 1, QLocale::DataSizeSIFormat);
}

constexpr std::array scales{1.0, 2.0, 3.0, 4.0};
}

// Dark gray behind the fitted preview, a checkerboard under transparency.
class ExportSheet::Preview : public QWidget {
public:
    using QWidget::QWidget;
    QImage image;

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.fillRect(rect(), QColor::fromRgbF(0.12f, 0.12f, 0.12f));
        if (image.isNull())
            return;
        QRectF target(QPointF(), QSizeF(image.size()).scaled(QSizeF(size()).shrunkBy(QMarginsF(12, 12, 12, 12)), Qt::KeepAspectRatio));
        target.moveCenter(QRectF(rect()).center());
        QPixmap checks(16, 16);
        checks.fill(Qt::white);
        QPainter tile(&checks);
        tile.fillRect(0, 0, 8, 8, QColor(204, 204, 204));
        tile.fillRect(8, 8, 8, 8, QColor(204, 204, 204));
        painter.fillRect(target, QBrush(checks));
        painter.setRenderHint(QPainter::SmoothPixmapTransform);
        painter.drawImage(target, image);
    }
};

ExportSheet::ExportSheet(const VectorDocument &document, DocumentExporter::Format format, std::function<void(std::optional<RasterOptions>)> finish,
                         QWidget *parent)
    : QWidget(parent), m_document(document), m_format(format), m_finish(std::move(finish)), m_wait(new QTimer(this)),
      m_preview(new Preview(this)), m_scale(new QComboBox(this)), m_quality(new QSlider(Qt::Horizontal, this)),
      m_percent(text(QString(), 13, QFont::Normal, QPalette::WindowText, this)),
      m_transparent(new QCheckBox(QStringLiteral("Transparent background"), this)),
      m_size(text(QString(), 13, QFont::Normal, QPalette::PlaceholderText, this)),
      m_note(text(QString(), 13, QFont::Normal, QPalette::PlaceholderText, this)), m_export(new QPushButton(QStringLiteral("Export…"), this))
{
    setObjectName(QStringLiteral("exportSheet"));
    const bool jpeg = format == DocumentExporter::Format::jpeg;
    bool number = false;
    const int saved = QSettings().value(qualityKey).toInt(&number);
    m_options.quality = number ? std::clamp(saved, 0, 100) : 90;
    auto *column = new QVBoxLayout(this);
    column->setContentsMargins(24, 24, 24, 24);
    column->setSpacing(16);
    column->addWidget(text(jpeg ? QStringLiteral("Export JPEG") : QStringLiteral("Export PNG"), 17, QFont::Bold, QPalette::WindowText, this));
    m_preview->setObjectName(QStringLiteral("exportPreview"));
    m_preview->setFixedSize(560, 330);
    column->addWidget(m_preview);

    auto *scale = new QHBoxLayout;
    scale->addWidget(new QLabel(QStringLiteral("Resolution"), this));
    m_scale->setObjectName(QStringLiteral("exportScale"));
    for (const double each : scales)
        m_scale->addItem(QStringLiteral("%1× · %2 ppi").arg(each).arg(72 * each));
    connect(m_scale, &QComboBox::activated, this, [this](int index) {
        m_options.scale = scales.at(size_t(index));
        request();
    });
    scale->addWidget(m_scale);
    scale->addStretch(1);
    column->addLayout(scale);

    auto *quality = new QHBoxLayout;
    auto *qualityLabel = new QLabel(QStringLiteral("Quality"), this);
    qualityLabel->setBuddy(m_quality);
    m_quality->setObjectName(QStringLiteral("jpegQuality"));
    m_quality->setRange(0, 100);
    m_quality->setValue(m_options.quality);
    connect(m_quality, &QSlider::valueChanged, this, [this](int value) {
        m_options.quality = value;
        request();
    });
    m_percent->setObjectName(QStringLiteral("jpegPercent"));
    m_percent->setFixedWidth(45);
    m_percent->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    quality->addWidget(qualityLabel);
    quality->addWidget(m_quality, 1);
    quality->addWidget(m_percent);
    auto *qualityRow = new QWidget(this);
    qualityRow->setLayout(quality);
    quality->setContentsMargins(0, 0, 0, 0);
    qualityRow->setVisible(jpeg);
    column->addWidget(qualityRow);

    // JPEG has no alpha: the artboard's paper shows.
    m_transparent->setObjectName(QStringLiteral("exportTransparent"));
    m_transparent->setVisible(!jpeg);
    connect(m_transparent, &QCheckBox::toggled, this, [this](bool transparent) {
        m_options.transparent = transparent;
        request();
    });
    column->addWidget(m_transparent);

    auto *buttons = new QHBoxLayout;
    m_size->setObjectName(QStringLiteral("exportSize"));
    m_note->setObjectName(QStringLiteral("exportBytes"));
    buttons->addWidget(m_size);
    buttons->addWidget(m_note);
    buttons->addStretch(1);
    auto *cancel = new QPushButton(QStringLiteral("Cancel"), this);
    cancel->setObjectName(QStringLiteral("exportCancel"));
    cancel->setAutoDefault(false);
    connect(cancel, &QPushButton::clicked, this, [this] { m_finish(std::nullopt); });
    m_export->setObjectName(QStringLiteral("exportConfirm"));
    m_export->setDefault(true);
    NativeShortcut::bind(*this, m_export, cancel);
    connect(m_export, &QPushButton::clicked, this, [this] {
        if (m_format == DocumentExporter::Format::jpeg)
            QSettings().setValue(qualityKey, m_options.quality);
        m_finish(m_options);
    });
    buttons->addWidget(cancel);
    buttons->addWidget(m_export);
    column->addLayout(buttons);

    m_wait->setObjectName(QStringLiteral("exportWait"));
    m_wait->setSingleShot(true);
    m_wait->setInterval(200);
    connect(m_wait, &QTimer::timeout, this, &ExportSheet::encode);
    request();
}

// A pause of 200 ms: a slider drag encodes once.
void ExportSheet::request()
{
    m_bytes = -1;
    m_wait->start();
    synchronize();
}

void ExportSheet::encode()
{
    m_wait->stop();
    const bool jpeg = m_format == DocumentExporter::Format::jpeg;
    const QImage rendered = VectorRenderer::render(m_document, m_options.scale, !jpeg && m_options.transparent);
    QByteArray data;
    QBuffer buffer(&data);
    buffer.open(QIODevice::WriteOnly);
    QImageWriter writer(&buffer, jpeg ? "jpeg" : "png");
    if (jpeg)
        writer.setQuality(m_options.quality);
    writer.write(jpeg ? rendered.convertToFormat(QImage::Format_RGB32) : rendered);
    m_bytes = data.size();
    // JPEG shows what its quality does to the picture.
    m_preview->image = jpeg ? QImage::fromData(data, "jpeg") : rendered;
    m_preview->update();
    synchronize();
}

void ExportSheet::synchronize()
{
    const QLocale english(QLocale::English, QLocale::UnitedStates);
    const qint64 width = std::lround(m_document.size.width() * m_options.scale), height = std::lround(m_document.size.height() * m_options.scale);
    m_size->setText(QStringLiteral("%1 × %2 px").arg(english.toString(width), english.toString(height)));
    m_percent->setText(QStringLiteral("%1%").arg(m_options.quality));
    m_note->setText(m_bytes >= 0 ? QStringLiteral("· ") + fileSize(m_bytes) : QStringLiteral("· Updating preview…"));
    m_export->setEnabled(width > 0 && height > 0);
}
