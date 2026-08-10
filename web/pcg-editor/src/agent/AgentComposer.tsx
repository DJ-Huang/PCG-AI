// AgentComposer.tsx — Message composer: attachment chips, auto-grow textarea,
// paperclip / drag / paste attachment entries, Enter to send, Shift+Enter for
// newline, send switches to stop while a request is in flight.

import { useCallback, useRef, useState, type ChangeEvent, type ClipboardEvent, type DragEvent, type KeyboardEvent } from 'react';

export interface AgentAttachment {
  id: string;
  file: File;
  previewUrl: string | null;
}

const ACCEPT_EXTENSIONS = ['.png', '.jpg', '.jpeg', '.txt', '.json', '.pcg'];
const MAX_FILE_BYTES = 10 * 1024 * 1024;

function isAccepted(file: File): boolean {
  const lower = file.name.toLowerCase();
  return ACCEPT_EXTENSIONS.some((ext) => lower.endsWith(ext));
}

interface AgentComposerProps {
  sending: boolean;
  agentLabel: string;
  disabled?: boolean;
  onSend: (text: string, attachments: AgentAttachment[]) => void;
  onStop: () => void;
}

export default function AgentComposer({ sending, agentLabel, disabled = false, onSend, onStop }: AgentComposerProps) {
  const [text, setText] = useState('');
  const [attachments, setAttachments] = useState<AgentAttachment[]>([]);
  const [rejectNote, setRejectNote] = useState('');
  const textareaRef = useRef<HTMLTextAreaElement>(null);
  const fileInputRef = useRef<HTMLInputElement>(null);

  const autoGrow = useCallback(() => {
    const el = textareaRef.current;
    if (!el) return;
    el.style.height = 'auto';
    el.style.height = `${Math.min(el.scrollHeight, 160)}px`;
    el.style.overflowY = el.scrollHeight > 160 ? 'auto' : 'hidden';
  }, []);

  const addFiles = useCallback((files: Iterable<File>) => {
    const accepted: AgentAttachment[] = [];
    let rejected = 0;
    for (const file of files) {
      if (!isAccepted(file) || file.size > MAX_FILE_BYTES) {
        rejected += 1;
        continue;
      }
      accepted.push({
        id: `${Date.now()}-${Math.random().toString(36).slice(2, 8)}`,
        file,
        previewUrl: file.type.startsWith('image/') ? URL.createObjectURL(file) : null,
      });
    }
    if (accepted.length > 0) {
      setAttachments((prev) => [...prev, ...accepted]);
    }
    setRejectNote(
      rejected > 0 ? `${rejected} file(s) skipped (allowed: ${ACCEPT_EXTENSIONS.join(' ')}, ≤10MB)` : '',
    );
  }, []);

  const removeAttachment = useCallback((id: string) => {
    setAttachments((prev) => {
      const target = prev.find((a) => a.id === id);
      if (target?.previewUrl) URL.revokeObjectURL(target.previewUrl);
      return prev.filter((a) => a.id !== id);
    });
  }, []);

  const handleSend = useCallback(() => {
    const trimmed = text.trim();
    if (!trimmed && attachments.length === 0) return;
    if (sending || disabled) return;
    onSend(trimmed, attachments);
    setText('');
    setAttachments((prev) => {
      prev.forEach((a) => a.previewUrl && URL.revokeObjectURL(a.previewUrl));
      return [];
    });
    setRejectNote('');
    requestAnimationFrame(autoGrow);
  }, [text, attachments, sending, disabled, onSend, autoGrow]);

  const onKeyDown = (e: KeyboardEvent<HTMLTextAreaElement>) => {
    if (e.key === 'Enter' && !e.shiftKey) {
      e.preventDefault();
      handleSend();
    }
  };

  const onDrop = (e: DragEvent) => {
    e.preventDefault();
    addFiles(e.dataTransfer.files);
  };

  const onPaste = (e: ClipboardEvent) => {
    if (e.clipboardData.files.length > 0) {
      e.preventDefault();
      addFiles(e.clipboardData.files);
    }
  };

  const onFilePick = (e: ChangeEvent<HTMLInputElement>) => {
    if (e.target.files) addFiles(e.target.files);
    e.target.value = '';
  };

  return (
    <div className="pcg-agent-composer" onDrop={onDrop} onDragOver={(e) => e.preventDefault()}>
      <div className="pcg-agent-composer__card">
        {attachments.length > 0 && (
          <div className="pcg-agent-composer__chips">
            {attachments.map((a) => (
              <span key={a.id} className="pcg-agent-composer__chip" title={a.file.name}>
                {a.previewUrl ? (
                  <img src={a.previewUrl} alt="" className="pcg-agent-composer__thumb" />
                ) : (
                  <span className="pcg-agent-composer__file-icon">📄</span>
                )}
                <span className="pcg-agent-composer__chip-name">{a.file.name}</span>
                <button
                  type="button"
                  className="pcg-agent-composer__chip-remove"
                  onClick={() => removeAttachment(a.id)}
                  title="Remove attachment"
                >
                  ✕
                </button>
              </span>
            ))}
          </div>
        )}
        <textarea
          ref={textareaRef}
          className="pcg-agent-composer__textarea"
          placeholder="Plan, build, @ nodes, attach refs…"
          value={text}
          rows={1}
          onChange={(e) => {
            setText(e.target.value);
            autoGrow();
          }}
          onKeyDown={onKeyDown}
          onPaste={onPaste}
        />
        {rejectNote && <div className="pcg-agent-composer__reject">{rejectNote}</div>}
        <div className="pcg-agent-composer__buttons">
          <span className="pcg-agent-composer__pill" title={agentLabel}>
            ∞ {agentLabel}
          </span>
          <span className="pcg-agent-composer__spacer" />
          <input
            ref={fileInputRef}
            type="file"
            multiple
            accept={ACCEPT_EXTENSIONS.join(',')}
            style={{ display: 'none' }}
            onChange={onFilePick}
          />
          <button
            type="button"
            className="pcg-agent-composer__attach"
            onClick={() => fileInputRef.current?.click()}
            title={`Attach files (${ACCEPT_EXTENSIONS.join(' ')})`}
          >
            📎
          </button>
          {sending ? (
            <button type="button" className="pcg-agent-composer__send" onClick={onStop} title="Stop">
              ■
            </button>
          ) : (
            <button
              type="button"
              className="pcg-agent-composer__send"
              onClick={handleSend}
              disabled={disabled || (!text.trim() && attachments.length === 0)}
              title="Send (Enter)"
            >
              ↑
            </button>
          )}
        </div>
      </div>
    </div>
  );
}
