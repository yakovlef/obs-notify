/*
 * Stream Notify
 * Copyright (C) 2026 Stream Notify contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#pragma once

#include <QString>

namespace obs_notify {

QString resolveStreamUrl(const QString &customUrl, const QString &twitchLogin);
QString renderPostText(const QString &postTemplate, const QString &title, const QString &category,
                       const QString &url);
QString telegramReplyMarkup(const QString &buttonText, const QString &url);

} // namespace obs_notify
