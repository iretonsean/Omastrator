#pragma once
#include <QString>
#include <QStringList>

// Omastrator's design vocabulary for dictation (docs/OS-SUITE.md): the words
// Whisper should expect. Setup writes the default to vocabulary.txt, where
// the user can add their own; one term per line, # starts a comment.
namespace Vocabulary {
QString defaultText();
// $XDG_CONFIG_HOME/omastrator/vocabulary.txt.
QString path();
// The user's file, else the default: terms only.
QStringList load();
QStringList parse(const QString &text);
}
