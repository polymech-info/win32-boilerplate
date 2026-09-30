VC++ 2015-2022 x64 redistributable (optional NSIS bundle)
---------------------------------------------------------
Place VC_redist.x64.exe here to have the installer run it silently
(/install /quiet /norestart) so system-wide CRT matches the app.

Download (or run from repo root):
  powershell -ExecutionPolicy Bypass -File scripts/fetch-vc-redist.ps1

Direct link: https://aka.ms/vs/17/release/vc_redist.x64.exe

Licensing: use only the official Microsoft redistributable; see Microsoft
documentation for VC++ redistribution terms.

If this file is absent, the NSIS build still works; that step is skipped.
The app may still run from app-local msvcp140/vcruntime140 copies in dist/.
