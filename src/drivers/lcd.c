/*
 * Copyright (c) 2024, @momentics <momentics@gmail.com>
 * Based on Dmitry (DiSlord) dislordlive@gmail.com
 * All rights reserved.
 *
 * This is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3, or (at your option)
 * any later version.
 *
 * The software is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with GNU Radio; see the file COPYING.  If not, write to
 * the Free Software Foundation, Inc., 51 Franklin Street,
 * Boston, MA 02110-1301, USA.
 */
#include "ch.h"
#include "hal.h"
#include "nanovna.h"
#include "app/shell.h"
#include "chprintf.h"
#include "spi.h"

// Pin macros for LCD
#define LCD_CS_LOW palClearPad(GPIOB, GPIOB_LCD_CS)
#define LCD_CS_HIGH palSetPad(GPIOB, GPIOB_LCD_CS)
#define LCD_RESET_ASSERT palClearPad(GPIOA, GPIOA_LCD_RESET)
#define LCD_RESET_NEGATE palSetPad(GPIOA, GPIOA_LCD_RESET)
#define LCD_DC_CMD palClearPad(GPIOB, GPIOB_LCD_CD)
#define LCD_DC_DATA palSetPad(GPIOB, GPIOB_LCD_CD)

// SPI bus for LCD
#define LCD_SPI SPI1
#ifdef __USE_DISPLAY_DMA__
// DMA channels for used in LCD SPI bus
#define LCD_DMA_RX DMA1_Channel2 // DMA1 channel 2 use for SPI1 rx
#define LCD_DMA_TX DMA1_Channel3 // DMA1 channel 3 use for SPI1 tx
#endif

// Custom display definition
#if defined(LCD_DRIVER_ILI9341) || defined(LCD_DRIVER_ST7789)
// Set SPI bus speed for LCD
#define LCD_SPI_SPEED SPI_BR_DIV2
// Read speed, need more slow, not define if need use some as Tx speed
#define ILI9341_SPI_RX_SPEED SPI_BR_DIV2
// Read speed, need more slow, not define if need use some as Tx speed
#define ST7789V_SPI_RX_SPEED SPI_BR_DIV8
// Allow enable DMA for read display data (can not stable on full speed, on less speed slower)
#define __USE_DISPLAY_DMA_RX__
#elif defined(LCD_DRIVER_ST7796S)
// Set SPI bus speed for LCD
#define LCD_SPI_SPEED SPI_BR_DIV2
// Read speed, need more slow, not define if need use some as Tx speed
#define LCD_SPI_RX_SPEED SPI_BR_DIV4
// Allow enable DMA for read display data
#define __USE_DISPLAY_DMA_RX__
#endif

// Disable DMA rx on disabled DMA tx
#ifndef __USE_DISPLAY_DMA__
#undef __USE_DISPLAY_DMA_RX__
#endif

// LCD display buffer
alignas(4) pixel_t spi_buffer[SPI_BUFFER_SIZE];
// Default foreground & background colors
pixel_t foreground_color = 0;
pixel_t background_color = 0;

//*****************************************************
// SPI functions, settings and data
//*****************************************************
void spi_tx_byte(const uint8_t data) {
  while (SPI_TX_IS_NOT_EMPTY(LCD_SPI))
    ;
  SPI_WRITE_8BIT(LCD_SPI, data);
}
// Transmit buffer to SPI bus  (len should be > 0)
void spi_tx_buffer(const uint8_t* buffer, uint16_t len) {
  while (len--) {
    while (SPI_TX_IS_NOT_EMPTY(LCD_SPI))
      ;
    SPI_WRITE_8BIT(LCD_SPI, *buffer++);
  }
}

// Receive byte from SPI bus
uint8_t spi_rx_byte(void) {
  // Start RX clock (by sending data)
  SPI_WRITE_8BIT(LCD_SPI, 0xFF);
  while (SPI_RX_IS_EMPTY(LCD_SPI))
    ;
  return SPI_READ_8BIT(LCD_SPI);
}

// Receive buffer from SPI bus (len should be > 0)
void spi_rx_buffer(uint8_t* buffer, uint16_t len) {
  do {
    SPI_WRITE_8BIT(LCD_SPI, 0xFF);
    while (SPI_RX_IS_EMPTY(LCD_SPI))
      ;
    *buffer++ = SPI_READ_8BIT(LCD_SPI);
  } while (--len);
}

void spi_drop_rx(void) {
  // Drop Rx buffer after tx and wait tx complete
#if 1
  while (SPI_RX_IS_NOT_EMPTY(LCD_SPI) || SPI_IS_BUSY(LCD_SPI))
    (void)SPI_READ_8BIT(LCD_SPI);
  (void)SPI_READ_8BIT(LCD_SPI);
#else
  while (SPI_IS_BUSY(LCD_SPI))
    ;
  (void)SPI_READ_16BIT(LCD_SPI);
  (void)SPI_READ_16BIT(LCD_SPI);
#endif
}

//*****************************************************
// SPI DMA settings and data
//*****************************************************
#ifdef __USE_DISPLAY_DMA__
static const uint32_t txdmamode = 0 | STM32_DMA_CR_PL(STM32_SPI_SPI1_DMA_PRIORITY) // Set priority
                                  | STM32_DMA_CR_DIR_M2P;                          // Memory to Spi

static const uint32_t rxdmamode = 0 | STM32_DMA_CR_PL(STM32_SPI_SPI1_DMA_PRIORITY) // Set priority
                                  | STM32_DMA_CR_DIR_P2M;                          // SPI to Memory

// SPI transmit byte buffer use DMA (65535 bytes limit)
static inline void spi_dma_tx_buffer(const uint8_t* buffer, uint16_t len, bool wait) {
  dmaChannelSetMemory(LCD_DMA_TX, buffer);
  dmaChannelSetTransactionSize(LCD_DMA_TX, len);
  dmaChannelSetMode(LCD_DMA_TX,
                    txdmamode | STM32_DMA_CR_BYTE | STM32_DMA_CR_MINC | STM32_DMA_CR_EN);
  if (wait)
    dmaChannelWaitCompletion(LCD_DMA_TX);
}

// Wait DMA Rx completion
static void dma_channel_wait_completion_rx_tx(void) {
  dmaChannelWaitCompletion(LCD_DMA_TX);
  dmaChannelWaitCompletion(LCD_DMA_RX);
  //  while (SPI_IS_BUSY(LCD_SPI));   // Wait SPI tx/rx
}

// SPI receive byte buffer use DMA
static const uint16_t dummy_tx = 0xFFFF;
static inline void spi_dma_rx_buffer(uint8_t* buffer, uint16_t len, bool wait) {
  // Init Rx DMA buffer, size, mode (spi and mem data size is 8 bit), and start
  dmaChannelSetMemory(LCD_DMA_RX, buffer);
  dmaChannelSetTransactionSize(LCD_DMA_RX, len);
  dmaChannelSetMode(LCD_DMA_RX,
                    rxdmamode | STM32_DMA_CR_BYTE | STM32_DMA_CR_MINC | STM32_DMA_CR_EN);
  // Init dummy Tx DMA (for rx clock), size, mode (spi and mem data size is 8 bit), and start
  dmaChannelSetMemory(LCD_DMA_TX, &dummy_tx);
  dmaChannelSetTransactionSize(LCD_DMA_TX, len);
  dmaChannelSetMode(LCD_DMA_TX, txdmamode | STM32_DMA_CR_BYTE | STM32_DMA_CR_EN);
  if (wait)
    dma_channel_wait_completion_rx_tx();
}
#else
// Replace DMA function vs no DMA
#define dma_channel_wait_completion_rx_tx()                                                        \
  {                                                                                                \
  }
#define spi_dma_tx_buffer(buffer, len, flag) spi_tx_buffer(buffer, len)
#define spi_dma_rx_buffer(buffer, len, flag) spi_rx_buffer(buffer, len)
#endif // __USE_DISPLAY_DMA__

static void spi_init(void) {
  rccEnableSPI1(FALSE);
  LCD_SPI->CR1 = 0;
  LCD_SPI->CR1 = SPI_CR1_MSTR  // SPI is MASTER
                 | SPI_CR1_SSM // Software slave management (The external NSS pin is free for other
                               // application uses)
                 | SPI_CR1_SSI // Internal slave select (This bit has an effect only when the SSM
                               // bit is set. Allow use NSS pin as I/O)
                 | LCD_SPI_SPEED // Baud rate control
                 | SPI_CR1_CPHA  // Clock Phase
                 | SPI_CR1_CPOL  // Clock Polarity
      ;
  LCD_SPI->CR2 = SPI_CR2_8BIT    // SPI data size, set to 8 bit
                 | SPI_CR2_FRXTH // SPI_SR_RXNE generated every 8 bit data
//             | SPI_CR2_SSOE      //
#ifdef __USE_DISPLAY_DMA__
                 | SPI_CR2_TXDMAEN // Tx DMA enable
#ifdef __USE_DISPLAY_DMA_RX__
                 | SPI_CR2_RXDMAEN // Rx DMA enable
#endif
#endif
      ;
// Init SPI DMA Peripheral
#ifdef __USE_DISPLAY_DMA__
  dmaChannelSetPeripheral(LCD_DMA_TX, &LCD_SPI->DR); // DMA Peripheral Tx
#ifdef __USE_DISPLAY_DMA_RX__
  dmaChannelSetPeripheral(LCD_DMA_RX, &LCD_SPI->DR); // DMA Peripheral Rx
#endif
#endif
  // Enable DMA on SPI
  LCD_SPI->CR1 |= SPI_CR1_SPE; // SPI enable
}

//******************************************************************************
//           All LCD (ILI9341, ST7789V, ST9996s) level 1 commands
//******************************************************************************
#define LCD_NOP 0x00        // No operation
#define LCD_SWRESET 0x01    // Software reset
#define LCD_RDDID 0x04      // Read display ID
#define LCD_RDNUMED 0x05    // Read Number of the Errors on DSI (only ST7796s)
#define LCD_RDDST 0x09      // Read display status
#define LCD_RDDPM 0x0A      // Read Display Power Mode
#define LCD_RDD_MADCTL 0x0B // Read Display MADCTL
#define LCD_RDDCOLMOD 0x0C  // Read Display Pixel Format
#define LCD_RDDIM 0x0D      // Read Display Image Mode
#define LCD_RDDSM 0x0E      // Read Display Signal Mode
#define LCD_RDDSDR 0x0F     // Read Display Self-Diagnostic Result
#define LCD_SLPIN 0x10      // Sleep in
#define LCD_SLPOUT 0x11     // Sleep Out
#define LCD_PTLON 0x12      // Partial Display Mode On
#define LCD_NORON 0x13      // Normal Display Mode On
#define LCD_INVOFF 0x20     // Display Inversion Off
#define LCD_INVON 0x21      // Display Inversion On
#define LCD_GAMSET 0x26     // Gamma Set (only ILI9341 and ST7789V)
#define LCD_DISPOFF 0x28    // Display Off
#define LCD_DISPON 0x29     // Display On
#define LCD_CASET 0x2A      // Column Address Set
#define LCD_RASET 0x2B      // Row Address Set
#define LCD_RAMWR 0x2C      // Memory Write
#define LCD_RGBSET 0x2D     // Color Set  (only ILI9341)
#define LCD_RAMRD 0x2E      // Memory Read
#define LCD_PTLAR 0x30      // Partial Area
#define LCD_VSCRDEF 0x33    // Vertical Scrolling Definition
#define LCD_TEOFF 0x34      // Tearing Effect Line OFF
#define LCD_TEON 0x35       // Tearing Effect Line On
#define LCD_MADCTL 0x36     // Memory Data Access Control
#define LCD_VSCSAD 0x37     // Vertical Scroll Start Address of RAM
#define LCD_IDMOFF 0x38     // Idle Mode Off
#define LCD_IDMON 0x39      // Idle mode on
#define LCD_COLMOD 0x3A     // Interface Pixel Format
#define LCD_WRMEMC 0x3C     // Write_Memory_Continue (only ILI9341)
#define LCD_RDMEMC 0x3E     // Read Memory Continue
#define LCD_STE 0x44        // Set Tear Scanline
#define LCD_GSCAN 0x45      // Get Scanline
#define LCD_WRDISBV 0x51    // Write Display Brightness
#define LCD_RDDISBV 0x52    // Read Display Brightness Value
#define LCD_WRCTRLD 0x53    // Write CTRL Display
#define LCD_RDCTRLD 0x54    // Read CTRL Value Display
#define LCD_WRCACE 0x55     // Write Content Adaptive Brightness Control and Color Enhancement
#define LCD_RDCABC 0x56     // Read Content Adaptive Brightness Control
#define LCD_WRCABCMB 0x5E   // Write CABC Minimum Brightness
#define LCD_RDCABCMB 0x5F   // Read CABC Minimum Brightness
#define LCD_RDID1 0xDA      // Read ID1
#define LCD_RDID2 0xDB      // Read ID2
#define LCD_RDID3 0xDC      // Read ID3

// MEMORY_ACCESS_CONTROL register
#define LCD_MADCTL_MH 0x04
#define LCD_MADCTL_BGR 0x08
#define LCD_MADCTL_RGB 0x00
#define LCD_MADCTL_ML 0x10
#define LCD_MADCTL_MV 0x20
#define LCD_MADCTL_MX 0x40
#define LCD_MADCTL_MY 0x80
// Display rotation enum
enum {
  DISPLAY_ROTATION_0 = 0,
  DISPLAY_ROTATION_90,
  DISPLAY_ROTATION_180,
  DISPLAY_ROTATION_270,
};

//******************************************************************************
// Custom ILI9391 level 2 commands
//******************************************************************************
#define ILI9341_IFMODE 0xB0    // RGB Interface Signal Control
#define ILI9341_FRMCTR1 0xB1   // Frame Rate Control (In Normal Mode/Full Colors)
#define ILI9341_FRMCTR2 0xB2   // Frame Rate Control (In Idle Mode/8 colors)
#define ILI9341_FRMCTR3 0xB3   // Frame Rate control (In Partial Mode/Full Colors)
#define ILI9341_INVTR 0xB4     // Display Inversion Control
#define ILI9341_PRCTR 0xB5     // Blanking Porch Control
#define ILI9341_DISCTRL 0xB6   // Display Function Control
#define ILI9341_ETMOD 0xB7     // Entry Mode Set
#define ILI9341_BKLTCTRL1 0xB8 // Backlight Control 1
#define ILI9341_BKLTCTRL2 0xB9 // Backlight Control 2
#define ILI9341_BKLTCTRL3 0xBA // Backlight Control 3
#define ILI9341_BKLTCTRL4 0xBB // Backlight Control 4
#define ILI9341_BKLTCTRL5 0xBC // Backlight Control 5
#define ILI9341_BKLTCTRL7 0xBE // Backlight Control 7
#define ILI9341_BKLTCTRL8 0xBF // Backlight Control 8
#define ILI9341_PWCTRL1 0xC0   // Power Control 1
#define ILI9341_PWCTRL2 0xC1   // Power Control 2
#define ILI9341_VMCTRL1 0xC5   // VCOM Control 1
#define ILI9341_VMCTRL2 0xC7   // VCOM Control 2
#define ILI9341_NVMWR 0xD0     // NV Memory Write
#define ILI9341_NVMPKEY 0xD1   // NV Memory Protection Key
#define ILI9341_RDNVM 0xD2     // NV Memory Status Read
#define ILI9341_RDID4 0xD3     // Read ID4
#define ILI9341_PGAMCTRL 0xE0  // Positive Gamma Correction
#define ILI9341_NGAMCTRL 0xE1  // Negative Gamma Correction
#define ILI9341_DGAMCTRL1 0xE2 // Digital Gamma Control 1
#define ILI9341_DGAMCTRL2 0xE3 // Digital Gamma Control 2
#define ILI9341_IFCTL 0xF6     // Interface Control
// Extend register commands
#define ILI9341_POWERA 0xCB    // Power control A
#define ILI9341_POWERB 0xCF    // Power control B
#define ILI9341_DTCA 0xE8      // Driver timing control A
#define ILI9341_DTCB 0xEA      // Driver timing control B
#define ILI9341_POWER_SEQ 0xED // Power on sequence control
#define ILI9341_3GAMMA_EN 0xF2 // Enable 3G
#define ILI9341_PUMPCTRL 0xF7  // Pump ratio control

//******************************************************************************
// Custom ST7789V level 2 commands
//******************************************************************************
#define ST7789V_RAMCTRL 0xB0    // RAM Control
#define ST7789V_RGBCTRL 0xB1    // RGB Interface Control
#define ST7789V_PORCTRL 0xB2    // Porch Setting
#define ST7789V_FRCTRL1 0xB3    // Frame Rate Control 1 (In partial mode/ idle colors)
#define ST7789V_INVTR 0xB4      // Display Inversion Control (only ILI9341)
#define ST7789V_PARCTRL 0xB5    // Partial Control
#define ST7789V_GCTRL 0xB7      // Gate Control
#define ST7789V_GTADJ 0xB8      // Gate On Timing Adjustment
#define ST7789V_DGMEN 0xBA      // Digital Gamma Enable
#define ST7789V_VCOMS 0xBB      // VCOM Setting
#define ST7789V_POWSAVE 0xBC    // Power Saving Mode
#define ST7789V_DLPOFFSAVE 0xBD // Display off power save
#define ST7789V_LCMCTRL 0xC0    // LCM Control
#define ST7789V_IDSET 0xC1      // ID Code Setting
#define ST7789V_VDVVRHEN 0xC2   // VDV and VRH Command Enable
#define ST7789V_VRHS 0xC3       // VRH Set
#define ST7789V_VDVS 0xC4       // VDV Set
#define ST7789V_VCMOFSET 0xC5   // VCOM Offset Set
#define ST7789V_FRCTRL2 0xC6    // Frame Rate Control in Normal Mode
#define ST7789V_CABCCTRL 0xC7   // CABC Control
#define ST7789V_REGSEL1 0xC8    // Register Value Selection 1
#define ST7789V_REGSEL2 0xCA    // Register Value Selection 2
#define ST7789V_PWMFRSEL 0xCC   // PWM Frequency Selection
#define ST7789V_PWCTRL1 0xD0    // Power Control 1
#define ST7789V_VAPVANEN 0xD2   // Enable VAP/VAN signal output
#define ST7789V_CMD2EN 0xDF     // Command 2 Enable
#define ST7789V_PVGAMCTRL 0xE0  // Positive Voltage Gamma Control
#define ST7789V_NVGAMCTRL 0xE1  // Negative Voltage Gamma Control
#define ST7789V_DGMLUTR 0xE2    // Digital Gamma Look-up Table for Red
#define ST7789V_DGMLUTB 0xE3    // Digital Gamma Look-up Table for Blue
#define ST7789V_GATECTRL 0xE4   // Gate Control
#define ST7789V_SPI2EN 0xE7     // SPI2 Enable
#define ST7789V_PWCTRL2 0xE8    // Power Control 2
#define ST7789V_EQCTRL 0xE9     // Equalize time control
#define ST7789V_PROMCTRL 0xEC   // Program Mode Control
#define ST7789V_PROMEN 0xFA     // Program Mode Enable
#define ST7789V_NVMSET 0xFC     // NVM Setting
#define ST7789V_PROMACT 0xFE    // Program action

//******************************************************************************
// Custom ST7796s level 2 commands
//******************************************************************************
#define ST7796S_IFMODE 0xB0    // Interface Mode Control
#define ST7796S_FRMCTR1 0xB1   // Frame Rate Control (In Normal Mode/Full Colors)
#define ST7796S_FRMCTR2 0xB2   // Frame Rate Control 2 (In Idle Mode/8 colors)
#define ST7796S_FRMCTR3 0xB3   // Frame Rate Control3 (In Partial Mode/Full Colors)
#define ST7796S_DIC 0xB4       // Display Inversion Control
#define ST7796S_BPC 0xB5       // Blanking Porch Control
#define ST7796S_DFC 0xB6       // Display Function Control
#define ST7796S_EM 0xB7        // Entry Mode Set
#define ST7796S_PWR1 0xC0      // Power Control 1
#define ST7796S_PWR2 0xC1      // Power Control 2
#define ST7796S_PWR3 0xC2      // Power Control 3
#define ST7796S_VCMPCTL 0xC5   // VCOM Control
#define ST7796S_VCMOFFSET 0xC6 // Vcom Offset Registe
#define ST7796S_NVMADW 0xD0    // NVM Address/Data Write
#define ST7796S_NVMBPROG 0xD1  // NVM Byte Program
#define ST7796S_NVMSR 0xD2     // Status Read
#define ST7796S_RDID4 0xD3     // Read ID4
#define ST7796S_PGC 0xE0       // Positive Gamma Control
#define ST7796S_NGC 0xE1       // Negative Gamma Control
#define ST7796S_DGC1 0xE2      // Digital Gamma Control 1
#define ST7796S_DGC2 0xE2      // Digital Gamma Control 2
#define ST7796S_DOCA 0xE8      // Display Output Ctrl Adjust
#define ST7796S_CSCON 0xF0     // Command Set Control
#define ST7796S_SPI 0xFB       // Read Control

//******************************************************************************
// Low level Display driver functions
//******************************************************************************
// Used only in double buffer mode
#ifndef lcd_get_cell_buffer
#define LCD_BUFFER_1 0x01
#define LCD_DMA_RUN 0x02
static uint8_t LCD_dma_status = 0;

// Return free buffer for render
pixel_t* lcd_get_cell_buffer(void) {
  return &spi_buffer[(LCD_dma_status & LCD_BUFFER_1) ? SPI_BUFFER_SIZE / 2 : 0];
}
#endif

// Disable inline for this function
static void lcd_send_command(uint8_t cmd, uint16_t len, const uint8_t* data) {
  // Uncomment on low speed SPI (possible get here before previous tx complete)
  while (SPI_IS_BUSY(LCD_SPI))
    ;
  LCD_CS_LOW;
  LCD_DC_CMD;
  SPI_WRITE_8BIT(LCD_SPI, cmd);
  // Need wait transfer complete and set data bit
  while (SPI_IS_BUSY(LCD_SPI))
    ;
  LCD_DC_DATA;
  spi_tx_buffer(data, len);
  //  while (SPI_IN_TX_RX(LCD_SPI));
  // LCD_CS_HIGH;
}

// Send command to LCD and read 32bit answer
// LCD_RDDID command, need shift result right by 7 bit
// 0x00858552 for ST7789V (9.1.3 RDDID (04h): Read Display ID)
// 0x006BFFFF for ST7796S ?? no id description in datasheet
// 0x00000000 for ili9341 ?? no id description in datasheet
uint32_t lcd_send_register(uint8_t cmd, uint8_t len, const uint8_t* data) {
  lcd_bulk_finish();
  SPI_BR_SET(LCD_SPI, SPI_BR_DIV16); // Set most safe read speed
  lcd_send_command(cmd, len, data);  // Send command
  spi_drop_rx();                     // Skip data from rx buffer
  uint32_t ret;
  ret = spi_rx_byte();
  ret <<= 8;
  ret |= spi_rx_byte();
  ret <<= 8;
  ret |= spi_rx_byte();
  ret <<= 8;
  ret |= spi_rx_byte();
  LCD_CS_HIGH;
  SPI_BR_SET(LCD_SPI, LCD_SPI_SPEED);
  return ret;
}

//******************************************************************************
// Display driver init sequence and hardware depend functions
//******************************************************************************
// ILI9341 and ST7789V Lcd init sequence + lcd depend image rotate function
#if defined(LCD_DRIVER_ILI9341) || defined(LCD_DRIVER_ST7789)
typedef enum { ili9341_type = 0, st7789v } lcd_type_t;
static lcd_type_t lcd_type = ili9341_type;
static const uint8_t ili9341_init_seq[] = {
    // ILI9341 init sequence
    // cmd,           len, data...,
    LCD_SWRESET, 0, // SW reset
    LCD_DISPOFF, 0, // display off
                    // ILI9341_POWERB,     3, 0x00, 0xC1, 0x30,                // Power control B
    // ILI9341_POWER_SEQ,  4, 0x64, 0x03, 0x12, 0x81,          // Power on sequence control
    // ILI9341_DTCA,       3, 0x85, 0x00, 0x78,                // Driver timing control A
    // ILI9341_POWERA,     5, 0x39, 0x2C, 0x00, 0x34, 0x02,    // Power control A
    // ILI9341_PUMPCTRL,   1, 0x20,                            // Pump ratio control
    // ILI9341_DTCB,       2, 0x00, 0x00,                      // Driver timing control B
    ILI9341_PWCTRL1, 1, 0x23,                      // POWER_CONTROL_1
    ILI9341_PWCTRL2, 1, 0x10,                      // POWER_CONTROL_2
    ILI9341_VMCTRL1, 2, 0x3e, 0x28,                // VCOM_CONTROL_1
    ILI9341_VMCTRL2, 1, 0xBE,                      // VCOM_CONTROL_2
    LCD_MADCTL, 1, LCD_MADCTL_MV | LCD_MADCTL_BGR, // landscape
    LCD_COLMOD, 1, 0x55,                           // COLMOD_PIXEL_FORMAT_SET : 16 bit pixel
    ILI9341_FRMCTR1, 2, 0x00, 0x18,                // Frame Rate
    // ILI9341_3GAMMA_EN,  1, 0x00,                            // Gamma Function Disable
    LCD_GAMSET, 1, 0x01, // gamma set for curve 01/2/04/08
    ILI9341_PGAMCTRL, 15, 0x0F, 0x31, 0x2B, 0x0C, 0x0E, 0x08, 0x4E, 0xF1, 0x37, 0x07, 0x10, 0x03,
    0x0E, 0x09, 0x00, // positive gamma correction
    ILI9341_NGAMCTRL, 15, 0x00, 0x0E, 0x14, 0x03, 0x11, 0x07, 0x31, 0xC1, 0x48, 0x08, 0x0F, 0x0C,
    0x31, 0x36, 0x0F, // negative gamma correction
    // LCD_CASET,          4, 0x00, 0x00, 0x01, 0x3f,          // Column Address Set: x = 0, width
    // 320
    // LCD_RASET,          4, 0x00, 0x00, 0x00, 0xef,          // Page Address Set: y = 0, height
    // 240
    ILI9341_ETMOD, 1, 0x06,               // entry mode
    ILI9341_DISCTRL, 3, 0x08, 0x82, 0x27, // display function control
    ILI9341_IFCTL, 3, 0x00, 0x00, 0x00,   // Interface Control (set WEMODE=0)
    LCD_SLPOUT, 0,                        // sleep out
    LCD_DISPON, 0,                        // display on
    0                                     // sentinel
};

// ST7789 LCD_RDDID read return 0x42C2A97F (need shift right by 7 bit, so ID1 = 0x85, ID2 = 0x85,
// ID3 = 0x52)
#define ST7789V_ID 0x858552
static const uint8_t ST7789V_init_seq[] = {
    // ST7789V init sequence
    // cmd,           len, data...,
    LCD_SWRESET, 0, // SW reset
    LCD_DISPOFF, 0, // display off
    LCD_MADCTL, 1, LCD_MADCTL_MX | LCD_MADCTL_MV | LCD_MADCTL_RGB, LCD_COLMOD, 1,
    0x55, // COLMOD_PIXEL_FORMAT_SET : 16 bit pixel
          // ST7789V_PORCTRL,    5, 0x0C, 0x0C, 0x00, 0x33, 0x33,
    // ST7789V_GCTRL,      1, 0x35,
    ST7789V_VCOMS, 1, 0x1F,          // default 0x20
                                     // ST7789V_LCMCTRL,    1, 0x2C,
    ST7789V_VDVVRHEN, 2, 0x01, 0xC3, // default 0x01, 0xFF !!! why need C3? datasheet say 0xFF
                                     // ST7789V_VDVS,       1, 0x20,
    // ST7789V_FRCTRL2,    1, 0x0F,
    // ST7789V_PWCTRL1,    2, 0xA4, 0xA1,
    LCD_SLPOUT, 0, // sleep out
    LCD_DISPON, 0, // display on
    0              // sentinel
};

// Read display ID and detect type
static const uint8_t* get_lcd_init(void) {
  uint32_t id = lcd_send_register(LCD_RDDID, 0, 0) >> 7;
  if (id == ST7789V_ID)
    lcd_type = st7789v;
  return lcd_type == ili9341_type ? ili9341_init_seq : ST7789V_init_seq;
}

void lcd_set_rotation(uint8_t r) {
  static const uint8_t lcd_rotation_const[] = {
      // ILI9341 LCD_MADCTL rotation settings
      (LCD_MADCTL_MV | LCD_MADCTL_BGR), (LCD_MADCTL_MY | LCD_MADCTL_BGR),
      (LCD_MADCTL_MX | LCD_MADCTL_MY | LCD_MADCTL_MV | LCD_MADCTL_BGR),
      (LCD_MADCTL_MX | LCD_MADCTL_BGR),
      // ST7789 LCD_MADCTL rotation settings
      (LCD_MADCTL_MX | LCD_MADCTL_MV | LCD_MADCTL_RGB), (LCD_MADCTL_RGB),
      (LCD_MADCTL_MY | LCD_MADCTL_MV | LCD_MADCTL_RGB),
      (LCD_MADCTL_MX | LCD_MADCTL_MY | LCD_MADCTL_RGB)};
  lcd_send_command(LCD_MADCTL, 1, &lcd_rotation_const[lcd_type * 4 + r]);
}

#endif

#ifdef LCD_DRIVER_ST7796S
static const uint8_t ST7796S_init_seq[] = {
    // ST7996s init sequence
    // cmd,           len, data...,
    LCD_SWRESET, 0,                                // SW reset
    LCD_DISPOFF, 0,                                // display off
    ST7796S_IFMODE, 1, 0x00,                       // Interface Mode Control
    ST7796S_FRMCTR1, 1, 0x0A,                      // Frame Rate
    ST7796S_DIC, 1, 0x02,                          // Display Inversion Control , 2 Dot
    ST7796S_DFC, 3, 0x02, 0x02, 0x3B,              // RGB/MCU Interface Control
    ST7796S_EM, 1, 0xC6,                           // EntryMode
    ST7796S_PWR1, 2, 0x17, 0x15,                   // Power Control 1
    ST7796S_PWR2, 1, 0x41,                         // Power Control 2
                                                   // ST7796S_VCMPCTL,    3, 0x00, 0x4D, 0x90,
    ST7796S_VCMPCTL, 3, 0x00, 0x12, 0x80,          // VCOM Control
    LCD_MADCTL, 1, LCD_MADCTL_MV | LCD_MADCTL_BGR, // landscape, BGR
    LCD_COLMOD, 1, 0x55,                           // Interface Pixel Format, 16bpp
    // ST7796S_PGC,       15, 0x00, 0x03, 0x09, 0x08, 0x16, 0x0A, 0x3F, 0x78, 0x4C, 0x09, 0x0A,
    // 0x08, 0x16, 0x1A, 0x0F,  // P-Gamma
    // ST7796S_NGC,       15, 0x00, 0X16, 0X19, 0x03, 0x0F, 0x05, 0x32, 0x45, 0x46, 0x04, 0x0E,
    // 0x0D, 0x35, 0x37, 0x0F,  // N-Gamma
    // 0xE9,               1, 0x00,                            // Set Image Func
    LCD_WRDISBV, 1, 0xFF, // Set Brightness to Max
    // 0xF7,               4, 0xA9, 0x51, 0x2C, 0x82,          // Adjust Control ??
    // LCD_INVON,          1, 0x01,                            // Inverse colors
    LCD_SLPOUT, 0, // sleep out
    LCD_DISPON, 0, // display on
    0              // sentinel
};

static const uint8_t* get_lcd_init(void) {
  return ST7796S_init_seq;
}

void lcd_set_rotation(uint8_t r) {
  static const uint8_t ST7796S_rotation_const[] = {
      (LCD_MADCTL_MV | LCD_MADCTL_BGR), (LCD_MADCTL_MY | LCD_MADCTL_BGR),
      (LCD_MADCTL_MX | LCD_MADCTL_MY | LCD_MADCTL_MV | LCD_MADCTL_BGR),
      (LCD_MADCTL_MX | LCD_MADCTL_BGR)};
  lcd_send_command(LCD_MADCTL, 1, &ST7796S_rotation_const[r]);
}
#endif

void lcd_init(void) {
  spi_init();
  LCD_RESET_ASSERT;
  chThdSleepMilliseconds(5);
  LCD_RESET_NEGATE;
  chThdSleepMilliseconds(5); // need time before LCD ready after reset
  const uint8_t* p = get_lcd_init();
  while (*p) {
    lcd_send_command(p[0], p[1], &p[2]);
    p += 2 + p[1];
    chThdSleepMilliseconds(2);
  }
  lcd_clear_screen();
}

void lcd_set_window(int x, int y, int w, int h, uint16_t cmd) {
  // Any LCD exchange start from this
  dma_channel_wait_completion_rx_tx();
  // uint8_t xx[4] = { x >> 8, x, (x+w-1) >> 8, (x+w-1) };
  // uint8_t yy[4] = { y >> 8, y, (y+h-1) >> 8, (y+h-1) };
  uint32_t xx = __REV16(x | ((x + w - 1) << 16));
  uint32_t yy = __REV16(y | ((y + h - 1) << 16));
  lcd_send_command(LCD_CASET, 4, (uint8_t*)&xx);
  lcd_send_command(LCD_RASET, 4, (uint8_t*)&yy);
  lcd_send_command(cmd, 0, NULL);
}

// Set DMA data size, depend from pixel size
#define LCD_DMA_MODE (LCD_PIXEL_SIZE == 2 ? STM32_DMA_CR_HWORD : STM32_DMA_CR_BYTE)

//
// LCD read data functions (Copy screen data to buffer)
//
#if defined(LCD_DRIVER_ILI9341) || defined(LCD_DRIVER_ST7789)
// ILI9341 or ST7789 send data in RGB888 format, need parse it
void lcd_read_memory(int x, int y, int w, int h, uint16_t* out) {
  uint16_t len = w * h;
  lcd_set_window(x, y, w, h, LCD_RAMRD);
  // Set read speed (if different from write speed)
  if (lcd_type == st7789v && ST7789V_SPI_RX_SPEED != LCD_SPI_SPEED)
    SPI_BR_SET(LCD_SPI, ST7789V_SPI_RX_SPEED);
  else if (ILI9341_SPI_RX_SPEED != LCD_SPI_SPEED)
    SPI_BR_SET(LCD_SPI, ILI9341_SPI_RX_SPEED);
  spi_drop_rx();                   // Skip data from SPI rx buffer
  spi_rx_byte();                   // require 8bit dummy clock
  uint8_t* rgbbuf = (uint8_t*)out; // receive pixel data to buffer
#ifndef __USE_DISPLAY_DMA_RX__
  spi_rx_buffer(rgbbuf, len * LCD_RX_PIXEL_SIZE);
  do {                                                // Parse received data to RGB565 format
    *out++ = RGB565(rgbbuf[0], rgbbuf[1], rgbbuf[2]); // read data is always 18bit
    rgbbuf += LCD_RX_PIXEL_SIZE;
  } while (--len);
#else
  len *= LCD_RX_PIXEL_SIZE;              // Set data size for DMA read
  spi_dma_rx_buffer(rgbbuf, len, false); // Start DMA read, and not wait completion
  do { // Parse received data to RGB565 format while data receive by DMA
    uint16_t left =
        dmaChannelGetTransactionSize(LCD_DMA_RX) + LCD_RX_PIXEL_SIZE; // Get DMA data left
    if (left > len)
      continue; // Next pixel RGB data not ready
    do {        // Process completed by DMA data
      *out++ = RGB565(rgbbuf[0], rgbbuf[1], rgbbuf[2]);
      rgbbuf += LCD_RX_PIXEL_SIZE;
      len -= LCD_RX_PIXEL_SIZE;
    } while (left < len);
  } while (len);
  dma_channel_wait_completion_rx_tx(); // Stop DMA transfer
#endif
  SPI_BR_SET(LCD_SPI, LCD_SPI_SPEED); // restore SPI speed
  LCD_CS_HIGH;                        // stop read
}
#elif defined(LCD_DRIVER_ST7796S)
// ST7796S send data in RGB565 format, not need parse
void lcd_read_memory(int x, int y, int w, int h, uint16_t* out) {
  uint16_t len = w * h;
  lcd_set_window(x, y, w, h, LCD_RAMRD);
  // Set read speed (if need different)
  if (LCD_SPI_RX_SPEED != LCD_SPI_SPEED)
    SPI_BR_SET(LCD_SPI, LCD_SPI_RX_SPEED);
  spi_drop_rx(); // Skip data from rx buffer
  spi_rx_byte(); // require 8bit dummy clock
  // receive pixel data to buffer
#ifndef __USE_DISPLAY_DMA_RX__
  spi_rx_buffer((uint8_t*)out, len * 2);
#else
  spi_dma_rx_buffer((uint8_t*)out, len * 2, true);
#endif
  // restore speed if need
  if (LCD_SPI_RX_SPEED != LCD_SPI_SPEED)
    SPI_BR_SET(LCD_SPI, LCD_SPI_SPEED);
  LCD_CS_HIGH;
}
#endif

void lcd_set_flip(bool flip) {
  dma_channel_wait_completion_rx_tx();
  lcd_set_rotation(flip ? DISPLAY_ROTATION_180 : DISPLAY_ROTATION_0);
}

// Wait completion before next data send
#ifndef lcd_bulk_finish
void lcd_bulk_finish(void) {
  dmaChannelWaitCompletion(LCD_DMA_TX); // Wait DMA
  // while (SPI_IN_TX_RX(LCD_SPI));         // Wait tx
}
#endif

static void lcd_bulk_buffer(int x, int y, int w, int h, pixel_t* buffer) {
  lcd_set_window(x, y, w, h, LCD_RAMWR);
#ifdef __USE_DISPLAY_DMA__
  dmaChannelSetMemory(LCD_DMA_TX, buffer);
  dmaChannelSetTransactionSize(LCD_DMA_TX, w * h);
  dmaChannelSetMode(LCD_DMA_TX, txdmamode | LCD_DMA_MODE | STM32_DMA_CR_MINC | STM32_DMA_CR_EN);
#else
  spi_tx_buffer((uint8_t*)buffer, w * h * sizeof(pixel_t));
#endif

#ifdef __REMOTE_DESKTOP__
  if (sweep_mode & SWEEP_REMOTE) {
    remote_region_t rd = {"bulk\r\n", x, y, w, h};
    send_region(&rd, (uint8_t*)buffer, w * h * sizeof(pixel_t));
  }
#endif
}

// Copy part of spi_buffer to region, no wait completion after if buffer count !=1
#ifndef lcd_bulk_continue
void lcd_bulk_continue(int x, int y, int w, int h) {
  lcd_bulk_buffer(x, y, w, h, lcd_get_cell_buffer()); // Send new cell data
  LCD_dma_status ^= LCD_BUFFER_1;                     // Switch buffer
}
#endif

// Copy spi_buffer to region, wait completion after
void lcd_bulk(int x, int y, int w, int h) {
  lcd_bulk_buffer(x, y, w, h, spi_buffer); // Send data
  lcd_bulk_finish();                       // Wait
}

//******************************************************************************
//   Display draw functions
//******************************************************************************
// Fill region by some color
void lcd_fill(int x, int y, int w, int h) {
  lcd_set_window(x, y, w, h, LCD_RAMWR);
  uint32_t len = w * h;
#ifdef __USE_DISPLAY_DMA__
  dmaChannelSetMemory(LCD_DMA_TX, &background_color);
  while (len) {
    uint32_t delta = len > 0xFFFF ? 0xFFFF : len; // DMA can send only 65535 data in one run
    dmaChannelSetTransactionSize(LCD_DMA_TX, delta);
    dmaChannelSetMode(LCD_DMA_TX, txdmamode | LCD_DMA_MODE | STM32_DMA_CR_EN);
    dmaChannelWaitCompletion(LCD_DMA_TX);
    len -= delta;
  }
#else
  do {
    while (SPI_TX_IS_NOT_EMPTY(LCD_SPI))
      ;
    if (LCD_PIXEL_SIZE == 2)
      SPI_WRITE_16BIT(LCD_SPI, background_color);
    else
      SPI_WRITE_8BIT(LCD_SPI, background_color);
  } while (--len);
#endif

#ifdef __REMOTE_DESKTOP__
  if (sweep_mode & SWEEP_REMOTE) {
    remote_region_t rd = {"fill\r\n", x, y, w, h};
    send_region(&rd, (uint8_t*)&background_color, sizeof(pixel_t));
  }
#endif
}

#if 0
static void lcd_pixel(int x, int y, uint16_t color) {
  lcd_set_window(x, y, 1, 1, LCD_RAMWR);
  while (SPI_TX_IS_NOT_EMPTY(LCD_SPI));
  SPI_WRITE_16BIT(LCD_SPI, color);
}
#endif

void lcd_line(int x0, int y0, int x1, int y1) {
  // Modified Bresenham's line algorithm
  if (x1 < x0) {
    SWAP(int, x0, x1);
    SWAP(int, y0, y1);
  } // Need draw from left to right
  int dx = -(x1 - x0), sx = 1;
  int dy = (y1 - y0), sy = 1;
  if (dy < 0) {
    dy = -dy;
    sy = -1;
  }
  int err = -((dx + dy) < 0 ? dx : dy) / 2;
  while (1) {
    lcd_set_window(x0, y0, LCD_WIDTH - x0, 1, LCD_RAMWR); // prepare send Horizontal line
    while (1) {
      while (SPI_TX_IS_NOT_EMPTY(LCD_SPI))
        ;
      SPI_WRITE_16BIT(LCD_SPI, foreground_color); // Send color
      if (x0 == x1 && y0 == y1)
        return;
      int e2 = err;
      if (e2 > dx) {
        err -= dy;
        x0 += sx;
      }
      if (e2 < dy) {
        err -= dx;
        y0 += sy;
        break;
      } // Y coordinate change, next horizontal line
    }
  }
}

void lcd_clear_screen(void) {
  lcd_fill(0, 0, LCD_WIDTH, LCD_HEIGHT);
}

void lcd_set_foreground(uint16_t fg_idx) {
  foreground_color = GET_PALTETTE_COLOR(fg_idx);
}

void lcd_set_background(uint16_t bg_idx) {
  background_color = GET_PALTETTE_COLOR(bg_idx);
}

void lcd_set_colors(uint16_t fg_idx, uint16_t bg_idx) {
  foreground_color = GET_PALTETTE_COLOR(fg_idx);
  background_color = GET_PALTETTE_COLOR(bg_idx);
}

void lcd_blit_bitmap(uint16_t x, uint16_t y, uint16_t width, uint16_t height, const uint8_t* b) {
#if 1 // Use this for remote desktop (in this case bulk operation send to remote)
  pixel_t* buf = spi_buffer;
  uint8_t bits = 0;
  for (uint32_t c = 0; c < height; c++) {
    for (uint32_t r = 0; r < width; r++) {
      if ((r & 7) == 0)
        bits = *b++;
      *buf++ = (0x80 & bits) ? foreground_color : background_color;
      bits <<= 1;
    }
  }
  lcd_bulk(x, y, width, height);
#else
  uint8_t bits = 0;
  lcd_set_window(x, y, width, height, LCD_RAMWR);
  for (uint32_t c = 0; c < height; c++) {
    for (uint32_t r = 0; r < width; r++) {
      if ((r & 7) == 0)
        bits = *b++;
      while (SPI_TX_IS_NOT_EMPTY(LCD_SPI))
        ;
      SPI_WRITE_16BIT(LCD_SPI, (0x80 & bits) ? foreground_color : background_color);
      bits <<= 1;
    }
  }
#endif
}

void lcd_drawchar(uint8_t ch, int x, int y) {
  lcd_blit_bitmap(x, y, FONT_GET_WIDTH(ch), FONT_GET_HEIGHT, FONT_GET_DATA(ch));
}

#ifndef lcd_drawstring
void lcd_drawstring(int16_t x, int16_t y, const char* str) {
  int x_pos = x;
  while (*str) {
    uint8_t ch = *str++;
    if (ch == '\n') {
      x = x_pos;
      y += FONT_STR_HEIGHT;
      continue;
    }
    const uint8_t* char_buf = FONT_GET_DATA(ch);
    uint16_t w = FONT_GET_WIDTH(ch);
    lcd_blit_bitmap(x, y, w, FONT_GET_HEIGHT, char_buf);
    x += w;
  }
}
#endif

typedef struct {
  const void* vmt;
  int16_t start_x, start_y;
  int16_t x, y;
  uint16_t state;
} lcdPrintStream;

static void put_normal(lcdPrintStream* ps, uint8_t ch) {
  if (ch == '\n') {
    ps->x = ps->start_x;
    ps->y += FONT_STR_HEIGHT;
    return;
  }
  uint16_t w = FONT_GET_WIDTH(ch);
#if _USE_FONT_ < 3
  lcd_blit_bitmap(ps->x, ps->y, w, FONT_GET_HEIGHT, FONT_GET_DATA(ch));
#else
  lcd_blit_bitmap(ps->x, ps->y, w < 9 ? 9 : w, FONT_GET_HEIGHT, FONT_GET_DATA(ch));
#endif
  ps->x += w;
}

#if _USE_FONT_ != _USE_SMALL_FONT_
typedef void (*font_put_t)(lcdPrintStream* ps, uint8_t ch);
static font_put_t put_char = put_normal;
static void put_small(lcdPrintStream* ps, uint8_t ch) {
  if (ch == '\n') {
    ps->x = ps->start_x;
    ps->y += sFONT_STR_HEIGHT;
    return;
  }
  uint16_t w = sFONT_GET_WIDTH(ch);
#if _USE_SMALL_FONT_ < 3
  lcd_blit_bitmap(ps->x, ps->y, w, sFONT_GET_HEIGHT, sFONT_GET_DATA(ch));
#else
  lcd_blit_bitmap(ps->x, ps->y, w < 9 ? 9 : w, sFONT_GET_HEIGHT, sFONT_GET_DATA(ch));
#endif
  ps->x += w;
}
void lcd_set_font(int type) {
  put_char = type == FONT_SMALL ? put_small : put_normal;
}

#else
#define put_char put_normal
#endif

static msg_t lcd_put(void* ip, uint8_t ch) {
  lcdPrintStream* ps = ip;
  if (ps->state) {
    if (ps->state == R_BGCOLOR[0])
      lcd_set_background(ch);
    else if (ps->state == R_FGCOLOR[0])
      lcd_set_foreground(ch);
    ps->state = 0;
    return MSG_OK;
  } else if (ch < 0x09) {
    ps->state = ch;
    return MSG_OK;
  }
  put_char(ps, ch);
  return MSG_OK;
}

// Simple print in buffer function
int lcd_printf(int16_t x, int16_t y, const char* fmt, ...) {
  // Init small lcd print stream
  struct lcd_printStreamVMT {
    _base_sequential_stream_methods
  } lcd_vmt = {NULL, NULL, lcd_put, NULL};
  lcdPrintStream ps = {&lcd_vmt, x, y, x, y, 0};
  // Performing the print operation using the common code.
  va_list ap;
  va_start(ap, fmt);
  int retval = chvprintf((BaseSequentialStream*)(void*)&ps, fmt, ap);
  va_end(ap);
  // Return number of bytes that would have been written.
  return retval;
}

int lcd_printf_v(int16_t x, int16_t y, const char* fmt, ...) {
  // Init small lcd print stream
  struct lcd_printStreamVMT {
    _base_sequential_stream_methods
  } lcd_vmt = {NULL, NULL, lcd_put, NULL};
  lcdPrintStream ps = {&lcd_vmt, x, y, x, y, 0};
  lcd_set_foreground(LCD_FG_COLOR);
  lcd_set_background(LCD_BG_COLOR);
  lcd_set_rotation(DISPLAY_ROTATION_270);
  // Performing the print operation using the common code.
  va_list ap;
  va_start(ap, fmt);
  int retval = chvprintf((BaseSequentialStream*)(void*)&ps, fmt, ap);
  va_end(ap);
  lcd_set_rotation(DISPLAY_ROTATION_0);
  // Return number of bytes that would have been written.
  return retval;
}

void lcd_blit_bitmap_scale(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t size,
                           const uint8_t* b) {
  lcd_set_window(x, y, w * size, h * size, LCD_RAMWR);
  for (int c = 0; c < h; c++) {
    const uint8_t* ptr = b;
    uint8_t bits = 0;
    for (int i = 0; i < size; i++) {
      ptr = b;
      for (int r = 0; r < w; r++, bits <<= 1) {
        if ((r & 7) == 0)
          bits = *ptr++;
        for (int j = 0; j < size; j++) {
          while (SPI_TX_IS_NOT_EMPTY(LCD_SPI))
            ;
          SPI_WRITE_16BIT(LCD_SPI, (0x80 & bits) ? foreground_color : background_color);
        }
      }
    }
    b = ptr;
  }
}

int lcd_drawchar_size(uint8_t ch, int x, int y, uint8_t size) {
  const uint8_t* char_buf = FONT_GET_DATA(ch);
  uint16_t w = FONT_GET_WIDTH(ch);
#if 1 // Use this for remote desctop (in this case bulk operation send to remote)
  pixel_t* buf = spi_buffer;
  for (uint32_t c = 0; c < FONT_GET_HEIGHT; c++, char_buf++) {
    for (uint32_t i = 0; i < size; i++) {
      uint8_t bits = *char_buf;
      for (uint32_t r = 0; r < w; r++, bits <<= 1)
        for (uint32_t j = 0; j < size; j++)
          *buf++ = (0x80 & bits) ? foreground_color : background_color;
    }
  }
  lcd_bulk(x, y, w * size, FONT_GET_HEIGHT * size);
#else
  lcd_set_window(x, y, w * size, FONT_GET_HEIGHT * size, LCD_RAMWR);
  for (int c = 0; c < FONT_GET_HEIGHT; c++, char_buf++) {
    for (int i = 0; i < size; i++) {
      uint8_t bits = *char_buf;
      for (int r = 0; r < w; r++, bits <<= 1)
        for (int j = 0; j < size; j++) {
          while (SPI_TX_IS_NOT_EMPTY(LCD_SPI))
            ;
          SPI_WRITE_16BIT(LCD_SPI, (0x80 & bits) ? foreground_color : background_color);
        }
    }
  }
#endif
  return w * size;
}

void lcd_drawfont(uint8_t ch, int x, int y) {
  lcd_blit_bitmap(x, y, NUM_FONT_GET_WIDTH, NUM_FONT_GET_HEIGHT, NUM_FONT_GET_DATA(ch));
}

void lcd_drawstring_size(const char* str, int x, int y, uint8_t size) {
  while (*str)
    x += lcd_drawchar_size(*str++, x, y, size);
}

void lcd_vector_draw(int x, int y, const vector_data* v) {
  while (v->shift_x || v->shift_y) {
    int x1 = x + (int)v->shift_x;
    int y1 = y + (int)v->shift_y;
    if (!v->transparent)
      lcd_line(x, y, x1, y1);
    x = x1;
    y = y1;
    v++;
  }
}

#if 0
static const uint16_t colormap[] = {
  RGBHEX(0x00ff00), RGBHEX(0x0000ff), RGBHEX(0xff0000),
  RGBHEX(0x00ffff), RGBHEX(0xff00ff), RGBHEX(0xffff00)
};

void ili9341_test(int mode) {
  int x, y;
  int i;
  switch (mode) {
    default:
#if 1
    lcd_fill(0, 0, LCD_WIDTH, LCD_HEIGHT, 0);
    for (y = 0; y < LCD_HEIGHT; y++) {
      lcd_fill(0, y, LCD_WIDTH, 1, RGB(LCD_HEIGHT-y, y, (y + 120) % 256));
    }
    break;
    case 1:
      lcd_fill(0, 0, LCD_WIDTH, LCD_HEIGHT, 0);
      for (y = 0; y < LCD_HEIGHT; y++) {
        for (x = 0; x < LCD_WIDTH; x++) {
          ili9341_pixel(x, y, (y<<8)|x);
        }
      }
      break;
    case 2:
      //lcd_send_command(0x55, 0xff00);
      ili9341_pixel(64, 64, 0xaa55);
    break;
#endif
#if 1
    case 3:
      for (i = 0; i < 10; i++)
        lcd_drawfont(i, i*20, 120);
    break;
#endif
#if 0
    case 4:
      draw_grid(10, 8, 29, 29, 15, 0, 0xffff, 0);
    break;
#endif
    case 4:
      lcd_line(0, 0, 15, 100);
      lcd_line(0, 0, 100, 100);
      lcd_line(0, 15, 100, 0);
      lcd_line(0, 100, 100, 0);
    break;
  }
}
#endif
