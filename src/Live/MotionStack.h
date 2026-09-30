#pragma once
#include <QString>
#include <QStringList>

// What the agent is told about a project before it writes motion (docs/MOTION.md, section 4): its stack, the file its styles
// already live in, and where the motion tokens go. Read from the project's files; nothing is written.
namespace MotionStack {
struct Info {
    // "Vite + Tailwind v4", "Astro", "Plain HTML": how the project builds its CSS, in words the prompt can use.
    QString stack;
    // The style file the project already uses (relative), and where the motion tokens go: the file with `@theme` for
    // Tailwind v4, the one with `:root` custom properties, else the style file.
    QString styleFile;
    QString tokenFile;
    bool tailwindV4 = false;
    // Motion libraries in package.json (gsap, motion, framer-motion, animejs): what only the agent edits.
    QStringList libraries;
};

Info detect(const QString &folder);
}
