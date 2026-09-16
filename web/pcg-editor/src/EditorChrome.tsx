import { useEffect, useLayoutEffect, useRef, useState, type KeyboardEvent, type ReactNode } from 'react';

export type IconName = 'menu' | 'agent' | 'folder' | 'graph' | 'preview' | 'settings' | 'search' | 'fit' | 'plus' | 'save' | 'file' | 'download' | 'undo' | 'redo' | 'close' | 'chevron' | 'windows';
const paths: Record<IconName, string> = {
  windows: 'M3 3h18v18H3zM3 8h18M8 8v13',
  menu: 'M4 6h16M4 12h16M4 18h16', agent: 'M9 3h6M12 3v3M5 7h14v13H5zM8 11h1M15 11h1M9 16h6M2 11v5M22 11v5',
  folder: 'M3 6h7l2 3h9v11H3z', graph: 'M12 5v5M5 14v-4h14v4M3 15h4v5H3zM10 1h4v4h-4zM17 15h4v5h-4z',
  preview: 'M12 3 3 8v9l9 5 9-5V8zM3 8l9 5 9-5M12 13v9', settings: 'M9 3h6l1 3 3 1 2 5-2 5-3 1-1 3H9l-1-3-3-1-2-5 2-5 3-1zM15 12a3 3 0 1 1-6 0 3 3 0 0 1 6 0',
  search: 'M16 16l5 5M18 10a8 8 0 1 1-16 0 8 8 0 0 1 16 0', fit: 'M8 3H3v5M16 3h5v5M3 16v5h5M21 16v5h-5',
  plus: 'M12 4v16M4 12h16', save: 'M4 3h13l3 3v15H4zM8 3v6h8V3M8 21v-8h8v8', file: 'M5 3h9l5 5v13H5zM14 3v6h5',
  download: 'M12 3v12M7 10l5 5 5-5M4 17v4h16v-4', undo: 'M9 5 4 10l5 5M4 10h10a6 6 0 0 1 6 6v3',
  redo: 'm15 5 5 5-5 5M20 10H10a6 6 0 0 0-6 6v3', close: 'm6 6 12 12M6 18 18 6', chevron: 'm6 9 6 6 6-6',
};
export function Icon({ name }: { name: IconName }) {
  return <svg width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="1.6" strokeLinecap="round" strokeLinejoin="round" aria-hidden="true"><path d={paths[name]} /></svg>;
}
export interface MenuItem {
  label: string; icon?: IconName; shortcut?: string; onSelect?: () => void;
  checked?: boolean; disabled?: boolean; separator?: boolean; children?: MenuItem[];
}
function MenuLevel({ items, label, onSelect, onBack, onDismiss, nested = false }: {
  items: MenuItem[]; label: string; onSelect: (item: MenuItem) => void; onBack: () => void; onDismiss: () => void; nested?: boolean;
}) {
  const [expanded, setExpanded] = useState<string | null>(null);
  const root = useRef<HTMLDivElement>(null);
  const focus = (index: number) => {
    const buttons = root.current?.querySelectorAll<HTMLButtonElement>(':scope > [role=none] > button:not(:disabled)');
    if (buttons?.length) buttons[(index + buttons.length) % buttons.length].focus();
  };
  useLayoutEffect(() => { focus(0); }, []);
  const keyDown = (event: KeyboardEvent) => {
    event.stopPropagation();
    const buttons = [...(root.current?.querySelectorAll<HTMLButtonElement>(':scope > [role=none] > button:not(:disabled)') ?? [])];
    const index = buttons.indexOf(document.activeElement as HTMLButtonElement);
    if (event.key === 'Escape' || (nested && event.key === 'ArrowLeft')) { event.preventDefault(); onBack(); }
    else if (event.key === 'ArrowDown' || event.key === 'ArrowUp') { event.preventDefault(); focus(index + (event.key === 'ArrowDown' ? 1 : -1)); }
    else if (event.key === 'Home' || event.key === 'End') { event.preventDefault(); focus(event.key === 'Home' ? 0 : -1); }
    else if (event.key === 'Tab') onDismiss();
  };
  return <div ref={root} className={`pcg-menu__popup ${nested ? 'pcg-menu__popup--nested' : ''}`} role="menu" aria-label={label} onKeyDown={keyDown}>
    <div className="pcg-menu__caption">{label}</div>
    {items.map((item) => <div key={item.label} role="none" className="pcg-menu__row" onMouseEnter={() => setExpanded(item.children ? item.label : null)}>
      {item.separator && <div role="separator" className="pcg-menu__divider" />}
      <button type="button" role={item.checked === undefined ? 'menuitem' : 'menuitemcheckbox'} aria-checked={item.checked}
        aria-haspopup={item.children ? 'menu' : undefined} aria-expanded={item.children ? expanded === item.label : undefined}
        disabled={item.disabled} tabIndex={-1}
        onFocus={() => { if (expanded && expanded !== item.label) setExpanded(null); }}
        onKeyDown={(event) => { if (item.children && event.key === 'ArrowRight') { event.preventDefault(); event.stopPropagation(); setExpanded(item.label);
          const row = event.currentTarget.parentElement;
          row?.querySelector<HTMLButtonElement>('[role=menu] button:not(:disabled)')?.focus();
        } }}
        onClick={() => { if (item.children) setExpanded(item.label); else onSelect(item); }}>
        {item.checked !== undefined ? <span className={`pcg-menu__check ${item.checked ? 'is-checked' : ''}`} aria-hidden="true">{item.checked ? '✓' : ''}</span> : <Icon name={item.icon ?? 'file'} />}
        <span>{item.label}</span>{item.shortcut && <kbd>{item.shortcut}</kbd>}{item.children && <span className="pcg-menu__arrow">›</span>}
      </button>
      {item.children && expanded === item.label && <MenuLevel items={item.children} label={item.label} nested onSelect={onSelect} onDismiss={onDismiss} onBack={() => {
        setExpanded(null);
        const buttons = root.current?.querySelectorAll<HTMLButtonElement>(':scope > [role=none] > button');
        buttons?.[items.indexOf(item)]?.focus();
      }} />}
    </div>)}
  </div>;
}
export function EditorMenu({ label, children, items, className = '' }: { label: string; children: ReactNode; items: MenuItem[]; className?: string }) {
  const [open, setOpen] = useState(false);
  const root = useRef<HTMLDivElement>(null);
  const trigger = useRef<HTMLButtonElement>(null);
  useEffect(() => {
    if (!open) return;
    const outside = (event: PointerEvent) => { if (!root.current?.contains(event.target as Node)) setOpen(false); };
    const blur = () => setOpen(false);
    document.addEventListener('pointerdown', outside);
    window.addEventListener('blur', blur);
    return () => { document.removeEventListener('pointerdown', outside); window.removeEventListener('blur', blur); };
  }, [open]);
  const close = () => { setOpen(false); trigger.current?.focus(); };
  return <div className={`pcg-menu ${className}`} ref={root} onBlur={(event) => { if (!event.currentTarget.contains(event.relatedTarget)) setOpen(false); }}>
    <button ref={trigger} type="button" className="pcg-menu__trigger" aria-label={label} aria-haspopup="menu" aria-expanded={open}
      onClick={() => setOpen(!open)} onKeyDown={(event) => { if (event.key === 'ArrowDown') { event.preventDefault(); setOpen(true); } }}>
      {children}
    </button>
    {open && <MenuLevel label={label} items={items} onBack={close} onDismiss={close} onSelect={(item) => { close(); item.onSelect?.(); }} />}
  </div>;
}
export function Brand() {
  return <div className="pcg-brand"><img src={`${import.meta.env.BASE_URL}picg-logo.png`} alt="PICG logo" /><span>PICG</span></div>;
}
