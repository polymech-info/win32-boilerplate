export type SkillEntry = {
  name: string;
  description?: string;
  source?: "roaming" | "workspace" | string;
  available?: boolean;
  active?: boolean;
  always?: boolean;
  pinned?: boolean;
  disabled?: boolean;
  missing_requirements?: string[] | string;
  path?: string;
};

export type SkillsSettings = {
  enabled: boolean;
  roaming_enabled: boolean;
  workspace_enabled: boolean;
  pinned: string[];
  disabled: string[];
  roots?: { roaming?: string; workspace?: string };
  skills?: SkillEntry[];
};

export function normalizeSkillName(v: string): string {
  return v.trim().toLowerCase();
}
