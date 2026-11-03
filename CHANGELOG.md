# Changelog

## 1.0.0

- Debian 12 + OpenMediaVault 7 for the Seagate NAS 2-Bay (n090201 / SRPD20).
- Installer that runs on the factory Seagate NAS OS over SSH (no NAND flashing).
- `seagate-nas2bay-boot`: fixed device tree, uImage builder + kernel hook, initramfs with
  two-stage fail-safe rollback to the factory OS, `nas-boot-select`, first-boot setup.
- `openmediavault-seagate-nas2bay`: Fan & Thermal, LED & Night Mode, Chassis Buttons with
  One-Touch USB Backup, Hardware Overview dashboard widget; English + Turkish.
