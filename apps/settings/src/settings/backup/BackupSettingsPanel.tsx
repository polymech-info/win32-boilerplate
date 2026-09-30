type BackupLabels = {
  title: string;
  secSettingsFile: string;
  descSettingsFile: string;
  lblEncryptExport: string;
  btnExportSettings: string;
  btnImportSettings: string;
  secClipboard: string;
  descClipboard: string;
  btnCopyToClipboard: string;
  btnPasteFromClipboard: string;
  secProfileArchive: string;
  descProfileArchive: string;
  btnExportArchive: string;
  btnImportArchive: string;
};

type Props = {
  busy: boolean;
  encryptExport: boolean;
  labels: BackupLabels;
  onEncryptExportChange: (value: boolean) => void;
  onExportSettings: () => void;
  onImportSettings: () => void;
  onCopyToClipboard: () => void;
  onPasteFromClipboard: () => void;
  onExportArchive: () => void;
  onImportArchive: () => void;
};

export function BackupSettingsPanel({
  busy,
  encryptExport,
  labels,
  onEncryptExportChange,
  onExportSettings,
  onImportSettings,
  onCopyToClipboard,
  onPasteFromClipboard,
  onExportArchive,
  onImportArchive,
}: Props) {
  return (
    <div className="pm-settings-wrap">
      <section className="pm-provider-card pm-backup-card">
        <h2>{labels.title}</h2>

        <div className="pm-backup-section">
          <h3>{labels.secSettingsFile}</h3>
          <p className="pm-backup-desc">{labels.descSettingsFile}</p>
          <label className="pm-check-row">
            <input type="checkbox" checked={encryptExport} onChange={(e) => onEncryptExportChange(e.target.checked)} />
            <span>{labels.lblEncryptExport}</span>
          </label>
          <div className="pm-backup-actions">
            <button className="btn" type="button" onClick={onExportSettings} disabled={busy}>
              {labels.btnExportSettings}
            </button>
            <button className="btn" type="button" onClick={onImportSettings} disabled={busy}>
              {labels.btnImportSettings}
            </button>
          </div>
        </div>

        <div className="pm-backup-section">
          <h3>{labels.secProfileArchive}</h3>
          <p className="pm-backup-desc">{labels.descProfileArchive}</p>
          <div className="pm-backup-actions">
            <button className="btn" type="button" onClick={onExportArchive} disabled={busy}>
              {labels.btnExportArchive}
            </button>
            <button className="btn" type="button" onClick={onImportArchive} disabled={busy}>
              {labels.btnImportArchive}
            </button>
          </div>
        </div>

        <div className="pm-backup-section">
          <h3>{labels.secClipboard}</h3>
          <p className="pm-backup-desc">{labels.descClipboard}</p>
          <div className="pm-backup-actions">
            <button className="btn" type="button" onClick={onCopyToClipboard} disabled={busy}>
              {labels.btnCopyToClipboard}
            </button>
            <button className="btn" type="button" onClick={onPasteFromClipboard} disabled={busy}>
              {labels.btnPasteFromClipboard}
            </button>
          </div>
        </div>
      </section>
    </div>
  );
}
