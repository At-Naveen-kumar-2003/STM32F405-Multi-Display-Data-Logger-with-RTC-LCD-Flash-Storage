# STM32F405-Multi-Display-Data-Logger-with-RTC-LCD-Flash-Storage


## Overview

This project is a real-time monitoring and data logging system developed using the **STM32F405RGTx** microcontroller.

The system displays real-time clock and date information from a **DS3231 RTC**, monitors two analog inputs using the ADC peripheral, and stores historical records inside the STM32 internal Flash memory using EEPROM emulation techniques.

An LCD interface provides multiple display pages, while push buttons allow navigation, logging, and administrative functions.

---

## Features

### Real-Time Clock Display

* DS3231 RTC integration via I2C
* 12-hour AM/PM clock display
* Date display (DD/MM/YYYY)

### ADC Monitoring

* POT1 connected to ADC Channel 12 (PC2)
* POT2 connected to ADC Channel 11 (PC1)
* Real-time value and percentage display

### Administrative Display Modes

* Clock View
* POT1 Monitoring View
* POT2 Monitoring View
* Log Viewer Mode

### Data Logging

* Automatic logging every 60 seconds
* Manual logging using SW3
* Stores:

  * Timestamp
  * POT1 raw ADC value
  * POT2 percentage value

### Internal Flash EEPROM Emulation

* Uses STM32F405 Sector 7
* Circular buffer implementation
* Retains data after power cycling
* Stores last 5 log records

### User Interface

* 16x2 I2C LCD
* Four navigation buttons
* Four status LEDs
* Buzzer feedback

---

## Hardware Connections

### I2C Devices

| Device     | STM32 Pin |
| ---------- | --------- |
| DS3231 SCL | PB10      |
| DS3231 SDA | PB11      |
| LCD SCL    | PB10      |
| LCD SDA    | PB11      |

---

### Analog Inputs

| Signal | STM32 Pin | ADC Channel |
| ------ | --------- | ----------- |
| POT1   | PC2       | ADC1_CH12   |
| POT2   | PC1       | ADC1_CH11   |

---

### Push Buttons

| Button | STM32 Pin |
| ------ | --------- |
| SW1    | PB7       |
| SW2    | PB3       |
| SW3    | PB4       |
| SW4    | PA15      |

---

### LEDs

| LED  | STM32 Pin |
| ---- | --------- |
| LED1 | PC6       |
| LED2 | PB15      |
| LED3 | PB14      |
| LED4 | PB13      |

---

### Buzzer

| Device | STM32 Pin |
| ------ | --------- |
| Buzzer | PC9       |

---

## Operating Modes

### Normal Mode

Displays:

```text
10:30:15 AM
12/06/2026
```

---

### POT1 Mode

```text
ADMIN: POT1
VAL:2048 (50%)
```

---

### POT2 Mode

```text
ADMIN: POT2
VAL:3072 (75%)
```

---

### Log Mode

Automatically scrolls through the latest records.

Example:

```text
LOG1 12/06 10:30
P1:2048 P2:75%
```

---

## Button Functions

| Button | Function             |
| ------ | -------------------- |
| SW1    | Cycle Admin Pages    |
| SW2    | Beep Feedback        |
| SW3    | Manual Log Save      |
| SW4    | Return to Clock Mode |

---

## Flash Memory Layout

Sector Used:

```text
0x080E0000 (Sector 7)
```

### Header

| Offset | Description  |
| ------ | ------------ |
| 0x00   | Magic Word   |
| 0x04   | Head Index   |
| 0x08   | Record Count |
| 0x0C   | Reserved     |

### Records

Each record stores:

* Seconds
* Minutes
* Hours
* Day
* Month
* POT1 ADC Value
* POT2 Percentage

Maximum stored records:

```text
5 Records
```

---

## Software Architecture

Modules included:

* System Clock Configuration
* GPIO Driver
* ADC Driver
* I2C Driver
* DS3231 Driver
* LCD Driver
* Flash EEPROM Emulation
* Button Debounce State Machine
* User Interface Manager
* Data Logger

---

## Development Environment

* STM32CubeIDE
* STM32F405RGTx
* CMSIS
* ARM GCC Compiler

---

