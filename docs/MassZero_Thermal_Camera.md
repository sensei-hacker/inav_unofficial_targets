# MassZero Thermal Camera Integration

## Overview

The MassZero Thermal Camera (MZTC) integration lets INAV configure and control a MassZero thermal camera over a UART. INAV sets the camera's image parameters, palette, zoom, mirroring and shutter behaviour. It surfaces the link state on the OSD and over MSP.

The camera produces its own analog video. That video goes to the video transmitter in the usual way. The camera exposes a UART for control and a composite video output for the picture. It has no digital data interface. Its serial protocol has no frame-read command. INAV therefore has no access to thermal pixels or per-pixel temperatures. The OSD elements described here report camera state.

## Purchase information

**MassZero Thermal Camera**
Website: [https://masszerofpv.com](https://masszerofpv.com)
Model: MassZero Thermal Camera Core Component
Contact MassZero for current pricing and availability.

## Build support

`USE_MZTC` is enabled on targets with more than 512 KB of flash and on SITL. Targets with 512 KB or less do not include the feature. None of the settings or commands below exist on those boards.

## Hardware requirements

- An INAV-compatible flight controller with more than 512 KB of flash
- A MassZero Thermal Camera Core Component
- A free UART
- A power supply matching the camera's specification

## Wiring

### Serial connection

The FC controls the camera over a normal bidirectional UART.

- **TX (FC)** goes to **RX (camera)**
- **RX (FC)** goes to **TX (camera)**
- **GND** goes to **GND**
- **VCC** goes to the camera's supply pin at the voltage its datasheet specifies

### Video connection

The camera's analog video output goes to the video transmitter, or to whatever else consumes composite video on the aircraft. It does not connect to the flight controller. INAV has no analog video input.

### Complete wiring example

```
MassZero Thermal Camera    Destination
=======================    ================================
VCC                     ->  FC regulated supply (see datasheet)
GND                     ->  FC GND
TX                      ->  FC RX (UART2)
RX                      ->  FC TX (UART2)
Video Out               ->  VTX video in
GND (video)             ->  VTX GND
```

## Serial port setup

Assign the `MZTC` function to the UART the camera is wired to. Then tell the driver which port to use. The port index in `mztc_port` is the same zero-based index the CLI `serial` command uses. UART1 is 0, UART2 is 1, and so on.

```
set mztc_enabled = ON
set mztc_port = 1
set mztc_baudrate = 8
save
```

`mztc_baudrate` is an index into INAV's baud rate table. Index 8 is 115200. MassZero cameras ship at that rate.

## How the link is established

The driver does not treat an open UART as a working camera. After it opens the port it stays in the initializing state. It sends a read-model command roughly twice a second. The camera is only reported as connected once it answers with a valid packet. If no valid packet arrives for three seconds the driver closes the port, flags a timeout and starts over.

`mztc` reporting `Connected: NO` with a rising error flag is a genuine wiring, baud rate or power problem.

`Link quality` is the share of recent probes the camera answered. A marginal connection shows up as a value below 100.

## Configuration

All persistent settings live in the CLI and are documented in [Settings.md](Settings.md). The `mztc_*` commands below act on the camera immediately. They do not survive a reboot on their own.

### Operating mode

```
set mztc_mode = STANDBY
```

| Mode | Meaning |
| --- | --- |
| `DISABLED` | The driver holds the port and issues no periodic work |
| `STANDBY` | Periodic status polling only. This is the default |
| `CONTINUOUS` | Reserved. Behaves like `STANDBY` |
| `TRIGGERED` | Reserved. Behaves like `STANDBY` |
| `ALERT` | Reserved. Behaves like `STANDBY` |
| `RECORDING` | Reserved. Behaves like `STANDBY` |
| `CALIBRATION` | Reserved. Behaves like `STANDBY` |
| `SURVEILLANCE` | Reserved. Behaves like `STANDBY` |

Every reserved mode is accepted and stored. They all behave like `STANDBY` today. The camera reports no temperature over its serial protocol. `ALERT` has nothing to act on.

### Image parameters

```
set mztc_brightness = 50
set mztc_contrast = 50
set mztc_digital_enhancement = 50
set mztc_spatial_denoise = 50
set mztc_temporal_denoise = 50
```

All five accept 0 to 100. The same values can be pushed to the camera without saving:

```
mztc_config 60 55 70
mztc_denoise 40 60
mztc_enhancement 70
```

### Colour palette

```
set mztc_palette_mode = WHITE_HOT
```

| Value | Palette |
| --- | --- |
| `WHITE_HOT` | White hot, the default |
| `BLACK_HOT` | Black hot |
| `FUSION_1` | Fusion 1 |
| `RAINBOW` | Rainbow |
| `FUSION_2` | Fusion 2 |
| `IRON_RED_1` | Iron red 1 |
| `IRON_RED_2` | Iron red 2 |
| `SEPIA` | Sepia |
| `COLOR_1` | Colour 1 |
| `COLOR_2` | Colour 2 |
| `ICE_FIRE` | Ice fire |
| `RAIN` | Rain |
| `GREEN_HOT` | Green hot |
| `RED_HOT` | Red hot |

`mztc_palette 5` applies a palette immediately by index.

### Zoom and mirror

```
set mztc_zoom_level = 1X
set mztc_mirror_mode = NONE
```

Zoom accepts `1X`, `2X`, `4X` and `8X`. Mirror accepts `NONE`, `HORIZONTAL`, `VERTICAL` and `CENTRAL`. `mztc_zoom 2` applies a zoom level immediately by index.

The camera manual contradicts itself on the zoom labels. Its prose says 1x, 2x, 4x and 8x. Its value table for the same command says 1x, 2x, 3x and 4x. The wire values 0 to 3 are the same either way. The setting works regardless of which set of labels is right.

### Shutter and flat field correction

A manual shutter cycle on this camera performs a flat field correction. One command covers both:

```
mztc_calibrate
```

Automatic correction is driven by two settings:

```
set mztc_auto_shutter = TIME_AND_TEMP
set mztc_ffc_interval = 5
```

`mztc_auto_shutter` accepts `TEMP_ONLY`, `TIME_ONLY` and `TIME_AND_TEMP`. `mztc_ffc_interval` is in minutes and accepts 1 to 60.

The camera runs the shutter schedule itself. INAV pushes both values to it on connect and then leaves it alone. Select `TEMP_ONLY` to stop the camera correcting on a timer, since it then reacts to temperature drift only.

### Vignetting correction

Vignetting correction is a one-shot action. There is no setting for it:

```
mztc_vignetting
```

Point the lens at a uniform surface before running it. The camera superimposes whatever it is looking at onto the correction. A cluttered scene makes the image worse.

Bad pixel removal is not exposed. The camera drives it through an on-screen cursor that has to be walked onto each bad pixel. A flight controller cannot do that usefully.

### Display options

```
set mztc_update_rate = 9
```

`mztc_update_rate` is the driver's poll rate in Hz. It accepts 1 to 30.

## CLI commands

| Command | Purpose |
| --- | --- |
| `mztc` | Print the camera state, link quality and reported device ID |
| `mztc_mode <0-7>` | Set the operating mode |
| `mztc_config <brightness> <contrast> <enhancement>` | Set the three image parameters at once |
| `mztc_palette <0-13>` | Set the colour palette |
| `mztc_zoom <0-3>` | Set the digital zoom level |
| `mztc_enhancement <0-100>` | Set digital enhancement on its own |
| `mztc_denoise <spatial> <temporal>` | Set both denoising levels |
| `mztc_calibrate` | Trigger a manual shutter cycle for a flat field correction |
| `mztc_vignetting` | Run one vignetting correction |
| `mztc_save` | Save the current image settings to the camera's own flash |
| `mztc_defaults` | Restore the camera to its factory defaults |
| `mztc_reconnect` | Close the port and restart the connection sequence |

Every command called with no arguments prints the current value.

## MSP commands

The camera is reachable over MSP V2 in INAV's own command range. Full payload layouts are in [the MSP message reference](development/msp/README.md).

| Command | Code | Direction | Payload |
| --- | --- | --- | --- |
| `MSP2_MZTC_CONFIG` | 0x2240 | Out | 15 bytes |
| `MSP2_MZTC_STATUS` | 0x2241 | Out | 7 bytes |
| `MSP2_SET_MZTC_CONFIG` | 0x2242 | In | 15 bytes |
| `MSP2_SET_MZTC_MODE` | 0x2243 | In | 1 byte |
| `MSP2_SET_MZTC_PALETTE` | 0x2244 | In | 1 byte |
| `MSP2_SET_MZTC_ZOOM` | 0x2245 | In | 1 byte |
| `MSP2_SET_MZTC_SHUTTER` | 0x2246 | In | 0 or 1 bytes |
| `MSP2_SET_MZTC_IMAGE_PARAMS` | 0x2247 | In | 3 bytes |
| `MSP2_SET_MZTC_CORRECTION` | 0x2248 | In | 2 bytes |
| `MSP2_SET_MZTC_VIGNETTING` | 0x2249 | In | 0 or 1 bytes |

Every field is read and written individually. The wire layout never depends on compiler padding.

`MSP2_SET_MZTC_CONFIG` validates the whole request against the same limits the CLI enforces before it applies any field. A request that fails validation is rejected in full. It changes nothing.

## OSD integration

The element below is an ordinary INAV OSD item. Position and enable it through the OSD layout in the configurator or with the `osd_layout` CLI command, exactly like any other element.

| Element | Shows |
| --- | --- |
| `OSD_MZTC_STATUS` | A three letter link state: `OK`, `INI`, `FFC`, `REC`, `ALT`, `ERR` or `OFF`. Blinks on `ERR` and `OFF` |

## Application setups

The camera has no preset mechanism. The setups below are ordinary CLI settings. Save them as part of a normal INAV configuration.

### Fire detection

High contrast and a hot-biased palette make a heat source stand out. A short correction interval keeps the image stable as the scene heats up.

```
set mztc_palette_mode = IRON_RED_1
set mztc_brightness = 60
set mztc_contrast = 80
set mztc_digital_enhancement = 70
set mztc_spatial_denoise = 30
set mztc_temporal_denoise = 40
set mztc_ffc_interval = 3
save
```

### Search and rescue

Body heat is a small signal against a cool background. Denoising matters more than contrast here.

```
set mztc_palette_mode = WHITE_HOT
set mztc_brightness = 55
set mztc_contrast = 65
set mztc_digital_enhancement = 60
set mztc_spatial_denoise = 60
set mztc_temporal_denoise = 70
set mztc_ffc_interval = 5
save
```

### Surveillance

A long correction interval keeps the shutter from interrupting a static scene. Heavy denoising keeps a still image clean.

```
set mztc_palette_mode = BLACK_HOT
set mztc_brightness = 50
set mztc_contrast = 60
set mztc_digital_enhancement = 55
set mztc_spatial_denoise = 70
set mztc_temporal_denoise = 80
set mztc_ffc_interval = 15
save
```

### Rapidly changing environments

Frequent correction keeps the image calibrated when the ambient temperature moves quickly. The cost is more shutter interruptions.

```
set mztc_palette_mode = RAINBOW
set mztc_brightness = 50
set mztc_contrast = 55
set mztc_digital_enhancement = 50
set mztc_spatial_denoise = 40
set mztc_temporal_denoise = 30
set mztc_auto_shutter = TIME_AND_TEMP
set mztc_ffc_interval = 1
save
```

### Industrial inspection

Absolute temperature accuracy matters more than a pleasing image. Denoising stays low.

```
set mztc_palette_mode = FUSION_1
set mztc_brightness = 45
set mztc_contrast = 70
set mztc_digital_enhancement = 65
set mztc_spatial_denoise = 50
set mztc_temporal_denoise = 50
set mztc_ffc_interval = 2
save
```

## Troubleshooting

### The camera never connects

`mztc` shows `Connected: NO` and the error flags include 0x01 or 0x10.

1. Confirm `mztc_enabled` is `ON` and `mztc_port` names the UART the camera is on.
2. Confirm the `MZTC` serial function is assigned to that UART.
3. Confirm `mztc_baudrate` is 8, unless the camera has been reconfigured away from 115200.
4. Check that TX and RX are crossed.
5. Check the supply voltage against the camera's datasheet.
6. Run `mztc_reconnect` after each change.

### The camera connects and then drops out

The driver closes the port after three seconds without a valid reply. Intermittent dropouts usually mean marginal wiring or a supply that sags. `Link quality` in the `mztc` output falls before the link drops. Watch it as an early warning.

### The image is poor

Raise `mztc_digital_enhancement` and `mztc_contrast` for a flat scene. Raise `mztc_spatial_denoise` and `mztc_temporal_denoise` for a grainy one. Run `mztc_calibrate` if the whole image has drifted. Shorten `mztc_ffc_interval` if it drifts again quickly.

## Safety

A thermal camera adds weight and current draw. Check the all-up weight and the power budget before flying. Local rules on thermal imaging vary. Confirm what applies where you fly.
