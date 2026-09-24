/*
 * Stream Notify
 * Copyright (C) 2026 Stream Notify contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "message-format.hpp"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

namespace obs_notify {

QString resolveStreamUrl(const QString &customUrl, const QString &twitchLogin)
{
    if (!customUrl.isEmpty()) return customUrl;
    if (!twitchLogin.isEmpty()) return "https://twitch.tv/" + twitchLogin;
    return {};
}

QString renderPostText(const QString &postTemplate, const QString &title, const QString &category,
                       const QString &url)
{
    QString result = postTemplate;
    if (title.isEmpty()) result.replace("{title}\n", "");
    if (category.isEmpty()) result.replace("{category}\n", "");
    result.replace("{title}", title);
    result.replace("{category}", category);
    result.replace("{url}", url);
    return result;
}

QString telegramReplyMarkup(const QString &buttonText, const QString &url)
{
    if (url.isEmpty() || buttonText.isEmpty()) return {};

    const QJsonObject button{{"text", buttonText}, {"url", url}};
    QJsonArray row;
    row.append(button);
    QJsonArray keyboard;
    keyboard.append(row);
    const QJsonObject root{{"inline_keyboard", keyboard}};
    return QString::fromUtf8(QJsonDocument(root).toJson(QJsonDocument::Compact));
}

} // namespace obs_notify
