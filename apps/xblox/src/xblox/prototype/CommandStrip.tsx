import type { CustomCommandItem } from "@pm/shared/customCommands/types";

import { commandActionLabel } from "./sample-data";

type CommandStripProps = {
  commands: CustomCommandItem[];
};

export function CommandStrip({ commands }: CommandStripProps) {
  return (
    <section className="xblox-command-strip" aria-label="Custom commands">
      {commands.slice(0, 5).map((command) => (
        <article key={command.id ?? command.label} className="xblox-command-card">
          <strong>{command.label ?? command.id}</strong>
          <span>{command.id}</span>
          <small>{commandActionLabel(command)}</small>
        </article>
      ))}
    </section>
  );
}
