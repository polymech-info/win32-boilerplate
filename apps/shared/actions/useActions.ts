import { actionStore } from './store';
import { Action } from './types';
import { useCallback, useSyncExternalStore } from 'react';

/**
 * Hook to interact with the Action System
 */
export const useActions = () => {
    const actions = useSyncExternalStore(actionStore.subscribe, actionStore.getSnapshot, actionStore.getSnapshot);
    const registerAction = actionStore.registerAction;
    const unregisterAction = actionStore.unregisterAction;
    const updateAction = actionStore.updateAction;
    const getAction = actionStore.getAction;

    const getActionsByGroup = useCallback((group: string) => {
        return Object.values(actions).filter(a => a.group === group);
    }, [actions]);

    const executeAction = useCallback(async (actionId: string, context?: any) => {
        const action = actions[actionId];
        if (action && !action.disabled && action.handler) {
            try {
                await action.handler(context);
            } catch (error) {
                console.error(`Error executing action ${actionId}:`, error);
            }
        } else {
            console.warn(`Action ${actionId} not found or disabled.`);
        }
    }, [actions]);

    return {
        actions,
        registerAction,
        unregisterAction,
        updateAction,
        getAction,
        getActionsByGroup,
        executeAction
    };
};
