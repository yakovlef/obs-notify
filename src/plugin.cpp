/*
 * Stream Notify
 * Copyright (C) 2026 Stream Notify contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <obs-module.h>
#include <obs-frontend-api.h>

#include "message-format.hpp"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFile>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPointer>
#include <QPushButton>
#include <QSettings>
#include <QSpinBox>
#include <QStandardPaths>
#include <QTextEdit>
#include <QTimer>
#include <QUrlQuery>
#include <QVBoxLayout>
#include <QFileInfo>
#include <QDir>
#include <QHttpMultiPart>
#include <QHttpPart>
#include <QDateTime>
#include <QPixmap>
#include <QWidget>
#include <QDesktopServices>
#include <QCoreApplication>
#include <QSslSocket>
#include <QFrame>
#include <QFont>
#include <QIcon>
#include <QPainter>
#include <QPainterPath>
#include <functional>
#ifdef _WIN32
#include <windows.h>
#include <wincrypt.h>
#endif

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE("stream-notify", "en-US")

namespace {
constexpr auto *module_name = "stream-notify";
constexpr auto *twitch_client_id = "axoa5y1um81tof8nni7baus7m7wl8t";

QString moduleText(const char *key)
{
    return QString::fromUtf8(obs_module_text(key));
}

QString protectSecret(const QString &plain)
{
    if (plain.isEmpty()) return {};
#ifdef _WIN32
    const QByteArray bytes = plain.toUtf8();
    DATA_BLOB input{static_cast<DWORD>(bytes.size()), reinterpret_cast<BYTE *>(const_cast<char *>(bytes.constData()))};
    DATA_BLOB output{};
    if (CryptProtectData(&input, L"Stream Notify", nullptr, nullptr, nullptr, 0, &output)) {
        const auto encoded = QString::fromLatin1(QByteArray(reinterpret_cast<const char *>(output.pbData), output.cbData).toBase64());
        LocalFree(output.pbData);
        return "dpapi:" + encoded;
    }
#endif
    return {};
}

QString unprotectSecret(const QString &stored)
{
    if (!stored.startsWith("dpapi:")) return stored; // Migrate older plaintext settings.
#ifdef _WIN32
    const QByteArray bytes = QByteArray::fromBase64(stored.mid(6).toLatin1());
    DATA_BLOB input{static_cast<DWORD>(bytes.size()), reinterpret_cast<BYTE *>(const_cast<char *>(bytes.constData()))};
    DATA_BLOB output{};
    if (CryptUnprotectData(&input, nullptr, nullptr, nullptr, nullptr, 0, &output)) {
        const QString plain = QString::fromUtf8(reinterpret_cast<const char *>(output.pbData), output.cbData);
        LocalFree(output.pbData);
        return plain;
    }
#endif
    return {};
}

struct Config {
    QString token, chat, text, url, button, image, title, category;
    int delay = 5;
    bool remove = true;
    bool screenshot = false;
};

QString settingsPath()
{
    const QString root = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    QDir().mkpath(root);
    const QString current = root + "/stream-notify.ini";
    const QString legacy = root + "/obs-telegram-notify.ini";
    if (!QFileInfo::exists(current) && QFileInfo::exists(legacy)) QFile::copy(legacy, current);
    return current;
}

Config loadConfig()
{
    QSettings s(settingsPath(), QSettings::IniFormat);
    Config c;
    c.token = unprotectSecret(s.value("telegram/token").toString());
    c.chat = s.value("telegram/chat").toString();
    const QString defaultPost = moduleText("Post.DefaultText");
    c.text = s.value("post/text", defaultPost).toString();
    if (c.text == QString::fromUtf8("🔴 Стрим начался!\n{title}\n{category}\nСмотреть: {url}") ||
        c.text == QString::fromUtf8("🔴 Стрим начался!\nСмотреть: %url%") ||
        c.text == QString::fromUtf8("🟢 Стрим начался! {title} {category} Смотреть: {url}") ||
        c.text == QString::fromUtf8("🟢 Стрим начался!\n{title}\n{category}\n\nСмотреть:\n{url}"))
        c.text = defaultPost;
    c.text.replace("%url%", "{url}");
    c.text.replace("%start_time%\n", "");
    c.text.replace("%start_time%", "");
    c.text.replace("{start_time}\n", "");
    c.text.replace("{start_time_msk}\n", "");
    c.text.replace("{start_time}", "");
    c.text.replace("{start_time_msk}", "");
    c.text.replace(QString::fromUtf8("Смотреть: {url}"), QString::fromUtf8("Смотреть:\n{url}"));
    c.url = s.value("post/url").toString();
    c.button = s.value("post/button", moduleText("Post.DefaultButton")).toString();
    if (c.button == QString::fromUtf8("Смотреть эфир")) c.button = moduleText("Post.DefaultButton");
    c.image = s.value("post/image").toString();
    c.delay = s.value("post/delay", 5).toInt();
    c.remove = s.value("post/remove", true).toBool();
    c.screenshot = s.value("post/screenshot", false).toBool();
    return c;
}

void saveConfig(const Config &c)
{
    QSettings s(settingsPath(), QSettings::IniFormat);
    s.setValue("telegram/token", protectSecret(c.token));
    s.setValue("telegram/chat", c.chat);
    s.setValue("post/text", c.text);
    s.setValue("post/url", c.url);
    s.setValue("post/button", c.button);
    s.setValue("post/image", c.image);
    s.setValue("post/delay", c.delay);
    s.setValue("post/remove", c.remove);
    s.setValue("post/screenshot", c.screenshot);
    s.sync();
}

class Plugin final : public QObject {
public:
    Plugin() : QObject(static_cast<QObject *>(obs_frontend_get_main_window()))
    {
        config = loadConfig();
        QSettings s(settingsPath(), QSettings::IniFormat);
        twitchAccess = unprotectSecret(s.value("twitch/access").toString());
        twitchRefresh = unprotectSecret(s.value("twitch/refresh").toString());
        twitchUserId = s.value("twitch/user_id").toString();
        twitchLogin = s.value("twitch/login").toString();
        twitchExpires = s.value("twitch/expires").toDateTime();
        twitchPoll.setSingleShot(false);
        connect(&twitchPoll, &QTimer::timeout, this, [this] { pollTwitchDevice(); });
        timer.setSingleShot(true);
        connect(&timer, &QTimer::timeout, this, [this] { publish(); });
        QTimer::singleShot(0, this, [this] { installControlsButton(); });
    }

    ~Plugin() override
    {
        delete controlsButton;
    }

    void showSettings()
    {
        QDialog d(static_cast<QWidget *>(obs_frontend_get_main_window()));
        d.setWindowTitle(moduleText("Settings.Title"));
        d.resize(557, 680);
        auto *layout = new QVBoxLayout(&d);
        auto *twitchRow = new QHBoxLayout;
        auto *twitchStatus = new QLabel(twitchLogin.isEmpty() ? moduleText("Twitch.Disconnected") : moduleText("Twitch.Connected").arg(twitchLogin));
        auto *twitchConnect = new QPushButton(moduleText("Twitch.Connect"));
        twitchRow->addWidget(twitchStatus);
        twitchRow->addWidget(twitchConnect);
        layout->addLayout(twitchRow);
        connect(twitchConnect, &QPushButton::clicked, &d, [this, twitchStatus] { connectTwitch(twitchStatus); });
        auto *separator = new QFrame;
        separator->setFrameShape(QFrame::HLine);
        separator->setFrameShadow(QFrame::Sunken);
        layout->addWidget(separator);
        auto *form = new QFormLayout;
        auto *token = new QLineEdit(config.token);
        token->setEchoMode(QLineEdit::Password);
        auto *chat = new QLineEdit(config.chat);
        chat->setMinimumWidth(0);
        chat->setPlaceholderText(moduleText("Telegram.ChannelPlaceholder"));
        auto *test = new QPushButton(moduleText("Post.Test"));
        auto *chatRow = new QWidget;
        auto *chatLayout = new QHBoxLayout(chatRow);
        chatLayout->setContentsMargins(0, 0, 0, 0);
        chatLayout->addWidget(chat, 1);
        chatLayout->addWidget(test);
        auto *url = new QLineEdit(config.url);
        settingsUrlField = url;
        url->setPlaceholderText(twitchStreamUrl());
        auto *button = new QLineEdit(config.button);
        auto *delay = new QSpinBox;
        delay->setRange(0, 3600);
        delay->setSuffix(moduleText("Delay.Suffix"));
        delay->setValue(config.delay);
        auto *remove = new QCheckBox(moduleText("Post.RemoveOnStop"));
        remove->setChecked(config.remove);
        auto *screenshot = new QCheckBox(moduleText("Image.CaptureOnPublish"));
        screenshot->setChecked(config.screenshot);
        screenshotField = screenshot;
        auto *image = new QLineEdit(config.image);
        image->setMinimumWidth(0);
        imageField = image;
        auto *browse = new QPushButton(moduleText("Image.Browse"));
        auto *capture = new QPushButton(moduleText("Image.Capture"));
        capture->setToolTip(moduleText("Image.CaptureTooltip"));
        auto *imagePreview = new QLabel;
        imagePreviewField = imagePreview;
        imagePreview->setAlignment(Qt::AlignCenter);
        imagePreview->setMinimumHeight(100);
        imagePreview->setMaximumHeight(150);
        auto refreshPreview = [this, image, imagePreview] {
            QPixmap pixmap;
            if (!manualSnapshot.isEmpty()) pixmap.loadFromData(manualSnapshot);
            else pixmap.load(image->text());
            if (pixmap.isNull()) imagePreview->setText(moduleText("Image.None"));
            else {
                imagePreview->setPixmap(pixmap.scaled(320, 140, Qt::KeepAspectRatio, Qt::SmoothTransformation));
                imagePreview->setToolTip(manualSnapshot.isEmpty() ? image->text() : moduleText("Image.TemporarySnapshot"));
            }
        };
        connect(image, &QLineEdit::textChanged, &d, [this, refreshPreview](const QString &path) {
            if (!path.isEmpty()) manualSnapshot.clear();
            refreshPreview();
        });
        refreshPreview();
        auto *imageRow = new QWidget;
        auto *imageLayout = new QHBoxLayout(imageRow);
        imageLayout->setContentsMargins(0, 0, 0, 0);
        imageLayout->addWidget(image);
        imageLayout->addWidget(browse);
        imageLayout->addWidget(capture);
        connect(browse, &QPushButton::clicked, &d, [image, &d] {
            const auto path = QFileDialog::getOpenFileName(&d, moduleText("Image.FileDialogTitle"), image->text(), "Images (*.png *.jpg *.jpeg)");
            if (!path.isEmpty()) image->setText(path);
        });
        connect(capture, &QPushButton::clicked, &d, [this] { captureManualSnapshot(); });
        auto styleHint = [](QLabel *label) {
            label->setWordWrap(true);
            QFont font = label->font();
            if (font.pointSizeF() > 0) font.setPointSizeF(qMax(8.0, font.pointSizeF() - 1.0));
            else if (font.pixelSize() > 0) font.setPixelSize(qMax(9, font.pixelSize() - 1));
            label->setFont(font);
        };
        form->addRow(moduleText("Telegram.BotToken"), token);
        auto *botHint = new QLabel(moduleText("Telegram.BotHint"));
        styleHint(botHint);
        form->addRow(QString(), botHint);
        form->addRow(moduleText("Telegram.Channel"), chatRow);
        auto *channelHint = new QLabel(moduleText("Telegram.ChannelHint"));
        styleHint(channelHint);
        form->addRow(QString(), channelHint);
        form->addRow(moduleText("Post.StreamUrl"), url);
        form->addRow(moduleText("Post.ButtonText"), button);
        form->addRow(moduleText("Post.Delay"), delay);
        auto *delayHint = new QLabel(moduleText("Post.DelayHint"));
        styleHint(delayHint);
        form->addRow(QString(), delayHint);
        form->addRow(moduleText("Image.Label"), imageRow);
        layout->addLayout(form);
        layout->addWidget(imagePreview);
        auto *body = new QTextEdit;
        body->setPlainText(config.text);
        body->setPlaceholderText(moduleText("Post.TextPlaceholder"));
        layout->addWidget(new QLabel(moduleText("Post.Text")));
        layout->addWidget(body);
        auto *placeholders = new QLabel(QString::fromUtf8("{title} {category} {url}"));
        styleHint(placeholders);
        layout->addWidget(placeholders);
        layout->addWidget(screenshot);
        auto *screenshotHint = new QLabel(moduleText("Image.PriorityHint"));
        styleHint(screenshotHint);
        layout->addWidget(screenshotHint);
        layout->addWidget(remove);
        auto *buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel);
        layout->addWidget(buttons);
        auto read = [&] {
            Config c;
            c.token = token->text().trimmed(); c.chat = chat->text().trimmed();
            c.url = url->text().trimmed(); c.button = button->text().trimmed();
            c.image = image->text().trimmed(); c.text = body->toPlainText();
            c.title = config.title; c.category = config.category;
            c.delay = delay->value(); c.remove = remove->isChecked(); c.screenshot = screenshot->isChecked();
            return c;
        };
        connect(buttons, &QDialogButtonBox::accepted, &d, [&] {
            config = read(); saveConfig(config); d.accept();
        });
        connect(buttons, &QDialogButtonBox::rejected, &d, &QDialog::reject);
        connect(test, &QPushButton::clicked, &d, [&] {
            config = read(); saveConfig(config); publish(true);
        });
        d.exec();
        settingsUrlField = nullptr;
        imageField = nullptr;
        imagePreviewField = nullptr;
        screenshotField = nullptr;
    }

    void event(obs_frontend_event e)
    {
        if (e == OBS_FRONTEND_EVENT_FINISHED_LOADING) {
            installControlsButton();
        } else if (e == OBS_FRONTEND_EVENT_THEME_CHANGED) {
            refreshBellIcon();
        } else if (e == OBS_FRONTEND_EVENT_STREAMING_STARTED) {
            ++generation;
            timer.start(config.delay * 1000);
        } else if (e == OBS_FRONTEND_EVENT_STREAMING_STOPPED) {
            ++generation;
            timer.stop();
            if (config.remove && messageId > 0) removePost();
        } else if (e == OBS_FRONTEND_EVENT_SCREENSHOT_TAKEN && (awaitingScreenshot || awaitingManualSnapshot)) {
            const bool manual = awaitingManualSnapshot;
            awaitingScreenshot = false;
            awaitingManualSnapshot = false;
            char *path = obs_frontend_get_last_screenshot();
            const QString imagePath = path ? QString::fromUtf8(path) : QString();
            if (path) bfree(path);
            const QByteArray imageData = consumeScreenshot(imagePath);
            if (manual) {
                if (!imageData.isEmpty()) previewSnapshot(imageData);
                else QMessageBox::warning(static_cast<QWidget *>(obs_frontend_get_main_window()), moduleText("Snapshot.Title"), moduleText("Snapshot.CaptureFailed"));
            } else if (!imageData.isEmpty()) sendPost({}, screenshotTest, imageData);
            else sendSelectedPost(screenshotTest);
        }
    }

private:
    QPointer<QPushButton> controlsButton;
    Config config;
    QTimer timer;
    QTimer twitchPoll;
    QNetworkAccessManager net;
    qint64 messageId = 0;
    int generation = 0;
    bool awaitingScreenshot = false;
    bool awaitingManualSnapshot = false;
    bool screenshotTest = false;
    QPointer<QLineEdit> imageField;
    QPointer<QLineEdit> settingsUrlField;
    QPointer<QLabel> imagePreviewField;
    QPointer<QCheckBox> screenshotField;
    QByteArray manualSnapshot;
    QString twitchAccess, twitchRefresh, twitchUserId, twitchLogin, twitchDeviceCode;
    QDateTime twitchExpires;
    int twitchPollSeconds = 5;
    QPointer<QLabel> twitchStatusLabel;

    void refreshBellIcon()
    {
        if (!controlsButton) return;
        QPixmap pixmap(48, 48);
        pixmap.setDevicePixelRatio(2);
        pixmap.fill(Qt::transparent);
        QPainter painter(&pixmap);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(Qt::NoPen);
        painter.setBrush(obs_frontend_is_theme_dark() ? QColor("#f2f2f2") : QColor("#202020"));
        QPainterPath bell;
        bell.moveTo(12, 4);
        bell.cubicTo(8.5, 4, 6.5, 6.5, 6.5, 10);
        bell.lineTo(6.5, 13.5);
        bell.cubicTo(6.5, 15.1, 5.8, 16.5, 4.4, 17.6);
        bell.cubicTo(4.0, 18.0, 4.3, 18.5, 4.9, 18.5);
        bell.lineTo(19.1, 18.5);
        bell.cubicTo(19.7, 18.5, 20.0, 18.0, 19.6, 17.6);
        bell.cubicTo(18.2, 16.5, 17.5, 15.1, 17.5, 13.5);
        bell.lineTo(17.5, 10);
        bell.cubicTo(17.5, 6.5, 15.5, 4, 12, 4);
        painter.drawPath(bell);
        painter.drawEllipse(QPointF(12, 20), 2, 1.4);
        painter.end();
        controlsButton->setIcon(QIcon(pixmap));
        controlsButton->setIconSize(QSize(20, 20));
    }

    void installControlsButton()
    {
        if (controlsButton) return;
        auto *main = static_cast<QWidget *>(obs_frontend_get_main_window());
        if (!main) return;
        auto *streamButton = main->findChild<QPushButton *>("streamButton");
        if (!streamButton) {
            for (auto *widget : QApplication::allWidgets()) {
                if (widget->objectName() == "streamButton") {
                    streamButton = qobject_cast<QPushButton *>(widget);
                    if (streamButton) break;
                }
            }
        }
        QHBoxLayout *row = nullptr;
        if (streamButton) {
            for (auto *parent = streamButton->parentWidget(); parent && !row; parent = parent->parentWidget()) {
                for (auto *candidate : parent->findChildren<QHBoxLayout *>()) {
                    if (candidate->indexOf(streamButton) >= 0) {
                        row = candidate;
                        break;
                    }
                }
            }
        }
        if (!streamButton || !row) {
            blog(LOG_WARNING, "[stream-notify] Controls row not found (stream button: %s); use Tools > Stream Notify",
                 streamButton ? "yes" : "no");
            return;
        }
        auto *bell = new QPushButton(streamButton->parentWidget());
        bell->setObjectName("obsNotifyBellButton");
        bell->setAccessibleName(moduleText("Controls.Open"));
        bell->setToolTip(moduleText("Controls.Tooltip"));
        bell->setFixedWidth(36);
        const int streamIndex = row->indexOf(streamButton);
        row->insertSpacing(streamIndex + 1, 2);
        row->insertWidget(streamIndex + 2, bell);
        connect(bell, &QPushButton::clicked, this, [this] { showSettings(); });
        controlsButton = bell;
        refreshBellIcon();
        blog(LOG_INFO, "[stream-notify] Controls bell installed");
    }

    void saveTwitchSession()
    {
        QSettings s(settingsPath(), QSettings::IniFormat);
        s.setValue("twitch/access", protectSecret(twitchAccess));
        s.setValue("twitch/refresh", protectSecret(twitchRefresh));
        s.setValue("twitch/user_id", twitchUserId);
        s.setValue("twitch/login", twitchLogin);
        s.setValue("twitch/expires", twitchExpires);
        s.sync();
    }

    void twitchStatus(const QString &message)
    {
        if (twitchStatusLabel) twitchStatusLabel->setText(message);
        if (settingsUrlField) settingsUrlField->setPlaceholderText(twitchStreamUrl());
        blog(LOG_INFO, "[stream-notify] Twitch: %s", qPrintable(message));
    }

    QNetworkReply *twitchForm(const QUrl &url, const QUrlQuery &form)
    {
        QNetworkRequest request(url);
        request.setHeader(QNetworkRequest::ContentTypeHeader, "application/x-www-form-urlencoded");
        request.setTransferTimeout(15000);
        return net.post(request, form.toString(QUrl::FullyEncoded).toUtf8());
    }

    void connectTwitch(QLabel *status)
    {
        twitchStatusLabel = status;
        twitchStatus(moduleText("Twitch.RequestingCode"));
        QUrlQuery form;
        form.addQueryItem("client_id", twitch_client_id);
        form.addQueryItem("scopes", "");
        auto *reply = twitchForm(QUrl("https://id.twitch.tv/oauth2/device"), form);
        connect(reply, &QNetworkReply::finished, this, [this, reply] {
            const auto json = QJsonDocument::fromJson(reply->readAll()).object();
            const auto networkError = reply->errorString();
            reply->deleteLater();
            twitchDeviceCode = json.value("device_code").toString();
            if (twitchDeviceCode.isEmpty()) {
                const auto reason = json.value("message").toString(networkError);
                twitchStatus(moduleText("Twitch.CodeRequestFailed").arg(reason));
                return;
            }
            twitchPollSeconds = qMax(5, json.value("interval").toInt(5));
            auto uri = QUrl(json.value("verification_uri").toString());
            const auto code = json.value("user_code").toString();
            twitchStatus(moduleText("Twitch.Code").arg(code));
            QDesktopServices::openUrl(uri);
            auto *notice = new QMessageBox(QMessageBox::Information, moduleText("Twitch.DialogTitle"),
                moduleText("Twitch.DialogMessage").arg(code),
                QMessageBox::Ok, static_cast<QWidget *>(obs_frontend_get_main_window()));
            notice->setAttribute(Qt::WA_DeleteOnClose);
            notice->show();
            twitchPoll.start(twitchPollSeconds * 1000);
            const QString expectedCode = twitchDeviceCode;
            QTimer::singleShot(json.value("expires_in").toInt(1800) * 1000, this, [this, expectedCode] {
                if (twitchDeviceCode == expectedCode) {
                    twitchPoll.stop(); twitchDeviceCode.clear();
                    twitchStatus(moduleText("Twitch.CodeExpired"));
                }
            });
        });
    }

    void pollTwitchDevice()
    {
        if (twitchDeviceCode.isEmpty()) return;
        twitchPoll.stop();
        QUrlQuery form;
        form.addQueryItem("client_id", twitch_client_id);
        form.addQueryItem("device_code", twitchDeviceCode);
        form.addQueryItem("grant_type", "urn:ietf:params:oauth:grant-type:device_code");
        auto *reply = twitchForm(QUrl("https://id.twitch.tv/oauth2/token"), form);
        connect(reply, &QNetworkReply::finished, this, [this, reply] {
            const auto json = QJsonDocument::fromJson(reply->readAll()).object();
            reply->deleteLater();
            const auto token = json.value("access_token").toString();
            if (!token.isEmpty()) {
                twitchDeviceCode.clear();
                twitchAccess = token;
                twitchRefresh = json.value("refresh_token").toString();
                twitchExpires = QDateTime::currentDateTimeUtc().addSecs(json.value("expires_in").toInt(14400));
                saveTwitchSession();
                validateTwitch();
                return;
            }
            const auto error = json.value("message").toString(json.value("error").toString());
            if (error.contains("authorization_pending", Qt::CaseInsensitive)) twitchPoll.start(twitchPollSeconds * 1000);
            else if (error.contains("slow_down", Qt::CaseInsensitive)) { twitchPollSeconds += 5; twitchPoll.start(twitchPollSeconds * 1000); }
            else {
                twitchDeviceCode.clear();
                twitchStatus(moduleText("Twitch.AuthorizationFailed").arg(error));
            }
        });
    }

    void validateTwitch()
    {
        QNetworkRequest request(QUrl("https://id.twitch.tv/oauth2/validate"));
        request.setRawHeader("Authorization", "OAuth " + twitchAccess.toUtf8());
        request.setTransferTimeout(15000);
        auto *reply = net.get(request);
        connect(reply, &QNetworkReply::finished, this, [this, reply] {
            const auto json = QJsonDocument::fromJson(reply->readAll()).object();
            reply->deleteLater();
            twitchUserId = json.value("user_id").toString();
            twitchLogin = json.value("login").toString();
            if (twitchUserId.isEmpty()) {
                twitchStatus(moduleText("Twitch.ValidationFailed"));
                return;
            }
            saveTwitchSession();
            twitchStatus(moduleText("Twitch.Connected").arg(twitchLogin));
            fetchTwitchInfo([] {});
        });
    }

    void refreshTwitch(std::function<void()> done)
    {
        if (twitchRefresh.isEmpty()) { done(); return; }
        QUrlQuery form;
        form.addQueryItem("client_id", twitch_client_id);
        form.addQueryItem("grant_type", "refresh_token");
        form.addQueryItem("refresh_token", twitchRefresh);
        auto *reply = twitchForm(QUrl("https://id.twitch.tv/oauth2/token"), form);
        connect(reply, &QNetworkReply::finished, this, [this, reply, done] {
            const auto json = QJsonDocument::fromJson(reply->readAll()).object();
            reply->deleteLater();
            const auto token = json.value("access_token").toString();
            if (!token.isEmpty()) {
                twitchAccess = token;
                twitchRefresh = json.value("refresh_token").toString();
                twitchExpires = QDateTime::currentDateTimeUtc().addSecs(json.value("expires_in").toInt(14400));
                saveTwitchSession();
            } else {
                twitchExpires = QDateTime();
                twitchStatus(moduleText("Twitch.RefreshFailed"));
            }
            done();
        });
    }

    void fetchTwitchInfo(std::function<void()> done)
    {
        if (twitchUserId.isEmpty() || twitchAccess.isEmpty()) { done(); return; }
        if (twitchExpires.isValid() && twitchExpires < QDateTime::currentDateTimeUtc().addSecs(60)) {
            refreshTwitch([this, done] { fetchTwitchInfo(done); });
            return;
        }
        QUrl url("https://api.twitch.tv/helix/channels");
        QUrlQuery query;
        query.addQueryItem("broadcaster_id", twitchUserId);
        url.setQuery(query);
        QNetworkRequest request(url);
        request.setRawHeader("Client-Id", twitch_client_id);
        request.setRawHeader("Authorization", "Bearer " + twitchAccess.toUtf8());
        request.setTransferTimeout(15000);
        auto *reply = net.get(request);
        connect(reply, &QNetworkReply::finished, this, [this, reply, done] {
            const auto json = QJsonDocument::fromJson(reply->readAll()).object();
            reply->deleteLater();
            const auto channels = json.value("data").toArray();
            const auto channel = channels.isEmpty() ? QJsonObject{} : channels.at(0).toObject();
            if (!channel.isEmpty()) {
                config.title = channel.value("title").toString();
                config.category = channel.value("game_name").toString();
                twitchStatus(moduleText("Twitch.Connected").arg(twitchLogin));
            } else twitchStatus(moduleText("Twitch.ChannelInfoFailed"));
            done();
        });
    }

    void captureManualSnapshot()
    {
        if (awaitingManualSnapshot || awaitingScreenshot) return;
        obs_source_t *scene = obs_frontend_preview_program_mode_active()
            ? obs_frontend_get_current_preview_scene() : obs_frontend_get_current_scene();
        if (!scene) {
            QMessageBox::warning(static_cast<QWidget *>(obs_frontend_get_main_window()), moduleText("Snapshot.Title"), moduleText("Snapshot.SceneNotFound"));
            return;
        }
        awaitingManualSnapshot = true;
        obs_frontend_take_source_screenshot(scene);
        obs_source_release(scene);
        QTimer::singleShot(10000, this, [this] {
            if (awaitingManualSnapshot) {
                awaitingManualSnapshot = false;
                QMessageBox::warning(static_cast<QWidget *>(obs_frontend_get_main_window()), moduleText("Snapshot.Title"), moduleText("Snapshot.Timeout"));
            }
        });
    }

    QByteArray consumeScreenshot(const QString &path)
    {
        QFile file(path);
        if (path.isEmpty() || !file.open(QIODevice::ReadOnly)) return {};
        const QByteArray data = file.readAll();
        file.close();
        if (!data.isEmpty() && !QFile::remove(path))
            blog(LOG_WARNING, "[stream-notify] Could not remove OBS screenshot: %s", qPrintable(path));
        return data;
    }

    void previewSnapshot(const QByteArray &data)
    {
        QPixmap pixmap;
        pixmap.loadFromData(data);
        if (pixmap.isNull()) {
            QMessageBox::warning(static_cast<QWidget *>(obs_frontend_get_main_window()), moduleText("Snapshot.Title"), moduleText("Snapshot.OpenFailed"));
            return;
        }
        QDialog preview(static_cast<QWidget *>(obs_frontend_get_main_window()));
        preview.setWindowTitle(moduleText("Snapshot.PreviewTitle"));
        auto *layout = new QVBoxLayout(&preview);
        auto *label = new QLabel;
        label->setPixmap(pixmap.scaled(800, 450, Qt::KeepAspectRatio, Qt::SmoothTransformation));
        layout->addWidget(label);
        auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
        buttons->button(QDialogButtonBox::Ok)->setText(moduleText("Snapshot.Use"));
        layout->addWidget(buttons);
        connect(buttons, &QDialogButtonBox::accepted, &preview, &QDialog::accept);
        connect(buttons, &QDialogButtonBox::rejected, &preview, &QDialog::reject);
        if (preview.exec() == QDialog::Accepted && imageField) {
            imageField->clear();
            manualSnapshot = data;
            config.image.clear();
            config.screenshot = false;
            saveConfig(config);
            if (imagePreviewField)
                imagePreviewField->setPixmap(pixmap.scaled(320, 140, Qt::KeepAspectRatio, Qt::SmoothTransformation));
            if (screenshotField) screenshotField->setChecked(false);
        }
    }

    QUrl endpoint(const QString &method) const
    {
        return QUrl("https://api.telegram.org/bot" + config.token + "/" + method);
    }

    QString twitchStreamUrl() const
    {
        if (!twitchLogin.isEmpty()) return "https://twitch.tv/" + twitchLogin;
        return {};
    }

    QString streamUrl() const
    {
        return obs_notify::resolveStreamUrl(config.url, twitchLogin);
    }

    QString bodyText() const
    {
        return obs_notify::renderPostText(config.text, config.title, config.category, streamUrl());
    }

    QString markup() const
    {
        return obs_notify::telegramReplyMarkup(config.button, streamUrl());
    }

    void publish(bool test = false)
    {
        if (config.token.isEmpty() || config.chat.isEmpty()) {
            blog(LOG_WARNING, "[stream-notify] Configure bot token and chat first");
            return;
        }
        if (!test && !obs_frontend_streaming_active()) return;
        fetchTwitchInfo([this, test] { publishReady(test); });
    }

    void publishReady(bool test)
    {
        if (!test && !obs_frontend_streaming_active()) return;
        if (config.screenshot) {
            screenshotTest = test;
            awaitingScreenshot = true;
            obs_frontend_take_screenshot();
            QTimer::singleShot(5000, this, [this, test] {
                if (awaitingScreenshot && screenshotTest == test) {
                    awaitingScreenshot = false;
                    sendSelectedPost(test);
                }
            });
        } else sendSelectedPost(test);
    }

    void sendSelectedPost(bool test)
    {
        if (!manualSnapshot.isEmpty()) sendPost({}, test, manualSnapshot);
        else sendPost(config.image, test);
    }

    void sendPost(const QString &imagePath, bool test, const QByteArray &imageData = {})
    {
        const bool photo = !imageData.isEmpty() || (!imagePath.isEmpty() && QFileInfo::exists(imagePath));
        const QString method = photo ? "sendPhoto" : "sendMessage";
        auto *multipart = new QHttpMultiPart(QHttpMultiPart::FormDataType);
        auto addText = [multipart](const QByteArray &name, const QString &value) {
            QHttpPart part;
            part.setHeader(QNetworkRequest::ContentDispositionHeader, "form-data; name=\"" + name + "\"");
            part.setBody(value.toUtf8());
            multipart->append(part);
        };
        addText("chat_id", config.chat);
        addText(photo ? "caption" : "text", bodyText());
        if (!markup().isEmpty()) addText("reply_markup", markup());
        if (photo) {
            QHttpPart part;
            const auto filename = imageData.isEmpty() ? QFileInfo(imagePath).fileName() : QString("snapshot.png");
            part.setHeader(QNetworkRequest::ContentDispositionHeader,
                           "form-data; name=\"photo\"; filename=\"" + filename.toUtf8() + "\"");
            if (!imageData.isEmpty()) part.setBody(imageData);
            else {
                auto *file = new QFile(imagePath, multipart);
                if (!file->open(QIODevice::ReadOnly)) { delete multipart; return; }
                part.setBodyDevice(file);
            }
            multipart->append(part);
        }
        QNetworkRequest request(endpoint(method));
        request.setTransferTimeout(15000);
        auto *reply = net.post(request, multipart);
        multipart->setParent(reply);
        const int sentGeneration = generation;
        const QString sentChat = config.chat;
        const QString sentToken = config.token;
        connect(reply, &QNetworkReply::finished, this, [this, reply, sentGeneration, sentChat, sentToken, test] {
            const auto doc = QJsonDocument::fromJson(reply->readAll());
            const auto object = doc.object();
            const auto id = object.value("result").toObject().value("message_id").toInteger();
            if (!object.value("ok").toBool() || id == 0) {
                const QString error = object.value("description").toString(reply->errorString());
                blog(LOG_WARNING, "[stream-notify] Telegram send failed: %s", qPrintable(error));
                if (test)
                    QMessageBox::warning(static_cast<QWidget *>(obs_frontend_get_main_window()),
                                         moduleText("TestPost.Title"),
                                         moduleText("TestPost.Failed").arg(error));
            } else if (!test) {
                if (sentGeneration == generation) messageId = id;
                else if (config.remove) deleteMessage(sentToken, sentChat, id);
            } else {
                QMessageBox::information(static_cast<QWidget *>(obs_frontend_get_main_window()),
                                         moduleText("TestPost.Title"),
                                         moduleText("TestPost.Sent"));
            }
            reply->deleteLater();
        });
    }

    void deleteMessage(const QString &token, const QString &chat, qint64 id)
    {
        QNetworkRequest request(QUrl("https://api.telegram.org/bot" + token + "/deleteMessage"));
        request.setHeader(QNetworkRequest::ContentTypeHeader, "application/x-www-form-urlencoded");
        request.setTransferTimeout(15000);
        QUrlQuery query;
        query.addQueryItem("chat_id", chat);
        query.addQueryItem("message_id", QString::number(id));
        auto *reply = net.post(request, query.toString(QUrl::FullyEncoded).toUtf8());
        connect(reply, &QNetworkReply::finished, this, [reply] {
            const auto result = QJsonDocument::fromJson(reply->readAll()).object();
            if (!result.value("ok").toBool())
                blog(LOG_WARNING, "[stream-notify] Telegram delete failed: %s", qPrintable(result.value("description").toString(reply->errorString())));
            reply->deleteLater();
        });
    }

    void removePost()
    {
        const auto id = messageId;
        messageId = 0;
        deleteMessage(config.token, config.chat, id);
    }
};

Plugin *plugin = nullptr;

void frontendEvent(obs_frontend_event event, void *)
{
    if (plugin) plugin->event(event);
}

void openSettings(void *)
{
    if (plugin) plugin->showSettings();
}
}

const char *obs_module_description(void)
{
    return obs_module_text("Module.Description");
}

bool obs_module_load(void)
{
    char *tlsFile = obs_module_file("tls/qschannelbackend.dll");
    if (tlsFile) {
        const QDir pluginData = QFileInfo(QString::fromUtf8(tlsFile)).dir();
        QCoreApplication::addLibraryPath(pluginData.absoluteFilePath(".."));
        bfree(tlsFile);
    }
    blog(LOG_INFO, "[%s] Qt TLS available: %s", module_name, QSslSocket::supportsSsl() ? "yes" : "no");
    plugin = new Plugin;
    obs_frontend_add_event_callback(frontendEvent, nullptr);
    obs_frontend_add_tools_menu_item(obs_module_text("Menu.Open"), openSettings, nullptr);
    blog(LOG_INFO, "[%s] loaded", module_name);
    return true;
}

void obs_module_unload(void)
{
    obs_frontend_remove_event_callback(frontendEvent, nullptr);
    delete plugin;
    plugin = nullptr;
}
