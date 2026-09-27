#include "System/ProjectCode.h"
#include "System/TokenFiles.h"
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <algorithm>

namespace {
std::optional<QByteArray> contents(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return std::nullopt;
    return file.readAll();
}

bool skipped(const QString &relative)
{
    static const QStringList folders{"node_modules", ".git", "dist", "build", ".next", ".svelte-kit", "out", "coverage", "vendor", ".cache"};
    for (const QString &part : relative.split(QLatin1Char('/'))) {
        if (folders.contains(part))
            return true;
    }
    return false;
}
}

namespace ProjectCode {
QString Source::label() const
{
    const QString name = QFileInfo(path).fileName();
    switch (kind) {
    case Kind::w3c:
        return name + QStringLiteral(" (W3C design tokens)");
    case Kind::tailwind:
        return name + QStringLiteral(" (Tailwind v4 @theme)");
    case Kind::tailwindConfig:
        return name + QStringLiteral(" (Tailwind v3 config, read only)");
    case Kind::css:
        return name + QStringLiteral(" (CSS custom properties)");
    }
    return name;
}

std::vector<Source> detect(const QString &folder)
{
    std::vector<Source> found;
    const QDir root(folder);
    for (const QString &name : {QStringLiteral("tokens.json"), QStringLiteral("design-tokens.json"), QStringLiteral("tokens/tokens.json"),
                                QStringLiteral("src/tokens.json"), QStringLiteral("src/design-tokens.json")}) {
        if (QFileInfo(root.filePath(name)).isFile())
            found.push_back({Source::Kind::w3c, QFileInfo(root.filePath(name)).absoluteFilePath()});
    }
    for (const QString &name : {QStringLiteral("tailwind.config.js"), QStringLiteral("tailwind.config.cjs"), QStringLiteral("tailwind.config.mjs"),
                                QStringLiteral("tailwind.config.ts")}) {
        if (QFileInfo(root.filePath(name)).isFile())
            found.push_back({Source::Kind::tailwindConfig, QFileInfo(root.filePath(name)).absoluteFilePath()});
    }
    QDirIterator walk(folder, {QStringLiteral("*.css")}, QDir::Files, QDirIterator::Subdirectories);
    std::vector<Source> styles;
    while (walk.hasNext()) {
        const QString path = walk.next();
        const QString relative = root.relativeFilePath(path);
        if (skipped(relative) || relative.count(QLatin1Char('/')) > 4)
            continue;
        const auto css = contents(path);
        if (!css || css->size() > 2 * 1024 * 1024)
            continue;
        if (css->contains("@theme"))
            styles.push_back({Source::Kind::tailwind, QFileInfo(path).absoluteFilePath()});
        else if (css->contains(":root") && css->contains("--"))
            styles.push_back({Source::Kind::css, QFileInfo(path).absoluteFilePath()});
    }
    std::sort(styles.begin(), styles.end(), [](const Source &a, const Source &b) { return a.path < b.path; });
    found.insert(found.end(), styles.begin(), styles.end());
    return found;
}

Pulled read(const std::vector<Source> &sources)
{
    Pulled pulled;
    std::vector<Source> ordered = sources;
    const auto rank = [](Source::Kind kind) {
        switch (kind) {
        case Source::Kind::tailwindConfig:
            return 0;
        case Source::Kind::css:
            return 1;
        case Source::Kind::tailwind:
            return 2;
        case Source::Kind::w3c:
            return 3;
        }
        return 0;
    };
    std::stable_sort(ordered.begin(), ordered.end(), [&](const Source &a, const Source &b) { return rank(a.kind) < rank(b.kind); });
    for (const Source &source : ordered) {
        const auto bytes = contents(source.path);
        if (!bytes)
            continue;
        pulled.reads.append(source.path);
        TokenFiles::Read read;
        switch (source.kind) {
        case Source::Kind::w3c:
            read = TokenFiles::readW3c(*bytes);
            break;
        case Source::Kind::tailwind:
            read = TokenFiles::readTailwind(*bytes);
            break;
        case Source::Kind::tailwindConfig:
            read = TokenFiles::readTailwindConfig(*bytes);
            break;
        case Source::Kind::css:
            read = TokenFiles::readCss(*bytes);
            break;
        }
        DesignTokens::merge(pulled.tokens, read.tokens);
        for (const QString &mode : read.modes) {
            if (!pulled.modes.contains(mode))
                pulled.modes.append(mode);
        }
        for (const QString &skip : read.skipped)
            pulled.skipped.append(QFileInfo(source.path).fileName() + QStringLiteral(": ") + skip);
    }
    return pulled;
}

SyncPlan pushPlan(const QString &folder, const std::vector<DesignToken> &tokens, const QStringList &modes)
{
    SyncPlan plan;
    plan.direction = SyncPlan::Direction::push;
    plan.title = QStringLiteral("Push Tokens to Code");
    const QString top = QFileInfo(folder).absoluteFilePath();
    if (!QFileInfo(top).isDir()) {
        plan.problem = QStringLiteral("The project folder %1 doesn't exist.").arg(top);
        return plan;
    }
    if (tokens.empty()) {
        plan.problem = QStringLiteral("This document has no tokens to push.");
        return plan;
    }
    std::vector<Source> sources = detect(top);
    bool wrote = false;
    for (const Source &source : sources) {
        if (source.kind == Source::Kind::tailwindConfig)
            continue;
        const std::optional<QByteArray> before = contents(source.path);
        QByteArray after;
        if (source.kind == Source::Kind::w3c)
            after = TokenFiles::writeW3c(tokens, before.value_or(QByteArray()));
        else if (source.kind == Source::Kind::tailwind)
            after = TokenFiles::writeTailwind(tokens, before.value_or(QByteArray()));
        else
            after = TokenFiles::writeCss(tokens, before.value_or(QByteArray()), modes);
        plan.writes.push_back({source.path, before, after, {}});
        wrote = true;
    }
    if (!std::any_of(sources.begin(), sources.end(), [](const Source &s) { return s.kind == Source::Kind::w3c; })) {
        const QString path = QDir(top).filePath(QStringLiteral("tokens.json"));
        plan.writes.push_back({path, std::nullopt, TokenFiles::writeW3c(tokens), {}});
        wrote = true;
    }
    Q_UNUSED(wrote);
    plan.git = SyncRunner::gitFor(top, QStringLiteral("Update design tokens from Omastrator"));
    plan.destination = plan.git
        ? QStringLiteral("The project at %1, committed to the git repository %2 on branch %3. Nothing is pushed to a remote.")
              .arg(top, plan.git->repository, plan.git->branch)
        : QStringLiteral("The project at %1 (not a git repository, so nothing is committed).").arg(top);
    return plan;
}

SyncPlan pullPlan(const QString &folder, const QString &documentName, std::function<QString(const Pulled &)> apply)
{
    SyncPlan plan;
    plan.direction = SyncPlan::Direction::pull;
    plan.title = QStringLiteral("Pull Tokens from Code");
    const QString top = QFileInfo(folder).absoluteFilePath();
    const std::vector<Source> sources = detect(top);
    if (sources.empty()) {
        plan.problem = QStringLiteral("No tokens.json, Tailwind theme or CSS variables were found in %1.").arg(top);
        return plan;
    }
    const Pulled pulled = read(sources);
    plan.reads = pulled.reads;
    plan.destination = QStringLiteral("This document, %1. No files are written and nothing is committed.").arg(documentName);
    plan.inApp = QStringLiteral("Merges %1 tokens by name into %2 as one undo step.").arg(pulled.tokens.size()).arg(documentName);
    if (!pulled.skipped.isEmpty())
        plan.inApp += QStringLiteral(" Skipped: %1.").arg(pulled.skipped.mid(0, 8).join(QStringLiteral(", ")));
    plan.apply = [apply, pulled] { return apply(pulled); };
    return plan;
}
}
