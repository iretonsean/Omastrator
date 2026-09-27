#pragma once
#include <QString>
#include <stdexcept>

// Why a file could not be read or written, in words an alert can show.
struct FileError : std::runtime_error {
    explicit FileError(const QString &message) : std::runtime_error(message.toStdString()) {}
    QString message() const { return QString::fromStdString(what()); }
};
