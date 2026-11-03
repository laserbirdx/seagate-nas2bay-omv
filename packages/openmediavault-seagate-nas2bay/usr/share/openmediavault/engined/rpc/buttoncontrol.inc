<?php
/**
 * Seagate NAS 2-Bay (n090201) - Chassis Buttons & One-Touch USB Backup RPC service.
 *
 * @license GPL-3.0-or-later
 */
namespace Engined\Rpc;

require_once("openmediavault/functions.inc");

class OMVRpcServiceButtonControl extends \OMV\Rpc\ServiceAbstract {
    private $configFile = "/etc/seagate-buttons.json";
    private $ledConfigFile = "/etc/seagate-led.json";
    private $daemon = "/usr/sbin/seagate-nas2bay-hwd";

    public function getName() {
        return "ButtonControl";
    }

    public function initialize() {
        $this->registerMethod("getSettings");
        $this->registerMethod("setSettings");
        $this->registerMethod("runUsbBackupNow");
    }

    private function defaults() {
        return [
            "long_press_sec" => 2,
            "led_feedback" => true,
            "usb_backup_sharedfolderref" => "",
            "usb_backup_path" => "",
            "usb_backup_subdir" => "USB_Backup",
            "usb_backup_mode" => "timestamp_folder",
            "usb_backup_prefix" => "USB_",
            "usb_backup_timestamp_fmt" => "YYYYMMDD_HHMMSS",
            "usb_backup_suffix" => "_Backup",
            "usb_led_during" => "blink_white_power",
            "usb_led_complete" => "flash_3x_restore",
            "usb_post_action" => "eject_usb",
            "sync_short_action" => "toggle_leds",
            "sync_long_action" => "usb_backup",
            "power_short_action" => "fan_boost_toggle",
            "power_long_action" => "poweroff",
            "reset_short_action" => "restart_smb",
            "reset_long_action" => "reset_network_dhcp",
            "last_event" => "None (Ready)"
        ];
    }

    private function readConfig() {
        $defaults = $this->defaults();
        if (file_exists($this->configFile)) {
            $data = json_decode(file_get_contents($this->configFile), true);
            if (is_array($data)) {
                return array_merge($defaults, $data);
            }
        }
        return $defaults;
    }

    private function sanitizeToken($s) {
        return preg_replace('/[^a-zA-Z0-9_\-\.]/', '', trim(strval($s)));
    }

    private function sanitizeRelPath($s) {
        $s = preg_replace('/[^a-zA-Z0-9_\-\.\/ ]/', '', trim(strval($s)));
        $parts = array_filter(explode("/", $s), function ($p) {
            return $p !== "" && $p !== "." && $p !== "..";
        });
        return implode("/", $parts);
    }

    private function resolveSharedFolderPath($uuid, $context) {
        if (!is_string($uuid) || !preg_match('/^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$/i', $uuid)) {
            return "";
        }
        try {
            $path = \OMV\Rpc\Rpc::call("ShareMgmt", "getPath", ["uuid" => $uuid], $context);
            return is_string($path) ? rtrim($path, "/") : "";
        } catch (\Exception $e) {
            return "";
        }
    }

    private function previewPath($cfg) {
        $base = $cfg["usb_backup_path"];
        if ($base === "") {
            return "(no destination shared folder selected)";
        }
        $sub = trim(strval($cfg["usb_backup_subdir"]), "/ ");
        $dir = ($sub !== "") ? ($base . "/" . $sub) : $base;
        switch ($cfg["usb_backup_timestamp_fmt"]) {
            case "YYYY-MM-DD_HH-MM": $ts = date("Y-m-d_H-i"); break;
            case "YYYY-MM-DD":       $ts = date("Y-m-d"); break;
            case "none":             $ts = ""; break;
            default:                 $ts = date("Ymd_His");
        }
        if ($cfg["usb_backup_mode"] === "sync_overwrite") {
            $name = trim($cfg["usb_backup_prefix"] . $cfg["usb_backup_suffix"], "_");
            return ($name !== "") ? ($dir . "/" . $name) : $dir;
        }
        return $dir . "/" . $cfg["usb_backup_prefix"] . $ts . $cfg["usb_backup_suffix"];
    }

    public function getSettings($params, $context) {
        $cfg = $this->readConfig();
        $result = [
            "status_info" => sprintf("Last Event: %s | Backup Path Preview: %s",
                $cfg["last_event"], $this->previewPath($cfg))
        ];
        foreach ($this->defaults() as $k => $v) {
            if ($k === "last_event" || $k === "usb_backup_path") {
                continue;
            }
            if (is_bool($v)) {
                $result[$k] = boolval($cfg[$k]);
            } elseif (is_int($v)) {
                $result[$k] = intval($cfg[$k]);
            } else {
                $result[$k] = strval($cfg[$k]);
            }
        }
        return $result;
    }

    public function setSettings($params, $context) {
        $cur = $this->readConfig();
        $str = function ($key, $def) use ($params) {
            return isset($params[$key]) ? strval($params[$key]) : $def;
        };

        $cur["long_press_sec"] = isset($params["long_press_sec"]) ? max(1, min(10, intval($params["long_press_sec"]))) : 2;
        $cur["led_feedback"] = !empty($params["led_feedback"]);
        $cur["usb_backup_sharedfolderref"] = $str("usb_backup_sharedfolderref", "");
        $cur["usb_backup_path"] = $this->resolveSharedFolderPath($cur["usb_backup_sharedfolderref"], $context);
        $cur["usb_backup_subdir"] = $this->sanitizeRelPath($str("usb_backup_subdir", "USB_Backup"));
        $cur["usb_backup_mode"] = $str("usb_backup_mode", "timestamp_folder");
        $cur["usb_backup_prefix"] = $this->sanitizeToken($str("usb_backup_prefix", "USB_"));
        $cur["usb_backup_timestamp_fmt"] = $str("usb_backup_timestamp_fmt", "YYYYMMDD_HHMMSS");
        $cur["usb_backup_suffix"] = $this->sanitizeToken($str("usb_backup_suffix", "_Backup"));
        $cur["usb_led_during"] = $str("usb_led_during", "blink_white_power");
        $cur["usb_led_complete"] = $str("usb_led_complete", "flash_3x_restore");
        $cur["usb_post_action"] = $str("usb_post_action", "eject_usb");
        foreach (["sync", "power", "reset"] as $b) {
            foreach (["short", "long"] as $t) {
                $k = "{$b}_{$t}_action";
                $cur[$k] = $str($k, $this->defaults()[$k]);
            }
        }

        file_put_contents($this->configFile, json_encode($cur, JSON_PRETTY_PRINT));

        if (file_exists($this->ledConfigFile)) {
            $lcfg = json_decode(file_get_contents($this->ledConfigFile), true);
            if (is_array($lcfg)) {
                $lcfg["sync_button_toggle"] = ($cur["sync_short_action"] === "toggle_leds");
                file_put_contents($this->ledConfigFile, json_encode($lcfg, JSON_PRETTY_PRINT));
            }
        }

        return $this->getSettings([], $context);
    }

    public function runUsbBackupNow($params, $context) {
        shell_exec($this->daemon . " --usb-backup-now >/dev/null 2>&1 &");
        return $this->getSettings([], $context);
    }
}
