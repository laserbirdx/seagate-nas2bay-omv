#!/bin/sh
# Syntax-check all shell scripts and verify the ubootenv templates.
cd "$(dirname "$0")/.."
for f in installer/install-from-stock.sh tools/*.sh packages/seagate-nas2bay-boot/usr/sbin/* \
         packages/seagate-nas2bay-boot/etc/kernel/postinst.d/* packages/seagate-nas2bay-boot/etc/initramfs-tools/hooks/* \
         packages/seagate-nas2bay-boot/usr/share/seagate-nas2bay-boot/init \
         packages/openmediavault-seagate-nas2bay/usr/share/openmediavault-seagate-nas2bay/webui-inject \
         packages/*/DEBIAN/post* packages/*/DEBIAN/pre*; do
    tr -d '\r' < "$f" > /tmp/chk.sh
    if sh -n /tmp/chk.sh; then echo "OK   $f"; else echo "FAIL $f"; fi
done
python3 -m py_compile packages/openmediavault-seagate-nas2bay/usr/sbin/seagate-nas2bay-hwd && echo "OK   hwd python"
S0='\324\153\020\015boot_count=0\000saved_entry=0\000\000'
S1='\343\001\322\014boot_count=0\000saved_entry=1\000\000'
printf "$S0" | cmp - packages/seagate-nas2bay-boot/usr/share/seagate-nas2bay-boot/ubootenv.slot0 && echo "slot0 bytes match (dash)"
printf "$S1" | cmp - packages/seagate-nas2bay-boot/usr/share/seagate-nas2bay-boot/ubootenv.slot1 && echo "slot1 bytes match (dash)"
if command -v busybox >/dev/null; then
  busybox sh -c "printf '$S0'" | cmp - packages/seagate-nas2bay-boot/usr/share/seagate-nas2bay-boot/ubootenv.slot0 && echo "slot0 bytes match (busybox)"
  busybox sh -n /tmp/chk.sh; tr -d '\r' < installer/install-from-stock.sh > /tmp/inst.sh; busybox sh -n /tmp/inst.sh && echo "installer OK (busybox sh -n)"
fi
command -v shellcheck >/dev/null && shellcheck -s sh -S error /tmp/inst.sh
