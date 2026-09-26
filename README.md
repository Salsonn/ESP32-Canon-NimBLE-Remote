[![Bluetooth Remote for Modern Canon Cameras](./.github/cover.jpg)](https://www.youtube.com/watch?v=mM_tIqrD_5A "ESP32 Canon BLE Remote Library Demo")
[Demo Video](https://www.youtube.com/watch?v=mM_tIqrD_5A)

# ESP32 Canon BLE Remote Library
This library is a port of [maxmacstn's Canon BLE implementation]([url](https://github.com/maxmacstn/ESP32-Canon-BLE-Remote)) to the NimBLE stack to make improvements on heat and power consumption of the device.

## Features
* Single firing and focus commands.
* Pair and remember. (Paring is only required for initial connection)
* Auto re-connect.

## Installation
1. Install [ArduinoNvs](https://github.com/rpolitex/ArduinoNvs) library, uses to store paring data to ESP32 NVS storage.
2. Install this library from the following way.
  - [Platform IO Library Manager](https://platformio.org/lib/show/12410/Canon%20BLE%20Remote/)
  - Arduino IDE Library Manager
  - Manual installation by copying this repository to your `library` folder

## Usage
I'm highly recommend you to checkout [example code](https://github.com/maxmacstn/ESP32-Canon-BLE-Remote/blob/master/examples/simpleRemote/simpleRemote.ino) to see how to use it.

1.  On camera, go to Wireless Communication Settings > Bluetooth Function > set bluetooth function to Remote. Clear all existing connection (if necesary) and press Pairing.
2.  Call `pair()` function. If you're trying example code, press shutter button while booting ESP32 to enter pairing mode.
3.  For picture mode, enable remote shutter in drive mode menu (Self-timer:10s/Remote). For video mode, press menu button and enable remote control.

If paring doesn’t work, clear all existing connection, power off and re-insert battery (necessary), then try again. 

## Schematic for testing
[![example code](./.github/demo_diagram.jpg)](https://github.com/maxmacstn/ESP32-Canon-BLE-Remote/blob/master/examples/simpleRemote/simpleRemote.ino)


## Tested device(s)
- Canon EOS M50 (EOS Kiss M)

(Feel free to help me test with other cameras, but technically, it should work)

## Background Info
- [CB Remote Android App](https://github.com/iebyt/cbremote)
- [Canon DSLR Bluetooth Remote Protocol Reverse Engineering](https://iandouglasscott.com/2018/07/04/canon-dslr-bluetooth-remote-protocol/)

## To-do
- [ ] Add support for W/T buttons.
- [ ] Optimize and cleanup code.
- [ ] Eliminate other dependency.

Feel free to contribute!
