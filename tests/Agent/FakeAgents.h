#pragma once
#include <QDir>
#include <QFile>
#include <QStringList>

// Stand-ins for claude, codex, opencode and gemini. Each records its folder, socket,
// opencode config and arguments as $FAKE_OUT/<name>.*, the arguments last and whole
// (a test that sees them may read the rest), then does what $FAKE_MODE says:
// roast, variations or overlay (answers through "$OMASTRATOR_BIN" agent),
// quiet (exits without answering), fail (an error on stderr), hang, or edithang
// (appends a line to $FAKE_EDIT_FILE in its folder, then hangs).
namespace FakeAgents {
inline constexpr const char *script = R"sh(#!/bin/sh
out="$FAKE_OUT/$(basename "$0")"
pwd > "$out.cwd"
printf '%s' "$OPENCODE_CONFIG_CONTENT" > "$out.config"
printf '%s' "$OMASTRATOR_SOCKET" > "$out.socket"
: > "$out.argv.part"
for argument in "$@"; do printf '%s\000' "$argument" >> "$out.argv.part"; done
mv "$out.argv.part" "$out.argv"
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
overlay)
  "$OMASTRATOR_BIN" agent insert_svg "{\"svg\": \"<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 40 20'><rect width='40' height='20' fill='#00ff00'/></svg>\", \"name\": \"Mock-up\", \"at\": [5, 5]}" || exit 1
  "$OMASTRATOR_BIN" agent proposal_finish "{\"title\": \"Mock-up\", \"summary\": \"A green box over it.\"}" || exit 1
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
edithang)
  echo "// fixed by the fake agent" >> "$FAKE_EDIT_FILE"
  sleep 300 &
  echo $! > "$out.child"
  wait
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
