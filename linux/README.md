# PM-Image GTK4 GUI (Linux)

GNOME-style native UI for PM-Image on Ubuntu and other GTK4-based Linux distributions.

## Architecture

```
GTK4 + libadwaita + libpanel
├── Navigation dock (Files / Projects / Recent / Favorites / Chat)
├── Center workspace (Main tabs + native file browser + chat web panel)
├── Right inspector dock
└── Bottom dock (Logs / Problems / Tasks) + status bar
```

## Prerequisites

### Ubuntu/Debian

```bash
# GTK4 + libadwaita (required)
sudo apt update
sudo apt install libgtk-4-dev libadwaita-1-dev

# Optional: WebKitGTK for embedded web panels (docs, chat, settings)
sudo apt install libwebkitgtk-6.0-dev

# Build essentials (if not installed)
sudo apt install build-essential cmake ninja-build pkg-config
```

### Fedora

```bash
# GTK4 + libadwaita
sudo dnf install gtk4-devel libadwaita-devel

# Optional: WebKitGTK
sudo dnf install webkitgtk6.0-devel
```

## Building

### Configure with GUI enabled

```bash
cd packages/media/cpp
npm run config -- -DPM_BUILD_LINUX_GUI=ON
```

Or manually with CMake:

```bash
cmake --preset release-linux -DPM_BUILD_LINUX_GUI=ON
```

### Build the GUI

```bash
npm run build:gui:linux
```

Or manually:

```bash
cmake --build build/linux-release --target pm-image-gui
```

The binary will be output to `dist/pm-image-gui`.

## Running

```bash
./dist/pm-image-gui
```

## Development

### Project Structure

```
linux/
├── CMakeLists.txt      # CMake build configuration
├── src/
│   └── main.cpp        # GTK4 app entry point
├── README.md           # This file
└── ui.md               # Design notes and architecture
```

### Key Classes/Concepts

| Component | GTK4/libadwaita |
|-----------|----------------|
| Window / Workspace | `PanelWorkbench` + `PanelDocumentWorkspace` |
| Header/Toolbar | `AdwHeaderBar` + `PanelOmniBar` |
| Main destinations | `GtkStack` |
| File Browser | `GtkDirectoryList` + `GtkColumnView` |
| Web Panel (Chat) | `WebKitWebView` (via WebKitGTK) |

### Native File Browser (Linux)

- The `Files` destination is a GTK-native file browser (no web wrapper).
- Built with `GtkDirectoryList` + `GtkColumnView` columns for `Name`, `Type`, and `Size`.
- Uses native `GFile` navigation (`Home`, `Up`, and double-click a folder to enter).
- Default root is the current user's home directory.
- This is the Linux counterpart to embedded Explorer on Win32, while remaining native to GNOME/GTK.

### Feature Roadmap

- [x] Basic GTK4/libadwaita window
- [x] libpanel workspace shell (left/right/bottom docks)
- [x] Native file browser with `GtkDirectoryList` + `GtkColumnView`
- [x] Chat web panel from navigation (`dist/chat.html` via WebKitGTK)
- [ ] Image thumbnail grid with `GtkGridView` (next)
- [ ] Image preview with `GtkPicture` (next)
- [ ] Drag & drop support (GTK DnD)
- [ ] Settings integration with `GSettings`
- [ ] .desktop file and app icon

## Packaging

### Flatpak (Recommended)

Flatpak provides the most predictable runtime for GTK4 + WebKitGTK + GStreamer.

```yaml
# TODO: Add flatpak manifest (com.polymech.pm-image.yml)
```

### AppImage

GTK4 + WebKitGTK in AppImage requires careful bundling of libraries.

### Distribution Packages

- `.deb` for Ubuntu/Debian
- `.rpm` for Fedora/openSUSE

## Troubleshooting

### WebKitGTK not found

The GUI builds without WebKitGTK if not found. To enable web panels:

```bash
# Verify WebKitGTK is installed
pkg-config --exists webkitgtk-6.0 && echo "WebKitGTK found"

# If not found, install it and reconfigure
sudo apt install libwebkitgtk-6.0-dev
npm run config -- -DPM_BUILD_LINUX_GUI=ON
```

### Dark theme not applying

```bash
# Force dark theme via gsettings
gsettings set org.gnome.desktop.interface color-scheme 'prefer-dark'
```

Or set via `GTK_THEME`:

```bash
GTK_THEME=Adwaita:dark ./dist/pm-image-gui
```
