# Changelog

All notable changes to Baandwidth Farms (a virtual pet that eats the Wi-Fi and Bluetooth it
hears nearby, by LowPopLabs). The version is the firmware's, in `firmware/version.txt`; the
simulator, the site and the release zip carry the same number.

## 0.4.4 - 2026-11-22

### Changed
- **Diet screen:** "day 2 of 14" counts the pet's days, which on "medium" and "fast" Life
  speed are shorter than ours, and nothing said so. At those speeds the line now adds the
  real time left in the stage: "day 2 of 14, about 3 real days left".

## 0.4.3 - 2026-11-22

### Added
- **Bluetooth cars:** the pet had eaten car hotspots over Wi-Fi but never a Bluetooth car in
  four days, because a car's own stereo uses the older Bluetooth that the pet cannot hear.
  It now knows what cars do send: a Tesla listening for its owner's phone, and things added
  to a car (diagnostic dongles, dash cameras, wireless phone-link boxes, fitted stereos,
  hands-free kits and tyre sensors).
- **More car makers:** Mercedes, Dodge, Cadillac, Acura and Infiniti count as cars by name,
  over Wi-Fi and Bluetooth.

## 0.4.2 - 2026-11-22

### Added
- **Touch setting:** "off" stops a touch from lighting a dark screen, for carrying the pet in
  a pocket, where cloth and bumps wake it again and again. The side button still lights it,
  and a lit screen works as usual. An evening in a shirt pocket had drained the battery at
  twice the usual rate while the scanning stayed the same.
- **Dev log:** a `wakes` line beside each `battery` line counts what lit the screen since the
  last one (touch, raise or button) and the seconds it stayed lit.

## 0.4.1 - 2026-11-21

### Fixed
- **Riding:** a 45 minute drive in a truck was mostly missed, because the truck shook the
  watch like a walk, and an evening sat by a fire with no network in earshot was taken for
  a ride. A ride is now told by networks going by: three scans while carried that hear
  networks the pet has not met and few or none that it has. It lasts through five scans of
  empty road. Shake and steps are no longer part of it.

### Changed
- **Dev log:** the `motion` line also says how many of the networks a scan heard were new
  (`new=`), which is what the riding rule goes by.

## 0.4.0 - 2026-11-21

### Added
- **Favourite food:** while the pet is growing, a button on its card picks one food group
  (networks, hotspots, music, cars or phones). That group counts five times over when the
  form is decided (`favourite_weight` in `tuning.txt`), so an owner where there is nearly
  all Wi-Fi or nearly all Bluetooth can still steer what the pet grows into. Meals, mood,
  the diet chart and Prism are unaffected. Saves from earlier versions load with no favourite.

## 0.3.2 - 2026-11-21

### Added
- **Riding:** the pet can tell a ride in a car, bus or train from a walk. A ride is a small
  steady shake with no step rhythm while the networks around are few and faint, for two
  scans in a row. A day with a ride can say so in the journal (the `drove` sentences), and
  the food log's motion rows say "riding". Scanning is unchanged.

## 0.3.1 - 2026-11-21

### Added
- **Public safety places:** fire, police and rescue stations count toward a new place badge.
  They are matched by whole phrases such as "fire dept" and "sheriff", so a joke name on a
  home network does not count.
- **Site:** a "What's next" section on the home page.

## 0.3.0 - 2026-11-21

### Added
- **Text size:** a Settings row, "normal" or "large". Large sets every page, the name
  keyboard and the stage label on the home screen in bigger letters, and rows grow to suit.

### Fixed
- A long badge description now wraps onto a second line, and a long status on a menu tile
  is cut short with dots; both used to run off the edge.

## 0.2.1 - 2026-11-21

### Changed
- The hidden preview of every form (hold the Settings title) now opens only while Dev log is
  on, so a stray hold cannot spoil the forms.

## 0.2.0 - 2026-11-21

### Added
- **`journal.txt` on the card:** the sentences the pet writes its journal from can be
  replaced, a list at a time. One sentence to a line; `{food}` marks where a food is named.
  `journal-defaults.txt` is written beside it with the built-in sentences.
- **Trait numbers in `tuning.txt`:** how strongly each personality trait pulls the pet
  toward napping, playing, sniffing and the rest, thirteen numbers in all.

### Changed
- Badge definitions stay built in. The plan had them moving to the card, but each badge is a
  rule in the code, so a file could only have renamed them.

## 0.1.1 - 2026-11-21

### Added
- **Version in Settings:** the foot of the settings screen says which version the watch is
  running, so it can be compared with the one on the install page.
- **Ko-fi link** at the foot of the web site's two pages.

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
