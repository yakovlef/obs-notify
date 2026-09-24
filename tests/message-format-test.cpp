/*
 * Stream Notify
 * Copyright (C) 2026 Stream Notify contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "message-format.hpp"

#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <iostream>

namespace {

int failures = 0;

void expect(bool condition, const char *message)
{
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);

    expect(obs_notify::resolveStreamUrl("", "streamer") == "https://twitch.tv/streamer",
           "Twitch login should produce the stream URL");
    expect(obs_notify::resolveStreamUrl("https://example.com/live", "streamer") == "https://example.com/live",
           "Custom URL should override the Twitch URL");
    expect(obs_notify::resolveStreamUrl("", "").isEmpty(), "Missing URL inputs should stay empty");

    const QString rendered = obs_notify::renderPostText(
        "Stream started!\n{title}\n{category}\n{url}", "", "", "https://twitch.tv/streamer");
    expect(rendered == "Stream started!\nhttps://twitch.tv/streamer",
           "Template rendering should replace values and remove empty metadata lines");

    const QString markup = obs_notify::telegramReplyMarkup("Watch stream", "https://twitch.tv/streamer");
    const QJsonObject root = QJsonDocument::fromJson(markup.toUtf8()).object();
    const QJsonArray keyboard = root.value("inline_keyboard").toArray();
    expect(keyboard.size() == 1 && keyboard.at(0).isArray(),
           "Telegram inline_keyboard must contain an array of button rows");
    const QJsonArray row = keyboard.at(0).toArray();
    expect(row.size() == 1 && row.at(0).toObject().value("text") == "Watch stream",
           "Telegram button text should be preserved");
    expect(row.at(0).toObject().value("url") == "https://twitch.tv/streamer",
           "Telegram button URL should be preserved");
    expect(obs_notify::telegramReplyMarkup("", "https://twitch.tv/streamer").isEmpty(),
           "Empty button text should disable reply markup");

    if (failures == 0) std::cout << "All message formatting tests passed.\n";
    return failures == 0 ? 0 : 1;
}
