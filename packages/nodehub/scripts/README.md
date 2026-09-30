# Build Scripts

Build scripts for the imgui-node-editor project.

## Build simple-example only

### Windows (PowerShell) - Recommended
```powershell
.\scripts\build.ps1          # Debug build
.\scripts\build.ps1 Release  # Release build
```

### Windows (Command Prompt)
```batch
scripts\build.bat            # Debug build
scripts\build.bat Release    # Release build
```

### Git Bash / WSL / Linux
```bash
./scripts/build.sh           # Debug build
./scripts/build.sh Release   # Release build
```

## Build all projects
```bash
cmake --build build
```

## Executables
- **Debug**: `build/bin/simple-example_d.exe`
- **Release**: `build/bin/simple-example.exe`

