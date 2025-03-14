// Copyright (c) 2021 The Brave Authors. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// you can obtain one at https://mozilla.org/MPL/2.0/.

import * as React from 'react'

import * as S from './style'
import SettingsPanel from '../settings-panel'
import ContactSupport from '../contact-support'
import PanelBox from '../panel-box'
import { getLocale } from '$web-common/locale'
import { PanelHeader } from '../main-panel'
import { PanelContent } from '../main-panel/style'

function LoadingPanel() {
  const [isSettingsPanelVisible, setSettingsPanelVisible] =
    React.useState(false)
  const [isContactSupportVisible, setContactSupportVisible] =
    React.useState(false)

  const handleSettingsButtonClick = () => setSettingsPanelVisible(true)
  const closeSettingsPanel = () => setSettingsPanelVisible(false)

  const showContactSupport = () => setContactSupportVisible(true)
  const closeContactSupport = () => setContactSupportVisible(false)

  if (isContactSupportVisible) {
    return <ContactSupport onCloseContactSupport={closeContactSupport} />
  }

  if (isSettingsPanelVisible) {
    return (
      <SettingsPanel
        closeSettingsPanel={closeSettingsPanel}
        showContactSupport={showContactSupport}
      />
    )
  }

  return (
    <PanelBox>
      <PanelHeader
        title={getLocale('braveVpn')}
        settingsTooltip={getLocale('braveVpnSettingsTooltip')}
        settingsOnClick={handleSettingsButtonClick}
      />
      <PanelContent>
        <S.Status>
          <S.LoadingIcon />
          <S.PanelDesc>{getLocale('braveVpnLoading')}</S.PanelDesc>
        </S.Status>
      </PanelContent>
    </PanelBox>
  )
}

export default LoadingPanel
