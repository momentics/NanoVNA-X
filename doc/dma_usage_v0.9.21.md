# DMA usage in firmware v0.9.21

## Display pipeline
- `__USE_DISPLAY_DMA__` is defined in `include/nanovna.h`, enabling DMA-backed transfers on the LCD SPI bus.
- `src/drivers/lcd.c` wires SPI1 TX/RX to DMA1 channels 3 and 2, and configures SPI1 to assert `TXDMAEN`/`RXDMAEN` so bulk transfers use DMA.
- Higher-level draw helpers (for example `lcd_bulk_buffer` and `lcd_fill`) stage frame-buffer data and start DMA transactions instead of polling the FIFO, only falling back to PIO when DMA is disabled via configuration.

## Measurement capture pipeline
- The sweep service owns the DMA-backed audio buffer that feeds DSP processing; it resets accumulation and dispatches `dsp_process` in the DMA ISR context (`i2s_lld_serve_rx_interrupt`).
- `include/platform/boards/stm32_peripherals.h` maps the DMA1 channel 4 interrupt handler to the sweep service ISR so the application code receives half/full transfer callbacks from the I2S RX stream.
- `boards/STM32F072/i2s.c` enables circular DMA on SPI2 RX (I²S) with half/full-complete interrupts, binding DMA1 channel 4 to the codec stream and starting the peripheral in DMA mode.
- `src/app/application.c` wires the DMA buffer into the codec by calling `init_i2s(sweep_service_rx_buffer(), ...)` during system startup, so every sweep reuses the DMA ring the ISR updates.

