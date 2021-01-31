//
//          Copyright (c) 2021, Scientific Toolworks, Inc.
//
// This software is licensed under the MIT License. The LICENSE.md file
// describes the conditions under which this software may be distributed.
//

#include "PatchDialog.h"
#include "conf/Settings.h"
#include "git/Diff.h"
#include "log/LogEntry.h"
#include "ui/RepoView.h"
#include <QByteArray>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QPushButton>
#include <QRadioButton>
#include <QRegExp>
#include <QSaveFile>
#include <QSettings>
#include <QStandardItem>
#include <QStandardItemModel>
#include <QString>
#include <QVBoxLayout>

namespace {

const QString kFormatKey = "patch/format";
const QString kLastDirKey = "patch/lastdir";

QString lastDir()
{
  QString dir = QSettings().value(kLastDirKey).toString();

  if (dir.isEmpty() || !QDir(dir).exists())
    dir = QDir::homePath();

  return dir;
}

void saveLastDir(const QString &path)
{
  if (path.isEmpty())
    return;

  QFileInfo fileInfo(path);
  QSettings().setValue(kLastDirKey, fileInfo.isDir() ? path : fileInfo.absolutePath());
}

} // anon. namespace

QString ApplyPatchDialog::getOpenFileName(QWidget *parent)
{
  QString path = QFileDialog::getOpenFileName(parent, tr("Apply Patch File"), lastDir(), filter());
  saveLastDir(path);
  return path;
}

QString ApplyPatchDialog::filter()
{
  return tr("Git Diff (*.diff *.patch);;All files (*)");
}

SavePatchDialog::SavePatchDialog(QWidget *parent, const QList<git::Commit> &commits)
  : QDialog(parent), mCommits(commits)
{
  Q_ASSERT(!mCommits.isEmpty());

  setAttribute(Qt::WA_DeleteOnClose);
  setWindowTitle(tr("Save Patch File"));

  // Format.
  QFrame *formatFrame = new QFrame(this);

  QRadioButton *diff = new QRadioButton(tr("Diff"), formatFrame);
  QRadioButton *mbox = new QRadioButton(tr("Mailbox"), formatFrame);
  QRadioButton *singleMailbox = new QRadioButton(tr("Single Mailbox"), formatFrame);
  connect(diff, &QRadioButton::toggled, [this](bool checked) {
    if (checked)
      selectFormat(Format::Diff);
  });
  connect(mbox, &QRadioButton::toggled, [this](bool checked) {
    if (checked)
      selectFormat(Format::Mailbox);
  });
  connect(singleMailbox, &QRadioButton::toggled, [this](bool checked) {
    if (checked)
      selectFormat(Format::SingleMailbox);
  });

  QHBoxLayout *formatLayout = new QHBoxLayout(formatFrame);
  formatLayout->setContentsMargins(0,0,0,0);
  formatLayout->addWidget(diff);
  formatLayout->addWidget(mbox);
  formatLayout->addWidget(singleMailbox);

  // Output Directory.
  QFrame *dirFrame = new QFrame(this);

  mDir = new QLineEdit(lastDir(), dirFrame);
  mDir->setMinimumWidth(mDir->sizeHint().width() * 2);
  connect(mDir, &QLineEdit::textChanged, this, &SavePatchDialog::updateSaveButton);

  QPushButton *browse = new QPushButton(tr("..."), dirFrame);
  connect(browse, &QPushButton::clicked, [this] {
    QString path = QFileDialog::getExistingDirectory(this, tr("Choose Directory"), mDir->text());
    if (!path.isEmpty())
      mDir->setText(path);
  });

  QHBoxLayout *dirLayout = new QHBoxLayout(dirFrame);
  dirLayout->setContentsMargins(0,0,0,0);
  dirLayout->addWidget(mDir);
  dirLayout->addWidget(browse);

  // File Name.
  QFrame *fileFrame = new QFrame(this);

  mFile = new QLineEdit(fileFrame);
  mFile->setMaximumWidth(static_cast<int>(mDir->minimumWidth() * 0.7));
  connect(mFile, &QLineEdit::textChanged, this, &SavePatchDialog::updateSaveButton);

  mFileExt = new QLabel(fileFrame);

  mFileList = new QListView(fileFrame);
  mFileList->setMaximumHeight(static_cast<int>(mFileList->sizeHint().height() * 0.5));
  mFileList->setEditTriggers(QListView::NoEditTriggers);

  QStandardItemModel *fileListModel = new QStandardItemModel(0, 1, mFileList);
  mFileList->setModel(fileListModel);

  QHBoxLayout *fileLayout = new QHBoxLayout(fileFrame);
  fileLayout->setContentsMargins(0,0,0,0);
  fileLayout->addWidget(mFile);
  fileLayout->addWidget(mFileExt);
  fileLayout->addWidget(mFileList);

  QFormLayout *form = new QFormLayout;
  form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
  form->addRow(tr("Format:"), formatFrame);
  form->addRow(tr("Output Directory:"), dirFrame);
  form->addRow(tr("File Name:"), fileFrame);

  QDialogButtonBox *buttons = new QDialogButtonBox(this);
  buttons->addButton(QDialogButtonBox::Cancel);
  mSaveButton = buttons->addButton(tr("Save"), QDialogButtonBox::AcceptRole);
  connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
  connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
  connect(mSaveButton, &QPushButton::clicked, this, &SavePatchDialog::save);

  QVBoxLayout *layout = new QVBoxLayout(this);
  layout->setSizeConstraint(QVBoxLayout::SetFixedSize);
  layout->addLayout(form);
  layout->addWidget(buttons);

  // Build a list of files for Mailbox format.
  walkCommits([this, fileListModel](int num, const git::Commit &commit) {
    fileListModel->appendRow(new QStandardItem(mailboxFileName(num, commit)));
  });

  Format format = static_cast<Format>(Settings::instance()->value(kFormatKey).toInt());
  switch (format) {
    case Format::Diff: diff->setChecked(true); break;
    case Format::Mailbox: mbox->setChecked(true); break;
    case Format::SingleMailbox: singleMailbox->setChecked(true); break;
  }
  updateSaveButton();
}

void SavePatchDialog::save() const
{
  Settings::instance()->setValue(kFormatKey, static_cast<int>(mFormat));
  saveLastDir(mDir->text());

  if (mFormat == Format::Mailbox)
    saveMailbox();
  else
    savePatch();
}

void SavePatchDialog::savePatch() const
{
  RepoView *view = RepoView::parentView(this);

  QString path = outputFilePath();
  LogEntry *entry = view->addLogEntry(path, tr("Save Patch"));

  QByteArray buffer = generatePatch();
  saveFile(entry, path, buffer);
}

void SavePatchDialog::saveMailbox() const
{
  RepoView *view = RepoView::parentView(this);

  QString totalCommits = QString::number(mCommits.size());
  QString fmt = mCommits.size() == 1 ? tr("%1 commit") : tr("%1 commits");
  LogEntry *entry = view->addLogEntry(fmt.arg(totalCommits), tr("Save Patch"));

  QDir dir(mDir->text());

  walkCommits([this, entry, &dir](int num, const git::Commit &commit) {
    QString path = dir.absoluteFilePath(mailboxFileName(num, commit));
    QByteArray buffer = commit.formatPatch(num, mCommits.size());

    if (!saveFile(entry, path, buffer))
      return;

    entry->addEntry(LogEntry::Entry, QFileInfo(path).fileName());
  });
}

bool SavePatchDialog::saveFile(LogEntry *parent, const QString &path, const QByteArray &buffer) const
{
  QSaveFile file(path);
  if (file.open(QSaveFile::WriteOnly)) {
    if (file.write(buffer) != -1)
      file.commit();
  }

  if (file.error() != QSaveFile::NoError) {
    parent->addEntry(LogEntry::Error,
      tr("Unable to save patch '%1' - %2").arg(QFileInfo(path).fileName(), file.errorString()));
    return false;
  }

  return true;
}

QByteArray SavePatchDialog::generatePatch() const
{
  if (mFormat == Format::Diff) {
    const git::Commit &newCommit = mCommits.first();
    const git::Commit &oldCommit = mCommits.size() > 1 ? mCommits.last() : git::Commit();
    return newCommit.diff(oldCommit).toBuffer();
  } else {
    QByteArray buffer;
    walkCommits([this, &buffer](int num, const git::Commit &commit) mutable {
      buffer += commit.formatPatch(num, mCommits.size());
    });
    return buffer;
  }
}

void SavePatchDialog::selectFormat(Format format)
{
  mFormat = format;

  bool singleFile = format != Format::Mailbox;
  mFile->setVisible(singleFile);
  mFileExt->setVisible(singleFile);
  mFileList->setVisible(!singleFile);

  mFileExt->setText(fileExtension());
  updateSaveButton();
}

void SavePatchDialog::updateSaveButton()
{
  bool dirValid = !mDir->text().isEmpty();
  bool fileValid = mFormat == Format::Mailbox || !mFile->text().isEmpty();
  mSaveButton->setEnabled(dirValid && fileValid);
}

QString SavePatchDialog::mailboxFileName(int num, const git::Commit &commit) const
{
  QString name = commit.summary().replace(QRegExp("[/ ]"), "-")
                                 .remove(QRegExp("[^a-zA-Z0-9-_.]"));
  return QStringLiteral("%1-%2.patch").arg(num, 4, 10, QLatin1Char('0'))
                                      .arg(name);
}

QString SavePatchDialog::fileExtension() const
{
  return mFormat == Format::Diff ? ".diff" : ".patch";
}

QString SavePatchDialog::outputFilePath() const
{
  return QDir(mDir->text()).absoluteFilePath(mFile->text()) + fileExtension();
}

void SavePatchDialog::walkCommits(
  const std::function<void(int num, const git::Commit &commit)> &callback) const
{
  auto end = mCommits.rend();
  int num = 1;
  for (auto it = mCommits.rbegin(); it != end; ++it)
    callback(num++, *it);
}
