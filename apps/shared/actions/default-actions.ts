import { Action } from './types';

export const UNDO_ACTION_ID = 'Edit/Undo';
export const REDO_ACTION_ID = 'Edit/Redo';
export const COPY_ACTION_ID = 'Edit/Copy';
export const PASTE_ACTION_ID = 'Edit/Paste';
export const FINISH_ACTION_ID = 'File/Finish';
export const CANCEL_ACTION_ID = 'File/Cancel';

export const createDefaultActions = (): Action[] => [
    {
        id: UNDO_ACTION_ID,
        label: 'Undo',
        icon: 'undo',
        group: 'History',
        shortcut: 'ctrl+z',
        visibilities: {
            Ribbon: true
        },
        handler: () => { console.warn('Undo action not implemented') }
    },
    {
        id: REDO_ACTION_ID,
        label: 'Redo',
        icon: 'redo',
        group: 'History',
        shortcut: 'ctrl+y',
        visibilities: {
            Ribbon: true
        },
        handler: () => { console.warn('Redo action not implemented') }
    },
    {
        id: COPY_ACTION_ID,
        label: 'Copy',
        icon: 'copy',
        group: 'Clipboard',
        shortcut: 'ctrl+c',
        visibilities: {
            Toolbar: true,
            ContextMenu: true
        },
        handler: () => { console.warn('Copy action not implemented') }
    },
    {
        id: PASTE_ACTION_ID,
        label: 'Paste',
        icon: 'paste',
        group: 'Clipboard',
        shortcut: 'ctrl+v',
        visibilities: {
            Toolbar: true,
            ContextMenu: true
        },
        handler: () => { console.warn('Paste action not implemented') }
    },
    {
        id: FINISH_ACTION_ID,
        label: 'Finish',
        icon: 'finish',
        group: 'Exit',
        visibilities: {
            Ribbon: true
        },
        handler: () => { console.warn('Finish action not implemented') }
    },
    {
        id: CANCEL_ACTION_ID,
        label: 'Cancel',
        icon: 'cancel',
        group: 'Exit',
        visibilities: {
            Ribbon: true
        },
        handler: () => { console.warn('Cancel action not implemented') }
    }
];
