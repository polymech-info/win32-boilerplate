import type { ChatPresetSnapshot, CommandVariableOption, CustomCommandGroup } from "./types";
import { EditorSection, FieldRow } from "./FormControls";
import { VariableInputField } from "./VariableBuilder";

export function GroupEditor({
  group,
  groupIndex,
  chatPresets,
  variableOptions,
  onChange,
  onRemove,
  onAddItem,
  onAddCurrentChat,
  onAddSavedChat,
}: {
  group: CustomCommandGroup;
  groupIndex: number;
  chatPresets: ChatPresetSnapshot[];
  variableOptions?: CommandVariableOption[];
  onChange: (patch: Partial<CustomCommandGroup>) => void;
  onRemove: () => void;
  onAddItem: (kind: "button" | "dropdown" | "separator") => void;
  onAddCurrentChat: () => void;
  onAddSavedChat: (presetId: string) => void;
}) {
  return (
    <div className="pm-custom-command-editor flush">
      <div className="pm-mcp-head">
        <strong>Group {groupIndex + 1}: {group.label || "Group"}</strong>
        <button className="btn" type="button" onClick={onRemove}>Remove group</button>
      </div>
      <EditorSection title="General">
        <FieldRow label="Group label">
          <VariableInputField value={group.label || ""} variables={variableOptions} onChange={(label) => onChange({ label })} />
        </FieldRow>
        <FieldRow label="Group ID">
          <VariableInputField value={group.id || ""} variables={variableOptions} onChange={(id) => onChange({ id })} />
        </FieldRow>
      </EditorSection>
      <div className="pm-custom-actions">
        <button className="btn" type="button" onClick={() => onAddItem("button")}>Add button</button>
        <button className="btn" type="button" onClick={() => onAddItem("dropdown")}>Add dropdown</button>
        <button className="btn" type="button" onClick={() => onAddItem("separator")}>Add separator</button>
        <button className="btn" type="button" onClick={onAddCurrentChat}>Add current chat</button>
        {chatPresets.length ? (
          <select className="pm-select" defaultValue="" onChange={(e) => {
            onAddSavedChat(e.target.value);
            e.currentTarget.value = "";
          }}>
            <option value="">Add saved chat preset...</option>
            {chatPresets.map((p) => <option key={p.id} value={p.id}>{p.name}</option>)}
          </select>
        ) : null}
      </div>
    </div>
  );
}
