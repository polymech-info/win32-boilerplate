import type React from "react";
import { Checkbox } from "@/components/ui/checkbox";
import { Select, SelectContent, SelectItem, SelectTrigger, SelectValue } from "@/components/ui/select";

export function FieldRow({ label, children, wide }: { label: string; children: React.ReactNode; wide?: boolean }) {
  return (
    <div className={`pm-custom-field ${wide ? "wide" : ""}`}>
      <label>{label}</label>
      <div>{children}</div>
    </div>
  );
}

export function CheckRow({
  label,
  checked,
  onChange,
}: {
  label: string;
  checked: boolean;
  onChange: (checked: boolean) => void;
}) {
  return (
    <div className="pm-custom-field pm-custom-check-field">
      <label>{label}</label>
      <Checkbox checked={checked} onCheckedChange={(value) => onChange(value === true)} />
    </div>
  );
}

export function SelectField({
  value,
  options,
  onChange,
  placeholder = "Select...",
}: {
  value: string;
  options: Array<{ value: string; label: string; disabled?: boolean }>;
  onChange: (value: string) => void;
  placeholder?: string;
}) {
  return (
    <Select value={value} onValueChange={onChange}>
      <SelectTrigger className="pm-radix-select-trigger">
        <SelectValue placeholder={placeholder} />
      </SelectTrigger>
      <SelectContent className="pm-radix-select-content">
        {options.map((option) => (
          <SelectItem key={option.value} value={option.value} disabled={option.disabled}>
            {option.label}
          </SelectItem>
        ))}
      </SelectContent>
    </Select>
  );
}

export function EditorSection({
  title,
  description,
  className,
  children,
}: {
  title: string;
  description?: string;
  className?: string;
  children: React.ReactNode;
}) {
  return (
    <section className={`pm-custom-section ${className || ""}`}>
      <div className="pm-custom-section-head">
        <h4>{title}</h4>
        {description ? <p>{description}</p> : null}
      </div>
      <div className="pm-custom-grid">{children}</div>
    </section>
  );
}
