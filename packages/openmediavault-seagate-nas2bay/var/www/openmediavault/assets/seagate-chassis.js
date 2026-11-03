
(function () {
  "use strict";

  const I18N = {
    tr: {
      widgetTitle: "Donanım Özeti",
      frontView: "Ön Panel",
      rearView: "Arka Panel",
      bay1Title: "Disk Yuvası 1 (/dev/sda)",
      bay2Title: "Disk Yuvası 2 (/dev/sdb)",
      model: "Model",
      serial: "Seri No",
      capacity: "Kapasite",
      temperature: "Sıcaklık",
      smartStatus: "S.M.A.R.T.",
      raidVolume: "RAID",
      healthy: "İYİ",
      fanTitle: "Soğutma Fanı",
      fanSpeed: "Hız",
      fanMode: "Mod",
      modeAuto: "Otomatik",
      modeManual: "Manuel",
      cpuTemp: "CPU Sıcaklığı",
      thresholds: "Eşikler",
      frontUsbTitle: "Ön USB 3.0 Portu",
      rearUsbTitle: "Arka USB 3.0 Portu",
      usbConnected: "TAKILI",
      usbEmpty: "BOŞ",
      usbDeviceName: "Cihaz",
      usbNoDevice: "Yok",
      usbBlockDev: "Bölüm",
      usbCapacity: "Boyut",
      usbSpeed: "Hız",
      usbId: "VID:PID",
      ethTitle: "Ağ Arayüzü (eth0)",
      ethIp: "IPv4",
      ethMac: "MAC",
      ethSpeed: "Bağlantı",
      ethActivity: "Anlık Trafik",
      ethTotal: "Toplam RX / TX",
      ethPhyTemp: "PHY Sıcaklığı",
      powerTitle: "Sistem ve LED Durumu",
      sysUptime: "Çalışma Süresi",
      sysRam: "Bellek",
      sysLoad: "Yük Ortalaması",
      ledState: "Güç LED (B/K)",
      qBay1: "Disk 1 (/dev/sda)",
      qBay2: "Disk 2 (/dev/sdb)",
      qFan: "Kasa Fanı",
      qFrontUsb: "Ön USB",
      qRearUsb: "Arka USB",
      qEth: "eth0 Trafik"
    },
    en: {
      widgetTitle: "Hardware Overview",
      frontView: "Front Panel",
      rearView: "Rear Panel",
      bay1Title: "Drive Bay 1 (/dev/sda)",
      bay2Title: "Drive Bay 2 (/dev/sdb)",
      model: "Model",
      serial: "Serial",
      capacity: "Capacity",
      temperature: "Temperature",
      smartStatus: "S.M.A.R.T.",
      raidVolume: "RAID",
      healthy: "GOOD",
      fanTitle: "Cooling Fan",
      fanSpeed: "Speed",
      fanMode: "Mode",
      modeAuto: "Automatic",
      modeManual: "Manual",
      cpuTemp: "CPU Temp",
      thresholds: "Thresholds",
      frontUsbTitle: "Front USB 3.0 Port",
      rearUsbTitle: "Rear USB 3.0 Port",
      usbConnected: "ACTIVE",
      usbEmpty: "EMPTY",
      usbDeviceName: "Device",
      usbNoDevice: "None",
      usbBlockDev: "Device Node",
      usbCapacity: "Size",
      usbSpeed: "Speed",
      usbId: "VID:PID",
      ethTitle: "Network Interface (eth0)",
      ethIp: "IPv4",
      ethMac: "MAC",
      ethSpeed: "Link Speed",
      ethActivity: "Traffic Rate",
      ethTotal: "Total RX / TX",
      ethPhyTemp: "PHY Temp",
      powerTitle: "System & LED Status",
      sysUptime: "Uptime",
      sysRam: "Memory",
      sysLoad: "Load Average",
      ledState: "Power LED (W/R)",
      qBay1: "Disk 1 (/dev/sda)",
      qBay2: "Disk 2 (/dev/sdb)",
      qFan: "Chassis Fan",
      qFrontUsb: "Front USB",
      qRearUsb: "Rear USB",
      qEth: "eth0 Traffic"
    }
  };

  let state = {
    data: null,
    prevEth: null,
    rxRateStr: "0.0 KB/s",
    txRateStr: "0.0 KB/s",
    activeHotspot: null,
    fetching: false
  };

  function getLang() {
    const urlLoc = new URLSearchParams(window.location.search).get("locale") || "";
    const lsLoc = localStorage.getItem("locale") || "";
    const combined = (urlLoc + " " + lsLoc).toLowerCase();
    if (combined.includes("tr")) return "tr";
    if (combined.includes("en")) return "en";
    return (navigator.language || "").toLowerCase().includes("tr") ? "tr" : "en";
  }

  function t(key) {
    const lang = getLang();
    return (I18N[lang] && I18N[lang][key]) || I18N.en[key] || key;
  }

  function formatRate(bytesPerSec) {
    if (bytesPerSec >= 1048576) {
      return (bytesPerSec / 1048576).toFixed(2) + " MB/s";
    }
    return (bytesPerSec / 1024).toFixed(1) + " KB/s";
  }

  let tooltipEl = null;
  function ensureTooltip() {
    if (!tooltipEl) {
      tooltipEl = document.createElement("div");
      tooltipEl.className = "omv-hw-tooltip";
      document.body.appendChild(tooltipEl);
    }
    return tooltipEl;
  }

  function buildTooltipHtml(zone) {
    const d = state.data;
    if (!d) {
      return `<div class="omv-hw-tt-title"><span>...</span></div>`;
    }

    if (zone === "bay1" || zone === "bay2") {
      const disk = d.disks[zone];
      const title = zone === "bay1" ? t("bay1Title") : t("bay2Title");
      return `
        <div class="omv-hw-tt-title">
          <span>${title}</span>
          <span class="omv-hw-tt-status ok">${t("healthy")}</span>
        </div>
        <div class="omv-hw-tt-row"><span class="omv-hw-tt-key">${t("model")}</span><span class="omv-hw-tt-val">${disk.model}</span></div>
        <div class="omv-hw-tt-row"><span class="omv-hw-tt-key">${t("serial")}</span><span class="omv-hw-tt-val">${disk.serial}</span></div>
        <div class="omv-hw-tt-row"><span class="omv-hw-tt-key">${t("capacity")}</span><span class="omv-hw-tt-val">${disk.size}</span></div>
        <div class="omv-hw-tt-row"><span class="omv-hw-tt-key">${t("temperature")}</span><span class="omv-hw-tt-val">${disk.temp}</span></div>
        <div class="omv-hw-tt-row"><span class="omv-hw-tt-key">${t("smartStatus")}</span><span class="omv-hw-tt-val">${disk.smart}</span></div>
        <div class="omv-hw-tt-row"><span class="omv-hw-tt-key">${t("raidVolume")}</span><span class="omv-hw-tt-val">${disk.raid}</span></div>
      `;
    }

    if (zone === "fan") {
      const f = d.fan;
      const modeStr = f.mode === "auto" ? t("modeAuto") : t("modeManual");
      return `
        <div class="omv-hw-tt-title">
          <span>${t("fanTitle")}</span>
          <span class="omv-hw-tt-status info">${f.rpm} RPM</span>
        </div>
        <div class="omv-hw-tt-row"><span class="omv-hw-tt-key">${t("fanSpeed")}</span><span class="omv-hw-tt-val">${f.rpm} RPM (${f.pct}%)</span></div>
        <div class="omv-hw-tt-row"><span class="omv-hw-tt-key">${t("fanMode")}</span><span class="omv-hw-tt-val">${modeStr}</span></div>
        <div class="omv-hw-tt-row"><span class="omv-hw-tt-key">${t("cpuTemp")}</span><span class="omv-hw-tt-val">${f.cpu_temp} °C</span></div>
        <div class="omv-hw-tt-row"><span class="omv-hw-tt-key">${t("thresholds")}</span><span class="omv-hw-tt-val">${f.thresholds}</span></div>
      `;
    }

    if (zone === "front_usb" || zone === "rear_usb") {
      const u = zone === "front_usb" ? d.usb.front : d.usb.rear;
      const title = zone === "front_usb" ? t("frontUsbTitle") : t("rearUsbTitle");
      if (u.connected) {
        return `
          <div class="omv-hw-tt-title">
            <span>${title}</span>
            <span class="omv-hw-tt-status ok">${t("usbConnected")}</span>
          </div>
          <div class="omv-hw-tt-row"><span class="omv-hw-tt-key">${t("usbDeviceName")}</span><span class="omv-hw-tt-val">${u.name}</span></div>
          <div class="omv-hw-tt-row"><span class="omv-hw-tt-key">${t("usbBlockDev")}</span><span class="omv-hw-tt-val">${u.block_dev}</span></div>
          <div class="omv-hw-tt-row"><span class="omv-hw-tt-key">${t("usbCapacity")}</span><span class="omv-hw-tt-val">${u.capacity}</span></div>
          <div class="omv-hw-tt-row"><span class="omv-hw-tt-key">${t("usbSpeed")}</span><span class="omv-hw-tt-val">${u.speed}</span></div>
          <div class="omv-hw-tt-row"><span class="omv-hw-tt-key">${t("usbId")}</span><span class="omv-hw-tt-val">${u.usb_id}</span></div>
        `;
      } else {
        return `
          <div class="omv-hw-tt-title">
            <span>${title}</span>
            <span class="omv-hw-tt-status muted">${t("usbEmpty")}</span>
          </div>
          <div class="omv-hw-tt-row"><span class="omv-hw-tt-key">${t("usbDeviceName")}</span><span class="omv-hw-tt-val">${t("usbNoDevice")}</span></div>
          <div class="omv-hw-tt-row"><span class="omv-hw-tt-key">${t("usbSpeed")}</span><span class="omv-hw-tt-val">${u.speed}</span></div>
        `;
      }
    }

    if (zone === "eth") {
      const e = d.eth;
      return `
        <div class="omv-hw-tt-title">
          <span>${t("ethTitle")}</span>
          <span class="omv-hw-tt-status ok">${e.operstate}</span>
        </div>
        <div class="omv-hw-tt-row"><span class="omv-hw-tt-key">${t("ethIp")}</span><span class="omv-hw-tt-val">${e.ip}</span></div>
        <div class="omv-hw-tt-row"><span class="omv-hw-tt-key">${t("ethActivity")}</span><span class="omv-hw-tt-val">RX ${state.rxRateStr} / TX ${state.txRateStr}</span></div>
        <div class="omv-hw-tt-row"><span class="omv-hw-tt-key">${t("ethTotal")}</span><span class="omv-hw-tt-val">${e.rx_human} / ${e.tx_human}</span></div>
        <div class="omv-hw-tt-row"><span class="omv-hw-tt-key">${t("ethSpeed")}</span><span class="omv-hw-tt-val">${e.speed}</span></div>
        <div class="omv-hw-tt-row"><span class="omv-hw-tt-key">${t("ethMac")}</span><span class="omv-hw-tt-val">${e.mac}</span></div>
        <div class="omv-hw-tt-row"><span class="omv-hw-tt-key">${t("ethPhyTemp")}</span><span class="omv-hw-tt-val">${e.phy_temp}</span></div>
      `;
    }

    if (zone === "power") {
      const s = d.system;
      return `
        <div class="omv-hw-tt-title">
          <span>${t("powerTitle")}</span>
          <span class="omv-hw-tt-status info">${s.hostname}</span>
        </div>
        <div class="omv-hw-tt-row"><span class="omv-hw-tt-key">${t("sysUptime")}</span><span class="omv-hw-tt-val">${s.uptime}</span></div>
        <div class="omv-hw-tt-row"><span class="omv-hw-tt-key">${t("sysRam")}</span><span class="omv-hw-tt-val">${s.mem_used_mb} / ${s.mem_total_mb} MiB (${s.mem_pct}%)</span></div>
        <div class="omv-hw-tt-row"><span class="omv-hw-tt-key">${t("sysLoad")}</span><span class="omv-hw-tt-val">${s.load}</span></div>
        <div class="omv-hw-tt-row"><span class="omv-hw-tt-key">${t("ledState")}</span><span class="omv-hw-tt-val">${s.white_led} / ${s.red_led}</span></div>
      `;
    }
    return "";
  }

  function showTooltip(zone, evt) {
    state.activeHotspot = zone;
    const tt = ensureTooltip();
    tt.innerHTML = buildTooltipHtml(zone);
    tt.classList.add("visible");
    positionTooltip(evt);
  }

  function positionTooltip(evt) {
    if (!tooltipEl || !evt) return;
    const pad = 14;
    const rect = tooltipEl.getBoundingClientRect();
    let x = evt.clientX + pad;
    let y = evt.clientY + pad;
    if (x + rect.width > window.innerWidth - 10) {
      x = evt.clientX - rect.width - pad;
    }
    if (y + rect.height > window.innerHeight - 10) {
      y = evt.clientY - rect.height - pad;
    }
    tooltipEl.style.left = Math.max(8, x) + "px";
    tooltipEl.style.top = Math.max(8, y) + "px";
  }

  function hideTooltip() {
    state.activeHotspot = null;
    if (tooltipEl) {
      tooltipEl.classList.remove("visible");
    }
  }

  async function fetchVisualData() {
    if (state.fetching) return;
    state.fetching = true;
    try {
      const resp = await fetch("/rpc.php", {
        method: "POST",
        credentials: "same-origin",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify({
          service: "FanControl",
          method: "getVisualOverview",
          params: {},
          options: null
        })
      });
      if (!resp.ok) return;
      const json = await resp.json();
      if (json && json.response) {
        const cur = json.response;
        if (state.prevEth && cur.eth && cur.ts_ms > state.prevEth.ts_ms) {
          const dtSec = (cur.ts_ms - state.prevEth.ts_ms) / 1000.0;
          const dRx = Math.max(0, cur.eth.rx_bytes - state.prevEth.rx_bytes);
          const dTx = Math.max(0, cur.eth.tx_bytes - state.prevEth.tx_bytes);
          state.rxRateStr = formatRate(dRx / dtSec);
          state.txRateStr = formatRate(dTx / dtSec);
        }
        if (cur.eth) {
          state.prevEth = {
            ts_ms: cur.ts_ms,
            rx_bytes: cur.eth.rx_bytes,
            tx_bytes: cur.eth.tx_bytes
          };
        }
        state.data = cur;
        updateRenderedWidgets();
        if (state.activeHotspot && tooltipEl && tooltipEl.classList.contains("visible")) {
          tooltipEl.innerHTML = buildTooltipHtml(state.activeHotspot);
        }
      }
    } catch (e) {
      // ignore
    } finally {
      state.fetching = false;
    }
  }

  function createWidgetElement() {
    const wrapper = document.createElement("div");
    wrapper.className = "omv-hw-widget-wrapper";
    wrapper.id = "sg-interactive-chassis-widget";
    renderWidgetInner(wrapper);
    return wrapper;
  }

  function renderWidgetInner(container) {
    const d = state.data;
    const bay1Val = d ? `${d.disks.bay1.model} (${d.disks.bay1.temp})` : "--";
    const bay2Val = d ? `${d.disks.bay2.model} (${d.disks.bay2.temp})` : "--";
    const fanVal = d ? `${d.fan.rpm} RPM / CPU ${d.fan.cpu_temp}°C` : "--";
    const fUsbVal = d ? (d.usb.front.connected ? d.usb.front.capacity + " (" + d.usb.front.block_dev + ")" : t("usbNoDevice")) : "--";
    const rUsbVal = d ? (d.usb.rear.connected ? d.usb.rear.capacity + " (" + d.usb.rear.block_dev + ")" : t("usbNoDevice")) : "--";
    const ethVal = d ? `RX ${state.rxRateStr} / TX ${state.txRateStr}` : "--";

    container.innerHTML = `
      <div class="omv-hw-card">
        <div class="omv-hw-card-header">
          <div class="omv-hw-card-title">${t("widgetTitle")}</div>
        </div>
        <div class="omv-hw-card-content">
          <div class="omv-hw-chassis-grid">
            <div class="omv-hw-chassis-box">
              <div class="omv-hw-chassis-label">${t("frontView")}</div>
              <div class="omv-hw-img-wrap">
                <img src="assets/images/nas_front.png" alt="Front">
                <div class="omv-hw-hotspot" data-zone="power" style="left:17.5%; top:36.0%; width:8.5%; height:10.0%;"></div>
                <div class="omv-hw-hotspot" data-zone="power" style="left:17.2%; top:65.5%; width:10.0%; height:14.0%;"></div>
                <div class="omv-hw-hotspot" data-zone="front_usb" style="left:18.5%; top:81.5%; width:7.5%; height:10.5%;"></div>
                <div class="omv-hw-hotspot" data-zone="bay1" style="left:31.5%; top:23.0%; width:25.5%; height:68.0%;"></div>
                <div class="omv-hw-hotspot" data-zone="bay2" style="left:57.2%; top:23.0%; width:25.5%; height:68.0%;"></div>
              </div>
            </div>
            <div class="omv-hw-chassis-box">
              <div class="omv-hw-chassis-label">${t("rearView")}</div>
              <div class="omv-hw-img-wrap">
                <img src="assets/images/nas_rear.png" alt="Rear">
                <div class="omv-hw-hotspot" data-zone="fan" style="left:9.5%; top:25.0%; width:55.0%; height:49.0%;"></div>
                <div class="omv-hw-hotspot" data-zone="rear_usb" style="left:74.5%; top:42.5%; width:7.5%; height:9.8%;"></div>
                <div class="omv-hw-hotspot" data-zone="eth" style="left:70.0%; top:64.5%; width:12.0%; height:10.8%;"></div>
                <div class="omv-hw-hotspot" data-zone="power" style="left:72.0%; top:84.5%; width:8.5%; height:7.5%;"></div>
              </div>
            </div>
          </div>

          <div class="omv-hw-summary-grid">
            <div class="omv-hw-tile" data-zone="bay1">
              <div class="omv-hw-tile-label">${t("qBay1")}</div>
              <div class="omv-hw-tile-value">${bay1Val}</div>
            </div>
            <div class="omv-hw-tile" data-zone="bay2">
              <div class="omv-hw-tile-label">${t("qBay2")}</div>
              <div class="omv-hw-tile-value">${bay2Val}</div>
            </div>
            <div class="omv-hw-tile" data-zone="fan">
              <div class="omv-hw-tile-label">${t("qFan")}</div>
              <div class="omv-hw-tile-value">${fanVal}</div>
            </div>
            <div class="omv-hw-tile" data-zone="eth">
              <div class="omv-hw-tile-label">${t("qEth")}</div>
              <div class="omv-hw-tile-value">${ethVal}</div>
            </div>
            <div class="omv-hw-tile" data-zone="front_usb">
              <div class="omv-hw-tile-label">${t("qFrontUsb")}</div>
              <div class="omv-hw-tile-value">${fUsbVal}</div>
            </div>
            <div class="omv-hw-tile" data-zone="rear_usb">
              <div class="omv-hw-tile-label">${t("qRearUsb")}</div>
              <div class="omv-hw-tile-value">${rUsbVal}</div>
            </div>
          </div>
        </div>
      </div>
    `;

    container.querySelectorAll("[data-zone]").forEach((el) => {
      const zone = el.getAttribute("data-zone");
      el.addEventListener("mouseenter", (e) => showTooltip(zone, e));
      el.addEventListener("mousemove", (e) => positionTooltip(e));
      el.addEventListener("mouseleave", () => hideTooltip());
    });
  }

  function updateRenderedWidgets() {
    const el = document.getElementById("sg-interactive-chassis-widget");
    if (el) {
      renderWidgetInner(el);
    }
  }

  function ensureMounted() {
    const hash = window.location.hash || "";
    const isDashboard = hash.startsWith("#/dashboard");

    if (!isDashboard) {
      hideTooltip();
      return;
    }

    const marker = document.querySelector(".seagate-visual-widget-marker");
    if (marker) {
      const widgetCard = marker.closest("mat-card");
      if (widgetCard && !widgetCard.querySelector(".omv-hw-card")) {
        const wrapper = widgetCard.closest(".omv-dashboard-widget-container") || widgetCard;
        wrapper.innerHTML = "";
        wrapper.appendChild(createWidgetElement());
        fetchVisualData();
      }
      return;
    }

    if (document.getElementById("sg-interactive-chassis-widget")) {
      return;
    }

    // Mount directly inside OMV's .widgets-container column grid so it matches all other OMV widgets!
    const widgetsGrid = document.querySelector("omv-dashboard-page .widgets-container");
    if (widgetsGrid) {
      widgetsGrid.insertBefore(createWidgetElement(), widgetsGrid.firstChild);
      fetchVisualData();
    }
  }

  setInterval(ensureMounted, 500);
  setInterval(() => {
    if (document.getElementById("sg-interactive-chassis-widget")) {
      fetchVisualData();
    }
  }, 4000);

  window.addEventListener("hashchange", () => {
    setTimeout(ensureMounted, 150);
  });
})();
