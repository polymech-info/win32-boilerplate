import { Action, ActionState } from './types';

type Listener = () => void;

let actions: Record<string, Action> = {};
const listeners = new Set<Listener>();

function emit() {
    listeners.forEach((listener) => listener());
}

export const actionStore: ActionState & {
    subscribe: (listener: Listener) => () => void;
    getSnapshot: () => Record<string, Action>;
} = {
    get actions() {
        return actions;
    },

    registerAction: (action: Action) => {
        actions = {
            ...actions,
            [action.id]: action
        };
        emit();
    },

    unregisterAction: (actionId: string) => {
        if (!actions[actionId]) return;
        const nextActions = { ...actions };
        delete nextActions[actionId];
        actions = nextActions;
        emit();
    },

    updateAction: (actionId: string, updates: Partial<Action>) => {
        const action = actions[actionId];
        if (!action) {
            console.warn(`Action with id ${actionId} not found.`);
            return;
        }
        actions = {
            ...actions,
            [actionId]: { ...action, ...updates }
        };
        emit();
    },

    getAction: (actionId: string) => {
        return actions[actionId];
    },

    getActionsByGroup: (group: string) => {
        return Object.values(actions).filter((action) => action.group === group);
    },

    getActionsByPath: (path: string) => {
        return Object.values(actions).filter((action) => action.id.startsWith(path));
    },

    subscribe: (listener: Listener) => {
        listeners.add(listener);
        return () => listeners.delete(listener);
    },

    getSnapshot: () => actions
};

export const useActionStore = {
    getState: () => actionStore,
    subscribe: actionStore.subscribe,
    setState: () => {
        console.warn('useActionStore.setState is not supported by the shared action store.');
    }
};
