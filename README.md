# Plasma Tweaks

Qt6 GUI tool for customizing KDE Plasma 6 applets whose QML is compiled into `.so` binaries.

## Supported Tweaks

- **Kickoff** — Category list item padding
- **System Tray** — Icon size override (autoSize mode)
- **Show Desktop** — Layout dimension constraint
- **DefaultCompactRepresentation** — Icon size constraint

## Build

```bash
make        # build
make run    # build & run
make clean  # clean
```

### Dependencies

Arch Linux:

```bash
sudo pacman -S qt6-base qt6-declarative qt6-quickcontrols2 cmake ninja \
  extra-cmake-modules plasma-framework plasma-desktop
```

## How It Works

1. Sparse-clones KDE source repos matching your installed Plasma version
2. Patches QML files with your values
3. Builds patched `.so` plugins (incremental, seconds after first build)
4. Installs via `pkexec` with automatic `.bak` backup
5. Restarts plasmashell

Runtime data: `~/.local/share/plasma-tweaks/`

## License

GPL-2.0-or-later
