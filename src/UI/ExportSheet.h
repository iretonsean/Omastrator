#pragma once
#include "UI/ProjectWorkspace.h"
#include <QWidget>
#include <functional>
#include <optional>

class QCheckBox;
class QComboBox;
class QLabel;
class QPushButton;
class QSlider;
class QTimer;

// PNG and JPEG export: scale, quality, paper, encoded preview.
class ExportSheet : public QWidget {
    Q_OBJECT
public:
    // The last export's quality, where the next one starts.
    static const QString qualityKey;

    ExportSheet(const VectorDocument &document, DocumentExporter::Format format, std::function<void(std::optional<RasterOptions>)> finish,
                QWidget *parent = nullptr);
    const RasterOptions &options() const { return m_options; }
    // The encoded file's bytes at the chosen settings.
    qint64 encodedBytes() const { return m_bytes; }
    // Encodes now rather than after the pause.
    void encode();

private:
    class Preview;
    void request();
    void synchronize();

    const VectorDocument m_document;
    const DocumentExporter::Format m_format;
    const std::function<void(std::optional<RasterOptions>)> m_finish;
    RasterOptions m_options;
    qint64 m_bytes = -1;
    QTimer *const m_wait;
    Preview *const m_preview;
    QComboBox *const m_scale;
    QSlider *const m_quality;
    QLabel *const m_percent;
    QCheckBox *const m_transparent;
    QLabel *const m_size;
    QLabel *const m_note;
    QPushButton *const m_export;
};
