# Windows installer

`Osdrop.iss` is the Inno Setup script for `Osdrop-Setup.exe`. It installs the shell, the engine,
`OsdropInput` and `OsdropService`, starts the service and adds a firewall rule for `Osdrop.exe` on
every network profile. Upgrades stop the service first. The uninstaller ends Osdrop, removes the
service, loads the system cursors again, deletes the firewall rule and the "Start with Windows"
entry. `scripts/windows/Build-Installer.ps1` builds it; see docs/RELEASING.md.

`licenses/` holds the license texts of the NuGet packages the app ships; the build adds the others.
