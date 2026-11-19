<?php
namespace Engined\Rpc;

require_once("openmediavault/functions.inc");

class OMVRpcServiceRaidControl extends \OMV\Rpc\ServiceAbstract {
    private $sysctlFile = "/etc/sysctl.d/99-seagate-nas.conf";

    public function getName() {
        return "RaidControl";
    }

    public function initialize() {
        $this->registerMethod("getSettings");
        $this->registerMethod("setSettings");
        $this->registerMethod("getStatus");
        $this->registerMethod("pauseResync");
        $this->registerMethod("resumeResync");
        $this->registerMethod("enableBitmap");
    }

    private function findDataArray() {
        $best = null;
        $bestSectors = 0;
        foreach (glob("/sys/block/md*") as $p) {
            $name = basename($p);
            if (!preg_match('/^md\d+$/', $name)) continue;
            $sz = floatval(@trim(file_get_contents("$p/size")));
            if ($sz > $bestSectors) {
                $bestSectors = $sz;
                $best = $name;
            }
        }
        return $best ?: "md0";
    }

    private function parseMdInfo() {
        $md = $this->findDataArray();
        $sys = "/sys/block/$md/md";
        $level = @trim(file_get_contents("$sys/level")) ?: "raid1";
        $action = @trim(file_get_contents("$sys/sync_action")) ?: "idle";
        $raidDisks = intval(@trim(file_get_contents("$sys/raid_disks")) ?: 2);
        $degraded = intval(@trim(file_get_contents("$sys/degraded")) ?: 0);
        $activeDisks = max(0, $raidDisks - $degraded);

        $pct = 100.0;
        $speedKbs = 0.0;
        $etaStr = "--";
        $hasBitmap = false;
        $members = "";
        $stateFlags = "";

        $mdstat = @file_get_contents("/proc/mdstat") ?: "";
        if (preg_match('/^' . preg_quote($md, '/') . '\s*:\s*(.+?)(?=\n[ \t]*\n|\z)/ms', $mdstat, $m)) {
            $block = $m[1];
            if (preg_match('/\[(\d+\/\d+)\]\s+\[([U_]+)\]/', $block, $sm)) {
                $stateFlags = "[{$sm[1]}] [{$sm[2]}]";
            }
            if (preg_match('/(resync|recovery|check|reshape)\s*=\s*([0-9.]+)%\s*\([^)]+\)\s*finish=([0-9.]+)min\s*speed=(\d+)K\/sec/', $block, $rm)) {
                $pct = floatval($rm[2]);
                $mins = floatval($rm[3]);
                $speedKbs = floatval($rm[4]);
                if ($mins >= 60) {
                    $hrs = floor($mins / 60);
                    $rem = intval(round($mins - $hrs * 60));
                    $etaStr = sprintf("%dh %dm", $hrs, $rem);
                } else {
                    $etaStr = sprintf("%.0f min", $mins);
                }
            } elseif (strpos($block, "resync=PENDING") !== false || $action === "frozen") {
                $comp = @trim(file_get_contents("$sys/sync_completed"));
                if (preg_match('/^(\d+)\s*\/\s*(\d+)$/', $comp, $cm) && floatval($cm[2]) > 0) {
                    $pct = round((floatval($cm[1]) / floatval($cm[2])) * 100.0, 1);
                } else {
                    $pct = 0.0;
                }
                $etaStr = "Paused";
            }
            if (strpos($block, "bitmap:") !== false) {
                $hasBitmap = true;
            }
            if (preg_match_all('/(sd[a-z]\d+)\[\d+\]/', $block, $dm)) {
                $members = implode(", ", $dm[1]);
            }
        }

        $minKb = intval(@trim(file_get_contents("/proc/sys/dev/raid/speed_limit_min")) ?: 5000);
        $maxKb = intval(@trim(file_get_contents("/proc/sys/dev/raid/speed_limit_max")) ?: 25000);

        return [
            "device" => "/dev/$md",
            "name" => $md,
            "level" => strtoupper($level),
            "action" => $action,
            "active_disks" => $activeDisks,
            "raid_disks" => $raidDisks,
            "degraded" => $degraded,
            "flags" => $stateFlags ?: "[$activeDisks/$raidDisks]",
            "members" => $members ?: "sda8, sdb8",
            "pct" => $pct,
            "speed_mbs" => round($speedKbs / 1024.0, 1),
            "eta" => $etaStr,
            "bitmap" => $hasBitmap,
            "min_mbs" => max(1, intval(round($minKb / 1000.0))),
            "max_mbs" => max(1, intval(round($maxKb / 1000.0)))
        ];
    }

    private function persistSysctl($minKb, $maxKb) {
        $lines = file_exists($this->sysctlFile) ? file($this->sysctlFile, FILE_IGNORE_NEW_LINES) : [];
        $out = [];
        foreach ($lines as $l) {
            if (preg_match('/^\s*dev\.raid\.speed_limit_(min|max)\s*=/', $l)) continue;
            $out[] = $l;
        }
        $out[] = "dev.raid.speed_limit_min = $minKb";
        $out[] = "dev.raid.speed_limit_max = $maxKb";
        file_put_contents($this->sysctlFile, implode("\n", $out) . "\n");
    }

    public function getSettings($params, $context) {
        $info = $this->parseMdInfo();
        if ($info["action"] === "resync" || $info["action"] === "recover" || $info["action"] === "check") {
            $stateText = sprintf(
                "%s (%s %s) | %s: %.1f%% @ %.1f MB/s (ETA %s) | Bitmap: %s",
                $info["device"], $info["level"], $info["flags"],
                strtoupper($info["action"]), $info["pct"], $info["speed_mbs"], $info["eta"],
                $info["bitmap"] ? "Active" : "Off"
            );
        } elseif ($info["action"] === "frozen") {
            $stateText = sprintf(
                "%s (%s %s) | PAUSED (Frozen @ %.1f%%) | Limit: %d-%d MB/s | Bitmap: %s",
                $info["device"], $info["level"], $info["flags"],
                $info["pct"], $info["min_mbs"], $info["max_mbs"],
                $info["bitmap"] ? "Active" : "Off"
            );
        } else {
            $stateText = sprintf(
                "%s (%s %s) | CLEAN / IDLE (100%%) | Limit: %d-%d MB/s | Bitmap: %s",
                $info["device"], $info["level"], $info["flags"],
                $info["min_mbs"], $info["max_mbs"],
                $info["bitmap"] ? "Active" : "Off"
            );
        }

        return [
            "live_status" => $stateText,
            "speed_limit_min_mb" => intval($info["min_mbs"]),
            "speed_limit_max_mb" => intval($info["max_mbs"])
        ];
    }

    public function setSettings($params, $context) {
        $minMb = max(1, min(150, intval($params["speed_limit_min_mb"] ?? 5)));
        $maxMb = max($minMb, min(200, intval($params["speed_limit_max_mb"] ?? 25)));
        $minKb = $minMb * 1000;
        $maxKb = $maxMb * 1000;

        @file_put_contents("/proc/sys/dev/raid/speed_limit_min", strval($minKb));
        @file_put_contents("/proc/sys/dev/raid/speed_limit_max", strval($maxKb));
        $this->persistSysctl($minKb, $maxKb);

        return $this->getSettings($params, $context);
    }

    public function pauseResync($params, $context) {
        $md = $this->findDataArray();
        @file_put_contents("/sys/block/$md/md/sync_action", "frozen");
        return $this->getSettings([], $context);
    }

    public function resumeResync($params, $context) {
        $md = $this->findDataArray();
        @file_put_contents("/sys/block/$md/md/sync_action", "idle");
        return $this->getSettings([], $context);
    }

    public function enableBitmap($params, $context) {
        $info = $this->parseMdInfo();
        if ($info["bitmap"]) {
            return $this->getSettings([], $context);
        }
        if ($info["action"] !== "idle") {
            throw new \OMV\Exception(
                "Cannot add write-intent bitmap while RAID %s is '%s' (%.1f%%). Wait until resync finishes (100%%), then click Enable Internal Bitmap.",
                $info["device"], $info["action"], $info["pct"]
            );
        }
        $out = [];
        $rc = 0;
        @exec("mdadm --grow --bitmap=internal " . escapeshellarg($info["device"]) . " 2>&1", $out, $rc);
        if ($rc !== 0) {
            throw new \OMV\Exception("mdadm bitmap failed: %s", implode(" ", $out));
        }
        return $this->getSettings([], $context);
    }

    public function getStatus($params, $context) {
        $info = $this->parseMdInfo();

        if ($info["action"] === "resync" || $info["action"] === "recover" || $info["action"] === "check") {
            $progVal = sprintf("%.1f%% (%.1f MB/s)", $info["pct"], $info["speed_mbs"]);
            $progDetail = "ETA: " . $info["eta"];
        } elseif ($info["action"] === "frozen") {
            $progVal = sprintf("PAUSED (%.1f%%)", $info["pct"]);
            $progDetail = "Frozen (Resume in Services -> RAID Control)";
        } else {
            $progVal = "100% (Idle / Clean)";
            $progDetail = "No active resync";
        }

        return [
            [
                "order" => 1,
                "metric" => "Data RAID Array",
                "value" => sprintf("%s (%s %s)", $info["device"], $info["level"], $info["flags"]),
                "detail" => ($info["degraded"] > 0 ? "DEGRADED — " : "Healthy — ") . $info["members"]
            ],
            [
                "order" => 2,
                "metric" => "Resync / Sync Status",
                "value" => $progVal,
                "detail" => $progDetail
            ],
            [
                "order" => 3,
                "metric" => "Resync Speed Limit",
                "value" => sprintf("%d – %d MB/s", $info["min_mbs"], $info["max_mbs"]),
                "detail" => "Configurable in Services -> RAID Control"
            ],
            [
                "order" => 4,
                "metric" => "Write-Intent Bitmap",
                "value" => $info["bitmap"] ? "Active (Fast Recovery)" : "Not Enabled",
                "detail" => $info["bitmap"] ? "Protects against full resync on power loss" : "Enable after resync reaches 100%"
            ]
        ];
    }
}
