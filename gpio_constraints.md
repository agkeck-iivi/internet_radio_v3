# GPIO Constraints for ESP32-S3 N16R8 (Octal SPI)

This document outlines the GPIO pins on the ESP32-S3 N16R8 DevKitC that are either reserved, have special functions, or should be used with caution to avoid system instability.

## 1. Reserved for Internal Memory (Octal SPI)
The **N16R8** variant uses **Octal SPI** for both its 16MB Flash and 8MB PSRAM. This consumes a significant number of GPIOs that are typically available on standard Quad SPI modules.

> [!CAUTION]
> **DO NOT USE** the following pins. Using them will interfere with memory access and cause the system to crash or fail to boot.

| GPIO | Function |
| :--- | :--- |
| **26** | SPICU / PSRAM D4 |
| **27** | SPICD / PSRAM D5 |
| **28** | SPIWP / PSRAM D6 |
| **29** | SPIHD / PSRAM D7 |
| **30** | SPIIO4 / PSRAM D0 |
| **31** | SPIIO5 / PSRAM D1 |
| **32** | SPIIO6 / PSRAM D2 |
| **33** | SPIIO7 / PSRAM D3 |
| **34** | SPICS1 / PSRAM CS |
| **35** | SPICLK / Flash CLK |
| **36** | SPICS0 / Flash CS |
| **37** | SPIDQS / Memory DQS |

---

## 2. On-Board Hardware
The DevKitC has specific pins hard-wired to peripheral components.

| GPIO | Component | Note |
| :--- | :--- | :--- |
| **48** | **RGB LED** | Connected to the WS2812 RGB LED. Toggling this pin will change the LED color. |
| **43** | **UART0 TX** | Connected to the USB-to-UART bridge (TX). |
| **44** | **UART0 RX** | Connected to the USB-to-UART bridge (RX). |
| **19** | **USB D-** | Native USB D- line. |
| **20** | **USB D+** | Native USB D+ line. |

---

## 3. Strapping Pins
These pins are sampled during the boot process to determine the boot mode or voltage levels. While they can be used as general-purpose I/O after boot, they must not be held in the "wrong" state during power-on.

| GPIO | Name | Default | Impact |
| :--- | :--- | :--- | :--- |
| **0** | GPIO0 | Pull-up | Low at boot enters Download Mode. |
| **45** | VDD_SPI | Pull-down | **CRITICAL:** Sets Flash/PSRAM voltage. Leave floating or low. |
| **46** | GPIO46 | Pull-down | Must be Low at boot for normal operation. |

---

## 4. Hardware Debugging (JTAG)
These pins are used for on-chip debugging. If you are not using an external JTAG debugger, these can be used for other purposes.

| GPIO | JTAG Function |
| :--- | :--- |
| **39** | MTCK |
| **40** | MTDO |
| **41** | MTDI |
| **42** | MTMS |

---

## 5. ADC2 Constraints (Wi-Fi Conflict)
GPIOs **11 through 20** are connected to ADC2. On the ESP32-S3, ADC2 cannot be used for analog readings while the Wi-Fi or Bluetooth drivers are active.

> [!TIP]
> Use ADC1 pins (GPIO 1-10) for analog sensors if Wi-Fi is enabled.

---

## Summary of Safe Pins
For high-speed signals like I2S or SPI, the following pins are generally "safe" and unencumbered on the S3-N16R8:
*   **GPIO 1 - 10** (ADC1, free for all uses)
*   **GPIO 15 - 18** (If not using specific peripherals)
*   **GPIO 21**
*   **GPIO 38**
*   **GPIO 47**
