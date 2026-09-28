#pragma once
#include <QString>

// `omastrator browser-host`: the native messaging host Omastrator's Chromium
// extension talks to (docs/OS-SUITE.md, "Live in your own browser").
// Chromium starts it and speaks length-prefixed JSON on stdin and stdout; it
// passes each message on to Omastrator's browser socket as one line, and each
// line back. It says hello with Chromium's process id (its parent), tells the
// extension whether Omastrator is there ({"type": "link", "connected"}), and
// keeps trying to reach it. {"type": "wake"} starts Omastrator in the
// background. Nothing but framed messages may reach stdout.
namespace BrowserHost {
// $XDG_RUNTIME_DIR/omastrator-browser.sock, or $OMASTRATOR_BROWSER_SOCKET.
QString socketPath();
// The native messaging host's name, as its manifest and the extension use it.
inline constexpr const char *name = "io.github.iretonsean.omastrator";
int run();
}
