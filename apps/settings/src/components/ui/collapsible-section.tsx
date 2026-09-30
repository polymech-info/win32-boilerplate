import React, { useState, type ReactNode, useEffect } from 'react';
import { ChevronDown, ChevronUp } from 'lucide-react';
import { Button } from '@/components/ui/button';
import { Card, CardHeader, CardContent } from '@/components/ui/card';

interface CollapsibleSectionProps {
  title: ReactNode;
  children: ReactNode;
  initiallyOpen?: boolean;
  storageKey?: string; // New prop for localStorage key
  className?: string;
  headerClassName?: string;
  headerContent?: ReactNode;
  titleClassName?: string;
  buttonClassName?: string;
  contentClassName?: string;
  asCard?: boolean; // New prop to decide if it should render as a Card
  onStateChange?: (isOpen: boolean) => void; // New prop for state change callback
  id?: string;
  minimal?: boolean; // New prop for minimal styling
  renderHeader?: (
    toggle: () => void,
    isOpen: boolean
  ) => React.ReactNode;
}

const CollapsibleSection: React.FC<CollapsibleSectionProps> = ({
  title,
  children,
  initiallyOpen = true,
  storageKey,
  className = '',
  headerClassName,
  headerContent,
  titleClassName,
  buttonClassName = '', // Made button smaller
  contentClassName,
  asCard = false, // Default to not rendering as a card
  onStateChange, // Destructure new prop
  id,
  minimal = false,
  renderHeader
}) => {
  const [isOpen, setIsOpen] = useState(() => {
    if (storageKey) {
      try {
        const storedState = localStorage.getItem(storageKey);
        if (storedState !== null) {
          return JSON.parse(storedState) as boolean;
        }
      } catch (error) {
        console.error(`Error reading CollapsibleSection state from localStorage for key "${storageKey}":`, error);
      }
    }
    return initiallyOpen;
  });

  useEffect(() => {
    if (storageKey) {
      try {
        localStorage.setItem(storageKey, JSON.stringify(isOpen));
        if (onStateChange) { // Call onStateChange when state is synced from localStorage
          onStateChange(isOpen);
        }
      } catch (error) {
        console.error(`Error writing CollapsibleSection state to localStorage for key "${storageKey}":`, error);
      }
    }
  }, [isOpen, storageKey, onStateChange]);

  const toggleOpen = () => {
    const newState = !isOpen;
    setIsOpen(newState);
    if (onStateChange) { // Call onStateChange when toggling
      onStateChange(newState);
    }
  };

  // Apply minimal styling if enabled
  const finalHeaderClassName = headerClassName || (minimal 
    ? 'flex justify-between items-center px-3 py-2 cursor-pointer border-b border-border'
    : 'flex justify-between items-center p-3 md:p-4 cursor-pointer border-b border-border'
  );
  
  const finalTitleClassName = titleClassName || (minimal
    ? 'text-sm font-semibold'
    : 'text-md md:text-lg font-semibold'
  );
  
  const finalContentClassName = contentClassName || (minimal
    ? 'p-3'
    : 'p-3 md:p-4'
  );
  
  const finalContainerClassName = minimal
    ? `rounded-lg border border-border bg-card ${className}`
    : `rounded-lg shadow-none md:shadow-md border border-border bg-card ${className}`;

  const chevronSize = minimal ? "h-4 w-4" : "h-5 w-5";
  
  const header = renderHeader ? (
    renderHeader(toggleOpen, isOpen)
  ) : (
    <div className={finalHeaderClassName} onClick={toggleOpen}>
      <div className={finalTitleClassName}>{title}</div>
      <div className="flex items-center gap-2">
        {headerContent}
        <Button
          variant="ghost"
          size="icon"
          onClick={(e) => {
            e.stopPropagation();
            toggleOpen();
          }}
          className={`${buttonClassName} ${minimal ? 'h-6 w-6' : 'h-8 w-8'}`}
        >
          {isOpen ? (
            <ChevronUp className={chevronSize} />
          ) : (
            <ChevronDown className={chevronSize} />
          )}
        </Button>
      </div>
    </div>
  );


  if (asCard) {
    const cardClassName = minimal 
      ? `${className}` 
      : `shadow-none md:shadow-md ${className}`;
    return (
      <Card className={cardClassName} id={id}>
        <CardHeader className={`cursor-pointer ${finalHeaderClassName}`} onClick={toggleOpen}>
          <div className="flex justify-between items-center w-full">
            <div className={finalTitleClassName}>{title}</div>
            <div className="flex items-center gap-2">
              {headerContent}
              <Button 
                variant="ghost" 
                size="icon" 
                onClick={(e) => { e.stopPropagation(); toggleOpen(); }} 
                className={`${buttonClassName} ${minimal ? 'h-6 w-6' : 'h-8 w-8'}`}
              >
                {isOpen ? <ChevronUp className={chevronSize} /> : <ChevronDown className={chevronSize} />}
              </Button>
            </div>
          </div>
        </CardHeader>
        {isOpen && <CardContent className={finalContentClassName}>{children}</CardContent>}
      </Card>
    );
  }

  return (
    <div className={finalContainerClassName} id={id}>
      {header}
      {isOpen && <div className={finalContentClassName}>{children}</div>}
    </div>
  );
};

export default CollapsibleSection;

