# Noctalia

Noctalia is a native Wayland desktop shell for people who want a polished, configurable Linux desktop without stitching
together a separate bar, launcher, notification daemon, lock screen, wallpaper tool, and settings UI.

It provides the shell layer around your compositor: bars, widgets, dock, launcher, control center, notifications,
wallpaper, lock screen, session actions, clipboard history, OSDs, tray integration, and desktop widgets. The project is
built directly on Wayland and OpenGL ES with no Qt or GTK dependency, so the UI, rendering, configuration, and IPC model
are designed as one cohesive shell instead of a collection of unrelated panels and scripts.

> [!IMPORTANT]
> Noctalia v5 is currently in Beta. While the core features and architecture are stabilizing, you may still encounter occasional configuration or behavior adjustments as we prepare for the final release.

## Ubuntu 24.04 port (branch `ubuntu-24.04`)

This branch is upstream **v5.0.1** (`f95e95caf`) adapted to build and run on **Ubuntu 24.04 LTS (noble)**. Upstream
targets newer libraries than noble ships; the port keeps every feature and changes only how the code talks to those
libraries. Each change is a separate commit on top of the tag, so `git log v5.0.1..ubuntu-24.04` shows the full delta.

| Library | Upstream expects | Ubuntu 24.04 ships |
| --- | --- | --- |
| sdbus-c++ | 2.x | 1.4.0 |
| WirePlumber | 0.5 | 0.4 |
| libwayland-client | provides `wl_proxy_get_display()` | 1.22, no such call |
| stb (`libstb-dev`) | includes `stb_image_resize2.h` | header missing |
| C++23 compiler | default `g++` | `g++-14` (default is g++-13) |

### Commits

**1. `Port to Ubuntu 24.04 libraries (sdbus-c++ 1.4, WirePlumber 0.4)`**: 37 files under `src/` plus `meson.build`.

| Area | sdbus-c++ 2.x / upstream | sdbus-c++ 1.4 / this branch |
| --- | --- | --- |
| Exported D-Bus objects (notifications, tray watcher, MPRIS, agents, debug, KWin bridge) | `addVTable(registerMethod(...), ...)` | `registerMethod/Signal/Property(...)` per member, then `finishRegistration()` |
| Bus and error names | strong types `sdbus::ServiceName`, `sdbus::Error::Name` | `std::string` |
| Async call replies | `std::optional<sdbus::Error>` | `const sdbus::Error*` (`nullptr` on success) |
| Event loop integration (`session_bus`, `system_bus`, poll sources) | `processPendingEvent()`, `PollData::eventFd` | `processPendingRequests()`, `PollData::fd` |
| Structured values (tray tooltips, dbusmenu, NetworkManager, BlueZ) | decoded as `std::tuple` | decoded as `sdbus::Struct<...>` |
| UPower charge threshold (new in v5.0.1) | `getProperty(std::string_view)`, optional error | `std::string` arguments, `const sdbus::Error*` |
| WirePlumber mixer | `wp_core_new(ctx, nullptr, nullptr)`, `wp_core_load_component(..., nullptr, ...)` | 0.4 signatures |
| Virtual keyboard (`wayland_connection`, `virtual_keyboard_service`) | display from `wl_proxy_get_display()` | `wl_display*` passed explicitly |
| `meson.build` | requires `wireplumber-0.5` and system `stb_image_resize2.h` | falls back to `wireplumber-0.4`; uses `third_party/stb` |

`third_party/stb/stb/stb_image_resize2.h` (stb v2.18, public domain) is vendored in the same commit so that every commit
builds.

**2. `Subscribe D-Bus proxies to their signals on sdbus-c++ 1.4`**: 33 lines in 14 files.

In sdbus-c++ 2.x, `proxy->uponSignal(...).call(...)` subscribes immediately. In 1.4 it only stores the handler; the
match rule is installed by `IProxy::finishRegistration()`. Commit 1 alone compiles, but no proxy ever receives a
signal. Consequences observed before this commit:

| Service | Lost signal | Symptom |
| --- | --- | --- |
| Tray | `NameOwnerChanged` | icons of closed apps stay; reopening the app adds a duplicate |
| logind | `PrepareForSleep`, `Lock`, `Unlock` | `lockscreen.lock_before_suspend` cannot lock before sleep |
| UPower, power profiles | `PropertiesChanged`, `DeviceAdded/Removed` | battery and profile updated only by polling |
| NetworkManager, wpa_supplicant, iwd | `PropertiesChanged`, scan and BSS signals | network state updated only by polling |
| BlueZ | `InterfacesAdded/Removed`, `PropertiesChanged` | devices appear late or not at all |
| MPRIS, accounts, fingerprint, keyboard backlight, notification and secret-service watchers | name-owner and property signals | stale state until the next poll |

`finishRegistration()` is called once per proxy, right after its last `uponSignal()` and before the proxy is moved
into a container. A standalone test against `libsdbus-c++` 1.4.0 receives 0 `NameOwnerChanged` signals without the
call and 2 with it.

**3. `Add Debian packaging for Ubuntu 24.04`**: `debian/` with noble build dependencies (`libsdbus-c++-dev` 1.4,
`libwireplumber-0.4-dev`, `g++-14`).

Blur through `ext-background-effect` needs no change on this branch: upstream v5.0.1 already handles the
wayland-protocols 1.45 definition of the `blur` capability bit (`873de57e2`).

### Build on Ubuntu 24.04

```sh
sudo apt install build-essential debhelper meson ninja-build pkg-config g++-14 \
  libwayland-dev wayland-protocols libegl-dev libgles-dev libepoxy-dev \
  libfreetype-dev libfontconfig-dev libcairo2-dev libpango1.0-dev libharfbuzz-dev \
  librsvg2-dev libxkbcommon-dev libglib2.0-dev libsdbus-c++-dev libsecret-1-dev \
  libsodium-dev libpolkit-agent-1-dev libpolkit-gobject-1-dev libpipewire-0.3-dev \
  libwireplumber-0.4-dev libcurl4-openssl-dev libqalculate-dev libpam0g-dev \
  libxml2-dev libical-dev libtomlplusplus-dev libstb-dev libwebp-dev libjxl-dev \
  libsndfile1-dev libjemalloc-dev libmd4c-dev

git clone -b ubuntu-24.04 https://github.com/kabronius/noctalia.git
cd noctalia

# Binary in build-release/noctalia
CC=gcc-14 CXX=g++-14 meson setup build-release --buildtype=release -Dprefix=/usr
meson compile -C build-release

# Or a .deb in the parent directory
dpkg-buildpackage -b -us -uc --no-sign
```

### Verification status

Checked on Ubuntu 24.04 with niri 26.04:

- full build with g++-14 against sdbus-c++ 1.4.0 and WirePlumber 0.4; package `5.0.1-0ubuntu1~24.04`;
- tray: a StatusNotifierItem that exits is removed (`item unregistered` in the log);
- compositor blur requested and applied (`blur-effect-create`);
- Polkit authentication agent active, notification daemon owning `org.freedesktop.Notifications`, MPRIS discovery
  active.

Not exercised: `PrepareForSleep` during a real suspend, fingerprint authentication, iwd and wpa_supplicant backends
(the test machine uses NetworkManager).

### Updating to a newer upstream release

Rebase the three commits onto the new tag and rebuild. Code added upstream after v5.0.1 may again use sdbus-c++ 2.x
APIs; the compiler reports each one, and the table under commit 1 lists the matching 1.4 replacement. Every new
`uponSignal()` on a proxy also needs a `finishRegistration()` after it.


<p><br/></p>

<p align="center">
  <img src="https://assets.noctalia.dev/noctalia-logo.svg?v=2" alt="Noctalia Logo" style="width: 192px" />
</p>

<p align="center">
  <a href="https://docs.noctalia.dev/noctalia/getting-started/installation/">
    <img
      src="https://img.shields.io/badge/Install_Noctalia-FFF59B?style=for-the-badge&labelColor=FFF59B"
      alt="Install Noctalia"
      style="height: 50px"
    />
  </a>
</p>

<p><br/></p>

<p align="center">
  <a href="https://github.com/noctalia-dev/noctalia/commits">
    <img src="https://img.shields.io/github/last-commit/noctalia-dev/noctalia?style=for-the-badge&labelColor=FFF59B&color=FFF59B&logo=git&logoColor=070722&label=commit" alt="Last commit" />
  </a>
  <a href="https://github.com/noctalia-dev/noctalia/stargazers">
    <img src="https://img.shields.io/github/stars/noctalia-dev/noctalia?style=for-the-badge&labelColor=FFF59B&color=FFF59B&logo=github&logoColor=070722" alt="GitHub stars" />
  </a>
  <a href="https://docs.noctalia.dev">
    <img src="https://img.shields.io/badge/docs-FFF59B?style=for-the-badge&logo=gitbook&logoColor=070722&labelColor=FFF59B" alt="Documentation" />
  </a>
  <a href="https://discord.noctalia.dev">
    <img src="https://img.shields.io/badge/discord-FFF59B?style=for-the-badge&labelColor=FFF59B&logo=discord&logoColor=070722" alt="Discord" />
  </a>
</p>

## Why Noctalia?

Most Wayland setups leave the desktop shell to a stack of small tools: one bar, another launcher, another notification
daemon, a lock screen, a wallpaper daemon, scripts for session actions, and separate config formats for each piece. That
can be flexible, but it also makes a complete desktop feel fragile and hard to keep visually consistent.

Noctalia solves that by providing one configurable shell layer that owns the common desktop surfaces and services while
still fitting into compositor-driven Wayland workflows. It is meant for users who want the control of a custom desktop
environment with fewer moving parts and a consistent UI.

To understand the values and philosophy guiding the project, read our [ethos](https://noctalia.dev/ethos).

## What It Includes

- Multi-monitor bars with configurable widgets, taskbar, workspaces, system tray, media, network, battery, brightness,
  weather, clipboard, and custom script-backed widgets.
- Dock, launcher, control center, notification toasts/history, wallpaper picker, OSD overlays, lock screen, session
  panel, and desktop widgets.
- TOML configuration with hot reload, GUI-managed overrides, theme/palette support, template application, and IPC for
  runtime control.
- Direct Wayland integration for layer-shell, session lock, idle behavior, clipboard, foreign toplevels, workspaces,
  fractional scaling, and compositor-specific workspace backends where needed.

## Wayland Compositor Support

Noctalia supports Wayland compositors that provide the layer-shell protocols it needs for shell surfaces. Workspace
integration works through compositor-native backends where needed, or through `ext-workspace-v1` on compositors that
implement it.

Current compositor integrations include Niri, Hyprland, Sway, Scroll, Mango, Labwc, Triad, dwl, and other compatible
Wayland compositors. Other compositors may run Noctalia but can have reduced workspace, window, output, or
session-action integration depending on the protocols and IPC they expose.

## Scope

Noctalia is a desktop shell, not a full desktop environment. It provides the visual and service layer around your
Wayland compositor: bars, panels, launcher, notifications, dock, lock screen, idle behavior, OSDs, theming, wallpapers,
desktop widgets, and multi-monitor shell surfaces.

Window management, tiling, file management, removable-drive mounting, printers management and screen mirroring/casting
belong to the compositor, dedicated desktop applications, or system services.

Display/login greeter support lives in the separate [Noctalia Greeter](https://github.com/noctalia-dev/noctalia-greeter)
project. Noctalia may integrate with those pieces when useful, but it does not replace them.

The plugin system is available for user-installed extensions. Features that are useful to some users but not essential
to the core shell can live there: extra bar widgets, launcher providers, desktop widgets, panels, shortcuts, background
services, compositor-specific extras, hardware-specific controls, and third-party service integrations.

## Build from source

Source dependencies, distro-specific package commands, build modes, and install layouts live in
[BUILDING.md](BUILDING.md).

## Configuration

A ready-to-use starting config with all defaults is at [example.toml](example.toml). The full configuration reference
lives in the [documentation site](https://docs.noctalia.dev/noctalia/). The source MDX files are in
[`docs/user/`](docs/user/); sync them to a local docs checkout with `tools/sync-docs.sh`.

## Contributing

Developer notes, architecture overview, code style, project layout, and debugging commands live in
[CONTRIBUTING.md](CONTRIBUTING.md).

Bug reports, fixes, documentation updates, themes, and configuration examples are welcome. For general help and design
discussion, join the community on [Discord](https://discord.noctalia.dev).

## Credits

Thank you to the [contributors](https://github.com/noctalia-dev/noctalia/graphs/contributors) and community
members who test Noctalia, report issues, share configurations, and help shape the project.

## Donations

Donations are appreciated but completely optional.

<p>
  <a href="https://www.buymeacoffee.com/noctalia">
    <img src="https://img.shields.io/badge/Buy_Me_a_Coffee-FFF59B?style=for-the-badge&logo=buymeacoffee&logoColor=070722&labelColor=FFF59B" alt="Buy Me a Coffee">
  </a>
  <a href="https://ko-fi.com/noctaliadev">
    <img src="https://img.shields.io/badge/Ko--fi-FFF59B?style=for-the-badge&logo=kofi&logoColor=070722&labelColor=FFF59B" alt="Ko-fi">
  </a>
</p>

## License

MIT License. See [LICENSE](LICENSE) for details.

## Packaging

Distro packaging notes (description, deps, install layout, Meson options) live in
[PACKAGING.md](PACKAGING.md).

## Star History

<p align="center">
  <a href="https://github.com/noctalia-dev/noctalia/stargazers">
    <img src="https://api.noctalia.dev/stars" alt="Star History" />
  </a>
</p>
