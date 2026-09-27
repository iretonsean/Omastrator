#pragma once
#include "System/TokenFiles.h"
#include <map>
#include <set>

// What the token file readers and writers share.
namespace TokenFileParts {
struct Declaration {
    QString name;
    QString value;
    qsizetype valueStart = 0;
    qsizetype valueEnd = 0;
    // "theme" (Tailwind's @theme), "root", "dark" or "other".
    QString context;
};
struct Block {
    QString selector;
    QString context;
    qsizetype open = 0;
    qsizetype close = 0;
};
// Every custom property declared in a block, with where its value sits.
std::vector<Declaration> scan(const QString &css, std::vector<Block> *blocks);
QString slug(const QString &text);
QString prefixOf(TokenKind kind);
bool isAlias(TokenKind kind, const QString &segment);
QString numeral(double value);
QString familyOf(const QString &stack);
int weightOf(const QString &value);
}
