#include "Canvas/EditorCanvas.h"
#include "Document/EditorSession.h"
#include "Live/DevServers.h"
#include "Live/PageTemplates.h"
#include "System/PagePlan.h"
#include "System/TokenFiles.h"
#include "UI/AgentBridge.h"
#include "UI/BrowserViews.h"
#include "UI/GeneratePageSheet.h"
#include "UI/SyncConfirmDialog.h"
#include <QDir>
#include <QFileInfo>
#include <QTimer>
#include <algorithm>

// Generate a page in an empty Browser View, and Build It from one (docs/MOTION.md, section 4). The agent writes a new project
// into a staging folder; the plan lists the real files; Confirm creates the folder, its repository and one commit, and starts
// the dev server the frame then shows. Nothing exists before Confirm, and Stop or Esc ends the run at any point before it.

using Line = AgentBridge::ActivityLine;

struct BrowserViews::Generation {
    QString folder;
    QString staging;
    // The agent's run, so Stop ends this run and no other task.
    QString requestId;
    PageTemplates::Stack stack = PageTemplates::Stack::viteTailwind;
    QString description;
    // Build It from an empty frame: no agent writes the page; the design is built into the new project once its page is up.
    bool build = false;
    QString note;
    // The agent is writing.
    bool agentRunning = false;
    // The template as staged, so an agent that changed nothing is noticed.
    std::vector<PageTemplates::File> starting;
    std::vector<Line> lines;
    // Indexes into `lines`; -1 where the stack has no such step.
    int writing = -1, commit = -1, install = -1, server = -1;
    QMetaObject::Connection steps;
};

namespace {
QString titleOf(const QString &folderName)
{
    QStringList words = QString(folderName).replace(QLatin1Char('-'), QLatin1Char(' ')).replace(QLatin1Char('_'), QLatin1Char(' ')).split(QLatin1Char(' '), Qt::SkipEmptyParts);
    for (QString &word : words)
        word[0] = word[0].toUpper();
    return words.isEmpty() ? QStringLiteral("Page") : words.join(QLatin1Char(' '));
}

bool sameFiles(const std::vector<PageTemplates::File> &a, const std::vector<PageTemplates::File> &b)
{
    if (a.size() != b.size())
        return false;
    for (size_t i = 0; i < a.size(); ++i)
        if (a[i].path != b[i].path || a[i].bytes != b[i].bytes)
            return false;
    return true;
}

QString canonical(const QString &folder)
{
    const QString resolved = QFileInfo(folder).canonicalFilePath();
    return resolved.isEmpty() ? folder : resolved;
}
}

QString BrowserViews::noPage()
{
    return QStringLiteral("No page yet.");
}

BrowserViewHost::Empty BrowserViews::empty(const QUuid &frame) const
{
    Empty offer;
    const VectorObject *object = m_session.hasDocument() ? m_session.document()->find(frame) : nullptr;
    // A plain frame has no page to generate into until its Browser View switch is on, and one that is off shows nothing.
    if (!object || !object->showsPage() || !object->browser->url.isEmpty())
        return offer;
    if (const auto found = m_generations.constFind(frame); found != m_generations.constEnd()) {
        offer.generating = (*found)->agentRunning;
        return offer;
    }
    if (message(frame) != noPage())
        return offer;
    offer.offered = true;
    offer.build = hasDesign(frame);
    if (!offer.build)
        offer.buildLine = QStringLiteral("Design it with vectors and text first; Claude turns the frame into code.");
    return offer;
}

QString BrowserViews::generateLine(const QUuid &frame) const
{
    const auto found = m_generations.constFind(frame);
    if (found == m_generations.constEnd())
        return {};
    for (const Line &line : (*found)->lines)
        if (line.state == Line::State::running)
            return line.text + QStringLiteral("…");
    return QStringLiteral("Waiting for you to confirm the new project.");
}

bool BrowserViews::fillGenerating(const QUuid &frame, Bar &bar) const
{
    const auto found = m_generations.constFind(frame);
    if (found == m_generations.constEnd() || !m_agent)
        return false;
    bar.build = (*found)->agentRunning ? QStringLiteral("Writing with %1…").arg(AgentBridge::displayName(m_agent->waiting() ? m_agent->waiting()->agent : QString()))
                                       : QStringLiteral("Starting…");
    bar.buildBusy = true;
    bar.buildTip = QStringLiteral("Nothing is written to your project until you confirm. Click to stop.");
    return true;
}

QString BrowserViews::generatedProject(const QUuid &frame) const
{
    const auto found = m_generated.constFind(frame);
    if (found == m_generated.constEnd() || !m_session.hasDocument())
        return {};
    const VectorObject *object = m_session.document()->find(frame);
    // The frame still shows the dev server it was given: another address is another site.
    if (!object || !object->browser || found->dev.isEmpty() || object->browser->url.scheme() != found->dev.scheme()
        || object->browser->url.host() != found->dev.host() || object->browser->url.port() != found->dev.port())
        return {};
    return found->folder;
}

void BrowserViews::releaseGenerated(const QUuid &frame)
{
    const auto found = m_generated.find(frame);
    if (found == m_generated.end())
        return;
    const quint64 lease = found->lease;
    m_generated.erase(found);
    DevServers::shared().release(lease);
}

void BrowserViews::showActivity(const QUuid &frame)
{
    if (const auto found = m_generations.constFind(frame); found != m_generations.constEnd() && m_agent)
        m_agent->setActivity((*found)->lines);
    scheduleRepaint(frame);
    emit frameChanged(frame);
}

void BrowserViews::runGenerateAction(const QUuid &frame, Action action, const QString &note)
{
    if (!m_agent) {
        emit notice(QStringLiteral("Open the project's window to generate a page."));
        return;
    }
    if (action == Action::stopBuild || (action == Action::buildButton && m_generations.contains(frame))) {
        cancelGenerate(frame);
        return;
    }
    const VectorObject *object = m_session.hasDocument() ? m_session.document()->find(frame) : nullptr;
    if (!object || !object->showsPage() || !object->browser->url.isEmpty())
        return;
    if (m_generations.contains(frame)) {
        emit notice(QStringLiteral("A page is already being written for this frame."));
        return;
    }
    if (m_agent->waiting()) {
        emit notice(QStringLiteral("The agent is still working on another change."));
        return;
    }
    const bool build = action != Action::generatePage;
    const QString failure = GeneratePageSheet::open(
        m_canvas ? m_canvas->window() : nullptr, !build, object->name, [this, frame, build, note](const GeneratePageSheet::Answer &answer) {
            // Build It into a folder that already holds a project builds into it as it is, as Hand to Agent does.
            if (build && QFileInfo(answer.folder).isDir() && !PageTemplates::folderProblem(answer.folder).isEmpty())
                return startBuild(frame, answer.folder, note);
            return startGenerate(frame, answer.description, answer.stack, answer.folder, build, note);
        });
    if (!failure.isEmpty())
        emit notice(failure);
}

QString BrowserViews::startGenerate(const QUuid &frame, const QString &description, PageTemplates::Stack stack, const QString &folder, bool build,
                                    const QString &note)
{
    if (!m_agent)
        return QStringLiteral("Open the project's window to generate a page.");
    if (!m_session.hasDocument())
        return {};
    const VectorObject *object = m_session.document()->find(frame);
    if (!object || !object->browser)
        return QStringLiteral("That isn't a Browser View.");
    if (!object->browser->on)
        return QStringLiteral("Turn the frame's Browser View on first.");
    if (!object->browser->url.isEmpty())
        return QStringLiteral("This frame already has a page. Generate a page fills an empty one.");
    if (m_generations.contains(frame))
        return QStringLiteral("A page is already being written for this frame.");
    if (!build && description.trimmed().isEmpty())
        return QStringLiteral("Describe the page first.");
    const QString where = QDir::cleanPath(QDir(folder).absolutePath());
    if (const QString problem = PageTemplates::folderProblem(where); !problem.isEmpty())
        return problem;
    if (m_agent->waiting())
        return QStringLiteral("The agent is still working on another change.");
    // A page made here earlier (and undone) still holds its dev server: this run replaces it.
    releaseGenerated(frame);

    auto generation = std::make_shared<Generation>();
    generation->folder = where;
    generation->stack = stack;
    generation->description = description.trimmed();
    generation->build = build;
    generation->note = note;
    generation->starting = PageTemplates::files(stack, titleOf(QFileInfo(where).fileName()));
    // collect() lists files by path, and "the agent changed nothing" compares the two.
    std::sort(generation->starting.begin(), generation->starting.end(), [](const PageTemplates::File &a, const PageTemplates::File &b) { return a.path < b.path; });
    // The design system's tokens go into the template's own token file, the way Sync would write them.
    const VectorDocument &document = *m_session.document();
    bool tokens = false;
    if (!document.tokens.empty()) {
        const QString path = PageTemplates::tokenFile(stack);
        for (PageTemplates::File &file : generation->starting) {
            if (file.path != path)
                continue;
            file.bytes = PageTemplates::tokensAreTailwind(stack) ? TokenFiles::writeTailwind(document.tokens, file.bytes)
                                                                  : TokenFiles::writeCss(document.tokens, file.bytes, document.tokenModes);
            tokens = true;
        }
    }
    if (tokens)
        generation->lines.push_back({QStringLiteral("Using your design system tokens for colour and type"), Line::State::done});
    if (!build) {
        generation->writing = int(generation->lines.size());
        generation->lines.push_back({QStringLiteral("Writing the page from your description"), Line::State::running});
    }
    generation->commit = int(generation->lines.size());
    generation->lines.push_back({QStringLiteral("Commit “%1” to main").arg(PagePlan::commitMessage()), Line::State::pending});
    if (stack != PageTemplates::Stack::plainHtml) {
        generation->install = int(generation->lines.size());
        generation->lines.push_back({QStringLiteral("npm install"), Line::State::pending});
    }
    generation->server = int(generation->lines.size());
    generation->lines.push_back({QStringLiteral("Start the dev server"), Line::State::pending});
    m_generations.insert(frame, generation);

    if (build) {
        // Nothing to write first: the starter project is the plan, and the design is built into it afterwards.
        QTimer::singleShot(0, this, [this, frame, files = generation->starting] {
            if (m_generations.contains(frame))
                confirmGenerate(frame, files);
        });
        showActivity(frame);
        return {};
    }

    QString error;
    generation->staging = PageTemplates::makeStaging(&error);
    if (generation->staging.isEmpty()) {
        m_generations.remove(frame);
        return error;
    }
    if (const QString failure = PageTemplates::write(generation->staging, generation->starting); !failure.isEmpty()) {
        QDir(generation->staging).removeRecursively();
        m_generations.remove(frame);
        return failure;
    }
    AgentBridge::PageRequest request;
    request.staging = generation->staging;
    request.description = generation->description;
    request.stack = PageTemplates::describe(stack);
    for (const PageTemplates::File &file : generation->starting)
        request.files << file.path;
    request.tokenFile = tokens ? PageTemplates::tokenFile(stack) : QString();
    const QPointer<BrowserViews> guard(this);
    const QString staging = generation->staging;
    QString requestId;
    const QString failure = m_agent->generatePage(request, [guard, frame, staging](const AgentBridge::PageResult &result) {
        if (guard && guard->m_generations.contains(frame)) {
            guard->generateWritten(frame, result.cancelled, result.summary, result.error);
            return;
        }
        // Nobody is waiting for the files any more.
        QDir(staging).removeRecursively();
    }, &requestId);
    if (!failure.isEmpty()) {
        QDir(generation->staging).removeRecursively();
        m_generations.remove(frame);
        return failure;
    }
    generation->agentRunning = true;
    generation->requestId = requestId;
    // Where the steps show; the frame carries the current one over its empty page.
    m_agent->showLivePanel(false);
    showActivity(frame);
    return {};
}

void BrowserViews::generateWritten(const QUuid &frame, bool cancelled, const QString &summary, const QString &error)
{
    const auto found = m_generations.find(frame);
    if (found == m_generations.end())
        return;
    const std::shared_ptr<Generation> generation = *found;
    generation->agentRunning = false;
    if (cancelled) {
        // The bridge has removed the staging folder.
        generation->staging.clear();
        if (m_agent)
            m_agent->setActivity({});
        endGenerate(frame, QStringLiteral("Stopped. Nothing was written."));
        return;
    }
    if (!error.isEmpty()) {
        generation->staging.clear();
        generation->lines[generation->writing].state = Line::State::failed;
        showActivity(frame);
        endGenerate(frame, error);
        return;
    }
    QString problem;
    const std::vector<PageTemplates::File> files = PageTemplates::collect(generation->staging, &problem);
    QDir(generation->staging).removeRecursively();
    generation->staging.clear();
    if (!problem.isEmpty()) {
        generation->lines[generation->writing].state = Line::State::failed;
        showActivity(frame);
        endGenerate(frame, problem);
        return;
    }
    if (sameFiles(files, generation->starting)) {
        generation->lines[generation->writing].state = Line::State::failed;
        showActivity(frame);
        endGenerate(frame, summary.isEmpty() ? QStringLiteral("The agent didn't write a page.") : QStringLiteral("The agent didn't write a page: %1").arg(summary));
        return;
    }
    generation->lines[generation->writing].state = Line::State::done;
    showActivity(frame);
    // The agent's answer arrives through the socket, which waits for the reply: the plan opens after it.
    QTimer::singleShot(0, this, [this, frame, files] {
        if (m_generations.contains(frame))
            confirmGenerate(frame, files);
    });
}

void BrowserViews::confirmGenerate(const QUuid &frame, std::vector<PageTemplates::File> files)
{
    const auto found = m_generations.find(frame);
    if (found == m_generations.end())
        return;
    const std::shared_ptr<Generation> generation = *found;
    const VectorObject *object = m_session.hasDocument() ? m_session.document()->find(frame) : nullptr;
    if (!object || !object->browser) {
        endGenerate(frame, {});
        return;
    }
    if (!object->browser->url.isEmpty()) {
        endGenerate(frame, QStringLiteral("The frame has a page now, so nothing was written."));
        return;
    }
    SyncPlan plan = PagePlan::make(generation->folder, generation->stack, files, object->name);
    // The dialog runs its own event loop, and the agent's socket is answered meanwhile: a reset or a closed document can end this
    // object under it. Nothing below reads it without the guard.
    const QPointer<BrowserViews> guard(this);
    const QString folder = generation->folder;
    plan.apply = [guard, frame, folder] {
        if (!guard)
            return QString();
        if (!guard->m_generations.contains(frame)) {
            // Confirm was pressed after the run had ended (a reset): the files are written, and nothing is running.
            emit guard->notice(QStringLiteral("The project is in %1, but the page was reset while you were confirming, so no server is running for it.").arg(folder));
            return QString();
        }
        guard->serveGenerated(frame);
        return QString();
    };
    bool confirmed = false;
    const QString failure = SyncConfirmDialog::run(plan, guard && m_canvas ? m_canvas->window() : nullptr, &confirmed);
    if (!guard || !m_generations.contains(frame))
        return;
    if (!confirmed) {
        if (m_agent)
            m_agent->setActivity({});
        endGenerate(frame, plan.problem.isEmpty() ? QStringLiteral("Nothing was written.") : plan.problem);
        return;
    }
    if (!failure.isEmpty()) {
        generation->lines[generation->commit].state = Line::State::failed;
        showActivity(frame);
        endGenerate(frame, failure);
    }
}

void BrowserViews::serveGenerated(const QUuid &frame)
{
    const auto found = m_generations.find(frame);
    if (found == m_generations.end())
        return;
    const std::shared_ptr<Generation> generation = *found;
    const QString folder = generation->folder;
    generation->lines[generation->commit].state = Line::State::done;
    generation->lines[generation->commit].text = QStringLiteral("Committed “%1” to main").arg(PagePlan::commitMessage());
    if (generation->install >= 0)
        generation->lines[generation->install].state = Line::State::running;
    else
        generation->lines[generation->server].state = Line::State::running;
    // The server's own steps: installing, then starting.
    generation->steps = connect(&DevServers::shared(), &DevServers::step, this, [this, frame, folder](const QString &where, const QString &message) {
        if (DevServers::keyFor(folder) != where)
            return;
        const auto current = m_generations.find(frame);
        if (current == m_generations.end())
            return;
        Generation &state = **current;
        if (message.startsWith(QLatin1String("Installing")) && state.install >= 0) {
            state.lines[state.install].state = Line::State::running;
        } else if (message.startsWith(QLatin1String("Starting"))) {
            if (state.install >= 0)
                state.lines[state.install].state = Line::State::done;
            state.lines[state.server].state = Line::State::running;
        }
        showActivity(frame);
    });
    const quint64 lease = DevServers::shared().acquire(folder, this, [this, frame, folder](const DevServers::Result &result) {
        const auto current = m_generations.find(frame);
        if (current == m_generations.end() || !m_generated.contains(frame))
            return;
        const std::shared_ptr<Generation> state = *current;
        if (!result.error.isEmpty()) {
            // A lease that failed holds nothing.
            m_generated.remove(frame);
            if (state->install >= 0 && state->lines[state->install].state == Line::State::running)
                state->lines[state->install].state = Line::State::failed;
            else
                state->lines[state->server].state = Line::State::failed;
            showActivity(frame);
            endGenerate(frame, QStringLiteral("The project is in %1, but its dev server didn't start: %2").arg(folder, result.error.section(QLatin1Char('\n'), 0, 0)));
            return;
        }
        m_generated[frame].dev = result.url;
        // Switched off while it was starting: it freezes, and wakes with the switch.
        if (!browserViewOn(frame) && m_generated[frame].lease != 0)
            DevServers::shared().setPaused(m_generated[frame].lease, true);
        if (state->install >= 0)
            state->lines[state->install].state = Line::State::done;
        state->lines[state->server].state = Line::State::done;
        state->lines[state->server].text = QStringLiteral("Starting the dev server on %1:%2").arg(result.url.host(), QString::number(result.url.port()));
        showActivity(frame);
        const VectorObject *object = m_session.hasDocument() ? m_session.document()->find(frame) : nullptr;
        // One "Change URL" undo step; the page is the dev server's own address, so nothing is registered for it here.
        if (object && object->browser && object->browser->url.isEmpty())
            m_session.setBrowserUrl(frame, result.url);
        const bool build = state->build;
        const QString note = state->note;
        endGenerate(frame, build ? QString() : QStringLiteral("Page ready. Turn on Edit Page to select and change anything on it."));
        if (build) {
            const QString failure = startBuild(frame, folder, note);
            if (!failure.isEmpty())
                emit notice(failure);
        }
    });
    m_generated.insert(frame, Generated{canonical(folder), QUrl(), lease});
}

void BrowserViews::endGenerate(const QUuid &frame, const QString &text)
{
    const auto found = m_generations.find(frame);
    if (found != m_generations.end()) {
        const std::shared_ptr<Generation> generation = *found;
        m_generations.erase(found);
        disconnect(generation->steps);
        if (!generation->staging.isEmpty())
            QDir(generation->staging).removeRecursively();
    }
    scheduleRepaint(frame);
    emit frameChanged(frame);
    if (!text.isEmpty())
        emit notice(text);
}

void BrowserViews::cancelGenerate(const QUuid &frame)
{
    const auto found = m_generations.constFind(frame);
    if (found == m_generations.constEnd())
        return;
    const std::shared_ptr<Generation> generation = *found;
    if (generation->agentRunning && m_agent) {
        // The bridge ends this run, removes the staging folder and answers with cancelled. Another task the agent was given
        // meanwhile is not this run's to stop.
        m_agent->stopPage(generation->requestId);
        return;
    }
    // Serving: the project exists, and only the wait for its server ends.
    releaseGenerated(frame);
    if (m_agent)
        m_agent->setActivity({});
    endGenerate(frame, QStringLiteral("Stopped."));
}

void BrowserViews::abandonGenerations()
{
    QStringList writing;
    for (const std::shared_ptr<Generation> &generation : std::as_const(m_generations))
        if (generation->agentRunning)
            writing << generation->requestId;
    // With no generation left, the agent's answer only removes the staging folder.
    m_generations.clear();
    if (m_agent)
        for (const QString &id : std::as_const(writing))
            m_agent->stopPage(id);
}

void BrowserViews::stopGenerations()
{
    const QList<QUuid> frames = m_generations.keys();
    for (const QUuid &frame : frames)
        cancelGenerate(frame);
}
