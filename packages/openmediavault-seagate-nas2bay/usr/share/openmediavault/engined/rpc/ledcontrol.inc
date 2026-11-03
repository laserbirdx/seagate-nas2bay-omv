<?php
namespace Engined\Rpc;

require_once("openmediavault/functions.inc");

class OMVRpcServiceLedControl extends \OMV\Rpc\ServiceAbstract {
    private $configFile = "/etc/seagate-led.json";

    public function getName() {
        return "LedControl";
    }

    public function initialize() {
        $this->registerMethod("getSettings");
        $this->registerMethod("setSettings");
        $this->registerMethod("getStatus");
        $this->registerMethod("turnOffAll");
        $this->registerMethod("restoreDefaults");
    }

    private function readConfig() {
        $defaults = [
            "mode" => "auto",
            "manual_pwm" => 128,
            "temp_low" => 35,
            "temp_high" => 50,
            "pwm_min" => 65,
            "pwm_max" => 255,
            "preset_mode" => "normal",
            "led_white_power" => "on",
            "led_red_power" => "off",
            "led_white_sata" => "disk-activity",
            "led_red_sata" => "off",
            "schedule_enabled" => false,
            "off_start_hour" => 23,
            "off_end_hour" => 7,
            "sync_button_toggle" => true
        ];
        if (file_exists($this->configFile)) {
            $data = json_decode(file_get_contents($this->configFile), true);
            if (is_array($data)) {
                return array_merge($defaults, $data);
            }
        }
        return $defaults;
    }

    private function readSysfs($path, $default = "") {
        if (file_exists($path) && is_readable($path)) {
            return trim(@file_get_contents($path));
        }
        return $default;
    }

    private function getLedState($ledName) {
        $base = "/sys/class/leds/" . $ledName;
        if (!is_dir($base)) return "N/A";
        $bright = intval($this->readSysfs($base . "/brightness", "0"));
        $trigRaw = $this->readSysfs($base . "/trigger", "none");
        $trig = "none";
        if (preg_match('/\[([^\]]+)\]/', $trigRaw, $m)) {
            $trig = $m[1];
        }
        if ($trig !== "none") {
            return "ACTIVE (" . $trig . ")";
        }
        if ($ledName === "dart:white:sata0" || $ledName === "dart:white:sata1") { return ($bright === 0) ? "ON" : "OFF"; } return ($bright > 0) ? "ON" : "OFF";
    }

    private function isWithinOffSchedule($cfg) {
        if (empty($cfg["schedule_enabled"])) return false;
        $curHour = intval(date("G"));
        $start = intval($cfg["off_start_hour"]);
        $end = intval($cfg["off_end_hour"]);
        if ($start === $end) return true;
        if ($start < $end) {
            return ($curHour >= $start && $curHour < $end);
        } else {
            return ($curHour >= $start || $curHour < $end);
        }
    }

    public function getSettings($params, $context) {
        $cfg = $this->readConfig();
        $schedActive = $this->isWithinOffSchedule($cfg);
        $curTime = date("H:i");

        $wPwr = $this->getLedState("dart:white:power");
        $rPwr = $this->getLedState("dart:red:power");
        $wSata0 = $this->getLedState("dart:white:sata0");
        $wSata1 = $this->getLedState("dart:white:sata1");
        $rSata0 = $this->getLedState("dart:red:sata0");

        $schedStr = !empty($cfg["schedule_enabled"])
            ? sprintf("%02d:00 - %02d:00 (%s)", intval($cfg["off_start_hour"]), intval($cfg["off_end_hour"]), $schedActive ? "SLEEP ACTIVE" : "STANDBY")
            : "Disabled";

        $cfg["status_info"] = sprintf(
            "Power: %s | Disk 1/2 White: %s / %s | Fault Red: %s / %s | Schedule: %s (Clock: %s)",
            $wPwr, $wSata0, $wSata1, $rPwr, $rSata0, $schedStr, $curTime
        );

        return [
            "preset_mode" => strval($cfg["preset_mode"]),
            "led_white_power" => strval($cfg["led_white_power"]),
            "led_red_power" => strval($cfg["led_red_power"]),
            "led_white_sata" => strval($cfg["led_white_sata"]),
            "led_red_sata" => strval($cfg["led_red_sata"]),
            "schedule_enabled" => boolval($cfg["schedule_enabled"]),
            "off_start_hour" => intval($cfg["off_start_hour"]),
            "off_end_hour" => intval($cfg["off_end_hour"]),
            "sync_button_toggle" => boolval($cfg["sync_button_toggle"]),
            "status_info" => $cfg["status_info"]
        ];
    }

    public function setSettings($params, $context) {
        $current = $this->readConfig();
        $preset = isset($params["preset_mode"]) ? strval($params["preset_mode"]) : "custom";

        $wPwr = isset($params["led_white_power"]) ? strval($params["led_white_power"]) : $current["led_white_power"];
        $rPwr = isset($params["led_red_power"]) ? strval($params["led_red_power"]) : $current["led_red_power"];
        $wSata = isset($params["led_white_sata"]) ? strval($params["led_white_sata"]) : $current["led_white_sata"];
        $rSata = isset($params["led_red_sata"]) ? strval($params["led_red_sata"]) : $current["led_red_sata"];

        if ($preset === "all_off") {
            $wPwr = "off";
            $rPwr = "off";
            $wSata = "off";
            $rSata = "off";
        } elseif ($preset === "night") {
            $wPwr = "off";
            $rPwr = "off";
            $wSata = "off";
            $rSata = "off";
        } elseif ($preset === "normal") {
            $wPwr = "on";
            $rPwr = "off";
            $wSata = "disk-activity";
            $rSata = "off";
        }

        $current["preset_mode"] = $preset;
        $current["led_white_power"] = $wPwr;
        $current["led_red_power"] = $rPwr;
        $current["led_white_sata"] = $wSata;
        $current["led_red_sata"] = $rSata;
        $current["schedule_enabled"] = !empty($params["schedule_enabled"]);
        $current["off_start_hour"] = isset($params["off_start_hour"]) ? max(0, min(23, intval($params["off_start_hour"]))) : 23;
        $current["off_end_hour"] = isset($params["off_end_hour"]) ? max(0, min(23, intval($params["off_end_hour"]))) : 7;
        $current["sync_button_toggle"] = !empty($params["sync_button_toggle"]);

        file_put_contents($this->configFile, json_encode($current, JSON_PRETTY_PRINT));
        shell_exec("/usr/sbin/seagate-nas2bay-hwd --leds-only >/dev/null 2>&1 &");
        return $current;
    }

    public function turnOffAll($params, $context) {
        $current = $this->readConfig();
        $current["preset_mode"] = "all_off";
        $current["led_white_power"] = "off";
        $current["led_red_power"] = "off";
        $current["led_white_sata"] = "off";
        $current["led_red_sata"] = "off";
        file_put_contents($this->configFile, json_encode($current, JSON_PRETTY_PRINT));
        shell_exec("/usr/sbin/seagate-nas2bay-hwd --leds-only >/dev/null 2>&1");
        return $this->getSettings([], $context);
    }

    public function restoreDefaults($params, $context) {
        $current = $this->readConfig();
        $current["preset_mode"] = "normal";
        $current["led_white_power"] = "on";
        $current["led_red_power"] = "off";
        $current["led_white_sata"] = "disk-activity";
        $current["led_red_sata"] = "off";
        file_put_contents($this->configFile, json_encode($current, JSON_PRETTY_PRINT));
        shell_exec("/usr/sbin/seagate-nas2bay-hwd --leds-only >/dev/null 2>&1");
        return $this->getSettings([], $context);
    }

    public function getStatus($params, $context) {
        $cfg = $this->getSettings($params, $context);
        return [
            [
                "metric" => "Power White LED (Front Button)",
                "value" => $this->getLedState("dart:white:power"),
                "details" => "Mode: " . $cfg["led_white_power"]
            ],
            [
                "metric" => "Disk 1 & Disk 2 White Activity LEDs",
                "value" => $this->getLedState("dart:white:sata0") . " / " . $this->getLedState("dart:white:sata1"),
                "details" => "Mode: " . $cfg["led_white_sata"]
            ],
            [
                "metric" => "Power & Disk Red Fault LEDs",
                "value" => $this->getLedState("dart:red:power") . " / " . $this->getLedState("dart:red:sata0"),
                "details" => "Power Red: " . $cfg["led_red_power"] . " | Disk Red: " . $cfg["led_red_sata"]
            ],
            [
                "metric" => "Hourly Auto-Off Schedule",
                "value" => $cfg["schedule_enabled"] ? sprintf("%02d:00 -> %02d:00", $cfg["off_start_hour"], $cfg["off_end_hour"]) : "Disabled",
                "details" => $this->isWithinOffSchedule($this->readConfig()) ? "Currently in Night Sleep Window (LEDs OFF)" : "Currently in Daytime Window"
            ]
        ];
    }
}
