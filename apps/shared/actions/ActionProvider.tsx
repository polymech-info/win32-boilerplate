import React, { useEffect } from 'react';
import { useActions } from './useActions';
import { createDefaultActions } from './default-actions';

interface ActionProviderProps {
    children: React.ReactNode;
}

export const ActionProvider: React.FC<ActionProviderProps> = ({ children }) => {
    const { registerAction } = useActions();

    useEffect(() => {
        const defaults = createDefaultActions();
        defaults.forEach(action => {
            registerAction(action);
        });
    }, [registerAction]);
    return <>{children}</>;
};
