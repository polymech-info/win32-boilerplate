import { create } from "zustand";

export type ShellType = "powershell" | "cmd" | "bash" | "wsl" | "git-bash";

export interface ShellInstance {
  id: string;
  name: string;
  type: ShellType;
  createdAt: number;
  isActive: boolean;
}

export interface ShellSettings {
  fontSize: number;
  fontFamily: string;
  theme: "dark" | "light" | "system";
  cursorStyle: "block" | "bar" | "underline";
  cursorBlink: boolean;
  scrollback: number;
  wordSeparator: string;
  openLinksInApp: boolean;
}

interface ShellState {
  shells: ShellInstance[];
  activeShellId: string | null;
  settings: ShellSettings;
  showSettings: boolean;
  nextShellNumber: number;

  // Actions
  addShell: (type: ShellType) => string;
  removeShell: (id: string) => void;
  setActiveShell: (id: string) => void;
  updateShellName: (id: string, name: string) => void;
  toggleSettings: () => void;
  updateSettings: (settings: Partial<ShellSettings>) => void;
  getShellTypeLabel: (type: ShellType) => string;
  reorderShells: (oldIndex: number, newIndex: number) => void;
}

const defaultSettings: ShellSettings = {
  fontSize: 14,
  fontFamily: "Consolas, 'Courier New', monospace",
  theme: "dark",
  cursorStyle: "bar",
  cursorBlink: true,
  scrollback: 10000,
  wordSeparator: " \`~!@#$%^&*()-=+[{]}\\|;:'\",.<>/?",
  openLinksInApp: false,
};

const shellTypeLabels: Record<ShellType, string> = {
  powershell: "PowerShell",
  cmd: "CMD",
  bash: "Bash",
  wsl: "WSL",
  "git-bash": "Git Bash",
};

export const useShellStore = create<ShellState>()((set, get) => ({
  shells: [],
  activeShellId: null,
  settings: defaultSettings,
  showSettings: false,
  nextShellNumber: 1,

  addShell: (type: ShellType) => {
    const id = `shell-${Date.now()}-${Math.random().toString(36).substr(2, 9)}`;
    const number = get().nextShellNumber;
    const name = `${shellTypeLabels[type]} (${number})`;

    const newShell: ShellInstance = {
      id,
      name,
      type,
      createdAt: Date.now(),
      isActive: true,
    };

    set((state) => ({
      shells: [...state.shells.map((s) => ({ ...s, isActive: false })), newShell],
      activeShellId: id,
      nextShellNumber: number + 1,
    }));

    return id;
  },

  removeShell: (id: string) => {
    set((state) => {
      const shells = state.shells.filter((s) => s.id !== id);
      let activeShellId = state.activeShellId;

      if (activeShellId === id && shells.length > 0) {
        activeShellId = shells[shells.length - 1].id;
        shells[shells.length - 1].isActive = true;
      } else if (shells.length === 0) {
        activeShellId = null;
      }

      return { shells, activeShellId };
    });
  },

  setActiveShell: (id: string) => {
    set((state) => ({
      shells: state.shells.map((s) => ({
        ...s,
        isActive: s.id === id,
      })),
      activeShellId: id,
    }));
  },

  updateShellName: (id: string, name: string) => {
    set((state) => ({
      shells: state.shells.map((s) => (s.id === id ? { ...s, name } : s)),
    }));
  },

  toggleSettings: () => {
    set((state) => ({ showSettings: !state.showSettings }));
  },

  updateSettings: (newSettings: Partial<ShellSettings>) => {
    set((state) => ({
      settings: { ...state.settings, ...newSettings },
    }));
  },

  getShellTypeLabel: (type: ShellType) => shellTypeLabels[type],

  reorderShells: (oldIndex: number, newIndex: number) => {
    set((state) => {
      const shells = [...state.shells];
      const [removed] = shells.splice(oldIndex, 1);
      shells.splice(newIndex, 0, removed);
      return { shells };
    });
  },
}));
