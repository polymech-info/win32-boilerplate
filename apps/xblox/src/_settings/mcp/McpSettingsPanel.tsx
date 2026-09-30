import * as Switch from "@radix-ui/react-switch";

type McpServerForm = {
  id: string;
  enabled: boolean;
  name: string;
  type: string;
  command: string;
  args: string;
  url: string;
  headers: string;
  env: string;
  tool_timeout: string;
  enabled_tools: string;
};

type Props = {
  busy: boolean;
  mcpJsonPath: string;
  mcpServers: McpServerForm[];
  mcpPingResult: string;
  labels: {
    addServer: string;
    ping: string;
    removeServer: string;
    path: string;
    pingResult: string;
    enabled: string;
    serverName: string;
    type: string;
    typeAuto: string;
    typeStdio: string;
    typeSse: string;
    typeStreamableHttp: string;
    command: string;
    args: string;
    url: string;
    headers: string;
    env: string;
    toolTimeout: string;
    enabledTools: string;
  };
  isExpanded: (id: string) => boolean;
  onToggleExpanded: (id: string) => void;
  onAddServer: () => void;
  onRemoveServer: (id: string) => void;
  onPing: () => void;
  onServerEnabled: (id: string, enabled: boolean) => void;
  onServerField: (id: string, key: keyof McpServerForm, value: string) => void;
};

export function McpSettingsPanel({
  busy,
  mcpJsonPath,
  mcpServers,
  mcpPingResult,
  labels,
  isExpanded,
  onToggleExpanded,
  onAddServer,
  onRemoveServer,
  onPing,
  onServerEnabled,
  onServerField,
}: Props) {
  return (
    <div className="pm-settings-wrap">
      <section className="pm-provider-card">
        <div className="pm-row">
          <label>{labels.path}</label>
          <input value={mcpJsonPath} readOnly />
        </div>
        <div className="pm-presets-actions">
          <button className="btn" type="button" onClick={onAddServer}>
            {labels.addServer}
          </button>
          <button className="btn" type="button" onClick={onPing} disabled={busy}>
            {labels.ping}
          </button>
        </div>
      </section>
      <div className="pm-provider-list">
        {mcpServers.map((s) => (
          <section key={s.id} className="pm-provider-card">
            <div className="pm-mcp-head">
              <button className="pm-mcp-toggle" type="button" onClick={() => onToggleExpanded(s.id)}>
                <span>{isExpanded(s.id) ? "▾" : "▸"}</span>
                <strong>{s.name || "MCP Server"}</strong>
                <span className={`pm-mcp-state ${s.enabled ? "on" : "off"}`}>{s.enabled ? "ON" : "OFF"}</span>
              </button>
              <button className="btn" type="button" onClick={() => onRemoveServer(s.id)}>
                {labels.removeServer}
              </button>
            </div>
            {isExpanded(s.id) ? (
              <>
                <div className="pm-row">
                  <label>{labels.enabled}</label>
                  <div className="pm-mcp-switch-slot">
                    <Switch.Root
                      checked={s.enabled}
                      className="pm-radix-switch-root"
                      onCheckedChange={(v) => onServerEnabled(s.id, v)}
                    >
                      <Switch.Thumb className="pm-radix-switch-thumb" />
                    </Switch.Root>
                  </div>
                </div>
                <div className="pm-row">
                  <label>{labels.serverName}</label>
                  <input value={s.name} onChange={(e) => onServerField(s.id, "name", e.target.value)} />
                </div>
                <div className="pm-row">
                  <label>{labels.type}</label>
                  <select className="pm-select" value={s.type} onChange={(e) => onServerField(s.id, "type", e.target.value)}>
                    <option value="">{labels.typeAuto}</option>
                    <option value="stdio">{labels.typeStdio}</option>
                    <option value="sse">{labels.typeSse}</option>
                    <option value="streamableHttp">{labels.typeStreamableHttp}</option>
                  </select>
                </div>
                <div className="pm-row">
                  <label>{labels.command}</label>
                  <input value={s.command} onChange={(e) => onServerField(s.id, "command", e.target.value)} />
                </div>
                <div className="pm-row">
                  <label>{labels.args}</label>
                  <input value={s.args} onChange={(e) => onServerField(s.id, "args", e.target.value)} />
                </div>
                <div className="pm-row">
                  <label>{labels.url}</label>
                  <input value={s.url} onChange={(e) => onServerField(s.id, "url", e.target.value)} />
                </div>
                <div className="pm-row">
                  <label>{labels.headers}</label>
                  <input value={s.headers} onChange={(e) => onServerField(s.id, "headers", e.target.value)} />
                </div>
                <div className="pm-row">
                  <label>{labels.env}</label>
                  <input value={s.env} onChange={(e) => onServerField(s.id, "env", e.target.value)} />
                </div>
                <div className="pm-row">
                  <label>{labels.toolTimeout}</label>
                  <input value={s.tool_timeout} onChange={(e) => onServerField(s.id, "tool_timeout", e.target.value)} />
                </div>
                <div className="pm-row">
                  <label>{labels.enabledTools}</label>
                  <input value={s.enabled_tools} onChange={(e) => onServerField(s.id, "enabled_tools", e.target.value)} />
                </div>
              </>
            ) : null}
          </section>
        ))}
      </div>
      {mcpPingResult ? (
        <section className="pm-provider-card">
          <div className="pm-row">
            <label>{labels.pingResult}</label>
            <textarea value={mcpPingResult} readOnly rows={12} />
          </div>
        </section>
      ) : null}
    </div>
  );
}
