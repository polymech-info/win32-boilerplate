import * as Checkbox from "@radix-ui/react-checkbox";
import * as Switch from "@radix-ui/react-switch";
import { Check } from "lucide-react";

type ToolsSettings = {
  disabled_path_tools: string[];
  mcp_tools_enabled: boolean;
  disabled_mcp_servers: string[];
};

type Props = {
  tools: ToolsSettings;
  allToolIds: readonly string[];
  onToggleMcpToolsEnabled: (value: boolean) => void;
  onDisabledMcpServersChange: (value: string) => void;
  onTogglePathTool: (toolId: string, enabled: boolean) => void;
};

export function ToolsSettingsPanel({
  tools,
  allToolIds,
  onToggleMcpToolsEnabled,
  onDisabledMcpServersChange,
  onTogglePathTool,
}: Props) {
  return (
    <div className="pm-settings-wrap">
      <section className="pm-provider-card pm-tools-card">
        <h2>Tools</h2>
        <div className="pm-skill-switch-row">
          <span>MCP tools enabled (master)</span>
          <Switch.Root
            checked={tools.mcp_tools_enabled}
            className="pm-radix-switch-root"
            onCheckedChange={onToggleMcpToolsEnabled}
          >
            <Switch.Thumb className="pm-radix-switch-thumb" />
          </Switch.Root>
        </div>
        <div className="pm-row">
          <label>Disabled MCP servers (comma-separated)</label>
          <input
            value={tools.disabled_mcp_servers.join(", ")}
            onChange={(e) => onDisabledMcpServersChange(e.target.value)}
          />
        </div>
        <div className="pm-row">
          <label>Path tools (enabled)</label>
          <div className="pm-tools-grid">
            {allToolIds.map((id) => {
              const enabled = !tools.disabled_path_tools.includes(id);
              return (
                <label key={id} className="pm-radix-check">
                  <Checkbox.Root
                    checked={enabled}
                    className="pm-radix-check-root"
                    onCheckedChange={(next) => onTogglePathTool(id, next === true)}
                  >
                    <Checkbox.Indicator className="pm-radix-check-indicator">
                      <Check size={13} />
                    </Checkbox.Indicator>
                  </Checkbox.Root>
                  <span>{id}</span>
                </label>
              );
            })}
          </div>
        </div>
      </section>
    </div>
  );
}
