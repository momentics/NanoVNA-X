# DMA Architecture Overview

NanoVNA-X relies on DMA to keep the STM32F072 and STM32F303 parts responsive while streaming
screen updates, SD card payloads, and shell traffic.  The firmware now centralises the DMA setup
so that the SPI display/SD interface and the USART console can share the available channels
without starving each other.

## Channel allocation by platform

| Peripheral | Direction | STM32F072 (NanoVNA-H) | STM32F303 (NanoVNA-H4) |
|------------|-----------|------------------------|------------------------|
| SPI1 (LCD & SD) | TX | DMA1 Channel 3 | DMA1 Channel 3 |
| SPI1 (LCD & SD) | RX | DMA1 Channel 2 | DMA1 Channel 2 |
| USART1 (console) | TX | DMA1 Channel 4 | DMA1 Channel 4 |
| USART1 (console) | RX | DMA1 Channel 5 | DMA1 Channel 5 |

The F072 uses fixed channel assignments, therefore USART1 is explicitly remapped to channel 4/5 in
`mcuconf.h` to avoid clashing with the SPI1 transfers on channels 2/3.  The F303 shares the same DMA1
layout and benefits from the identical mapping.

## DMA manager

`src/drivers/dma.c` exposes a lightweight handle (`dma_channel_t`) that tracks the configured
peripheral address, base control bits (direction, priority), and the associated hardware channel.
The helper functions:

* disable the channel safely before changing any registers;
* wait for a running transfer to complete when a new transaction is queued;
* restart the channel with the required element size and increment flags; and
* provide `dma_channel_get_remaining()` so the LCD driver can poll DMA progress while parsing the
  RGB data returned by the panel.

The LCD/SD driver switches to these helpers instead of poking the registers directly.  The SPI1 DMA
handles are initialised once in `spi_init()` and reused for both LCD screen updates and SD card I/O,
so only one set of interrupts and status bits has to be monitored.  Blocking calls now simply wait on
the new helper rather than busy loops that cleared the CCR register after every transfer.

## UART console via DMA

`src/drivers/uart_dma.c` converts the ChibiOS `UARTDriver` into a `BaseSequentialStream` so the shell
code can keep using `streamWrite`, `streamRead` and `chvprintf`.  `HAL_USE_UART` and `UART_USE_WAIT`
are enabled and USART1 is configured for DMA in both board configurations.  The wrapper:

* starts and restarts the driver with the requested baud rate;
* handles chunked transfers for buffers larger than 65,535 frames (the DMA counter limit);
* exposes `uart_dma_write_timeout()` and `uart_dma_read_timeout()` helpers that return the actual
  number of bytes moved; and
* provides `uart_dma_flush_queues()` so the shell can drop pending transfers when switching between
  USB and UART endpoints.

This replaces the former interrupt-driven `SerialDriver` instance (`SD1`) and removes the need to
manually poke the TX/RX queues in `shell_reset_console()`.

## Extending the DMA setup

* Configure the desired stream in the board `mcuconf.h` file using `STM32_DMA_STREAM_ID(dma, channel)`
  and update the driver priority macros.  The helper will assert if a handle is initialised twice.
* Initialise a `dma_channel_t` handle early (usually when the peripheral itself is brought up), then
  call `dma_channel_start()` whenever a transfer is required.  The function can be invoked back to
  back: if the channel is still active the helper waits until `CNDTR` reaches zero before arming the
  next transaction.
* Use `dma_channel_wait()` only when the CPU must block.  For background operations (for example the
  LCD read-back path) poll `dma_channel_get_remaining()` and perform incremental work while DMA fills
  the buffer.
* Remember that all transfers share DMA1, so avoid assigning overlapping channels when enabling extra
  peripherals.  The table above documents the slots that are now reserved by the firmware.

