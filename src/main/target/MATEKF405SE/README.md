# FLIGHT CONTROLLER MATEK F405-SE

* Designed specifically for fixed wing INAV setups by MATEK
* http://www.mateksys.com/?portfolio=f405-se

## HW info

* MCU: 168MHz STM32F405RGT6
* IMU: MPU6000 (SPI)
* Baro: DPS310 (I2C)
* OSD: AT7456E (SPI)
* Blackbox: MicroSD slot (SPI)
* VCP & 6x Uarts, 1x Softserial_Tx supported
* 9x PWM outputs (7x Dshot compatible with BF/INAV)
* 2x I2C
* 3x ADC (voltage, current, RSSI)
* 4x RX6 pad(one per corner) for BLheli32 ESC telemetry
* 4x individual ESC power/signal pads
* 1x Group of  G/S1/S2/S3/S4 pads for 4in1 ESC Signal/GND
* 3x LEDs for FC STATUS (Blue, Green) and 3.3V indicator(Red)
* Built in inverter on UART2-RX for SBUS input
* PPM/UART Shared: UART2-RX
* Vbat filtered output power for VTX
* 2x Motors & 7x Servos
* 4x BEC + current sensor

### MATEKF405SE_PINIO
Replaces UART 6 Tx with USER 1 for PINIO and UART 6 Rx with USER 2 for PINIO
