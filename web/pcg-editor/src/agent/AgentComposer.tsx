// AgentComposer.tsx — Message composer: attachment chips, auto-grow textarea,
// paperclip / drag / paste attachment entries, Enter to send, Shift+Enter for
// newline, send switches to stop while a request is in flight.

import { useCallback, useEffect, useRef, useState, type ChangeEvent, type ClipboardEvent, type DragEvent, type KeyboardEvent } from 'react';

import type { ProviderDescriptor, ReasoningEffort } from './agentClient';

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
  providers?: ProviderDescriptor[];
  providerId?: string;
  modelId?: string;
  reasoningEffort?: ReasoningEffort;
  onModelChange?: (providerId: string, modelId: string) => void;
  onReasoningEffortChange?: (effort: ReasoningEffort) => void;
  onSend: (text: string, attachments: AgentAttachment[]) => void;
  onStop: () => void;
}

export default function AgentComposer({
  sending, agentLabel, disabled = false, providers = [], providerId = '', modelId = '', reasoningEffort = 'high',
  onModelChange, onReasoningEffortChange, onSend, onStop,
}: AgentComposerProps) {
  const [text, setText] = useState('');
  const [attachments, setAttachments] = useState<AgentAttachment[]>([]);
  const [rejectNote, setRejectNote] = useState('');
  const [showModels, setShowModels] = useState(false);
  const [showThinkingOptions, setShowThinkingOptions] = useState(false);
  const textareaRef = useRef<HTMLTextAreaElement>(null);
  const fileInputRef = useRef<HTMLInputElement>(null);
  const selectedModel = providers.find((provider) => provider.id === providerId)?.models.find((model) => model.id === modelId);
  const reasoningEfforts = selectedModel?.capabilities.reasoningEfforts ?? [];

  useEffect(() => {
    if (disabled || sending) {
      setShowModels(false);
      setShowThinkingOptions(false);
    }
  }, [disabled, sending]);

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
    if (e.nativeEvent.isComposing || e.nativeEvent.keyCode === 229) return;
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
          <button type="button" className="pcg-agent-composer__pill" title={agentLabel} onClick={() => {
            setShowThinkingOptions(false);
            setShowModels((value) => !value);
          }}>
            ∞ {agentLabel}
          </button>
          {selectedModel?.capabilities.reasoning && (
            <button
              type="button"
              className="pcg-agent-composer__thinking-pill"
              title="Thinking settings"
              onClick={() => {
                setShowModels(false);
                setShowThinkingOptions((value) => !value);
              }}
            >
              Thinking · {reasoningEfforts.length > 0
                ? reasoningEffort.charAt(0).toUpperCase() + reasoningEffort.slice(1)
                : 'On'}⌄
            </button>
          )}
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
        {showThinkingOptions && selectedModel?.capabilities.reasoning && (
          <div className="pcg-agent-composer__thinking-menu" role="dialog" aria-label="Thinking settings">
            <div className="pcg-agent-composer__thinking-heading">Options</div>
            <div className="pcg-agent-composer__thinking-row" title="Thinking is always enabled for this model">
              <span>Thinking</span>
              <span className="pcg-agent-composer__thinking-switch is-on" role="switch" aria-label="Thinking enabled" aria-checked="true" aria-disabled="true">
                <span />
              </span>
            </div>
            {reasoningEfforts.length > 0 ? (
              <div className="pcg-agent-composer__thinking-section">
                <div className="pcg-agent-composer__thinking-heading">Effort</div>
                {reasoningEfforts.map((effort) => (
                  <button
                    type="button"
                    key={effort}
                    className={effort === reasoningEffort ? 'is-selected' : ''}
                    aria-label={`Set thinking effort to ${effort}`}
                    onClick={() => {
                      onReasoningEffortChange?.(effort);
                      setShowThinkingOptions(false);
                    }}
                  >
                    <span>{effort}</span>{effort === reasoningEffort && <span aria-hidden="true">✓</span>}
                  </button>
                ))}
                <small>Changing effort resets Kimi's context cache.</small>
              </div>
            ) : (
              <div className="pcg-agent-composer__thinking-fixed">
                <span>Effort</span>
                <small>{selectedModel.name} always uses its built-in thinking mode.</small>
              </div>
            )}
          </div>
        )}
        {showModels && providers.length > 0 && (
          <div className="pcg-agent-composer__model-menu">
            {providers.flatMap((provider) => provider.models.map((model) => (
              <button
                type="button"
                key={`${provider.id}/${model.id}`}
                className={provider.id === providerId && model.id === modelId ? 'is-selected' : ''}
                onClick={() => { onModelChange?.(provider.id, model.id); setShowModels(false); }}
              >
                <span>{model.name}</span><small>{provider.name}{model.capabilities.reasoning ? ' · Thinking' : ''}</small>
              </button>
            )))}
          </div>
        )}
      </div>
    </div>
  );
}
