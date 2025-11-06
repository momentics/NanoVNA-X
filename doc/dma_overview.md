# DMA Architecture Overview

NanoVNA-X relies on Direct Memory Access (DMA) to ensure high performance and responsiveness on the STM32F072 and STM32F303 platforms. The DMA strategy is designed to prioritize the integrity of the measurement pipeline while offloading high-throughput tasks like screen rendering from the CPU.

## Architectural Rationale

Early firmware versions used DMA for the USART console, but this created resource conflicts with the measurement pipeline. To guarantee measurement stability, a key architectural decision was made: **DMA channels are now exclusively reserved for performance-critical hardware.**

The console has been moved to a standard interrupt-driven serial driver, which has a negligible impact on console performance but frees up DMA resources for the time-sensitive I2S audio codec and SPI display interface. This prevents data loss during sweeps and ensures a smooth, responsive user interface.

## Channel Allocation by Platform

The firmware reserves the following DMA1 channels for specific peripherals. These assignments are consistent across both supported platforms.

| Peripheral | Stream | STM32F072 (NanoVNA-H) | STM32F303 (NanoVNA-H4) |
|--------------------|-----------|------------------------|------------------------|
| SPI1 (LCD) | TX | DMA1 Channel 3 | DMA1 Channel 3 |
| SPI1 (LCD) | RX | DMA1 Channel 2 | DMA1 Channel 2 |
| SPI2 (I2S Codec) | RX | DMA1 Channel 4 | DMA1 Channel 4 |

## High-Performance Subsystems

### Measurement Pipeline (I2S)
The audio codec (TLV320AIC3204) streams measurement data to the MCU via the I2S protocol, which is implemented on the SPI2 peripheral. A circular DMA buffer on **DMA1 Channel 4** continuously receives this data in the background. The use of DMA is critical for ensuring that no samples are lost, which would otherwise corrupt the measurement results. The DMA controller issues half-transfer and full-transfer interrupts, which signal the DSP processing task to consume the new data.

### Display Rendering (SPI)
The LCD driver uses **DMA1 Channel 3 (TX)** and **Channel 2 (RX)** to handle all high-throughput rendering operations. Bulk transfers, such as drawing graph data or filling screen regions, are offloaded to the DMA controller. This allows the CPU to perform other tasks, such as processing user input, while the screen is being updated, leading to a much more responsive user interface.

### Console Implementation
The shell and console previously used a DMA-backed UART driver. This has been replaced by the standard ChibiOS `SerialDriver`, which uses a more traditional interrupt-based approach. While this driver does not use DMA, it provides a compatible `BaseSequentialStream` interface for the rest of the firmware. This change was a deliberate trade-off to ensure that the most critical DMA channels were available for the measurement pipeline.