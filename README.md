# Bambu P1S Monitor — XIAO ESP32-C3 captive-style setup portal (prototype)

This is source code for a **test firmware**, not a precompiled browser-flashable `.bin`. It has not yet been compiled or validated on physical hardware.

## Features
- First boot / failed Wi-Fi connection: ESP32 creates **Bambu-Monitor-Setup** Wi-Fi access point.
- Join with password **BambuSetup2026**, then visit **http://192.168.4.1/**.
- Enter Wi-Fi SSID/password, P1S LAN IP, P1S serial, LAN access code, printer name, and Thingino camera MJPEG URL.
- Saves settings in ESP32 NVS (Preferences), reboots, and serves the dashboard from compiled flash; no LittleFS upload needed.
- To edit settings later visit `http://ESP32_IP/setup` on the LAN.
- If Wi-Fi fails during boot, setup AP is offered again. No DNS hijacking/captive-portal auto-popup is implemented; enter the IP manually.

## One-click browser installer (after publishing to GitHub Pages)

This package includes a browser installer website in `site/` and a GitHub Actions workflow to build and publish the firmware. **The ZIP contains source, not a compiled binary.** GitHub Actions must succeed before the installer works.

1. Create a GitHub repository (public is simplest for Pages on GitHub Free). Upload the **contents** of this project, keeping `.github/workflows/build.yml` and `site/` intact. Do not put the project inside another nested folder.
2. In GitHub, open **Settings → Pages → Build and deployment → Source: GitHub Actions**.
3. Open **Actions → Build and publish browser installer → Run workflow** (or push to `main`). Wait until the build and deployment both succeed. If a build fails, check the Actions log rather than attempting to flash an incomplete build.
4. Your installer will be at `https://YOUR_GITHUB_USERNAME.github.io/YOUR_REPOSITORY/` (GitHub Pages also displays the exact URL).
5. In **desktop Chrome or Edge**, connect the XIAO ESP32-C3 by USB-C, open that URL and select **Connect & Install**.
6. On first boot, join Wi-Fi **Bambu-Monitor-Setup** (password `BambuSetup2026`) and browse to **http://192.168.4.1/** to configure it.

The installer uses ESP Web Tools and a single merged binary at flash offset `0x0`, built automatically from the ESP32-C3 bootloader, partitions, and application. Flashing erases/replaces firmware and may erase prior configuration. The manifest targets **ESP32-C3 only**; do not use this build on the Waveshare ESP32-S3.

To test the installer HTML locally without firmware, open `site/index.html`; flashing will not work until GitHub Actions has generated and published `site/firmware-merged.bin` over HTTPS.

Alternative: use PlatformIO CLI (`pio run -e seeed_xiao_esp32c3`).

## Requirements / caveats
- Bambu P1S must have LAN MQTT available. MQTT uses TLS 8883 and username `bblp` with printer LAN access code. TLS certificate is not verified (LAN-only trust model).
- The ESP32 **does** send a `pushall` status request, but does **not** send print/control commands.
- Thingino camera URL example: `http://192.168.1.50/x/video.mjpg`. Camera streams **directly to the browser**, not through the ESP32. Safari camera authentication may need a separate camera login.
- Use HTTP over your trusted LAN / WireGuard; **do not port-forward** the dashboard. `/setup` is not authenticated and local users can change its configuration. The fixed setup AP password should be changed before production use.
- Bambu MQTT status messages can exceed the 16 KB PubSubClient buffer. This prototype may drop larger messages or run low on heap on C3. Monitor serial output and `/api/status` diagnostics. ESP32-S3 with PSRAM is the intended permanent target.
- The setup AP is **not** a fully automatic captive portal; browse to `192.168.4.1` explicitly.
- A future production version should use a unique setup password, configuration authentication, and verified TLS where feasible.
