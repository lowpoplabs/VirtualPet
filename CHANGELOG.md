# Changelog

All notable changes to Baandwidth Farms (a virtual pet that eats the Wi-Fi and Bluetooth it
hears nearby, by LowPopLabs). The version is the firmware's, in `firmware/version.txt`; the
simulator, the site and the release zip carry the same number.

## 0.1.1 - 2026-11-21

### Added
- **Version in Settings:** the foot of the settings screen says which version the watch is
  running, so it can be compared with the one on the install page.

## 0.1.0 - 2026-11-21

First public release. Tested on the Waveshare ESP32-S3-Touch-AMOLED-2.06 watch; the firmware
also builds for the 2.16 inch square board, which has never been flashed.

### Added
- **Sensing:** passive Wi-Fi scans and Bluetooth LE scans turned into fifteen kinds of food.
  The pet never connects or pairs, and no network names or addresses are stored.
- **The pet:** stomach, mood, cravings, personality traits and eating moments with sound.
- **Growth:** egg, hatchling, juvenile (Grazer or Hunter) and seven adult forms decided by
  diet, over about twelve weeks at the built-in pace, with a life speed setting.
- **Screens:** diet, journal, recent finds, food-dex, badges, places, farm, pet card and
  settings.
- **Places:** habitats the pet recognises and you can name, place badges from network names
  (coffee shops, hotels, airports and so on), and off the grid.
- **Retirement and the farm:** a finished adult retires when its owner says so, and leaves an
  egg that inherits one of its traits. The farm keeps twelve.
- **Power:** plugged-in and battery modes, wrist raise, sleep on battery and a burn-in guard.
- **Settings files on the card:** tuning numbers, food values, and the food and place name
  lists, each checked before use.
- **Desktop simulator** and tests, a log replay tool, and a small web site with a manual and
  a browser installer.
- **Release zip** (`package.py`): the watch firmware with the browser installer, to run from
  your own computer, and the `esptool` command as the other way in.
