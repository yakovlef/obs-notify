# Stream Notify

Native OBS Studio plugin that sends a notification when a stream starts.

Stream Notify is a third-party plugin and is not affiliated with the OBS Project.

Telegram is currently supported. Discord support is planned for a future release.

## Features

- Publish a Telegram channel post when streaming starts.
- Read the stream title, category, and URL from a connected Twitch account.
- Configure the post text, link button, publication delay, and automatic deletion when streaming stops.
- Attach an image or capture the current OBS scene.
- English and Russian interface translations.

## Installation

Download the latest Windows installer from [Releases](https://github.com/yakovlef/obs-stream-notify/releases), close OBS Studio, and run the installer. It detects the OBS Studio installation directory automatically.

OBS Studio 31 or newer is required.

A ZIP archive for manual installation is also attached to every release. Extract the `stream-notify` directory into:

```text
C:\ProgramData\obs-studio\plugins
```

Create a Telegram bot with [BotFather](https://t.me/BotFather), add it to your channel as an administrator, then open **Tools → Stream Notify** in OBS Studio.

## Build

Requirements:

- Visual Studio 2022 Build Tools with **Desktop development with C++**;
- CMake 3.28 or newer;
- Git for Windows.

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 -DBUILD_TESTING=ON
cmake --build build --config Release --parallel
ctest --test-dir build -C Release --output-on-failure
```

The build system downloads the pinned OBS Studio and Qt dependencies automatically.

## License

[GNU General Public License version 2 or later](LICENSE)
