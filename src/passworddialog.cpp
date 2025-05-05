/*
 * <one line to give the program's name and a brief idea of what it does.>
 * Copyright (C) 2018  <copyright holder> <email>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include "passworddialog.h"
#include "ui_passworddialog.h"
#include <QInputDialog>


PasswordDialog::PasswordDialog(QWidget* parent):
    QDialog{parent},
    ui_{new Ui::PasswordDialog} {

    ui_->setupUi(this);
    connect(ui_->showPassword, &QCheckBox::toggled, this, &PasswordDialog::onTogglePassword);
}

PasswordDialog::~PasswordDialog() {
}

QString PasswordDialog::password() const {
    return ui_->passwordEdit->text();
}

void PasswordDialog::setPassword(const QString& password) {
    ui_->passwordEdit->setText(password);
}

void PasswordDialog::setEncryptFileList(bool value) {
    ui_->encryptFileList->setChecked(value);
}

bool PasswordDialog::encryptFileList() const {
    return ui_->encryptFileList->isChecked();
}


void PasswordDialog::onTogglePassword(bool toggled) {
    ui_->passwordEdit->setEchoMode(toggled ? QLineEdit::Normal : QLineEdit::Password);
}

// static
QString PasswordDialog::askPassword(QWidget* parent) {
    QInputDialog dlg;
    return QInputDialog::getText(parent, tr("Password"), tr("Password:"), QLineEdit::Password);
}

// static
// NOTE: This password dialog is made especially for RAR files without an extraction dialog,
// because 7z neither extracts them nor shows any error when there are empty or incomplete
// extracted files, but the extraction will succeed with a correct password if files are
// overwritten. However, it can be used anywhere no extraction dialog is shown.
QString PasswordDialog::askPasswordAndOverwrite(bool& overwrite, QWidget* parent) {
    QDialog dlg(parent);
    dlg.setWindowTitle(tr("Password"));
    QLabel *label = new QLabel(tr("Password:"));
    QLineEdit *le = new QLineEdit();
    le->setEchoMode(QLineEdit::Password);
    QCheckBox *check = new QCheckBox(tr("Overwrite existing files"));
    QDialogButtonBox *btns = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    QObject::connect(btns, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    QObject::connect(btns, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    QVBoxLayout *layout = new QVBoxLayout;
    layout->addWidget(label);
    layout->addWidget(le);
    layout->addWidget(check);
    layout->addWidget(btns);
    dlg.setLayout(layout);
    dlg.setMaximumHeight(dlg.minimumSizeHint().height()); // no vertical resizing
    le->setFocus(); // needed with Qt >= 6.6.1

    QString psswrd;
    switch (dlg.exec()) {
    case QDialog::Accepted:
        psswrd = le->text();
        overwrite = check->isChecked();
        break;
    default:
        break;
    }

    return psswrd;
}
