# GW018-DM local Zigbee coordinator firmware

This is a source fork of Jasper’s [GW018-DM AmebaD firmware project](https://github.com/jasperw1996/ambd_sdk_GW018-DM), which adapts the Seeed/Realtek AmebaD SDK for the Tuya GW018-DM WBRG1 (RTL8721CSM). It retains the full upstream SDK tree and adds a button-operated Wi-Fi setup portal, bounded Wi-Fi diagnostics, a Home Assistant OS log collector, and build/test tooling. The fork remains compatible with the separate ZS3L Zigbee processor; it does not replace that processor’s firmware.

The original gateway use in Jasper’s README is `tcp://<gateway-ip>:80` with Zigbee2MQTT `adapter: ezsp`. This project adds Wi-Fi provisioning and diagnostics around that bridge. See [Jasper’s upstream README](https://github.com/jasperw1996/ambd_sdk_GW018-DM/blob/dev/README.md) and the upstream notices kept in this tree for its lineage and hardware-specific background.

## Build host

The tested build host is Linux x86-64 (Debian/Ubuntu). Install the host tools:

```sh
sudo apt update
sudo apt install build-essential git curl tar python3 libc6-i386
```

The vendor ARM compiler is a 32-bit Linux executable, so `libc6-i386` is required on 64-bit Debian/Ubuntu. The build script downloads the approximately 250 MB compiler archive from Seeed over HTTPS and verifies its SHA-256. Reserve at least 10 GB free disk space for the SDK, compiler, and build products.

## Configure and build

Clone this fork and configure the OTA server host reachable by the gateway. This value is compiled into the WBRG1 OTA client; credentials are never needed by the build.

```sh
git clone https://github.com/bondesio/ambd_sdk_GW018-DM.git
cd ambd_sdk_GW018-DM
python3 tools/configure-ota-host.py gateway-updates.example.com
tools/build.sh
```

Replace the example host with a hostname or IPv4 address reachable by your gateway. It must serve `OTA_All.bin` on TCP port 8080 using the OTA client’s expected path. The helper writes the host to an ignored local header, so a site-specific hostname is not part of the source commit. The build helper fails while the documentation placeholder `ota.example.invalid` remains configured. Build products are written under the SDK’s normal `asdk/image` paths:

- `project/realtek_amebaD_va0_example/GCC-RELEASE/project_lp/asdk/image/km0_boot_all.bin`
- `project/realtek_amebaD_va0_example/GCC-RELEASE/project_hp/asdk/image/km4_boot_all.bin`
- `project/realtek_amebaD_va0_example/GCC-RELEASE/project_hp/asdk/image/km0_km4_image2.bin`
- `project/realtek_amebaD_va0_example/GCC-RELEASE/project_hp/asdk/image/OTA_All.bin`

The OTA image targets only the WBRG1 network processor. It is not a ZS3L Zigbee firmware image.

## First flash and recovery

The gateway can be flashed from a Raspberry Pi used as the Linux host. The complete procedure, including the Pi-specific uploader/serial-port caveat, is in [docs/flashing-from-a-raspberry-pi.md](docs/flashing-from-a-raspberry-pi.md).

For first installation or recovery, use a 3.3 V USB-to-TTL UART adapter and the Realtek/AmebaD ImageTool for Linux. The ImageTool is not included here; obtain `upload_image_tool_linux` and its support files from the [official AmebaD Arduino tool package](https://github.com/Ameba-AIoT/ameba-arduino-d/tree/master/Arduino_package/ameba_d_tools_linux) and review its instructions/notices. Stage the three KM0/KM4 images above next to `imgtool_flashloader_amebad.bin` in the ImageTool directory.

Connect ground to ground, gateway TX to adapter RX, and gateway RX to adapter TX on the board’s documented P1 header. Do not connect adapter VCC; power the gateway normally over USB-C. Confirm the exact board pinout and 3.3 V levels before connecting anything. With the gateway powered normally, connect the USB-UART adapter to enter UART download mode. Jasper documents these Linux commands for an RTL8721CSM target (change the serial device if needed):

```sh
./upload_image_tool_linux "$PWD" /dev/ttyUSB0 ameba_rtl8721csm Enable Enable 921600
./upload_image_tool_linux "$PWD" /dev/ttyUSB0 ameba_rtl8721csm Enable Disable 921600
```

The first command prepares/erases the target; the second writes the three images. Keep a known-good backup and do not interrupt power during flashing. See Jasper’s upstream instructions and the included [Realtek disclaimer](Realtek_Disclaimer-2019.pdf).

After a working custom firmware is installed, updates can use its OTA path: serve the newly built `OTA_All.bin` from the configured host on TCP port 8080, then hold the gateway button for at least three seconds while the gateway is connected to Wi-Fi. UART recovery may still be needed if an image is incorrect or incomplete.

## Wi-Fi portal and diagnostics

- A short button press opens or closes the `GW018-Setup` access point.
- Connect to it and browse to `http://192.168.43.1/` if the setup page does not open automatically.
- Wi-Fi credentials are submitted at runtime and are not compiled into the source.
- The setup page uses plain HTTP on the local setup network. Use it while physically present; a nearby client on that network could observe submitted credentials.
- The saved profile is updated only after Wi-Fi association and DHCP succeed; on failure the previous profile is retained.
- A long button hold (at least three seconds) starts OTA update.
- The WBRG1 can stream a bounded set of structured network and Zigbee-bridge health records on TCP port 81. See [docs/wifi-diagnostics.md](docs/wifi-diagnostics.md) for event scope and the Home Assistant OS collector.

The diagnostics stream is unauthenticated plain TCP and belongs on a trusted local network. It does not include Wi-Fi credentials, Zigbee payloads, arbitrary console output, or ROM/KM0 boot logs. Configure the HAOS add-on `host` option with the gateway’s station/LAN address before starting it. Collected rotating files are stored under `/config/logs/` on persistent Home Assistant configuration storage.

## Regression tests

From the repository root, run the source-level fixtures:

```sh
bash tests/bridge_health/run.sh
bash tests/gateway_diag/run.sh
bash tests/lwip_diag/run.sh
python3 -m unittest discover -s tests/collector -v
```

## Source and licensing

This repository is a GitHub fork of Jasper’s GW018-DM SDK adaptation. The upstream GitHub fork relationship is retained so its source history and attribution are visible. Keep the upstream license and notice files with the upstream files. [`LICENSES/PORTAL_OVERLAY_MIT.txt`](LICENSES/PORTAL_OVERLAY_MIT.txt) applies only to the original portal, diagnostics, collector, tests, and project tooling added here; it does not relicense the upstream SDK or third-party components. See [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) and the upstream notices before redistributing binaries or SDK-derived files.
