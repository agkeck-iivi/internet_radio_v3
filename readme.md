# Internet radio v3

## hardware

* ESP32s3 DevkitC N16R8
* [adafruit pcm5122 audio board](https://learn.adafruit.com/adafruit-pcm5122-i2s-dac/)
*

### prototype

### header wiring

### schematic

### build

## software

### version 3 software highlights

Version 3 represents a significant refinement in performance, power management, and hardware integration. Key changes include:

* **Audio Hardware Migration**: Moved from the ES8388 codec to the high-performance **PCM5122 DAC**. This included implementing logarithmic volume scaling for a natural listening experience and custom digital ramp rates for glitch-free transitions.
* **Boot Speed Optimization (Fast Connect)**: Cold boot time was reduced from ~21 seconds to **~6 seconds** by implementing WiFi state persistence in NVS. The system now caches BSSID, channel, IP, and DNS settings to bypass discovery and DHCP phases.
* **Advanced Power Management**: Introduced a configurable power-saving strategy supporting **Light Sleep** and **Deep Sleep** transitions. This includes robust watchdog protection to prevent system hangs during sleep-state changes.
* **Dedicated IR Component**: Extracted Bose IR protocol logic into a reusable `ir_remote` component. The system now automatically synchronizes external audio power states during the device's boot and sleep cycles.
* **UI & Observability**: Refined the LVGL interface with a queue-based thread-safety pattern, real-time throughput tracking (kb/s), and custom graphical indicators (e.g., specialized mute glyphs).
* **Web Configuration**: Introduced a web interface for managing device settings (Analog/Digital attenuation, Power Save modes) with immediate runtime application.
* **NVS Configuration Persistence**: All application settings are now persisted to NVS using individual keys, ensuring backward compatibility and state retention across reboots.
* **Stability & Cleanup**: Factored all hardware assignments into a central GPIO header, implemented spurious encoder signal blanking on wakeup, and aligned the project with **ESP-IDF v5.4.3**.

### audio pipeline

The audio pipeline is virtually the same as in version 1.  We added an accumulator to count the bytes read from the http stream and a periodic task to calculate/update the bitrate display on the screen.  This task calculates a 10 second weighted average of one second bitrates.  When this weighted average is 0 we know that we have not received data for 10 seconds.  We use this signal along with a delay of 15 seconds to determine if we need to reboot the device.  If we have not received data for 10 seconds and we are at least 15 seconds since last boot we reboot the device.

### audio board

In Version 3, the radio migrated from the ES8388 (legacy LyraT design) to the high-performance **PCM5122 DAC** (Adafruit board).

The PCM5122 integration features:
* **Custom Driver**: A dedicated driver implementation that adheres to ESP-ADF conventions while providing low-level register control.
* **Analog Gain Control**: To manage the high output levels of the PCM5122, we implemented a 6 dB reduction in the analog gain stage (Page 1, Register 2).
* **Logarithmic Volume Scaling**: Volume settings (0-100) are mapped logarithmically to the DAC's digital volume registers to provide a natural, linear-sounding response to the user.
* **Attenuation Considerations**: Despite the 6 dB analog reduction, the output remains quite loud. Further digital or analog attenuation may be required in future revisions to better align the volume range with typical AUX-input sensitivities.

### encoders

The hardware pulse counters track the position of the encoders. This device has interupts for pulse thresholds but not for changes in pulse counts.  We use a polling task to check the pulse counts and update the encoder position.  For the volume encoder we clamp the value to the range [0, 100] and arrange the relationship between the pulse count and the [0, 100] range to saturate at the endpoints.  In this way, even if the user turns well past an endpoint the reverse movement will immediately affect the value.

### lvgl

LVGL is used for the display interface. Since LVGL is not thread-safe, we implement a **Queue-based Producer/Consumer pattern** to manage all UI updates safely.

#### Thread Safety & Queue

* **Producer**: Any task (e.g., Wi-Fi events, Encoder Logic) that wants to update the UI creates a `ui_update_message_t` struct. This struct contains an event `type` (enum) and optional `data` (int or string pointer). This message is sent to `g_ui_queue`.
* **Consumer**: The `process_ui_updates()` function runs in the main loop (or a dedicated UI task). It consumes messages from `g_ui_queue` and performs the actual LVGL API calls (e.g., `lv_label_set_text`, `lv_screen_load`). This ensures all LVGL operations happen in a single context.

#### Screens

The application uses three primary screens:

1. **Home Screen**: The main dashboard showing the Volume Slider, Station Call Sign, Origin, and Bitrate.
2. **Station Selection**: Displays a "Roller" widget allowing the user to scroll through the list of stations.
3. **Message Screen**: A generic, reusable screen with a centered label used for notifications (e.g., "Rebooting", "Provisioning", "IP Address").

#### Messaging

To display a message on the **Message Screen**:

1. **Fixed Messages**: Helper functions like `switch_to_reboot_screen()` send a message type (e.g., `SWITCH_TO_REBOOT_SCREEN`) that triggers the consumer to load the Message Screen and set a hardcoded string (e.g., "Rebooting").
2. **Arbitrary Messages**: To display dynamic text (like an IP address), the system uses the `UPDATE_IP_LABEL` message type.
    * The producer sends a message with `type = UPDATE_IP_LABEL` and `data.str_value = "My String"`.
    * The consumer reads specific string pointer and updates the label on the Message Screen.
    * *Note: Ensure the string pointer remains valid until processed, or use static/global buffers.*

### wifi

The device relies on Wi-Fi for internet connectivity. If no Wi-Fi credentials are provided (e.g., typically on the first boot), the device enters **Provisioning Mode**.

**Provisioning Mode**:

* The device broadcasts a Bluetooth LE (BLE) service.
* Use the **Espressif BLE Provisioning** app (available on iOS and Android) to connect to the device.
* Follow the app instructions to scan for Wi-Fi networks and provide the SSID and password.
* Once credentials are received, the device will connect to the Wi-Fi network and save the credentials to NVS for future boots.
* **Forced Reprovisioning**: To reset the Wi-Fi credentials and force the device back into provisioning mode, **press and hold the Volume Encoder button** while powering on (or rebooting) the device.

### ir

The Bose Wave radio uses an IR remote control for all functions. Version 2 is designed for this particular unit so we generate our own IR signals to control the radio.  After sniffing the IR signals with ir_nec_transciever project we see that the radio does not use the NEC protocal.  Using 10 samples of the on/off button and 10 samples of the aux button we create a concensus signal for each message. The colab: `bose_sig_analysis.ipynb` shows the analysis of the IR signals.

The bose IR protocal is stateful: the on/off button performs different functions based on the state of the radio.  The aux button is stateless, this signal will always result in the radio being on with input from the aux jack.  Therefore we use aux as the on signal and aux + on/off as the off signal.  We send aux during the boot sequence to ensure that the radio is on when we boot.  We expect the user to double click the volume encoder to turn the radio off.

### nvs

The system uses Non-Volatile Storage (NVS) to persist all critical states and settings:
* **Application Settings**: Analog and Digital attenuation, Power Save mode, sleep delays, and IR transmitter status.
* **Device State**: Current volume level, mute status, and the last selected station index.
* **WiFi State**: Cached BSSID, channel, and IP configuration for "Fast Connect" boot optimizations.

Settings are stored under the `app_config` namespace using individual keys for robust schema evolution.

### application configuration

The radio provides a web interface and a JSON API for real-time configuration. Access the interface at `http://<ESP32_IP_ADDRESS>/config`.

#### Configuration Parameters

| Parameter | Key | Values | Description |
| :--- | :--- | :--- | :--- |
| **Analog Attenuation** | `analog_attenuation` | `0` (0dB), `1` (-6dB) | Hardware gain stage control. |
| **Digital Attenuation** | `digital_attenuation` | `0, 6, 12, ..., 48` | Digital signal scaling after hardware gain. |
| **Power Save Mode** | `power_save_mode` | `0` (None), `1` (Light), `2` (Deep) | Sleep strategy selection. |
| **Light Sleep Delay** | `light_sleep_delay_ms` | Milliseconds | Timeout to enter Light Sleep. |
| **Deep Sleep Delay** | `deep_sleep_delay_ms` | Milliseconds | Additional timeout to move from Light to Deep Sleep. |
| **Enable IR Remote** | `ir_is_enabled` | `true`, `false` | Master toggle for the IR transmitter. |

#### API Access

* **GET `/api/config`**: Returns the current application configuration.
* **POST `/api/config`**: Updates the configuration immediately. Changes are persisted to NVS.

Example update with all parameters:
```bash
curl -X POST -H "Content-Type: application/json" \
     -d '{
       "analog_attenuation": 1,
       "digital_attenuation": 12,
       "power_save_mode": 2,
       "light_sleep_delay_ms": 30000,
       "deep_sleep_delay_ms": 60000,
       "ir_is_enabled": true
     }' \
     http://<ESP32_IP_ADDRESS>/api/config
```

Initial station data is stored in a constant array.  On first boot, if the spiffs is not initialized we create a default station json file based on the constant array.  Thereafter, on boot the spiffs should be found and the json file serves as the source of truth for station data.  We provide an API to load the station data from the json file and save the station data to the json file.

To download the current stations:

```bash
curl http://<ESP32_IP_ADDRESS>/api/stations -o stations.json
```

To update the stations:

```bash
curl -X POST -H "Content-Type: application/json" -d @stations.json http://<ESP32_IP_ADDRESS>/api/stations
```

where the codec enum uses these values:

0: MP3
1: AAC
2: OGG
3: FLAC

For help finding stream URIs and codecs for your favorite stations, see the [Station Discovery Guide](station_discovery.md).

### web update to station data

We provide a web interface to update the station data at <ESP32_IP_ADDRESS>/api/stations (or just <ESP_IP_ADDRESS> where there is a link to station data.)  From the web interface we can add, remove, and update station data as well a reorder the list of stations.  The station data is saved to the spiffs and a reboot will apply the changes.

## power management

The radio implements a multi-stage power-saving strategy to minimize energy consumption when idle. 

*Note: In local tests setting the router's default dns server to 8.8.8.8  (google dns) or 1.1.1.1 (cloudflare dns) results in faster wakeup times.*

### Power Requirements Table

The following table outlines the current draw and estimated annual energy costs for each operational state. Estimates assume 24/7 operation in the specified state and an electricity cost of **$0.15 per kWh**.

| State | Current (mA) | Power (W) | Annual Energy (kWh) | Annual Cost ($)* |
| :--- | :--- | :--- | :--- | :--- |
| **None (Active)** | 190 | 0.95 | 8.32 | $1.25 |
| **Light Sleep** | 18 | 0.09 | 0.79 | $0.12 |
| **Deep Sleep** | ~8 (est.)† | 0.04 | 0.35 | $0.05 |

*\*Costs are rounded to the nearest cent.*
*†Deep sleep current is dominated by the DevKitC's onboard LDO quiescent current (~5mA) and Power LED (~3mA), as the ESP32-S3 chip itself draws <100µA in this state.*

## operation

The radio's user interface is driven by two rotary encoders, each equipped with an integrated push button (switch).

### Control Summary Table

| Control | Action | Function |
| :--- | :--- | :--- |
| **Volume Encoder** | Rotation | Adjust volume (0-100); Auto-unmutes if turned |
| | Single Click | Toggle Mute/Unmute |
| | Double Click | Send **Bose ON/OFF** IR command |
| | **Hold at Boot** | **Force Reprovisioning** (Wipe Wi-Fi credentials) |
| **Station Encoder** | Rotation | Scroll through stations; Selects after 2s inactivity |
| | Short Press | Display **IP Address** (3 seconds) |
| | Long Press (>1.5s) | **Reboot** device |

---

### Detailed Usage

#### Volume Encoder (Left)

* **Rotation**: Sets the volume level between 0 and 100.
  * The volume is saved to NVS and persists across reboots.
  * If the device is muted, any rotation will automatically restore the previous volume plus or minus the rotation delta.
* **Single Click**: Toggles the mute state. Muting sets output to 0 while remembering the previous level for restoration.
* **Double Click**: Triggers the IR transmitter to send a power toggle command to the connected Bose system.
* **Boot Action**: Holding this button during power-on or reset will trigger `wifi_prov_mgr_reset_provisioning()`, allowing you to connect a new Wi-Fi network via BLE.

#### Station Encoder (Right)

* **Rotation**: Navigates the station list.
  * Turning the knob switches the display to the **Station Selection** screen.
  * **Auto-Selection**: Once you stop turning, the radio waits for **2 seconds** before committing the change, switching the stream, and returning to the Home screen.
* **Short Press**: Briefly switches the screen to show the current IP address. This is useful for accessing the web configuration interface.
* **Long Press**: Holding for more than 1.5 seconds triggers an immediate system reboot. A "Rebooting" message will appear on the display as confirmation.

## Bugs

* 5v power does not work perfectly. On first power on after a period off,
the system will not boot. If the 5v power is cycled once, it will work fine.
  * A 10 uF decoupling capacitor was added across the 5v header but that didn't solve the issue.
  * A 470 uF capacitor was added across the 5v header but that didn't solve the issue.
  * A 10 uF capacitor was added to rst pin to ground but that didn't solve the issue.
  * A 470 uF capacitor was added to rst pin to ground but that didn't solve the issue.

  * **Solved** by adding a 10K pullup resistor from GPIO 0 to 3.3v.  This causes the radio to reliably boot into stored application when running on external 5v supply.
