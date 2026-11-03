#!/bin/sh
# =============================================================================
#  install-from-stock.sh
#  Install Debian 12 + OpenMediaVault 7 on a Seagate NAS 2-Bay (n090201/SRPD20)
#  directly from the factory Seagate NAS OS 4.x over SSH.
#
#  - NAND / U-Boot are NOT modified.
#  - User data on the RAID1+LVM volume is NOT touched; the OS lives in a
#    single file (debian_omv.img) on that volume.
#  - The factory OS stays installed (root_2 / md5) and is used automatically
#    as a fallback if Debian fails to boot.
#
#  Run as root on the NAS (stock OS):
#     sh install-from-stock.sh [options]
#
#  Options:
#     --image FILE     rootfs image   (default: seagate-nas2bay-omv7-rootfs-*.img.gz next to this script)
#     --boot FILE      boot bundle    (default: seagate-nas2bay-omv7-boot-*.tar.gz next to this script)
#     --size GB        final size of the root image, grown on first boot (default: 32)
#     --password PW    password for both 'root' and the OMV web 'admin' user (asked if omitted)
#     --dry-run        only run the checks, change nothing
#     --boot-debian    only select Debian for the next boot (already installed)
#     --uninstall      restore the factory boot files and boot the factory OS
#     --yes            do not ask for confirmation
# =============================================================================
set -e

HERE=$(cd "$(dirname "$0")" && pwd)
IMAGE=""; BOOTPKG=""; SIZE_GB=32; PASSWORD=""; DRYRUN=0; YES=0; UNINSTALL=0; SELECT_DEBIAN=0
IMG_NAME=debian_omv.img
DTB_NAME=armada-370-n090201.dtb

say()  { printf '\033[1;34m==>\033[0m %s\n' "$*"; }
warn() { printf '\033[1;33mWARNING:\033[0m %s\n' "$*"; }
die()  { printf '\033[1;31mERROR:\033[0m %s\n' "$*" >&2; exit 1; }

while [ $# -gt 0 ]; do
    case "$1" in
        --image) IMAGE="$2"; shift ;;
        --boot) BOOTPKG="$2"; shift ;;
        --size) SIZE_GB="$2"; shift ;;
        --password) PASSWORD="$2"; shift ;;
        --dry-run) DRYRUN=1 ;;
        --uninstall) UNINSTALL=1 ;;
        --boot-debian) SELECT_DEBIAN=1 ;;
        --yes|-y) YES=1 ;;
        -h|--help) sed -n '2,30p' "$0"; exit 0 ;;
        *) die "unknown option: $1" ;;
    esac
    shift
done

[ "$(id -u)" = 0 ] || die "run as root (on NAS OS: 'sudo -i' or 'su')"

# ----------------------------------------------------------------------------- checks
say "Checking hardware"
MODEL=""
[ -r /proc/device-tree/model ] && MODEL=$(tr -d '\0' < /proc/device-tree/model)
COMPAT=""
[ -r /proc/device-tree/compatible ] && COMPAT=$(tr '\0' ' ' < /proc/device-tree/compatible)
echo "    model: ${MODEL:-unknown}  compatible: ${COMPAT:-unknown}"
case "$COMPAT $MODEL" in
    *n090201*|*n090203*) ;;
    *) die "this does not look like a Seagate NAS 2-Bay (n090201). Aborting." ;;
esac

# disks / partitions created by the factory OS
NVDEVS=""
for d in /dev/sda3 /dev/sdb3; do [ -b "$d" ] && NVDEVS="$NVDEVS $d"; done
[ -n "$NVDEVS" ] || die "no nv_data partitions (sda3/sdb3) found"
R1PARTS=""
for d in /dev/sda4 /dev/sdb4; do [ -b "$d" ] && R1PARTS="$R1PARTS $d"; done
[ -n "$R1PARTS" ] || die "no root_1 partitions (sda4/sdb4) found"

# data volume (vg-lv) and its mount point
LV=""
for d in /dev/mapper/vg-lv /dev/vg/lv; do [ -b "$d" ] && { LV="$d"; break; }; done
[ -n "$LV" ] || die "data volume /dev/mapper/vg-lv not found (create a volume in NAS OS first)"
DATA=$(awk -v a="$LV" '$1==a || $1=="/dev/dm-0" {print $2; exit}' /proc/mounts)
[ -n "$DATA" ] || die "data volume $LV is not mounted"
echo "    data volume: $LV mounted on $DATA"

# root_1 array (normally md4); assemble if the factory OS left it stopped
find_md4() {
    for p in $R1PARTS; do
        b=$(basename "$p")
        md=$(awk -v p="$b" '$0 ~ "^md" && $0 ~ p"\\[" {print $1; exit}' /proc/mdstat)
        [ -n "$md" ] && { echo "/dev/$md"; return; }
    done
}
MD4=$(find_md4)
if [ -z "$MD4" ]; then
    if [ "$DRYRUN" = 1 ]; then
        warn "root_1 array not assembled (would assemble /dev/md4 from$R1PARTS)"
        MD4=/dev/md4
    else
        say "Assembling root_1 array /dev/md4"
        mdadm --assemble /dev/md4 $R1PARTS || die "could not assemble /dev/md4"
        MD4=/dev/md4
    fi
fi
echo "    root_1 boot array: $MD4"
if grep -q "^$MD4 " /proc/mounts; then
    die "$MD4 is mounted by the running OS - are you running from slot 0? Boot the factory OS (slot 1) first."
fi

TMPM=/tmp/seagate-omv-inst.$$
mkdir -p "$TMPM"
cleanup() { umount "$TMPM/nv" 2>/dev/null || true; umount "$TMPM/md4" 2>/dev/null || true; rm -rf "$TMPM"; }
trap cleanup EXIT

write_nv() {   # write_nv <ubootenv-file>
    mkdir -p "$TMPM/nv"
    for d in $NVDEVS; do
        mp=$(awk -v a="$d" '$1==a {print $2; exit}' /proc/mounts)
        if [ -n "$mp" ]; then
            cat "$1" > "$mp/ubootenv"
        else
            mount -t ext2 "$d" "$TMPM/nv" && { cat "$1" > "$TMPM/nv/ubootenv"; sync; umount "$TMPM/nv"; }
        fi
        echo "    $d: ubootenv updated"
    done
    sync
}

BACKUP="$DATA/seagate-omv-backup"
SLOT0_BYTES='\324\153\020\015boot_count=0\000saved_entry=0\000\000'   # crc32 + env, Debian
SLOT1_BYTES='\343\001\322\014boot_count=0\000saved_entry=1\000\000'   # crc32 + env, factory OS

# ----------------------------------------------------------------------------- select debian
if [ "$SELECT_DEBIAN" = 1 ]; then
    [ -f "$DATA/$IMG_NAME" ] || die "$DATA/$IMG_NAME not found - install first"
    [ "$DRYRUN" = 1 ] && { say "Dry run: would select Debian"; exit 0; }
    printf "$SLOT0_BYTES" > "$TMPM/slot0"
    write_nv "$TMPM/slot0"
    say "Debian selected for the next boot. Reboot now."
    exit 0
fi

# ----------------------------------------------------------------------------- uninstall
if [ "$UNINSTALL" = 1 ]; then
    LAST=$(ls -1 "$BACKUP"/md4-boot-*.tar 2>/dev/null | tail -n1)
    [ -n "$LAST" ] || die "no boot backup found in $BACKUP"
    say "Restoring factory boot files from $LAST"
    [ "$DRYRUN" = 1 ] && exit 0
    mkdir -p "$TMPM/md4"
    mount -t ext2 "$MD4" "$TMPM/md4"
    rm -rf "$TMPM/md4/boot"
    (cd "$TMPM/md4" && tar -xf "$LAST")
    sync; umount "$TMPM/md4"
    printf "$SLOT1_BYTES" > "$TMPM/slot1"
    write_nv "$TMPM/slot1"
    say "Done. The factory OS will boot. You may delete $DATA/$IMG_NAME afterwards."
    exit 0
fi

# ----------------------------------------------------------------------------- files
[ -n "$IMAGE" ] || IMAGE=$(ls -1 "$HERE"/seagate-nas2bay-omv7-rootfs-*.img.gz 2>/dev/null | tail -n1)
[ -n "$BOOTPKG" ] || BOOTPKG=$(ls -1 "$HERE"/seagate-nas2bay-omv7-boot-*.tar.gz 2>/dev/null | tail -n1)
[ -f "$IMAGE" ] || die "rootfs image not found (use --image)"
[ -f "$BOOTPKG" ] || die "boot bundle not found (use --boot)"
echo "    rootfs: $IMAGE"
echo "    boot  : $BOOTPKG"

if [ -f "$HERE/SHA256SUMS" ] && command -v sha256sum >/dev/null 2>&1; then
    say "Verifying checksums"
    (cd "$HERE" && for f in "$(basename "$IMAGE")" "$(basename "$BOOTPKG")"; do
        grep " $f\$" SHA256SUMS | sha256sum -c - || exit 1
    done) || die "checksum mismatch - download again"
fi

IMG_GZ_MB=$(( $(ls -l "$IMAGE" | awk '{print $5}') / 1048576 ))
NEED_MB=$(( SIZE_GB * 1024 + 1024 ))
FREE_MB=$(df -m "$DATA" | awk 'NR==2 {print $4}')
echo "    free space on $DATA: ${FREE_MB} MB (need ~${NEED_MB} MB)"
[ "$FREE_MB" -ge "$NEED_MB" ] || die "not enough free space on the data volume"
[ -e "$DATA/$IMG_NAME" ] && warn "$DATA/$IMG_NAME already exists and will be REPLACED"

if [ "$DRYRUN" = 1 ]; then
    say "Dry run finished - all checks passed. Nothing was changed."
    exit 0
fi

# ----------------------------------------------------------------------------- confirm
cat <<EOF

  This will:
   1. unpack the Debian 12 / OMV 7 image to $DATA/$IMG_NAME
   2. back up and replace the boot files in $MD4:/boot (factory slot 0)
   3. tell U-Boot to start Debian on the next boot
  Your files on $DATA are not modified. The factory OS stays as fallback.

EOF
if [ "$YES" != 1 ]; then
    printf "Type YES to continue: "; read -r ans; [ "$ans" = YES ] || die "aborted"
fi

if [ -z "$PASSWORD" ]; then
    while :; do
        printf "New password for 'root' and OMV 'admin': "; stty -echo 2>/dev/null; read -r PASSWORD; stty echo 2>/dev/null; echo
        printf "Repeat: "; stty -echo 2>/dev/null; read -r P2; stty echo 2>/dev/null; echo
        [ -n "$PASSWORD" ] && [ "$PASSWORD" = "$P2" ] && break
        warn "passwords empty or different, try again"
    done
fi
case "$PASSWORD" in *:*) die "password must not contain ':'" ;; esac

# ----------------------------------------------------------------------------- install
say "1/4 Unpacking root image (takes a few minutes)"
gunzip -c "$IMAGE" > "$DATA/$IMG_NAME.part"
mv -f "$DATA/$IMG_NAME.part" "$DATA/$IMG_NAME"
sync

say "2/4 Installing boot files into $MD4"
mkdir -p "$TMPM/boot" "$TMPM/md4" "$BACKUP"
tar -xzf "$BOOTPKG" -C "$TMPM/boot"
[ -f "$TMPM/boot/uImage" ] && [ -f "$TMPM/boot/$DTB_NAME" ] || die "boot bundle is incomplete"
mdadm --readwrite "$MD4" 2>/dev/null || true
mount -t ext2 "$MD4" "$TMPM/md4"
STAMP=$(date +%Y%m%d-%H%M%S)
(cd "$TMPM/md4" && tar -cf "$BACKUP/md4-boot-$STAMP.tar" boot)
echo "    factory slot-0 boot files saved to $BACKUP/md4-boot-$STAMP.tar"
mkdir -p "$TMPM/md4/boot"
cp "$TMPM/boot/uImage" "$TMPM/md4/boot/uImage"
cp "$TMPM/boot/$DTB_NAME" "$TMPM/md4/boot/$DTB_NAME"
sync; umount "$TMPM/md4"

say "3/4 Preparing first boot"
INST="$DATA/.seagate-omv-install"
mkdir -p "$INST"; chmod 700 "$INST"
umask 077
printf 'root:%s\nadmin:%s\n' "$PASSWORD" "$PASSWORD" > "$INST/credentials"
echo $(( SIZE_GB * 1024 )) > "$INST/target_size_mb"

say "4/4 Selecting Debian for the next boot"
printf "$SLOT0_BYTES" > "$TMPM/slot0"
write_nv "$TMPM/slot0"

cat <<EOF

  Installation finished.

  Reboot now (NAS OS web UI or 'reboot'). The first Debian boot takes 2-4 min
  (SSH keys are generated and the root image is grown).

  Then open   http://<nas-ip>/      login: admin  /  <your password>
  SSH:        ssh root@<nas-ip>

  If Debian does not come up, the NAS falls back to the factory OS by itself.
  Logs: $DATA/debian_initramfs.log, $DATA/seagate-omv-firstboot.log

EOF
