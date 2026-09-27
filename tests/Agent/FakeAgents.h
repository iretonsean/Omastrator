#pragma once
#include <QDir>
#include <QFile>
#include <QStringList>

// Stand-ins for claude, codex, opencode and gemini. Each records its arguments,
// folder, socket and opencode config as $FAKE_OUT/<name>.*, then does what
// $FAKE_MODE says: roast or variations (answers through "$OMASTRATOR_BIN" agent),
// quiet (exits without answering), fail (an error on stderr) or hang.
namespace FakeAgents {
inline constexpr const char *script = R"sh(#!/bin/sh
out="$FAKE_OUT/$(basename "$0")"
: > "$out.argv"
for argument in "$@"; do printf '%s\000' "$argument" >> "$out.argv"; done
pwd > "$out.cwd"
printf '%s' "$OPENCODE_CONFIG_CONTENT" > "$out.config"
printf '%s' "$OMASTRATOR_SOCKET" > "$out.socket"
task=""
for argument in "$@"; do case "$argument" in "Omastrator task:"*) task="$argument";; esac; done
request=$(printf '%s\n' "$task" | sed -n '1s/.*(request \([^)]*\)).*/\1/p')
case "$FAKE_MODE" in
roast)
  "$OMASTRATOR_BIN" agent show_roast "{\"requestId\": \"$request\", \"roast\": \"Headless, and it still hurts.\", \"feedback\": [{\"title\": \"Add contrast\", \"detail\": \"Use #111111 text.\"}], \"suggestedPrompt\": \"More contrast\"}" || exit 1
  echo "Roast delivered."
  ;;
variations)
  "$OMASTRATOR_BIN" agent show_variations "{\"requestId\": \"$request\", \"variations\": [{\"name\": \"Box\", \"svg\": \"<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 10 10'><rect width='10' height='10'/></svg>\"}]}" || exit 1
  ;;
quiet)
  echo "I looked at it and decided not to."
  ;;
fail)
  echo "Starting up"
  echo "Error: not logged in. Run /login" >&2
  echo "   " >&2
  exit 1
  ;;
hang)
  sleep 300 &
  echo $! > "$out.child"
  wait
  ;;
esac
exit 0
)sh";

// Writes the four fakes into `directory`/bin and returns that folder, for the front of PATH.
inline QString install(const QString &directory)
{
    const QString bin = QDir(directory).filePath(QStringLiteral("bin"));
    QDir().mkpath(bin);
    for (const char *name : {"claude", "codex", "opencode", "gemini"}) {
        QFile file(QDir(bin).filePath(QString::fromLatin1(name)));
        if (!file.open(QIODevice::WriteOnly))
            return {};
        file.write(script);
        file.close();
        file.setPermissions(QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
    }
    return bin;
}

inline QString read(const QString &path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? QString::fromUtf8(file.readAll()) : QString();
}

// The arguments a fake was given, as it received them.
inline QStringList arguments(const QString &out, const QString &name)
{
    QStringList list = read(QDir(out).filePath(name + QStringLiteral(".argv"))).split(QChar(0));
    if (!list.isEmpty() && list.last().isEmpty())
        list.removeLast();
    return list;
}
}
