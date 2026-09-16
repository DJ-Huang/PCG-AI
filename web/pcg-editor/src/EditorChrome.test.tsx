import { cleanup, fireEvent, render, screen, within } from '@testing-library/react';
import { afterEach, describe, expect, it, vi } from 'vitest';
import { EditorMenu } from './EditorChrome';

afterEach(cleanup);

function setup() {
  const save = vi.fn();
  const toggle = vi.fn();
  render(<EditorMenu label="Main menu" items={[
    { label: 'File', children: [{ label: 'Save', onSelect: save }, { label: 'Reveal', disabled: true }] },
    { label: 'Windows', children: [{ label: 'Preview', checked: true, onSelect: toggle }] },
  ]}>Menu</EditorMenu>);
  return { save, toggle, trigger: screen.getByRole('button', { name: 'Main menu' }) };
}

describe('workspace main menu', () => {
  it('opens a submenu by hover then click without closing it', () => {
    const { save, trigger } = setup();
    fireEvent.click(trigger);
    const file = screen.getByRole('menuitem', { name: /File/ });
    fireEvent.mouseEnter(file.parentElement!);
    fireEvent.click(file);
    fireEvent.click(screen.getByRole('menuitem', { name: 'Save' }));
    expect(save).toHaveBeenCalledOnce();
    expect(screen.queryByRole('menu')).not.toBeInTheDocument();
    expect(trigger).toHaveFocus();
  });
  it('navigates into and back out of nested menus using the keyboard', () => {
    const { trigger } = setup();
    fireEvent.keyDown(trigger, { key: 'ArrowDown' });
    const file = screen.getByRole('menuitem', { name: /File/ });
    expect(file).toHaveFocus();
    fireEvent.keyDown(file, { key: 'ArrowRight' });
    const save = screen.getByRole('menuitem', { name: 'Save' });
    expect(save).toHaveFocus();
    fireEvent.keyDown(save, { key: 'ArrowDown' });
    expect(save).toHaveFocus(); // Disabled entries are skipped.
    fireEvent.keyDown(save, { key: 'ArrowLeft' });
    expect(file).toHaveFocus();
    fireEvent.keyDown(file, { key: 'Escape' });
    expect(trigger).toHaveFocus();
    expect(trigger).toHaveAttribute('aria-expanded', 'false');
  });
  it('exposes panel check states and dispatches the selected action', () => {
    const { toggle, trigger } = setup();
    fireEvent.click(trigger);
    fireEvent.click(screen.getByRole('menuitem', { name: /Windows/ }));
    const submenu = screen.getByRole('menu', { name: 'Windows' });
    const preview = within(submenu).getByRole('menuitemcheckbox', { name: 'Preview' });
    expect(preview).toHaveAttribute('aria-checked', 'true');
    fireEvent.click(preview);
    expect(toggle).toHaveBeenCalledOnce();
  });
  it('dismisses on an outside pointer event', () => {
    const { trigger } = setup();
    fireEvent.click(trigger);
    fireEvent.pointerDown(document.body);
    expect(trigger).toHaveAttribute('aria-expanded', 'false');
  });
});
