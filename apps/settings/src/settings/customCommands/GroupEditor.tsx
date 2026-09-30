import type { CommandVariableOption, CustomCommandGroup } from "./types";
import { EditorSection, FieldRow } from "./FormControls";
import { VariableInputField } from "./VariableBuilder";

export function GroupEditor({
  group,
  groupIndex,
  variableOptions,
  onChange,
  onRemove,
  onAddItem,
}: {
  group: CustomCommandGroup;
  groupIndex: number;
  variableOptions?: CommandVariableOption[];
  onChange: (patch: Partial<CustomCommandGroup>) => void;
  onRemove: () => void;
  onAddItem: (kind: "button" | "dropdown" | "separator") => void;
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
      </div>
    </div>
  );
}
