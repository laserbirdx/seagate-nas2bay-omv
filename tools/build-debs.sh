#!/bin/sh
# Build the two .deb packages from packages/*. Works on any Linux with
# dpkg-deb (Debian/Ubuntu, WSL). Output: dist/*.deb
set -e
REPO=$(cd "$(dirname "$0")/.." && pwd)
OUT="$REPO/dist"
WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT
mkdir -p "$OUT"

for pkg in "$REPO"/packages/*/; do
    name=$(basename "$pkg")
    cp -a "$pkg" "$WORK/$name"
    t="$WORK/$name"
    # sane permissions (sources may come from a Windows checkout)
    find "$t" -type d -exec chmod 0755 {} +
    find "$t" -type f -exec chmod 0644 {} +
    for f in DEBIAN/postinst DEBIAN/prerm DEBIAN/postrm DEBIAN/preinst; do
        [ -f "$t/$f" ] && chmod 0755 "$t/$f"
    done
    find "$t/usr/sbin" "$t/etc/kernel" "$t/etc/initramfs-tools/hooks" \
         "$t/usr/share/openmediavault-seagate-nas2bay/webui-inject" \
         "$t/usr/share/seagate-nas2bay-boot/init" \
         -type f -exec chmod 0755 {} + 2>/dev/null || true
    # strip CRLF from text files
    find "$t" -type f ! -name '*.png' ! -name '*.dtb' ! -name 'ubootenv.*' -exec sed -i 's/\r$//' {} +
    ver=$(sed -n 's/^Version: //p' "$t/DEBIAN/control")
    arch=$(sed -n 's/^Architecture: //p' "$t/DEBIAN/control")
    dpkg-deb --root-owner-group -Zxz --build "$t" "$OUT/${name}_${ver}_${arch}.deb"
done
( cd "$OUT" && sha256sum *.deb > SHA256SUMS.debs )
ls -l "$OUT"
