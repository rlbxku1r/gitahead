//
//          Copyright (c) 2021, Scientific Toolworks, Inc.
//
// This software is licensed under the MIT License. The LICENSE.md file
// describes the conditions under which this software may be distributed.
//

#include "PatchDialog.h"
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QSettings>
#include <QString>

namespace {

const QString kLastDirKey = "patch/lastdir";

} // anon. namespace

QString PatchDialog::getApplyFileName(QWidget *parent)
{
  QString path = QFileDialog::getOpenFileName(parent, tr("Apply Patch File"), lastDir(), filter());
  saveLastDir(path);
  return path;
}

QString PatchDialog::getSaveFileName(QWidget *parent)
{
  QString path = QFileDialog::getSaveFileName(parent, tr("Save Patch File"), lastDir(), filter());
  saveLastDir(path);
  return path;
}

QString PatchDialog::filter()
{
  return tr("Git Diff (*.patch *.diff);;All files (*)");
}

QString PatchDialog::lastDir()
{
  QString dir = QSettings().value(kLastDirKey).toString();

  if (dir.isEmpty() || !QDir(dir).exists())
    dir = QDir::homePath();

  return dir;
}

void PatchDialog::saveLastDir(const QString &path)
{
  if (path.isEmpty())
    return;

  QString dir = QFileInfo(path).absolutePath();
  QSettings().setValue(kLastDirKey, dir);
}
