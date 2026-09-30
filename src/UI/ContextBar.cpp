#include "UI/ContextBar.h"
#include "Agent/AgentProtocol.h"
#include "Agent/Dictation.h"
#include "Agent/Island.h"
#include "Document/EditorSession.h"
#include "UI/AgentBridge.h"
#include "UI/AgentPanels.h"
#include <QDir>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QStandardPaths>
#include <QToolButton>
#include <QtConcurrent>
#include <csignal>

namespace {
QString kindName(ObjectKind kind)
{
    switch (kind) {
    case ObjectKind::layer: return QStringLiteral("Layer");
    case ObjectKind::group: return QStringLiteral("Group");
    case ObjectKind::path: return QStringLiteral("Path");
    case ObjectKind::text: return QStringLiteral("Text");
    case ObjectKind::image: return QStringLiteral("Image");
    case ObjectKind::frame: return QStringLiteral("Frame");
    }
    return QString();
}

QString recorderProgram()
{
    const QString overridden = qEnvironmentVariable("OMASTRATOR_PW_RECORD");
    return overridden.isEmpty() ? QStringLiteral("pw-record") : overridden;
}
}

ContextBar::ContextBar(EditorSession &session, AgentBridge *agent, QWidget *parent)
    : QWidget(parent), m_session(session), m_agent(agent), m_selection(new QLabel(this))
{
    setObjectName(QStringLiteral("contextBar"));
    m_selection->setObjectName(QStringLiteral("contextSelection"));
    m_selection->setMinimumWidth(160);
    auto *row = new QHBoxLayout(this);
    row->setContentsMargins(14, 4, 10, 4);
    row->setSpacing(10);
    row->addWidget(m_selection);
    if (m_agent) {
        // The agent's state (waiting, then Keep and Discard) sits in this row, full width, never over the art.
        m_proposal = new ProposalBar(*m_agent, session, this);
        row->addWidget(m_proposal, 1);
        m_ask = new QLineEdit(this);
        m_ask->setObjectName(QStringLiteral("askField"));
        m_ask->setPlaceholderText(QStringLiteral("Ask the agent to change the selection…"));
        m_ask->setClearButtonEnabled(true);
        m_ask->setMinimumWidth(280);
        m_ask->setMaximumWidth(460);
        row->addWidget(m_ask, 1);
        m_mic = new QToolButton(this);
        m_mic->setObjectName(QStringLiteral("askMic"));
        m_mic->setText(QStringLiteral("Mic"));
        // The icon theme may not have a microphone; the word stands in.
        m_mic->setIcon(QIcon::fromTheme(QStringLiteral("audio-input-microphone")));
        m_mic->setToolButtonStyle(m_mic->icon().isNull() ? Qt::ToolButtonTextOnly : Qt::ToolButtonIconOnly);
        m_mic->setToolTip(QStringLiteral("Hold to speak (Super+Alt+V)"));
        m_mic->setAccessibleName(QStringLiteral("Hold to speak"));
        m_mic->setCheckable(true);
        row->addWidget(m_mic);
        connect(m_ask, &QLineEdit::returnPressed, this, &ContextBar::ask);
        connect(m_mic, &QToolButton::pressed, this, &ContextBar::startListening);
        connect(m_mic, &QToolButton::released, this, &ContextBar::stopListening);
        connect(m_agent, &AgentBridge::proposalChanged, this, &ContextBar::synchronize);
        connect(m_agent, &AgentBridge::waitingChanged, this, &ContextBar::synchronize);
    } else {
        row->addStretch(1);
    }
    connect(&m_session, &EditorSession::changed, this, &ContextBar::synchronize);
    synchronize();
}

// A recording left running would hold the mic after the window goes.
ContextBar::~ContextBar()
{
    if (m_recorder.state() != QProcess::NotRunning) {
        m_recorder.kill();
        m_recorder.waitForFinished(1000);
    }
    disconnect(&m_session, &EditorSession::changed, this, &ContextBar::synchronize);
}

void ContextBar::synchronize()
{
    const std::optional<VectorDocument> &document = m_session.document();
    QString text;
    if (document) {
        const std::vector<QUuid> &selection = m_session.selection();
        if (selection.empty()) {
            text = QStringLiteral("Nothing selected");
        } else if (selection.size() == 1) {
            const VectorObject *object = document->find(selection.front());
            const QRectF box = document->bounds(selection.front());
            const QLocale english(QLocale::English, QLocale::UnitedStates);
            const QString name = object ? (object->name.isEmpty() ? kindName(object->kind) : object->name) : QString();
            text = QStringLiteral("%1 · %2 × %3").arg(name, english.toString(box.width(), 'f', 0), english.toString(box.height(), 'f', 0));
        } else {
            text = QStringLiteral("%1 objects").arg(selection.size());
        }
    }
    if (m_selection->text() != text)
        m_selection->setText(text);
    if (m_ask) {
        // One request at a time: a waiting agent or an open proposal comes first; without a document there's nothing to ask about.
        const bool open = document.has_value() && !m_agent->hasProposalIn(m_session) && !m_agent->waiting();
        m_ask->setEnabled(open);
        m_mic->setEnabled(open || isListening());
    }
}

void ContextBar::ask()
{
    const QString instruction = m_ask->text().trimmed();
    if (instruction.isEmpty() || !m_agent)
        return;
    if (const QString failure = m_agent->editWithInstruction(instruction); !failure.isEmpty()) {
        emit notice(failure);
        return;
    }
    m_ask->clear();
}

bool ContextBar::isListening() const
{
    return m_recorder.state() != QProcess::NotRunning;
}

void ContextBar::startListening()
{
    if (isListening() || m_transcribing || !m_ask || !m_ask->isEnabled())
        return;
    if (Dictation::voxtype().isEmpty()) {
        emit notice(Dictation::missingVoxtype());
        m_mic->setChecked(false);
        return;
    }
    const QString recorder = recorderProgram();
    if (QStandardPaths::findExecutable(recorder).isEmpty() && !QFileInfo(recorder).isExecutable()) {
        emit notice(QStringLiteral("pw-record isn't installed, so there's nothing to record with. It comes with PipeWire."));
        m_mic->setChecked(false);
        return;
    }
    QDir().mkpath(Island::runtimeDirectory());
    m_wav = QDir(Island::runtimeDirectory()).filePath(QStringLiteral("ask-dictation.wav"));
    QFile::remove(m_wav);
    // Whisper's rate, one channel; a minute at most in case the release never arrives.
    m_recorder.start(QStringLiteral("timeout"), {QStringLiteral("-s"), QStringLiteral("INT"), QStringLiteral("60"), recorder, QStringLiteral("--rate"),
                                                  QStringLiteral("16000"), QStringLiteral("--channels"), QStringLiteral("1"), QStringLiteral("--format"),
                                                  QStringLiteral("s16"), m_wav});
    if (!m_recorder.waitForStarted(3000)) {
        emit notice(QStringLiteral("Couldn't start recording."));
        m_mic->setChecked(false);
        return;
    }
    m_mic->setChecked(true);
    m_ask->setPlaceholderText(QStringLiteral("Listening…"));
}

void ContextBar::stopListening()
{
    if (!isListening())
        return;
    m_mic->setChecked(false);
    // SIGINT lets pw-record finish the WAV header; `timeout` passes it on.
    ::kill(pid_t(m_recorder.processId()), SIGINT);
    if (!m_recorder.waitForFinished(3000))
        m_recorder.kill();
    m_transcribing = true;
    m_ask->setPlaceholderText(QStringLiteral("Transcribing…"));
    const QString wav = m_wav;
    auto *watcher = new QFutureWatcher<std::pair<QString, QString>>(this);
    connect(watcher, &QFutureWatcher<std::pair<QString, QString>>::finished, this, [this, watcher] {
        const auto [words, error] = watcher->result();
        watcher->deleteLater();
        m_transcribing = false;
        m_ask->setPlaceholderText(QStringLiteral("Ask the agent to change the selection…"));
        if (!error.isEmpty()) {
            emit notice(error);
            return;
        }
        heard(words);
    });
    // Whisper takes a few seconds: off the UI thread.
    watcher->setFuture(QtConcurrent::run([wav] {
        QString error;
        const QString words = QFileInfo(wav).size() > 44 ? Dictation::transcribe(wav, &error) : QString();
        QFile::remove(wav);
        return std::pair(words, error);
    }));
}

void ContextBar::heard(const QString &words)
{
    const QString normalized = Dictation::normalize(words);
    const Dictation::Command command = Dictation::parse(normalized);
    if (command.tier == 0 || normalized.isEmpty()) {
        emit notice(QStringLiteral("Didn't catch that."));
        return;
    }
    if (command.tier == 1 && m_agent) {
        try {
            m_agent->tools().call(command.method, command.params);
            emit notice(QStringLiteral("Heard: “%1” → %2").arg(words.trimmed(), command.description));
            return;
        } catch (const AgentProtocol::Error &failure) {
            emit notice(failure.message());
            return;
        }
    }
    // Anything else waits in Ask, so a wrong word can be fixed before Enter.
    m_ask->setText(words.trimmed());
    m_ask->setFocus(Qt::OtherFocusReason);
    emit notice(QStringLiteral("Heard: “%1”. Press Enter to ask.").arg(words.trimmed()));
}
