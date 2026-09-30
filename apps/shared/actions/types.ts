import { ReactNode } from 'react';

export type ActionVisibility = 'Ribbon' | 'Command' | 'ContextMenu' | 'Toolbar';

export interface Action {
    /**
     * Unique identifier for the action, typically the command path (e.g., "File/Edit/Undo")
     */
    id: string;
    /**
     * Readable label for the action
     */
    label: string;
    /**
     * Icon for the action (Lucide icon component or string name)
     */
    icon?: any;
    /**
     * Group for organizing actions in UI (e.g., "History", "File")
     */
    group?: string;
    /**
     * The handler function to execute when the action is triggered
     */
    handler: (context?: any) => void | Promise<void>;
    /**
     * Keyboard shortcut (e.g., "ctrl+z")
     */
    shortcut?: string;
    /**
     * Whether the action is currently disabled
     */
    disabled?: boolean;
    /**
     * Whether the action is currently visible
     */
    visible?: boolean;
    /**
     * Specific visibility settings for different UI contexts
     */
    visibilities?: Partial<Record<ActionVisibility, boolean>>;
    /**
     * Tooltip text
     */
    tooltip?: string;
    /**
     * Any additional metadata
     */
    metadata?: Record<string, any>;
    /**
     * Parent action ID if this is a sub-action
     */
    parentId?: string;
}

export type ActionState = {
    actions: Record<string, Action>;
    registerAction: (action: Action) => void;
    unregisterAction: (actionId: string) => void;
    updateAction: (actionId: string, updates: Partial<Action>) => void;
    getAction: (actionId: string) => Action | undefined;
    getActionsByGroup: (group: string) => Action[];
    getActionsByPath: (path: string) => Action[];
}
