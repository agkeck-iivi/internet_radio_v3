#ifndef _GPIO_ASSIGNMENTS_H_
#define _GPIO_ASSIGNMENTS_H_

// ==========================================
// I2C BUS (Audio Codec)
// ==========================================
#define I2C_SDA_GPIO 39
#define I2C_SCL_GPIO 38

// ==========================================
// I2S BUS (PCM5122 Audio Codec)
// ==========================================
// #define I2S_MCK_GPIO              16

#define I2S_BCK_GPIO 20      // note that we can't use internal usb.
#define I2S_DATA_OUT_GPIO 21 // Connects to DIN on PCM5122
#define I2S_WS_LRCK_GPIO 47
#define I2S_DATA_IN_GPIO 4 // Unused by DAC, but defined

// ==========================================
// SPI BUS (SSD1306 Display)
// ==========================================
#define DISPLAY_SPI_SCLK_GPIO 7
#define DISPLAY_SPI_MOSI_GPIO 15
#define DISPLAY_SPI_RST_GPIO 16
#define DISPLAY_SPI_DC_GPIO 17
#define DISPLAY_SPI_CS_GPIO 18

// ==========================================
// ENCODERS (Rotary Push Buttons)
// ==========================================
#define VOLUME_ENCODER_A_GPIO 1
#define VOLUME_ENCODER_B_GPIO 2
#define VOLUME_ENCODER_PRESS_GPIO 42

#define STATION_ENCODER_A_GPIO 5
#define STATION_ENCODER_B_GPIO 4
#define STATION_ENCODER_PRESS_GPIO 6

// ==========================================
// IR REMOTE
// ==========================================
#define IR_TX_GPIO 8

#endif // _GPIO_ASSIGNMENTS_H_
