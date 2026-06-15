/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2012 Synergy App Ltd
 * SPDX-FileCopyrightText: (C) 2008 Volker Lanz <vl@fidra.de>
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "ScreenSettingsDialog.h"
#include "ui_ScreenSettingsDialog.h"

#include "gui/config/Screen.h"
#include "validators/AliasValidator.h"
#include "validators/ScreenNameValidator.h"
#include "validators/ValidationError.h"

#include "common/Settings.h"
#if defined(HAVE_AUDIO_SUPPORT)
#include "audio/AudioDevices.h"
#endif

#include <QMessageBox>

using enum ScreenConfig::Modifier;
using enum ScreenConfig::SwitchCorner;
using enum ScreenConfig::Fix;

ScreenSettingsDialog::~ScreenSettingsDialog() = default;

ScreenSettingsDialog::ScreenSettingsDialog(QWidget *parent, Screen *screen, const ScreenList *screens)
    : QDialog(parent, Qt::WindowTitleHint | Qt::WindowSystemMenuHint),
      ui{std::make_unique<Ui::ScreenSettingsDialog>()},
      m_screen(screen)
{

  ui->setupUi(this);
  ui->buttonBox->button(QDialogButtonBox::Cancel)->setFocus();

  ui->lineNameEdit->setText(m_screen->name());

  const auto valNameError = new validators::ValidationError(this, ui->lblNameError);
  const auto valName = new validators::ScreenNameValidator(ui->lineNameEdit, valNameError, screens);
  ui->lineNameEdit->setValidator(valName);

  const auto valAliasError = new validators::ValidationError(this, ui->lblAliasError);
  const auto valAlias = new validators::AliasValidator(ui->lineAddAlias, valAliasError);
  ui->lineAddAlias->setValidator(valAlias);

  for (int i = 0; i < m_screen->aliases().count(); i++)
    new QListWidgetItem(m_screen->aliases()[i], ui->listAliases);

  ui->comboShift->setCurrentIndex(m_screen->modifier(static_cast<int>(Shift)));
  ui->comboCtrl->setCurrentIndex(m_screen->modifier(static_cast<int>(Ctrl)));
  ui->comboAlt->setCurrentIndex(m_screen->modifier(static_cast<int>(Alt)));
  ui->comboMeta->setCurrentIndex(m_screen->modifier(static_cast<int>(Meta)));
  ui->comboSuper->setCurrentIndex(m_screen->modifier(static_cast<int>(Super)));
  ui->comboAltGr->setCurrentIndex(m_screen->modifier(static_cast<int>(AltGr)));

  ui->chkDeadTopLeft->setChecked(m_screen->switchCorner(static_cast<int>(TopLeft)));
  ui->chkDeadTopRight->setChecked(m_screen->switchCorner(static_cast<int>(TopRight)));
  ui->chkDeadBottomLeft->setChecked(m_screen->switchCorner(static_cast<int>(BottomLeft)));
  ui->chkDeadBottomRight->setChecked(m_screen->switchCorner(static_cast<int>(BottomRight)));
  ui->sbSwitchCornerSize->setValue(m_screen->switchCornerSize());

  ui->chkFixCapsLock->setChecked(m_screen->fix(CapsLock));
  ui->chkFixNumLock->setChecked(m_screen->fix(NumLock));
  ui->chkFixScrollLock->setChecked(m_screen->fix(ScrollLock));
  ui->chkFixXTest->setChecked(m_screen->fix(XTest));

  ui->chkAudioRouting->setChecked(m_screen->audioRouting());

  // Audio always flows from a client to the server, so routing can only be enabled on a
  // client screen. Disable (and clear) it on the server's own screen.
  if (m_screen->isServer()) {
    ui->chkAudioRouting->setChecked(false);
    ui->chkAudioRouting->setEnabled(false);
    ui->chkAudioRouting->setToolTip(
        tr("Audio always routes from a client to the server, so this can only be enabled on a client screen.")
    );
  }

  // Server-side playback controls: output device, volume, mute. These are stored
  // in the shared settings keyed by screen name and applied by the server core.
  ui->comboAudioDevice->addItem(tr("System default"), QString());
#if defined(HAVE_AUDIO_SUPPORT)
  for (const auto &device : AudioDevices::outputDevices()) {
    ui->comboAudioDevice->addItem(device.name, device.id);
  }
#endif
  {
    const QString screenName = m_screen->name();
    const QString savedDevice = Settings::value(Settings::Audio::outputDeviceKey(screenName)).toString();
    const int deviceIndex = ui->comboAudioDevice->findData(savedDevice);
    ui->comboAudioDevice->setCurrentIndex(deviceIndex >= 0 ? deviceIndex : 0);

    const QVariant savedVolume = Settings::value(Settings::Audio::volumeKey(screenName));
    const int volume = savedVolume.isValid() ? savedVolume.toInt() : 100;
    ui->sliderAudioVolume->setValue(volume);
    ui->lblAudioVolumeValue->setText(QStringLiteral("%1%").arg(volume));

    ui->chkAudioMute->setChecked(Settings::value(Settings::Audio::muteKey(screenName)).toBool());
  }
  connect(ui->sliderAudioVolume, &QSlider::valueChanged, this, [this](int value) {
    ui->lblAudioVolumeValue->setText(QStringLiteral("%1%").arg(value));
  });

  connect(ui->buttonBox, &QDialogButtonBox::accepted, this, &ScreenSettingsDialog::accept);
  connect(ui->buttonBox, &QDialogButtonBox::rejected, this, &ScreenSettingsDialog::reject);
  connect(ui->btnAddAlias, &QPushButton::clicked, this, &ScreenSettingsDialog::addAlias);
  connect(ui->btnRemoveAlias, &QPushButton::clicked, this, &ScreenSettingsDialog::removeAlias);
  connect(ui->lineAddAlias, &QLineEdit::textChanged, this, &ScreenSettingsDialog::checkNewAliasName);
  connect(ui->listAliases, &QListWidget::itemSelectionChanged, this, &ScreenSettingsDialog::aliasSelected);
}

void ScreenSettingsDialog::accept()
{
  if (ui->lineNameEdit->text().isEmpty()) {
    QMessageBox::warning(
        this, tr("Screen name is empty"),
        tr("The screen name cannot be empty. "
           "Please either fill in a name or cancel the dialog.")
    );
    return;
  }
  if (!ui->lblNameError->text().isEmpty()) {
    return;
  }

  m_screen->setName(ui->lineNameEdit->text());

  m_screen->aliases().clear();

  for (int i = 0; i < ui->listAliases->count(); i++) {
    QString alias(ui->listAliases->item(i)->text());
    if (alias == ui->lineNameEdit->text()) {
      QMessageBox::warning(
          this, tr("Screen name matches alias"),
          tr("The screen name cannot be the same as an alias. "
             "Please either remove the alias or change the screen name.")
      );
      return;
    }
    if (!m_screen->aliases().contains(alias))
      m_screen->addAlias(alias);
  }

  m_screen->setModifier(Shift, ui->comboShift->currentIndex());
  m_screen->setModifier(Ctrl, ui->comboCtrl->currentIndex());
  m_screen->setModifier(Alt, ui->comboAlt->currentIndex());
  m_screen->setModifier(Meta, ui->comboMeta->currentIndex());
  m_screen->setModifier(Super, ui->comboSuper->currentIndex());
  m_screen->setModifier(AltGr, ui->comboAltGr->currentIndex());

  m_screen->setSwitchCorner(TopLeft, ui->chkDeadTopLeft->isChecked());
  m_screen->setSwitchCorner(TopRight, ui->chkDeadTopRight->isChecked());
  m_screen->setSwitchCorner(BottomLeft, ui->chkDeadBottomLeft->isChecked());
  m_screen->setSwitchCorner(BottomRight, ui->chkDeadBottomRight->isChecked());
  m_screen->setSwitchCornerSize(ui->sbSwitchCornerSize->value());

  m_screen->setFix(CapsLock, ui->chkFixCapsLock->isChecked());
  m_screen->setFix(NumLock, ui->chkFixNumLock->isChecked());
  m_screen->setFix(ScrollLock, ui->chkFixScrollLock->isChecked());
  m_screen->setFix(XTest, ui->chkFixXTest->isChecked());

  m_screen->setAudioRouting(ui->chkAudioRouting->isChecked());

  // Persist the server-side playback controls, keyed by the (possibly renamed) screen.
  const QString screenName = m_screen->name();
  Settings::setValue(Settings::Audio::outputDeviceKey(screenName), ui->comboAudioDevice->currentData().toString());
  Settings::setValue(Settings::Audio::volumeKey(screenName), ui->sliderAudioVolume->value());
  Settings::setValue(Settings::Audio::muteKey(screenName), ui->chkAudioMute->isChecked());

  QDialog::accept();
}

void ScreenSettingsDialog::addAlias()
{
  if (!ui->lineAddAlias->text().isEmpty() &&
      ui->listAliases->findItems(ui->lineAddAlias->text(), Qt::MatchFixedString).isEmpty()) {
    new QListWidgetItem(ui->lineAddAlias->text(), ui->listAliases);
    ui->lineAddAlias->clear();
  }
}

void ScreenSettingsDialog::removeAlias() const
{
  QList<QListWidgetItem *> items = ui->listAliases->selectedItems();
  qDeleteAll(items);
}

void ScreenSettingsDialog::checkNewAliasName(const QString &text)
{
  ui->btnAddAlias->setEnabled(!text.isEmpty() && ui->lblAliasError->text().isEmpty());
}

void ScreenSettingsDialog::aliasSelected()
{
  ui->btnRemoveAlias->setEnabled(!ui->listAliases->selectedItems().isEmpty());
}
