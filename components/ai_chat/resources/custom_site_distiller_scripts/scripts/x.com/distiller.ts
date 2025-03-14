// Copyright (c) 2024 The Brave Authors. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

import selectors from './selectors'
import { isSupportedPage } from './utils'
import { distillNotificationElement } from './notifications'
import { distillPostElement } from './post'
import { distillSeenUsers } from './user'
import { LEO_DISTILLATION_LEVEL } from '../distillation'

let _DISTILLATION_LEVEL = LEO_DISTILLATION_LEVEL.FULL

export function getDistillationLevel() {
  return _DISTILLATION_LEVEL
}

export default function distill(distillLevel = LEO_DISTILLATION_LEVEL.FULL) {
  if (!isSupportedPage(document)) {
    return null
  }

  _DISTILLATION_LEVEL = distillLevel

  const column = distillPrimaryColumn()
  const seenUsers = distillSeenUsers(distillLevel)

  /**
   * Include helpful metadata about users seen on the page, but in a way that
   * it isn't confused with the original content.
   */
  return (
    `${seenUsers}\n\nNote: The above user information is supplemental ` +
    `metadata and not part of the original page content.\n\n--- Page ` +
    `Content Below ---\n\n${column}`
  )
}

const config = {
  'tweet': {
    'selector': selectors.tweet,
    'distiller': distillPostElement
  },
  'notification': {
    'selector': selectors.notification,
    'distiller': distillNotificationElement
  }
} as Record<string, any>

/**
 * Extracts and processes all items (like tweets and
 * notifications) from the primary column of the page, using
 * configuration to determine which elements to distill.
 * TODO (Sampson): Add support for other primary column items,
 * such as "show more" buttons, "this post is from a suspended
 * account", tends, and more.
 */
function distillPrimaryColumn() {
  const primaryColumn = document.querySelector(selectors.primaryColumn)
  const selectorList = Object.values(config)
    .map((item) => item.selector)
    .join(', ')
  const timelineItems = primaryColumn?.querySelectorAll(selectorList) ?? []

  return Array.from(timelineItems)
    .map((item) => {
      const type = item.getAttribute('data-testid')
      const { distiller } = type && config[type]
      return distiller && distiller(item)
    })
    .filter(Boolean)
    .join('\n\n---\n\n')
}
