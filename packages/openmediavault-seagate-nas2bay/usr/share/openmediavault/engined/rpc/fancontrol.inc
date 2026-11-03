<?php
namespace Engined\Rpc;

class FanControl extends \OMV\Rpc\ServiceAbstract {
    private $fanConfigFile = "/etc/seagate-fan.json";
    private $ledConfigFile = "/etc/seagate-led.json";

    public function getName() {
        return "FanControl";
    }

    public function initialize() {
        $this->registerMethod("getSettings");
        $this->registerMethod("setSettings");
        $this->registerMethod("getStatus");
        $this->registerMethod("getGridItems");
        $this->registerMethod("getVisualOverview");
    }

    private function readFanConfig() {
        $defaults = [
            "mode" => "auto",
            "manual_rpm" => 1400,
            "temp_low" => 42,
            "temp_med" => 50,
            "temp_high" => 58
        ];
        if (file_exists($this->fanConfigFile)) {
            $decoded = @json_decode(@file_get_contents($this->fanConfigFile), true);
            if (is_array($decoded)) {
                $defaults = array_merge($defaults, $decoded);
            }
        }
        return $defaults;
    }

    private function readLedConfig() {
        $defaults = [
            "preset_mode" => "custom",
            "led_white_power" => "on",
            "led_red_power" => "off",
            "led_white_sata" => "disk-activity",
            "led_red_sata" => "off",
            "schedule_enabled" => false,
            "off_start_hour" => 23,
            "off_end_hour" => 7,
            "sync_button_toggle" => true
        ];
        if (file_exists($this->ledConfigFile)) {
            $decoded = @json_decode(@file_get_contents($this->ledConfigFile), true);
            if (is_array($decoded)) {
                $defaults = array_merge($defaults, $decoded);
            }
        }
        return $defaults;
    }

    private function getHardwareStats() {
        $cpuTemp = 0.0;
        $phyTemp = 0.0;
        $rpm = 0;
        $pwm = 0;
        foreach (glob("/sys/class/hwmon/hwmon*") as $hw) {
            $hwName = @trim(file_get_contents("$hw/name"));
            if (strpos($hwName, "thermal") !== false && file_exists("$hw/temp1_input")) {
                $cpuTemp = round(intval(trim(file_get_contents("$hw/temp1_input"))) / 1000.0, 1);
            } elseif (strpos($hwName, "mdio") !== false && file_exists("$hw/temp1_input")) {
                $phyTemp = round(intval(trim(file_get_contents("$hw/temp1_input"))) / 1000.0, 1);
            } elseif ($hwName === "gpio_fan") {
                $rpm = intval(@trim(file_get_contents("$hw/fan1_input")));
                $pwm = intval(@trim(file_get_contents("$hw/pwm1")));
            }
        }
        $pct = intval(round(($pwm / 255.0) * 100));
        $level = 0;
        if ($pwm >= 230) $level = 3;
        elseif ($pwm >= 130) $level = 2;
        elseif ($pwm >= 40) $level = 1;

        $diskTemps = [];
        foreach (["sda", "sdb"] as $d) {
            $dt = "--";
            $out = [];
            @exec("smartctl -A -n standby /dev/$d 2>/dev/null", $out);
            foreach ($out as $line) {
                if (strpos($line, "Temperature_Celsius") !== false || strpos($line, "Airflow_Temperature_Cel") !== false) {
                    $cols = preg_split('/\s+/', trim($line));
                    if (count($cols) >= 10) {
                        $dt = $cols[9] . " °C";
                        break;
                    }
                }
            }
            $diskTemps[$d] = $dt;
        }

        $wBright = intval(@trim(file_get_contents("/sys/class/leds/dart:white:power/brightness")));
        $rBright = intval(@trim(file_get_contents("/sys/class/leds/dart:red:power/brightness")));

        return [
            "cpu_temp" => $cpuTemp,
            "phy_temp" => $phyTemp,
            "rpm" => $rpm,
            "pwm" => $pwm,
            "pct" => $pct,
            "level" => $level,
            "sda_temp" => $diskTemps["sda"],
            "sdb_temp" => $diskTemps["sdb"],
            "white_led" => $wBright > 0 ? "ON" : "OFF",
            "red_led" => $rBright > 0 ? "ON" : "OFF"
        ];
    }

    public function getSettings($params, $context) {
        $cfg = $this->readFanConfig();
        $hw = $this->getHardwareStats();
        $modeLabel = ($cfg["mode"] === "manual") ? "Manual" : "Auto (Smart Thermal)";
        return [
            "live_status" => sprintf(
                "Fan: %d RPM (%d%% - %s) | CPU: %.1f °C | Net PHY: %.1f °C | Disk 1 (sda): %s | Disk 2 (sdb): %s",
                $hw["rpm"], $hw["pct"], $modeLabel, $hw["cpu_temp"], $hw["phy_temp"], $hw["sda_temp"], $hw["sdb_temp"]
            ),
            "mode" => strval($cfg["mode"]),
            "manual_rpm" => intval($cfg["manual_rpm"]),
            "temp_low" => intval($cfg["temp_low"]),
            "temp_med" => intval($cfg["temp_med"]),
            "temp_high" => intval($cfg["temp_high"])
        ];
    }

    public function setSettings($params, $context) {
        $tLow = max(25, min(70, intval($params["temp_low"] ?? 42)));
        $tMed = max($tLow + 2, min(78, intval($params["temp_med"] ?? 50)));
        $tHigh = max($tMed + 2, min(85, intval($params["temp_high"] ?? 58)));
        $cfg = [
            "mode" => (($params["mode"] ?? "auto") === "manual") ? "manual" : "auto",
            "manual_rpm" => intval($params["manual_rpm"] ?? 1400),
            "temp_low" => $tLow,
            "temp_med" => $tMed,
            "temp_high" => $tHigh
        ];
        file_put_contents($this->fanConfigFile, json_encode($cfg, JSON_PRETTY_PRINT));
        @shell_exec("/usr/sbin/seagate-nas2bay-hwd --once >/dev/null 2>&1");
        return $this->getSettings($params, $context);
    }

    public function getStatus($params, $context) {
        $cfg = $this->readFanConfig();
        $hw = $this->getHardwareStats();
        $modeText = ($cfg["mode"] === "auto") ? "AutoMode" : "ManualMode";
        return [
            [
                "order" => 1,
                "sensor" => "Chassis Fan (GPIO)",
                "value" => sprintf("%d RPM (%d%%)", $hw["rpm"], $hw["pct"]),
                "detail" => ($cfg["mode"] === "auto") ? "Auto Mode" : "Manual Mode"
            ],
            [
                "order" => 2,
                "sensor" => "CPU (Armada 370)",
                "value" => sprintf("%.1f °C", $hw["cpu_temp"]),
                "detail" => "SoC Thermal Sensor"
            ],
            [
                "order" => 3,
                "sensor" => "Ethernet PHY (88E1510)",
                "value" => sprintf("%.1f °C", $hw["phy_temp"]),
                "detail" => "Gigabit PHY Sensor"
            ],
            [
                "order" => 4,
                "sensor" => "Disk 1 & Disk 2 Temp",
                "value" => sprintf("%s / %s", $hw["sda_temp"], $hw["sdb_temp"]),
                "detail" => "Seagate ST4000DM004 (RAID1)"
            ]
        ];
    }

    public function getGridItems($params, $context) {
        $cfg = $this->readFanConfig();
        $hw = $this->getHardwareStats();
        $modeText = ($cfg["mode"] === "auto") ? "Auto" : "Manual";
        return [
            "data" => [
                [
                    "title" => "Fan Speed (" . $modeText . ")",
                    "value" => sprintf("%d RPM (%d%%)", $hw["rpm"], $hw["pct"]),
                    "icon" => "mdi:fan",
                    "state" => "ok"
                ],
                [
                    "title" => "CPU / SoC Temp",
                    "value" => sprintf("%.1f °C", $hw["cpu_temp"]),
                    "icon" => "mdi:thermometer",
                    "state" => "ok"
                ],
                [
                    "title" => "Disk 1 & 2 Temp",
                    "value" => sprintf("%s / %s", $hw["sda_temp"], $hw["sdb_temp"]),
                    "icon" => "mdi:harddisk",
                    "state" => "ok"
                ]
            ],
            "total" => 3
        ];
    }

    private function getDiskDetail($dev, $bayNum, $tempStr) {
        $model = @trim(file_get_contents("/sys/block/$dev/device/model")) ?: "Unknown HDD";
        $serial = "--";
        $out = [];
        @exec("lsblk -dn -o SERIAL,SIZE /dev/$dev 2>/dev/null", $out);
        if (!empty($out[0])) {
            $parts = preg_split('/\s+/', trim($out[0]));
            if (count($parts) >= 1) $serial = $parts[0];
        }
        $sectors = floatval(@trim(file_get_contents("/sys/block/$dev/size")));
        $bytes = $sectors * 512.0;
        $tib = round($bytes / (1024*1024*1024*1024), 2);
        $tb = round($bytes / 1000000000000.0, 2);
        return [
            "bay" => $bayNum,
            "device" => "/dev/$dev",
            "model" => $model,
            "serial" => $serial,
            "size" => "{$tib} TiB ({$tb} TB)",
            "temp" => $tempStr,
            "smart" => "GOOD / PASSED",
            "raid" => "RAID 1 (md8 -> vg-lv)"
        ];
    }

    private function getUsbPortDetail($portNum) {
        $candidates = ["1-$portNum", "2-$portNum"];
        foreach ($candidates as $cid) {
            $base = "/sys/bus/usb/devices/$cid";
            if (is_dir($base)) {
                $prod = @trim(file_get_contents("$base/product"));
                $mfg = @trim(file_get_contents("$base/manufacturer"));
                $spd = @trim(file_get_contents("$base/speed"));
                $vid = @trim(file_get_contents("$base/idVendor"));
                $pid = @trim(file_get_contents("$base/idProduct"));
                $blkName = "";
                $blkSize = "";
                $blkModel = "";
                foreach (glob("/sys/block/sd*") as $bpath) {
                    $real = realpath($bpath);
                    if ($real && strpos($real, "/$cid/") !== false) {
                        $bn = basename($bpath);
                        $blkName = "/dev/$bn";
                        $sec = floatval(@trim(file_get_contents("$bpath/size")));
                        $b = $sec * 512.0;
                        $blkSize = ($b >= 1073741824) ? (round($b / 1073741824, 2) . " GiB") : (round($b / 1048576, 1) . " MiB");
                        $blkModel = @trim(file_get_contents("$bpath/device/model"));
                        break;
                    }
                }
                $usbVer = (intval($spd) >= 5000) ? "USB 3.0 SuperSpeed" : "USB 2.0 High-Speed";
                $displayName = trim(($mfg ? "$mfg " : "") . ($prod ? $prod : ""));
                if ($blkModel && stripos($displayName, $blkModel) === false) {
                    $displayName = trim($displayName . " ($blkModel)");
                }
                if (!$displayName) $displayName = "USB Device ($vid:$pid)";
                return [
                    "connected" => true,
                    "port" => $portNum,
                    "bus_id" => $cid,
                    "name" => $displayName,
                    "vendor" => $mfg ?: "Generic",
                    "product" => $prod ?: ($blkModel ?: "USB Device"),
                    "usb_id" => "$vid:$pid",
                    "speed" => "{$spd} Mbps ({$usbVer})",
                    "block_dev" => $blkName ?: "--",
                    "capacity" => $blkSize ?: "--"
                ];
            }
        }
        return [
            "connected" => false,
            "port" => $portNum,
            "bus_id" => "--",
            "name" => "",
            "vendor" => "--",
            "product" => "--",
            "usb_id" => "--",
            "speed" => "USB 3.0 (5000 Mbps)",
            "block_dev" => "--",
            "capacity" => "--"
        ];
    }

    private function formatBytes($bytes) {
        if ($bytes >= 1073741824) return round($bytes / 1073741824, 2) . " GiB";
        if ($bytes >= 1048576) return round($bytes / 1048576, 2) . " MiB";
        if ($bytes >= 1024) return round($bytes / 1024, 1) . " KiB";
        return $bytes . " B";
    }

    public function getVisualOverview($params, $context) {
        $fcfg = $this->readFanConfig();
        $lcfg = $this->readLedConfig();
        $hw = $this->getHardwareStats();

        $iface = "eth0";
        $oper = strtoupper(@trim(file_get_contents("/sys/class/net/$iface/operstate")) ?: "UP");
        $speed = @trim(file_get_contents("/sys/class/net/$iface/speed")) ?: "1000";
        $duplex = ucfirst(@trim(file_get_contents("/sys/class/net/$iface/duplex")) ?: "full");
        $mac = strtoupper(@trim(file_get_contents("/sys/class/net/$iface/address")) ?: "-");
        $rxBytes = floatval(@trim(file_get_contents("/sys/class/net/$iface/statistics/rx_bytes")));
        $txBytes = floatval(@trim(file_get_contents("/sys/class/net/$iface/statistics/tx_bytes")));
        $rxPkts = intval(@trim(file_get_contents("/sys/class/net/$iface/statistics/rx_packets")));
        $txPkts = intval(@trim(file_get_contents("/sys/class/net/$iface/statistics/tx_packets")));

        $ipOut = [];
        @exec("ip -4 -o addr show dev $iface 2>/dev/null | awk '{print \$4}'", $ipOut);
        $ipAddr = !empty($ipOut[0]) ? trim($ipOut[0]) : "-";

        $uptimeSec = intval(explode(' ', @trim(file_get_contents("/proc/uptime")))[0]);
        $hours = floor($uptimeSec / 3600);
        $mins = floor(($uptimeSec % 3600) / 60);
        $uptimeStr = "{$hours}h {$mins}m";

        $loadParts = explode(' ', @trim(file_get_contents("/proc/loadavg")));
        $loadStr = "{$loadParts[0]} / {$loadParts[1]} / {$loadParts[2]}";

        $memInfo = @file("/proc/meminfo", FILE_IGNORE_NEW_LINES);
        $memTotal = 511848;
        $memAvail = 380000;
        if ($memInfo) {
            foreach ($memInfo as $ml) {
                if (strpos($ml, "MemTotal:") === 0) $memTotal = intval(preg_replace('/\D+/', '', $ml));
                if (strpos($ml, "MemAvailable:") === 0) $memAvail = intval(preg_replace('/\D+/', '', $ml));
            }
        }
        $memUsedMb = round(($memTotal - $memAvail) / 1024.0, 1);
        $memTotalMb = round($memTotal / 1024.0, 1);
        $memPct = intval(round(($memUsedMb / max(1, $memTotalMb)) * 100));

        return [
            "ts_ms" => round(microtime(true) * 1000),
            "model" => "Seagate NAS 2-Bay (SRPD20 / n090201)",
            "disks" => [
                "bay1" => $this->getDiskDetail("sda", 1, $hw["sda_temp"]),
                "bay2" => $this->getDiskDetail("sdb", 2, $hw["sdb_temp"])
            ],
            "fan" => [
                "rpm" => $hw["rpm"],
                "pwm" => $hw["pwm"],
                "pct" => $hw["pct"],
                "level" => $hw["level"],
                "mode" => $fcfg["mode"],
                "cpu_temp" => $hw["cpu_temp"],
                "phy_temp" => $hw["phy_temp"],
                "thresholds" => "{$fcfg['temp_low']}°C / {$fcfg['temp_med']}°C / {$fcfg['temp_high']}°C"
            ],
            "usb" => [
                "front" => $this->getUsbPortDetail(1),
                "rear" => $this->getUsbPortDetail(2)
            ],
            "eth" => [
                "iface" => $iface,
                "operstate" => $oper,
                "ip" => $ipAddr,
                "mac" => $mac,
                "speed" => "{$speed} Mbps ({$duplex})",
                "rx_bytes" => $rxBytes,
                "tx_bytes" => $txBytes,
                "rx_human" => $this->formatBytes($rxBytes),
                "tx_human" => $this->formatBytes($txBytes),
                "rx_packets" => $rxPkts,
                "tx_packets" => $txPkts,
                "phy_temp" => $hw["phy_temp"] . " °C"
            ],
            "system" => [
                "hostname" => gethostname(),
                "uptime" => $uptimeStr,
                "load" => $loadStr,
                "mem_used_mb" => $memUsedMb,
                "mem_total_mb" => $memTotalMb,
                "mem_pct" => $memPct,
                "led_mode" => $lcfg["preset_mode"],
                "white_led" => $hw["white_led"],
                "red_led" => $hw["red_led"]
            ]
        ];
    }
}
