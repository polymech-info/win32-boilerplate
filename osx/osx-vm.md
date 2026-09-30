# macOS guest on Windows 10 + VMware (dev VM)

This document captures a **practical** setup for a **host Windows 10** machine with **VMware** and a local toolkit under **`D:\osx`**, aligned with the links and commands noted in [osx.md](osx.md) (Catalina/VMware, QEMU, OpenCore, recovery).

**Licensing:** Apple’s EULA for macOS restricts where it may be run. Ensure your use case (e.g. internal build/test only, Apple Developer Program, or hardware you own) complies with your agreements before proceeding.

**Scope:** This is **x86_64 macOS in VMware** (Catalina matches your `macOS Catalina 10.15.7_19H15.iso`). The **`arm-64bit.7z`** and **`andr-9.0-r2-VB-64bit.7z`** files are not used for a classic Intel + VMware + Catalina flow (they are typically for other products such as Android/x86 or ARM images); you can keep them elsewhere and ignore them here.

---

## 0. What you already have in `D:\osx`

| File | Role in this runbook |
|------|----------------------|
| `macOS Catalina 10.15.7_19H15.iso` (~10.5 GB) | Boot/install macOS 10.15.7 in the new VM. |
| `darwin.iso` (~3.2 MB) | Optional if you use **OC4VM’s** `iso/darwin.iso` (recommended to match the release) — see §4. |
| `qemu-w64-setup-20260422.exe` | Optional if QEMU is **already** installed; otherwise use to install. |
| `tahoe_icloud.plist.xml` | Handy reference for **newer** macOS-in-VM (iCloud/Apple ID quirks); for Catalina, treat as optional. Original context: [jensd.be — Tahoe iCloud plist](https://jensd.be/download) (per [osx.md](osx.md) notes). |
| `macOS … .torrent` | Optional way to **fetch** the full `.iso` if you are still downloading — see [§2.0](#20-installer-isos--torrents-pyenb). |

### 2.0 Installer ISOs & torrents (pyenb)

The directory **[`https://data.pyenb.network/macOS/isos/torrents/`](https://data.pyenb.network/macOS/isos/torrents/)** indexes **.torrent** files for many full-disk installer **ISO** names (e.g. [macOS Catalina 10.15.7_19H15](https://data.pyenb.network/macOS/isos/torrents/macOS%20Catalina%2010.15.7%5F19H15.iso.torrent), [macOS Big Sur 11.7.10](https://data.pyenb.network/macOS/isos/torrents/macOS%20Big%20Sur%2011.7.10%5F20G1427.iso.torrent), Monterey, Ventura, Sonoma, Sequoia, and older OS X versions). That matches filenames like the Catalina image already under `D:\osx\`.

**Newest on that index (as of the listing):** **[macOS Sequoia 15.2_24C101](https://data.pyenb.network/macOS/isos/torrents/macOS%20Sequoia%2015.2%5F24C101.iso.torrent)** — it is the highest **Sequoia** build and the latest “Last modified” date in the directory. **Sequoia 15.2_24C5089C_Beta** is a **beta**; use it only if you want preview bits, not “newest stable.” The index does not list **Tahoe**; for OC4VM’s current take on **Tahoe** see the [OC4VM readme](https://github.com/DrDonk/OC4VM#2-functionality) (limited / not recommended for daily use).

**Caveats:** Third-party distribution of installer images is **not** Apple; only keep images you are entitled to use, and **verify** integrity (size, **sha** from a trusted source when available). For **OC4VM**-tested guests, **Sequoia** and **Big Sur+** are on the [official test list](https://github.com/DrDonk/OC4VM#2-functionality) — see [§3](#3-create-the-macos-vm-in-dosx-oc4vm-method) below.

---

**Install location for the new VM machine:** under **`D:\osx`** (e.g. a **copy of the OC4VM *release* `vmware/Intel` or `vmware/AMD` template**), not the **git** `vmware\` file alone — see [§2.1](#21-git-clone-dosxoc4vm-vs-official-releases).

**Your clone:** [OC4VM](https://github.com/DrDonk/OC4VM) is also at **`D:\osx\OC4VM`** (git). That tree holds **source** and **unprocessed** VMX templates; use a **[release](https://github.com/DrDonk/oc4vm/releases)** to run a VM, or see below.

**Your release (2.5.1):** The official **binary** package is unpacked at **`D:\osx\oc4vm-2.5.1`** (if the zip used a longer folder name, e.g. `oc4vm-2.5.1-…` from the download, that folder is the same idea — use whichever directory contains the **`vmware\Intel`**, **`vmware\AMD`**, **`iso`**, **`tools`**, etc. tree). This release is the one to use for **§2.2**/**§3**; **`darwin.iso`** for §4 is at **`D:\osx\oc4vm-2.5.1\iso\`** (adjust the prefix if your folder name differs).

**QEMU:** Use it for **disk conversion** (see §6), not to replace VMware if the goal is “Win10 + **VMware** + macOS”.

---

## 1. Host checks (before VMware)

- **BIOS/UEFI:** **Intel VT-x** enabled; **VT-d** optional.
- **CPU (OC4VM):** The host should expose **AVX, AVX2, F16C, RDRAND** — [OC4VM’s readme](https://github.com/DrDonk/OC4VM#2-functionality) states these are required. Plan for **AMD vs Intel** template (next section).
- **Disk / RAM:** Leave at least **~50 GB** free for the guest disk (more for Xcode later); **8–16 GB** host RAM with **4–6 GB** assigned to the macOS guest is a reasonable starting point.
- **Hyper-V:** If you use WSL2/Hyper-V heavily, it can conflict with VMware; prefer one stack per boot, or [VMware / Broadcom notes](https://knowledge.broadcom.com/) on co-existence where supported.

---

## 2. VMware + [OC4VM (OpenCore for VMware)](https://github.com/DrDonk/OC4VM)

Use **OC4VM** as the **default** way to run macOS in **VMware Workstation** on Windows. It ships **OpenCore** and **ready-made VM templates**; details are in the [repository readme](https://github.com/DrDonk/OC4VM#readme) and **[Wiki](https://github.com/DrDonk/OC4VM/wiki)**.

**Important (from upstream):** OC4VM is **also a replacement for the old “Unlocker”** — it **does *not* require patching VMware** on the host. You still install a normal, supported **VMware Workstation** build (upstream testing mentions **17.6** and **25H2**-era Workstation; pick a [release](https://github.com/DrDonk/oc4vm/releases) that matches your VMware version).

### 2.1 Git clone `D:\osx\OC4VM` vs official **releases**

- **`D:\osx\OC4VM`** ( **`git clone`** ) — use this to read the **readme**, [Wiki](https://github.com/DrDonk/OC4VM/wiki), **`tools\host\windows\`**, and **development** `vmware\` sources. The checked-in `vmware\macos.vmx` is a **templated** build file (Jinja / `{{…}}` placeholders) and is **not** the same as the **prebuilt** VM that ships in a **release** zip. Do **not** point VMware at that raw `macos.vmx` expecting a full working guest unless you have run the project’s build on **macOS** (see *“Building OC4VM”* in the [upstream readme](https://github.com/DrDonk/OC4VM#4-building-oc4vm)).
- **Binary release (recommended to install macOS the first time)** — from **[OC4VM releases](https://github.com/DrDonk/oc4vm/releases)**, download the **zip** for your **VMware** version, verify **sha512** if you use checksums, and **unzip** to a versioned path. **This machine:** **OC4VM 2.5.1** lives at **`D:\osx\oc4vm-2.5.1`**. That package contains the finished **`vmware/Intel` and `vmware/AMD`** trees (`opencore.iso`, **`macos.vmdk`**, `macos.nvram`, **`macos.vmx`**, etc.) described in the [“VMware Templates”](https://github.com/DrDonk/OC4VM#33-vmware-templates) section.

**Summary:** keep **`D:\osx\OC4VM`** for source **and** use **`D:\osx\oc4vm-2.5.1`** (release **2.5.1**) for the **runnable** template; copy **`Intel` or `AMD`** from **`D:\osx\oc4vm-2.5.1\vmware\…`** into e.g. **`D:\osx\macos-guest`** per §3, *or* open **`macos.vmx`** in place if you prefer not to copy.

### 2.2 What is in the release (from upstream)

| Subfolder | Use |
|-----------|-----|
| **`vmware/`** | **Start here** — **Intel** and **AMD** template VMs (pick the folder that matches the **host** CPU). |
| `config/` | Reference `config.plist` files for OpenCore. |
| `disks/` | OpenCore boot **DMG/VMDK** variants. |
| `iso/` | **VMware macOS guest tools** ( **`darwin.iso`** and related images). |
| `packages/` | Post-install packages. |
| `tools/` | OC4VM **host and guest** helpers (`tools help` in upstream docs). |

### 2.3 Template VM layout (per `vmware/Intel` or `vmware/AMD`)

Each template folder includes (names from [OC4VM readme](https://github.com/DrDonk/OC4VM#33-vmware-templates)):

| File | Role |
|------|------|
| `opencore.iso` | OpenCore **boot** ISO. |
| `macos.nvram` | Preconfigured NVRAM. |
| `macos.vmx` | VMware **VMX** (open this in Workstation). |
| `macos.vmdk` | Pre-formatted **APFS** data disk. |
| `macos.plist` | Fusion-style plist (useful mainly on **Fusion**). |

---

## 3. Create the macOS VM in `D:\osx` (OC4VM method)

1. Use the **2.5.1** release at **`D:\osx\oc4vm-2.5.1`** (not the bare git `D:\osx\OC4VM\vmware\` source template). **Copy the whole** **`Intel` or `AMD` template** folder from **`D:\osx\oc4vm-2.5.1\vmware\`** into **`D:\osx\`** and rename as needed, e.g. **`D:\osx\macos-catalina`** (keep **all** template files **together** — **`opencore.iso`**, **`macos.vmdk`**, `macos.nvram`, `macos.vmx`, …). *Alternatively,* open **`macos.vmx`** **directly** from **`D:\osx\oc4vm-2.5.1\vmware\Intel`** or **`…\AMD`** without copying (then your **`vmrun`** path points at that `macos.vmx`).
2. In **VMware Workstation** → **Open a Virtual Machine** → select **`macos.vmx`** (e.g. **`D:\osx\macos-catalina\macos.vmx`** or **`D:\osx\oc4vm-2.5.1\vmware\Intel\macos.vmx`** if you did not copy).
3. **Add installation media:** in VM **Settings**, attach your installer — for your machine, the **SATA/IDE/SCSI CD/DVD** device to **`D:\osx\macOS Catalina 10.15.7_19H15.iso`**, or a **.iso / .vmdk** the release expects for your macOS version (see OC4VM Wiki if the installer is not seen at boot).
4. **Power on** the VM, pick **OpenCore** / installer entry as shown in the [Wiki](https://github.com/DrDonk/OC4VM/wiki) if prompted, and **install macOS** onto the **template disk** the normal way.
5. Note the **`.vmx` path** for `vmrun` in §7.

**Catalina vs. OC4VM’s “tested” list:** The [OC4VM readme “Functionality”](https://github.com/DrDonk/OC4VM#2-functionality) lists guests tested from **Big Sur** through **Tahoe** — it does **not** spell out **Catalina 10.15**. If the **Catalina** installer fails to boot or install with a current template, use the [Wiki / discussions](https://github.com/DrDonk/OC4VM/discussions) for that combo or install a **supported** macOS (Big Sur+) and treat Catalina as a fallback you verify experimentally.

**Troubleshooting a stuck Apple logo** remains: OpenCore `config.plist`, NVRAM, recovery/SIP, and the exact **OC4VM** version — follow the **Wiki** and release notes, not ad-hoc unlocker steps.

**Optional:** [kiraio-moe / macOS-on-VMWare](https://github.com/kiraio-moe/macOS-on-VMWare) can add screenshots for a similar flow; **OC4VM** remains authoritative for **templates and tools**.

**`macrecovery` / PATH:** Only needed if you **download** a macOS installer from Apple’s servers. If you use only your local **`.iso`**, you can skip that.

### 3.1 Sequoia ISO: attach it on the **CD/DVD** device (common mistake)

The OC4VM template often leaves the **SATA CD/DVD** on **`auto detect`** with **Start connected = off** until you set it. In **VM** → **Settings** (VM **powered off**):

1. **CD/DVD (SATA)** → use **ISO image file** and browse to your finished download, e.g. **`D:\osx\macOS Sequoia 15.2_24C101.iso`** (name as on disk).
2. Enable **Connect at power on** and ensure the device is **Connected**. In **`macos.vmx`** this must be **`sata0:1.startConnected = "TRUE"`**. If it is **`FALSE`**, the log often shows **`DISKUTIL: sata0:1 : capacity=0`** and OpenCore may only list **OpenShell** — no **Sequoia** / **EFI Boot** entry from the ISO.

After that, a **`vmware.log`** should show a **non-zero** capacity for **`sata0:1`** when the image is valid, and **`sata0:1.fileName`** pointing at your **`.iso`**, not **`auto detect`**.

**This machine:** guest folder **`D:\osx\sequoia-vm`** and **`$vmPath = "D:\osx\sequoia-vm\macos.vmx"`** for `vmrun` after the guest is set up.

### 3.2 Decoding `vmware.log` (Workstation 17 + Sequoia + OC4VM)

| Log line / symptom | Meaning |
|--------------------|--------|
| `Cannot open …\AppData\Roaming\VMware\config.ini` | **Harmless** on first run; VMware uses defaults. |
| `GetFileAttributesExW("…\macos.vmpl") … error: 2` | **Usually harmless** — policy / lock helper file not present. |
| `OpenSSL config file` / `SSLConfigLoad` warnings | **Noise** on some installs; not usually fatal. |
| `About to do EFI boot: OpenCore` | **Good** — the OC4VM / OpenCore path is running. |
| `ToolsISO: Guest 'darwin24-64' not found in … isoimages_manifest.txt` | **Expected** on **Workstation 17.5.x**: the host does **not** ship a **darwin24** (Sequoia) entry for the **built-in** Tools CD. **Ignore** “Install VMware Tools” from the Workstation **VM** menu for this guest until **after** macOS is installed; then use **OC4VM’s** `darwin.iso` (§4) or the path inside the guest. |
| `sata0:1.fileName = "auto detect"` after you thought you set the ISO | **Fix in UI** (§3.1); power off, set CD/DVD to the **.iso** file, connect at power on. |
| `sata0:1.startConnected = "FALSE"` or **`DISKUTIL: sata0:1 : capacity=0`** | ISO path is set but **not connected** at boot — set **`sata0:1.startConnected = "TRUE"`** in **`macos.vmx`** or tick **Connect at power on** in Settings. Otherwise OpenCore often shows only **OpenShell**. |

---

## 4. `darwin.iso` — VMware guest tools (OC4VM provides them)

[OC4VM’s readme §3.4](https://github.com/DrDonk/OC4VM#34-vmware-macos-guest-tools) documents official macOS **guest** tools on the **OC4VM** boot path:

- **Inside the guest (after install):** mount the tools from  
  **`/Volumes/OPENCORE/OC4VM/iso/darwin.iso`**  
  and run the **VMware Tools** installer, then **reboot**.
- **From the release (host):** **`D:\osx\oc4vm-2.5.1\iso\darwin.iso`** (same 2.5.1 tree). Point the **VM CD/DVD** at that file instead of the small **`D:\osx\darwin.iso`** you had previously — **prefer the `darwin.iso` that matches your OC4VM/Workstation generation** so the installer matches the guest’s macOS/VMware expectations.

1. Eject the **Catalina** (or other) **installer** ISO if it is still the CD/DVD device after first boot to desktop.
2. Mount **darwin** from **OC4VM** as above, install tools, **reboot**.
3. If a tool build mismatches, check **the same** OC4VM **release** `iso/` tree or the [DrDonk OC4VM discussions](https://github.com/DrDonk/OC4VM/discussions) for the pair **(Workstation major version, guest macOS version)**.

### 4.1 Shared folders: host user profile (or repo) in the guest

**Prereqs:** **VMware Tools** (§4) is installed, guest rebooted. On the **Windows** host, in **VM** → **Settings** → **Options** → **Shared Folders**, set **Enabled** and **Add** a share (e.g. your Windows **user profile** `C:\Users\YourName` or the repo root).

- **In the guest** you do **not** `mount` `/mnt/hgfs` like Linux. VMware tools expose the share as a **volume** under:  
  **`/Volumes/VMware Shared Folders/<ShareName>`**  
  where **`<ShareName>`** is the **Name** you gave the folder in the VMware **Add Shared Folder** dialog (e.g. `UserProfile` or `polymech`), not the long Windows path.
- **In Terminal (paths contain spaces — quote them):**
  ```bash
  ls "/Volumes/VMware Shared Folders"
  cd "/Volumes/VMware Shared Folders/UserProfile"
  ```
- **In Finder** the volume often appears in the **sidebar** under “Locations” / **“VMware Shared Folders”**; open the share you named, then your usual `Desktop`, `Documents`, etc. under your profile.
- **Convenience:** create a short link in your guest home, e.g. `ln -s "/Volumes/VMware Shared Folders/UserProfile" ~/win-home`
- If **`/Volumes/VMware Shared Folders`** is **empty** or **missing:** confirm the VM option is **Enabled**, the host path still exists, **reboot the guest** after editing shares, and confirm the **VMware Tools** service is running. On some Sequoia-era guests, **System Settings → Privacy & Security → Full Disk Access** may be needed for VMware-related helpers if a share is blocked.

---

## 5. Recovery, SIP, and `tahoe_icloud` (optional / newer macOS)

These matter more for **Tahoe / Sequoia** and **latest** “Mac in a VM on PC” content (see the video/notes in [osx.md](osx.md)). For **Catalina 10.15.7** day-to-day, you can skip this until something breaks (e.g. kext, tools, or cloud login).

- **SIP (System Integrity Protection):** You may need **`CsrUtil.efi` status|disable …`**-style changes **temporarily** for a lab VM — follow **[OC4VM Wiki](https://github.com/DrDonk/OC4VM/wiki)** and release notes, not random plist edits; wrong flags can make the guest unbootable.
- **Liquid glasS / Sequoia UI (host-style fixes for guests):** Example from [osx.md](osx.md) notes:  
  `defaults write -g com.apple.SwiftUI.DisableSolarium -bool YES`  
  (Catalina does not use this the same way; it is for very new UI stacks.)
- **`tahoe_icloud.plist`:** If you **later** install a **newer** guest and need **Apple ID / iCloud** sign-in, compare this plist with the OpenCore/EFI recipe from [jensd.be](https://jensd.be) / your [osx.md](osx.md) “Updated config” link. Keep a **config.plist backup** before changes. For **App Store** login failures, see [§5.1](#51-app-store-apple-id-and-smbios-sequoia--oc4vm) below (VMware + OC4VM).

### 5.1 App Store, Apple ID, and SMBIOS (Sequoia / OC4VM)

**Symptom:** **App Store** or **Apple ID** sign-in fails (generic error, or server rejects the session). Apple’s services expect **Mac-like** hardware identifiers. On a default OC4VM guest, **System Information** may show a **Model Identifier** such as `VMware20,1`, a **VM-style serial**, and a **PC-style** `board-id` in I/O Registry — patterns Apple’s back end often associates with **non-Apple** or **virtual** hardware.

**Verify in the guest** (e.g. over SSH; paths assume a booted system):

```bash
system_profiler SPHardwareDataType | /usr/bin/grep -E 'Model|Serial'
# Optional: I/O Registry details (read-only; helps compare before/after SMBIOS changes)
# ioreg -rd1 -c IOPlatformExpertDevice
```

A **laptop** SMBIOS in `.vmx` (below) is sometimes paired with **“Pass power status to VM”** in VMware so battery/power state is plausible for that model; see *Secondary*.

#### Primary fix: `macos.vmx` (VMware SMBIOS)

1. **Generate one consistent identity** (serial, **MLB** “board serial”, **ROM** as 6 bytes, and **UUID** as needed) for a real Mac model that is sensible for your **guest macOS** version, e.g. [**MacBookPro16,1**](https://dortania.github.io/OpenCore-Install-Guide/extras/smbios-support.html) for many **x86_64** + recent macOS cases. A common tool is [**GenSMBIOS**](https://github.com/corpnewt/GenSMBIOS).
2. **Power the VM all the way off** (not suspend) before editing, and close Workstation or ensure the `.vmx` is not locked. Edit **`macos.vmx`** in a plain-text editor; keep straight ASCII quotes.
3. Add or set keys so VMware **does not** mirror the host, and so **model + board-id + serial** are internally consistent. Example pattern (replace placeholders with your **GenSMBIOS** output; **board-id** must **match** the **hw.model** you choose; this pair is for **MacBookPro16,1**):

```text
smbios.reflectHost = "FALSE"
board-id.reflectHost = "FALSE"
board-id = "Mac-E1008331FDC96864"
hw.model = "MacBookPro16,1"
hw.model.reflectHost = "FALSE"
serialNumber.reflectHost = "FALSE"
serialNumber = "GENERATED_SYSTEM_SERIAL"
efi.nvram.var.ROM.reflectHost = "FALSE"
efi.nvram.var.ROM = "AABBCCDDEEFF"
efi.nvram.var.MLB.reflectHost = "FALSE"
efi.nvram.var.MLB = "GENERATED_MLB"
```

The OC4VM template may already include `smbios.restrictSerialCharset = "TRUE"`, which limits which characters you can use in the serial; keep the generated value compatible with that. **Check Coverage:** before relying on a serial, look it up on [Apple Check Coverage](https://checkcoverage.apple.com). A result of **“Invalid”** (not tied to a real device) is often acceptable for a **private lab VM** so you do not collide with someone else’s machine.

**This repo’s Sequoia layout:** guest VM folder **`D:\osx\sequoia-vm`** and **`macos.vmx`** there (same `vmPath` / `vmrun` story as in [§3.1](#31-sequoia-iso-attach-it-on-the-cddvd-device-common-mistake)).

#### OC4VM caveat: OpenCore on `opencore.iso`

OC4VM boots from **`opencore.iso`**. The template’s **`opencore.vmdk`** is a small **descriptor** that maps to that **ISO** (raw flat image, not a separate APFS “EFI disk” you edit in the running guest in the same way as a physical Mac’s EFI). **OpenCore** reads **`EFI/OC/config.plist`**; **`PlatformInfo` → `Generic`** can **override** what VMware’s `.vmx` supplies. If, after a full **cold** boot, **About This Mac** / `system_profiler` still shows **`VMware20,1`**, you must align **OpenCore** with the same GenSMBIOS profile: **`SystemProductName`**, **`SystemSerialNumber`**, **`SystemUUID`**, **`MLB`**, and **`ROM`**. That means **extracting** the **ISO** (or rebuilding it), editing **`config.plist`**, and replacing **opencore** media **while the VM is off** — the **host** locks `opencore.iso` while the guest runs. The **APFS** volume’s **EFI** partition is often **empty** in an OC4VM install; the boot-time EFI **content** is on the **opencore** image. Cross-check the sample **`D:\osx\tahoe_icloud.plist.xml`**, the “Updated `config.plist`” notes in [jensd.be](https://jensd.be) / [osx.md](osx.md), and the [OC4VM Wiki](https://github.com/DrDonk/OC4VM/wiki).

<a id="sequoia-smbios-workflow"></a>
#### 5.1.1 Sequoia (`sequoia-vm`) — full SMBIOS workflow on Windows

The following is the **end-to-end** flow used for **`D:\osx\sequoia-vm`**: one **coherent** GenSMBIOS profile, OpenCore and VMware aligned, **Apple ID** / **iServices**-friendly NVRAM, then verification.

1. **Pick one line** (model + **serial + MLB + UUID + “ROM” as MAC**). A tab-separated file is in this repo: **`packages/media/cpp/osx/serials.txt`** (also mirrored on the dev host as **`D:\osx\serials.txt`**). One row looks like: **`MacBookPro16,1`**, system serial, **MLB**, **`SystemUUID`**, and **MAC** (6 pairs = same bytes you put in `ROM` for OpenCore). **Do not** mix columns from different rows.
2. **Back up** `D:\osx\sequoia-vm\opencore.iso` (and a copy of **`EFI/OC/config.plist`**) before edits.
3. **Extract the ISO** with 7-Zip to a work tree (e.g. `opencore-extract/OPENCORE/…`). The OpenCore `config` lives at **`OPENCORE/EFI/OC/config.plist`**.
4. **Edit `config.plist` (OpenCore) — must match the same row as `.vmx`:**  
   - **`PlatformInfo` → `Generic`:** set **`SystemProductName`**, **`SystemSerialNumber`**, **`MLB`**, **`SystemUUID`**. Set **`ROM`** to **`<data>`** with the **Base64** of the **6 MAC bytes** (from GenSMBIOS, not random hex).  
   - **`PlatformInfo`:** set **`UpdateSMBIOS`**, **`UpdateNVRAM`**, and **`UpdateDataHub`** to **`true`** (helps NVRAM and services see the new identity).  
   - **`Kernel` → `Quirks` → `CustomSMBIOSGuid`:** leave **`false`** for a **model + serial + iServices**-oriented profile that avoids host-side **Hardware UUID** tricks; set **`true`** *only* if you also do step **6b** and want **System Information → Hardware UUID** to match the **`SystemUUID`** (see [Dortania: iServices](https://dortania.github.io/OpenCore-Post-Install/universal/iservices.html)). **`CustomSMBIOSGuid` +** raw `opencore.iso` splicing + VMware UUID edits has been a source of **fragility**; prefer **`false`** until everything else works.  
5. **Repack `opencore.iso` (Windows / 7-Zip):** You cannot `7z u` this hybrid APM + HFS+ image (“Not implemented”). **Preferred — OpenCore `PlatformInfo` is what removes `VMware20,1` and applies your serial:** on **macOS** (or Linux with HFS+ read-write, not typical WSL1/WSL2), run **`bash packages/media/cpp/osx/repack-opencore-hfs.sh`** with **`--stock`** = **unmodified** **`…/oc4vm-2.5.1/…/AMD/opencore.iso`**, **`--plist`** = edited `opencore-extract/OPENCORE/EFI/OC/config.plist`, **`--out`** = final `opencore.iso`. The script builds the new image **in a local temp first**, then `cp`’s to `--out` — **do not** route `dd` output directly to a path on the **VMware shared folder (HGFS)**; that has produced **corrupt** ISOs and the **“CPU has been disabled by the guest operating system”** message. **In-guest (no other Mac):** put **`stock-opencore.iso`**, **`config.plist`**, and the two `repack-*.sh` scripts under a folder on the host share (e.g. **`C:\Users\zx\sequoia-repack`**) and run **`repack-in-guest.sh`** in Terminal; it writes **`opencore-repacked.iso`** on the share — **shut the VM down**, then on Windows **replace** `D:\…\sequoia-vm\opencore.iso` with that file. **If you have no way to run mount repack:** the **splice** script **`D:\osx\sequoia-vm\patch_hfs_plist.py`** rewrites a fixed on-disk `config.plist` in the HFS+ slice; it is **risky** (unclean volume / HLT). **Before any repack,** power the VM off. Re-discover offset/length if you upgrade **OC4VM**.  
6. **Edit `macos.vmx` (VMware) — same profile:**  
   - **(a) SMBIOS (use with any OpenCore profile):** `smbios.reflectHost` / `board-id` / `hw.model` / `serialNumber` / **MLB** / **ROM** ( **`efi.nvram.var.ROM`** = **12 hex characters**, no colons, same 6 bytes as the MAC) — match **OpenCore** and **`serials.txt`**.  
   - **(b) Hardware UUID (optional, only with `CustomSMBIOSGuid` = `true` in `config.plist`):** set **`system-id.enable`** to **`FALSE`**, and set **`uuid.bios`** + **`uuid.location`** from your **`SystemUUID`** in VMware’s 16-byte layout (two groups of 8 space-separated byte pairs, one hyphen, e.g. `683E213B-EE3F-4F2A-8513-4C51CCA6A6BC` → `68 3e 21 3b ee 3f 4f 2a-85 13 4c 51 cc a6 a6 bc`). If you are **not** chasing Hardware UUID, keep **`system-id.enable = "TRUE"`** and the default **`uuid.bios` / `uuid.location`** from the template. **Keep a `.vmx` backup.**
7. **Optional: reset host NVRAM file** — the VM’s **`.vmx`** line **`nvram = "macos.nvram"`** persists UEFI state. If **Hardware UUID** in System Information is **stale** after **step 6b**, **shut the VM down**, **rename** `sequoia-vm\macos.nvram` to e.g. `macos.nvram.bak.<date>` (do **not** delete until you have a booted guest), then power on (VMware will create a **new** `macos.nvram`). To roll back, copy the **`.bak`** back to **`macos.nvram`**. At the OpenCore picker, use **Reset NVRAM** (often under **Space** for hidden entries) if it appears, **one time**, after a profile change. For **SMBIOS-only** (step 6a, no 6b), you usually do **not** need this.  
8. **Verify in the guest** (SSH or Terminal): use **`D:\osx\verify.sh`**, or:  
   `system_profiler SPHardwareDataType | grep -E 'Model|Serial'`, and check **Model Identifier** = your SMBIOS, **not** `VMware20,1`, and **Serial Number (system)** = your chosen serial. **Hardware UUID** will often **still** show the VMware default until you do **4 + 6b** with **`CustomSMBIOSGuid` = `true`**.  
9. **From Windows over SSH (optional):**  
   `Get-Content "D:\osx\verify.sh" -Raw | ssh osx "bash -s"` (requires **`ssh osx`** from [§7.1](#71-passwordless-ssh-from-the-windows-host-optional) and a shell that can pipe UTF-8; the script is stored with **LF** line endings for `bash`).

#### Secondary checks

| Item | What to do |
|------|------------|
| **Pass power status to VM** | VMware: **Edit virtual machine settings** → **Options** (or **Advanced** depending on product) and enable when using a **notebook**-style `hw.model`. |
| **Date & time** | Guest: enable **set automatically**; template often has `tools.syncTime = "TRUE"`. |
| **Security keys (FIDO2 / YubiKey)** on the **Apple ID** | May not work through a VM; sign in with a **method that does not require** the key, or add an **app-specific password** for that Mac if offered. |
| **NAT vs bridged** | If sign-in still fails, try **Bridged** for the vNIC in VMware (or set `ethernet0.connectionType = "bridged"` in `.vmx`) so the guest has a different network path. |
| **Sequoia 15.x** | Community threads note stricter / changed behavior; see [dockur/macos#107](https://github.com/dockur/macos/issues/107) (Linux/KVM-oriented but sometimes relevant) and the [community / troubleshooting links](#smbios-and-app-store-references) at the end of this subsection. |

#### SMBIOS and App Store references

- [r/macOSVMs — Apple ID login on Sequoia VM in VMware](https://www.reddit.com/r/macOSVMs/comments/1q9hymw/apple_id_login_fails_on_macos_157_sequoia_vm_in/)
- [r/hackintosh — Apple ID / iServices on non-Apple / VM setups](https://www.reddit.com/r/hackintosh/comments/1gew74e/hackintosh_osx_apple_id_login_does_not_work_an/)
- [BlueBubbles — SMBIOS / VMware and `board-id` notes](https://docs.bluebubbles.app/server/advanced/macos-virtualization/running-a-macos-vm/enabling-imessage-in-a-vm)
- [Dortania — choosing SMBIOS / supported models](https://dortania.github.io/OpenCore-Install-Guide/extras/smbios-support.html)
- [setapp.com — cannot connect to App Store (troubleshooting article)](https://setapp.com/how-to/mac-cannot-connect-to-app-store)
- [Apple — If you can’t connect to the App Store](https://support.apple.com/108093)

---

## 6. QEMU: convert images (when the installer is a `.dmg`, not an `.iso`)

**You are using a `.iso` in VMware, so you may not need this.** If a guide gives you a **`.dmg`** to attach as a second disk, convert to **VMDK** and add the disk in VMware (power off the VM first).

Example (adjust paths, from [osx.md](osx.md)):

```bat
"%ProgramFiles%\qemu\qemu-img.exe" convert -O vmdk -o compat6 "D:\osx\installer.dmg" "D:\osx\tahoe-install.vmdk"
```

- Put **`macrecovery.exe`** and related **PATH** items only if you use Apple’s `macrecovery` flow; see [QEMU w64](https://qemu.weilnetz.de/w64/) and your [osx.md](osx.md) notes.

---

## 7. `vmrun` scripts (same idea as `manage-ubuntu-vm.*`)

After the VM exists, you can start/stop it from the **host** the same way as your Ubuntu sample: **`vmrun.exe`**, the **Workstation/Player** mode flag (`-T ws` or `-T player`), and the path to the **`.vmx`**.

1. **Copy** `D:\osx\manage-ubuntu-vm.ps1` → e.g. **`D:\osx\manage-macos-vm.ps1`**.
2. **Set** the variable to the **`.vmx`** (OC4VM template is often literally **`macos.vmx`**), for example:  
   `$vmPath = "D:\osx\macos-catalina\macos.vmx"`  
   or, if you run from the release folder without copying:  
   `$vmPath = "D:\osx\oc4vm-2.5.1\vmware\Intel\macos.vmx"` (use **`AMD`** instead of **`Intel`** when that matches your host).
3. **Optional:** copy the **`.bat` wrapper** and point it to `manage-macos-vm.ps1`.

**Commands (reference):**

```text
# Start with UI
"C:\Program Files\...\vmrun.exe" -T ws start "D:\osx\...\name.vmx" gui

# Start headless
"C:\Program Files\...\vmrun.exe" -T ws start "D:\osx\...\name.vmx" nogui

# Graceful stop
"C:\Program Files\...\vmrun.exe" -T ws stop "D:\osx\...\name.vmx" soft

# List
"C:\Program Files\...\vmrun.exe" list
```

### 7.1 Passwordless **SSH** from the Windows host (optional)

To log into the **macOS guest** (or a Linux VM) with **keys** instead of a password, use [ssh-key-auth.md](ssh-key-auth.md) and [setup-ssh-key.ps1](setup-ssh-key.ps1) (push a key from Windows) or [import-ssh-key-from-macos.ps1](import-ssh-key-from-macos.ps1) (pull a key you created in the guest). In the **guest**, enable **Remote Login** (macOS) or `sshd` (Linux). For a short command like **`ssh osx`**, add **`-AddHostAlias osx`** to the import script or edit **`%USERPROFILE%\.ssh\config`** as in [ssh-key-auth.md](ssh-key-auth.md) §2.2.

---

## 8. For `pm-image` on macOS (this repo)

Once the **guest** is running, follow [osx.md](osx.md) to install **Command Line Tools** / **Homebrew**, **libvips**, and build **`packages/media/cpp`** inside the VM like on a physical Mac. Match **Xcode/CLT** and **Homebrew** to the **installed** guest (e.g. **Sequoia 15.2** or **Catalina 10.15.7**); older guests need older toolchains.

---

## 9. Quick checklist

- [ ] [VMware Workstation](https://support.broadcom.com/) version compatible with **OC4VM 2.5.1** / [release notes](https://github.com/DrDonk/oc4vm/releases) (no **Unlocker** patch on the host — **OC4VM** replaces that model).
- [ ] **OC4VM 2.5.1** at **`D:\osx\oc4vm-2.5.1`**: from **`vmware\Intel` or `vmware\AMD`**, either **copy** the template to **`D:\osx\…`** or **open** `macos.vmx` in place, then **`macos.vmx`** in Workstation.
- [ ] Installer **ISO** attached in VM settings (e.g. **`macOS Catalina 10.15.7_19H15.iso`** if that combo works) — **install completed**.
- [ ] **VMware Tools** from **OC4VM** **`darwin.iso`** (§4) **installed in guest**; **reboot**.
- [ ] (Optional) **Shared Folders** on the host, then use **`/Volumes/VMware Shared Folders/<name>`** in the guest (§4.1).
- [ ] **`vmrun` script** `vmPath` set to the real **`.vmx`** (often **`…\macos.vmx`** from the template name).
- [ ] (Optional) **QEMU** for **.dmg → .vmdk** if you add disks from a **.dmg**.
- [ ] (Optional) **`tahoe_icloud.plist.xml`** / SIP / recovery for **Tahoe / Sequoia** or iCloud — see [OC4VM Wiki](https://github.com/DrDonk/OC4VM/wiki) and [osx.md](osx.md) notes; **Tahoe** in OC4VM is [called limited / poor performance in upstream](https://github.com/DrDonk/OC4VM#2-functionality).
- [ ] (If **App Store** / **Apple ID** in the **Sequoia** guest) [§5.1](#51-app-store-apple-id-and-smbios-sequoia--oc4vm) and [§5.1.1](#sequoia-smbios-workflow) — **one row** from **`serials.txt`**, **OpenCore** `config.plist` on **`opencore.iso`** (or `patch_hfs_plist.py`), **`.vmx`** SMBIOS + **`system-id.enable`** + **`uuid.bios`**, optional **`macos.nvram`** rename, then **`verify.sh`**.

---

## 10. Related

| Topic | Location |
|--------|-----------|
| Native / CMake macOS dev, AppKit, signing | [osx.md](osx.md) |
| **SSH** key / passwordless from Windows to guest | [ssh-key-auth.md](ssh-key-auth.md), [setup-ssh-key.ps1](setup-ssh-key.ps1) |
| Ubuntu `vmrun` example (pattern you follow) | `D:\osx\manage-ubuntu-vm.ps1` |
| App Store / Apple ID + SMBIOS (Sequoia, OC4VM) | [§5.1](#51-app-store-apple-id-and-smbios-sequoia--oc4vm), [§5.1.1](#sequoia-smbios-workflow) |
| `serials.txt` (GenSMBIOS-style rows) | `packages/media/cpp/osx/serials.txt` |
| Verify guest SMBIOS (script) | `D:\osx\verify.sh` (or pipe over SSH, [§5.1.1](#sequoia-smbios-workflow)) |
| Repack `opencore.iso` (mount; restores OpenCore model vs `VMware20,1`) | `packages/media/cpp/osx/repack-opencore-hfs.sh` |
| Same repack from the guest (HGFS) | `packages/media/cpp/osx/repack-in-guest.sh` (host folder e.g. `C:\Users\zx\sequoia-repack`) |
| HFS+ raw splice (Windows, risky) | `D:\osx\sequoia-vm\patch_hfs_plist.py` (after editing `opencore-extract/…/config.plist`) |

Primary: **[DrDonk / OC4VM](https://github.com/DrDonk/OC4VM)** (readme + [Wiki](https://github.com/DrDonk/OC4VM/wiki) + [releases](https://github.com/DrDonk/oc4vm/releases)). Also: [Broadcom/VMware downloads](https://support.broadcom.com/), [QEMU w64](https://qemu.weilnetz.de/w64/), [kiraio-moe / macOS-on-VMWare](https://github.com/kiraio-moe/macOS-on-VMWare) (supplementary), [unlocker (legacy, superseded for this flow by OC4VM per upstream)](https://github.com/DrDonk/OC4VM#introduction).
