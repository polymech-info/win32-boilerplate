#pragma once

// Write-target policy for LLM path tools (`write_file`, image tool output paths).
// Image / video / audio extensions are allowlisted elsewhere in llm_fs_guard.cpp;
// this table blocks scripts, shells, executables, PDFs, Office / zip-based docs,
// archives, and similar types that must not be produced by tool calls.

static const char* LLM_WRITE_BLOCKED_EXTENSIONS[] = {
    // shells & scripts
    ".bat", ".cmd", ".ps1", ".psm1", ".psd1",
    ".sh", ".bash", ".zsh", ".fish", ".ksh", ".csh", ".tcsh",
    ".py", ".pyw", ".pl", ".pm", ".rb", ".lua", ".php",
    ".js", ".jse", ".mjs", ".cjs",
    ".ts", ".tsx", ".mts", ".cts", ".jsx", ".vue", ".svelte",
    ".rs", ".go", ".java", ".class", ".cs", ".fs", ".fsx",
    ".gradle", ".groovy", ".scala", ".kt", ".kts", ".swift",
    ".r", ".R", ".dart", ".nim", ".ex", ".exs",
    ".tcl", ".awk", ".hta", ".msc", ".reg", ".inf",
    ".vbs", ".vbe", ".wsf", ".wsh",
    // Windows executables & installers
    ".exe", ".dll", ".msi", ".scr", ".com", ".pif", ".cpl", ".hta", ".msc",
    ".reg", ".lnk", ".url", ".scf", ".inf", ".application",
    // JVM / packages
    ".jar", ".war", ".ear",
    // OS packages
    ".deb", ".rpm", ".dmg", ".pkg", ".app",
    // documents & zip-based office
    ".pdf",
    ".doc", ".docx", ".dot", ".dotx", ".docm", ".dotm",
    ".xls", ".xlsx", ".xlsm", ".xlsb", ".xltx", ".xltm",
    ".ppt", ".pptx", ".pptm", ".potx", ".potm", ".pps", ".ppsx",
    ".odt", ".ods", ".odp", ".odg", ".odf",
    // archives & disk images
    ".zip", ".7z", ".rar", ".cab", ".tar", ".lz4",
    ".tgz", ".tbz2", ".txz",
    ".iso", ".img", ".vhd", ".vhdx", ".wim",
    // databases & data dumps (often credentials or bulk PII)
    ".sqlite", ".sqlite3", ".db", ".mdb", ".accdb",
    ".duckdb", ".realm", ".fdb",
    ".sql", ".dump", ".backup", ".bak",
    ".ndjson", ".parquet", ".orc", ".avro", ".bson",
    // other risky / opaque binary carriers
    ".wasm", ".apk", ".ipa", ".msix", ".appx",
    nullptr,
};

// Absolute normalized paths (forward slashes; Windows lowercased) that must not
// receive new files from LLM tools. Drive letter C: is retargeted to match the
// path under test (same rule as sensitive_paths.h).
static const char* LLM_WRITE_BLOCKED_ABSOLUTE_PREFIXES[] = {
#if defined(_WIN32)
    "C:\\Windows\\",
    "C:\\Program Files\\",
    "C:\\Program Files (x86)\\",
    "C:\\ProgramData\\",
#endif
    "/bin/",
    "/sbin/",
    "/boot/",
    "/lib/",
    "/lib64/",
    "/usr/bin/",
    "/usr/sbin/",
    "/etc/",
    "/root/",
    nullptr,
};
