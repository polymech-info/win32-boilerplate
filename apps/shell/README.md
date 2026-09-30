# PolyMech Shell App

A modern terminal application built with React, TypeScript, and xterm.js. Designed to integrate seamlessly with a C++ backend via WebView2.

## Features

- **Multiple Shell Support**: PowerShell, Command Prompt, Bash, WSL, Git Bash
- **Accordion-style Shell Management**: VS Code-inspired shell list on the right side
- **xterm.js Integration**: Full terminal emulation with customizable settings
- **Settings Panel**: Accessible via gear icon with appearance, cursor, and scrollback options
- **Theme Support**: Dark, light, and system theme options
- **Persistent Settings**: Zustand store with localStorage persistence
- **WebView2 Integration**: Ready for C++ backend communication

## Architecture

```
shell/
├── src/
│   ├── shell/
│   │   ├── index.ts              # Module exports
│   │   ├── shellStore.ts         # Zustand state management
│   │   ├── Terminal.tsx          # xterm.js terminal component
│   │   ├── ShellPanel.tsx        # Main shell UI with accordion
│   │   ├── SettingsPanel.tsx     # Settings modal
│   │   ├── usePtyWebSocket.ts    # WebSocket hook for PTY
│   │   └── shell-styles.css      # Complete stylesheet
│   └── main.tsx                  # App entry point
├── server/
│   └── pty-server.js             # Node-PTY testing server
├── package.json
└── README.md
```

## PTY Testing Server

For development/testing, a Node.js PTY server is included that bridges xterm.js to real shell processes:

```
Frontend (xterm.js) ← WebSocket → Node-PTY Server ← node-pty → Real Shell (PowerShell/Bash/etc)
```

### PTY Server Commands

| Script | Description |
|--------|-------------|
| `npm run server` | Start PTY server only (ws://localhost:8080) |
| `npm run dev:pty` | Start both PTY server and dev server |
| `npm run dev` | Dev server only (demo mode, echoes input) |

### PTY Protocol

WebSocket messages between frontend and server:

**Frontend → Server:**
- `{ type: "create", shellId, shellType, cols, rows }` - Create new PTY
- `{ type: "input", shellId, data }` - Send keystrokes
- `{ type: "resize", shellId, cols, rows }` - Resize terminal
- `{ type: "kill", shellId }` - Kill PTY process

**Server → Frontend:**
- `{ type: "output", shellId, data }` - Terminal output
- `{ type: "exit", shellId, exitCode, signal }` - Process exited
- `{ type: "created", shellId, pid }` - PTY created successfully

### Environment Variables

```bash
# Custom PTY server URL
VITE_PTY_WS_URL=ws://localhost:8080 npm run dev

# Custom PTY server port
SHELL_SERVER_PORT=9000 npm run server
```

## Components

### ShellStore (Zustand)

Manages global state for:
- Multiple shell instances (add, remove, activate)
- Terminal settings (font, theme, cursor, scrollback)
- Shell type definitions (PowerShell, CMD, Bash, WSL, Git Bash)
- Persistence to localStorage

### Terminal Component

Wraps xterm.js with:
- FitAddon for responsive sizing
- WebLinksAddon for clickable URLs
- Theme synchronization
- Data/resize callbacks for backend communication
- Demo mode for development (echos back input)

### ShellPanel Component

Main UI featuring:
- Header with shell count, new shell dropdown, settings button
- Terminal display area (active terminal)
- Accordion sidebar with shell list:
  - Expandable shell items with details
  - Click to activate
  - Double-click to rename
  - Close button
  - "Close All" footer button

### SettingsPanel Component

Modal overlay with sections:
- **Appearance**: Theme (dark/light/system), font size, font family
- **Cursor**: Style (line/block/bar), blink toggle
- **Scrollback**: Number of lines to keep
- **Advanced**: Word separators
- Reset to defaults button

## WebView2 Integration

The app communicates with the C++ backend via `window.chrome.webview.postMessage`:

### Outgoing Messages

```typescript
// Terminal input data
{ t: "shell_data", shellId: string, data: string }

// Terminal resize
{ t: "shell_resize", shellId: string, cols: number, rows: number }

// Window move (alt-drag)
{ t: "cweb_begin_move", src: "alt_drag" }
```

### Incoming Messages

```typescript
// Shell output from backend
{ t: "shell_output", shellId: string, data: string }

// Shell process exited
{ t: "shell_exit", shellId: string, code: number }

// Theme change from host
{ t: "cweb_theme", theme: "dark" | "light" }
```

## Development

```bash
# Install dependencies
npm install

# Start development server (demo mode, no real shell)
npm run dev

# Start with PTY server (real shell backend)
npm run dev:pty

# Or run separately:
npm run server    # Terminal 1: Start PTY server
npm run dev       # Terminal 2: Start dev server

# Build for production
npm run build

# Preview production build
npm run preview
```

## Dependencies

- `@xterm/xterm` - Terminal emulator
- `@xterm/addon-fit` - Auto-fit terminal to container
- `@xterm/addon-web-links` - Clickable URLs
- `zustand` - State management
- `lucide-react` - Icons

## Usage

The app automatically initializes with a default shell (PowerShell on Windows, Bash otherwise).

### Adding Shells
Click the `+` button in the header and select a shell type from the dropdown.

### Switching Shells
Click any shell in the accordion list on the right to activate it.

### Renaming Shells
Double-click a shell name in the accordion, or click the `...` menu.

### Closing Shells
Click the `×` button on a shell item, or use "Close All" at the bottom.

### Settings
Click the gear icon in the header to open settings. Changes are applied immediately and persisted.

## Future Enhancements

- [x] WebSocket backend integration for real shell processes (testing via node-pty)
- [ ] WebView2 C++ backend integration (production)
- [ ] Tab support (alternative to accordion)
- [ ] Split panes
- [ ] Search in terminal
- [ ] Custom keybindings
- [ ] Shell profiles
- [ ] Environment variable editor
- [ ] Working directory display/change
- [ ] Copy/paste integration
- [ ] Context menu
- [ ] Terminal bell/notification
- [ ] Auto-complete for paths

## Migration Notes

The original `main.tsx` has been backed up to `main.tsx.backup`. The new `main.tsx` replaces the settings panel with the shell application while maintaining:
- WebView2 message bridge structure
- Theme handling
- Alt-drag window movement
