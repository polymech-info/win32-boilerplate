# FreeCAD Installation via Scoop

## Quick Installation

### Option 1: PowerShell (Recommended)

Run in PowerShell as Administrator:

```powershell
# Install Scoop if not already installed
iwr -useb get.scoop.sh | iex

# Update Scoop
scoop update

# Install FreeCAD
scoop install freecad
```

### Option 2: Command Prompt (CMD)

Run in Command Prompt as Administrator:

```cmd
powershell -NoProfile -ExecutionPolicy Bypass -Command "iwr -useb get.scoop.sh | iex"
scoop update
scoop install freecad
```

## Verification

After installation completes, verify FreeCAD is installed:

```powershell
freecad --version
```

Or simply launch it:

```powershell
freecad
```

## Troubleshooting

### Scoop installation fails
- Ensure you're running PowerShell as Administrator
- Check internet connectivity
- Try: `Set-ExecutionPolicy -ExecutionPolicy RemoteSigned -Scope CurrentUser`

### FreeCAD installation fails
- Update Scoop: `scoop update`
- Check available buckets: `scoop bucket list`
- If needed, add the 'main' bucket: `scoop bucket add main`

### Can't find freecad command
- Restart your terminal/PowerShell session
- Verify installation path: `scoop which freecad`

## Additional Scoop Commands

```powershell
# List installed packages
scoop list

# Check for updates
scoop status

# Update FreeCAD specifically
scoop update freecad

# Uninstall FreeCAD
scoop uninstall freecad
```

## More Information

- Scoop documentation: https://scoop.sh
- FreeCAD website: https://www.freecadweb.org/
