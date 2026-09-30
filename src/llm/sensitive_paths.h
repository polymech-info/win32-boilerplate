#pragma once

// Single deny list for media::llm::llm_fs_guard_deny_reason (see llm_fs_guard.cpp).
// Paths are normalized to forward slashes; on Windows the path and these patterns are
// lowercased for comparison. Rows that must match macOS disk casing use mixed case;
// Windows-only subtrees use lowercase ASCII so they match after normalization.

static const char* SENSITIVE_PATH_PREFIXES[] = {
    // ===== UNIX system paths =====
    "/etc/",
    "/etc/shadow",
    "/etc/passwd",
    "/etc/sudoers",
    "/etc/ssh/",
    "/root/",
    "/var/lib/",
    "/var/run/secrets/",
    "/proc/",
    "/sys/",

    // ===== Generic secrets mounts =====
    "/run/secrets/",
    "/mnt/secrets/",
    "/secrets/",

    // ===== Portable user / app secrets (any prefix: /home/…, /Users/…, /export/home/…, any drive on Windows) =====
    "**/.ssh/**",
    "**/.gnupg/**",
    "**/.config/gcloud/**",
    "**/.aws/**",
    "**/.kube/**",
    "**/.docker/**",
    "**/.local/share/keyrings/**",

    // macOS user Library (under any home root; case as on disk — Unix paths are not lowercased)
    "**/Library/Keychains/**",
    "**/Library/Application Support/CloudDocs/**",
    "**/Library/Application Support/com.apple.TCC/**",
    "**/Library/Containers/**",
    "**/.zsh_history",
    "**/.bash_history",

    // ===== Windows portable (lowercase; matches any drive letter) =====
    "**/windows/system32/config/",
    "**/windows/system32/drivers/etc/hosts",
    "**/users/*/appdata/roaming/microsoft/credentials/**",
    "**/users/*/appdata/local/microsoft/credentials/**",
    "**/users/*/appdata/roaming/microsoft/protect/**",
    "**/users/*/appdata/roaming/microsoft/crypto/**",
    "**/users/*/appdata/roaming/microsoft/vault/**",
    "**/users/*/ntuser.dat",
    "**/Google/Chrome/**",
    "**/Mozilla/Firefox/**",

    // ===== Project-level (see media::path_matches_path_glob) =====
    "**/.git/**",
    "**/.env*",
    "**/node_modules/**",

    // ===== Sensitive basenames anywhere (merged former SENSITIVE_FILE_PATTERNS) =====
    "**/*.pem",
    "**/*.key",
    "**/*.p12",
    "**/*.pfx",
    "**/*.crt",
    "**/*.der",
    "**/*.csr",
    "**/*.env",
    "**/id_rsa",
    "**/id_dsa",
    "**/id_ecdsa",
    "**/id_ed25519",
    "**/known_hosts",
    "**/authorized_keys",
    "**/wallet.dat",
    "**/*.kdbx",
    "**/*.sqlite",
    "**/*.sqlite3",
    "**/*.sqlite-wal",
    "**/*.sqlite-shm",
    "**/*.db-shm",
    "**/*.db-wal",
    "**/*.db",
    "**/*.mdb",
    "**/*.accdb",
    "**/*.duckdb",
    "**/*.realm",
    "**/*.fdb",
    "**/*.sql",
    "**/*.dump",
    "**/*.backup",
    "**/*.bak",
    "**/*.ndjson",
    "**/*.parquet",
    "**/*.orc",
    "**/*.avro",
    "**/*.bson",
    "**/cookies.sqlite",
    "**/Login Data",
    "**/Web Data",
    "**/NTUSER.DAT",
    "**/config.json",
    "**/credentials.json",
    "**/secrets.json",

    // Typical source languages (.ts, .py, .sh, …) are allowed for file_glob / file_read so
    // agents can inspect repos. Binary / DB / secrets patterns above still apply.

    // common dev seed / migration / fixture filenames
    "**/seed.sql",
    "**/seeds.sql",
    "**/seed.json",
    "**/seeds.json",
    "**/seed.yaml",
    "**/seed.yml",
    "**/seeds.yaml",
    "**/seeds.yml",
    "**/seed.ts",
    "**/seed.js",
    "**/seed.mjs",
    "**/seed.cjs",
    "**/seeds.ts",
    "**/seeds.js",
    "**/fixtures.sql",
    "**/.seed.sql",

    nullptr,
};
