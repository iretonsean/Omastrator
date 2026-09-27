#include "UI/ObjectDialogs.h"
#include <QApplication>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFontComboBox>
#include <QLabel>
#include <QListWidget>
#include <QPointer>
#include <QPushButton>
#include <QStyle>
#include <QVBoxLayout>

// Type ▸ Find/Replace Font: the families in use, missing ones marked, and
// Replace All swapping one for another in a single undo step.
QDialog *ObjectDialogs::findFont(EditorSession &session, QWidget *window)
{
    auto *dialog = new QDialog(window);
    dialog->setObjectName(QStringLiteral("findFontDialog"));
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setWindowModality(Qt::WindowModal);
    dialog->setWindowTitle(QStringLiteral("Find/Replace Font"));
    auto *column = new QVBoxLayout(dialog);
    column->setContentsMargins(24, 20, 24, 20);
    column->setSpacing(10);
    auto *scope = new QComboBox(dialog);
    scope->setObjectName(QStringLiteral("findFontScope"));
    scope->setAccessibleName(QStringLiteral("Look in"));
    scope->addItems({QStringLiteral("Fonts in the document"), QStringLiteral("Fonts in the selection")});
    column->addWidget(scope);
    auto *list = new QListWidget(dialog);
    list->setObjectName(QStringLiteral("findFontList"));
    list->setAccessibleName(QStringLiteral("Fonts used"));
    list->setMinimumSize(320, 180);
    column->addWidget(list, 1);
    auto *note = new QLabel(dialog);
    note->setObjectName(QStringLiteral("findFontNote"));
    note->setWordWrap(true);
    note->setForegroundRole(QPalette::PlaceholderText);
    column->addWidget(note);
    column->addWidget(new QLabel(QStringLiteral("Replace with:"), dialog));
    auto *replacement = new QFontComboBox(dialog);
    replacement->setObjectName(QStringLiteral("findFontReplacement"));
    replacement->setAccessibleName(QStringLiteral("Replace with"));
    column->addWidget(replacement);
    auto *buttons = new QDialogButtonBox(dialog);
    QPushButton *find = buttons->addButton(QStringLiteral("Find"), QDialogButtonBox::ActionRole);
    find->setObjectName(QStringLiteral("findFontFind"));
    find->setToolTip(QStringLiteral("Select the text that uses this font"));
    QPushButton *replace = buttons->addButton(QStringLiteral("Replace All"), QDialogButtonBox::ActionRole);
    replace->setObjectName(QStringLiteral("findFontReplaceAll"));
    QPushButton *done = buttons->addButton(QStringLiteral("Done"), QDialogButtonBox::AcceptRole);
    done->setObjectName(QStringLiteral("dialogOK"));
    column->addWidget(buttons);

    const QPointer<EditorSession> watched(&session);
    const auto refill = [watched, list, scope, note, find, replace] {
        if (!watched)
            return;
        const QString chosen = list->currentItem() ? list->currentItem()->data(Qt::UserRole).toString() : QString();
        list->clear();
        const QStringList used = watched->usedFonts(scope->currentIndex() == 1);
        int missing = 0;
        for (const QString &family : used) {
            const bool installed = EditorSession::isFontInstalled(family);
            auto *item = new QListWidgetItem(installed ? family : QStringLiteral("%1 (missing)").arg(family), list);
            item->setData(Qt::UserRole, family);
            if (!installed) {
                ++missing;
                item->setIcon(QApplication::style()->standardIcon(QStyle::SP_MessageBoxWarning));
                item->setToolTip(QStringLiteral("Not installed: this text shows in a stand-in font"));
            }
            if (family == chosen || (chosen.isEmpty() && !installed && !list->currentItem()))
                list->setCurrentItem(item);
        }
        if (!list->currentItem() && list->count() > 0)
            list->setCurrentRow(0);
        note->setText(used.isEmpty() ? QStringLiteral("No text uses a font here.")
                      : missing == 1 ? QStringLiteral("One font isn’t installed. Replace it to see the text as it was made.")
                      : missing ? QStringLiteral("%1 fonts aren’t installed. Replace them to see the text as it was made.").arg(missing)
                                : QStringLiteral("Every font is installed."));
        find->setEnabled(list->currentItem() != nullptr);
        replace->setEnabled(list->currentItem() != nullptr);
    };
    QObject::connect(scope, &QComboBox::currentIndexChanged, dialog, refill);
    QObject::connect(list, &QListWidget::currentItemChanged, dialog, [find, replace, list] {
        find->setEnabled(list->currentItem() != nullptr);
        replace->setEnabled(list->currentItem() != nullptr);
    });
    QObject::connect(find, &QPushButton::clicked, dialog, [watched, list] {
        if (watched && list->currentItem())
            watched->selectTextsUsing(list->currentItem()->data(Qt::UserRole).toString());
    });
    QObject::connect(replace, &QPushButton::clicked, dialog, [watched, list, scope, replacement, refill] {
        if (!watched || !list->currentItem())
            return;
        watched->replaceFont(list->currentItem()->data(Qt::UserRole).toString(), replacement->currentFont().family(), scope->currentIndex() == 1);
        refill();
    });
    QObject::connect(buttons, &QDialogButtonBox::accepted, dialog, &QDialog::accept);
    refill();
    dialog->open();
    return dialog;
}
