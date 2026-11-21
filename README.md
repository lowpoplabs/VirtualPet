# Baandwidth Farms

*A virtual pet that eats the Wi-Fi and Bluetooth it hears nearby.*

A critter that lives on an ESP32-S3 touch watch and eats the Wi-Fi networks
and Bluetooth devices it hears nearby. It never connects or pairs: Wi-Fi scans
are passive, and Bluetooth scans only ask nearby devices for their name. What
it eats decides which of seven animals it grows into over about twelve weeks;
then it retires to the farm and leaves an egg. The project is Baandwidth Farms
(two a's: it is a pun on "baa"); each owner names their own pet. The
repository and the firmware are still called VirtualPet. The design is in
[the build plan](Wi-Fi%20and%20Bluetooth%20virtual%20pet%20build%20plan.md).
By **LowPopLabs**.

<!-- lpl:links -->
**[Download v0.2.0](https://github.com/lowpoplabs/VirtualPet/releases/latest)** · **[Install](https://lowpoplabs.github.io/VirtualPet/)** · **[Changelog](CHANGELOG.md)** · **[Ko-fi](https://ko-fi.com/lowpoplabs)**
<!-- /lpl:links -->

## Install

For the Waveshare ESP32-S3-Touch-AMOLED-2.06 watch.

- **From the web site** (Chrome or Edge on a computer): open
  **<https://lowpoplabs.github.io/VirtualPet/>**, plug in the watch and press
  Install. Nothing is installed on your computer. The
  [manual](https://lowpoplabs.github.io/VirtualPet/manual.html) is there too.
- **From your own computer:** download the zip from the latest release and
  unzip it. Run `python -m http.server --directory installer`, open
  `http://localhost:8000` and press Install: the same site, served locally.
- **With esptool:** the command, with the address of each part, is in
  `INSTALL.txt` in the zip.

Erase the watch the first time only. When updating a watch that already has a
pet, do not erase, or the pet and its farm go too. Afterwards swipe up, open
Settings and set the clock. A microSD card (FAT32) is optional; it is for
changing the pet's numbers and name lists.

## Layout

| Folder | What it is |
| --- | --- |
| `app/` | Shared pet and screen code. Depends only on LVGL and `app/include/pet_app.h`. |
| `sim/` | Desktop simulator: the shared app in an SDL2 window sized like either board. |
| `firmware/` | ESP-IDF project for both boards. `main/board.h` is the only board-specific interface. |
| `site/` | The web site: what the pet is, a manual, and a browser installer. |

## Simulator (Windows)

Needs Visual Studio Build Tools; uses the CMake and Ninja from the ESP-IDF install.
The first build downloads SDL2 and LVGL.

```powershell
.\sim\build.ps1 -Run                                         # demo: random food, 1 minute = 1 pet hour
.\sim\build\virtualpet_sim.exe --replay foodlog-day1.csv     # relive a watch log, 10 log minutes per second
.\sim\build\virtualpet_sim.exe --replay foodlog-day1.csv --speed 2000
.\sim\build\virtualpet_sim.exe --save pet.bin                # keep the same pet between runs
.\sim\build\virtualpet_sim.exe --sound rare                  # eating sounds: off, rare or all (default)
.\sim\build\virtualpet_sim.exe --battery 80                  # as if on battery: screen goes dark after 15 s
.\sim\build\virtualpet_sim.exe --board square
.\sim\build\virtualpet_sim.exe --page diet                   # open a screen straight away
.\sim\build\virtualpet_sim.exe --screenshot frame.bmp
```

The pet in the window is live: its stomach drains, it eats in batches, gets
cravings and changes mood. Click it to pat it. `--speed` sets how many pet
seconds pass per real second.

Drag across the window to swipe, as on the device:

| Gesture | Screen |
| --- | --- |
| Swipe left | Diet: the week's food by group, growth progress, what it is drifting toward |
| Swipe right | Journal: today's entry, then the last seven days |
| Swipe up | Menu: food-dex, badges, places, farm, pet card, settings |
| Swipe down | Recent finds: the last eight meals (kinds of food only, no names) |
| Hold the pet | Its card: name, stage, age, traits |

Swipe the opposite way, or tap the title, to go back. A screen left alone
returns to the pet. With `--save pet.bin` the settings are kept in
`pet.bin.cfg`. `--page NAME` (diet, journal, recent, menu, dex, badges,
farm, pet, settings, name, reset, places) and
`--swipe left|right|up|down|hold|touch|press|title` are for checks and
screenshots.

## Firmware

Needs ESP-IDF 5.5 (installed with the Espressif Installation Manager).

```powershell
.\firmware\build.ps1 -Board watch                      # ESP32-S3-Touch-AMOLED-2.06
.\firmware\build.ps1 -Board square                     # ESP32-S3-Touch-AMOLED-2.16
.\firmware\build.ps1 -Board watch -Flash -Port COM5    # build, flash, open the monitor
```

Each board has its own build folder (`build-watch`, `build-square`) and
sdkconfig. Display, touch, card and audio come from Waveshare's board support
packages, selected in `firmware/main/idf_component.yml`.

### Dev log (logs names)

Settings has a "Dev log" row. It is "off" unless you turn it on, and then
only food types, values, signal strength and battery level go to
`/sdcard/foodlog.csv`. Set to "names", each network or device name and how
it was classified is written too, to the card and the serial log. That is
for tuning: treat a log made that way as private, and turn the setting off
before the device goes to anyone else. Addresses are never logged either
way. The card must be FAT32.

Two kinds of row are there for tuning, whichever way the setting is:

- `motion`, after every scan: whether the device was carried or lay still
  since the last one, the average shake (thousandths of g between
  readings), steps a minute, and the number of readings. A walk shows as
  a strong shake with a rhythm of steps, a drive as a small steady shake
  with none, a desk as nothing. The pet does not use these yet; they are
  to find out whether a walk can be told from a drive.
- `heard`: a network that was already eaten, heard at least 8 dB more
  strongly than before. A network is logged when eaten, which is usually
  from a distance, so this is what shows how close it really came. It
  carries the name only when the setting is "names".

## Tests

Built by `sim\build.ps1`, run on the desktop:

- `sim\build\food_test.exe` checks the food classifier and the seen list.
- `sim\build\pet_core_test.exe` checks the pet core: stomach, mood, eating
  rules, cravings and the underfed state.
- `sim\build\pet_brain_test.exe` checks the action picker, personality
  traits, the save format, sounds and the power plan.
- `sim\build\pet_growth_test.exe` checks life stages and which form each
  kind of diet produces, and retirement: what the farm keeps, what the
  egg inherits, what carries over, a full farm, and the lineage badges.
- `sim\build\pet_days_test.exe` checks the day records behind the diet
  chart, the journal, badges, and that older saves still load.
- `sim\build\pet_places_test.exe` checks habitats (settling in, leaving,
  coming back, slow scans), place-name matching, place badge tiers, off
  the grid, Homebody or Roamer, and the save format, including saves from
  before chains were remembered by spelling.
- `sim\build\pet_config_test.exe` checks the settings files: good files,
  each kind of mistake, and that every `-defaults.txt` copy reads back as
  the built-in values.

`sim\build\look_sheet.exe OUT.bmp STAGE FORM [MOOD [ACTION]]` saves a picture
of the pet alone in a chosen stage, form, mood and action, for checking
artwork.

## Replaying a log

`pet_replay` feeds a `foodlog.csv` from the watch through the pet core and
prints, hour by hour, what the pet would have eaten and how it would have
felt, then a summary by food.

```powershell
.\sim\build\pet_replay.exe foodlog-day1.csv
.\sim\build\pet_replay.exe foodlog-day1.csv --drain 0.8 --gap 120 --junk-limit 8
.\sim\build\pet_replay.exe foodlog-day1.csv --as-logged
```

Rows from a names-on log are re-classified with the current rules, so a
classifier fix shows up in old logs; `--as-logged` keeps what the watch
wrote. The other options try out different tuning numbers
(`--drain`, `--gap`, `--start`, `--repeat`, `--junk-limit`), and
`--growth 84` squeezes a whole life into one day of log to see which form
that day's diet would produce.

## Places

After each Wi-Fi scan the pet looks at the fixed networks around it
(routers and printers, not phones or cars).

- **Habitats.** Ten minutes among the same networks makes a habitat; up to
  twelve are kept. The one with the most time (two hours or more) is home.
  The Places screen lists them and lets you name each one. Settling
  somewhere new, or visiting somewhere that is not home, lifts the pet's
  mood and goes in the journal.
- **Homebody or Roamer.** A Grazer becomes a Roamer when less than 60% of
  its plain-network food was eaten in any one habitat.
- **Place badges.** A network name heard strongly (-70 dBm or better), or
  heard faintly and then again at least two and a half minutes later (so
  staying, not driving past), is
  checked against the place name lists: coffee shops, hotels, stores,
  airports, planes, trains, libraries, campuses, hospitals, museums,
  restaurants and
  parks. Each chain counts once; a general word such as "library" counts
  once a day. Bronze at 1, silver at 3, gold at 10. The built-in lists
  lean toward US chains; `place-names.txt` on the card replaces them
  (see Settings files).
- **Off the grid.** No Wi-Fi and no Bluetooth at all for an hour (bronze),
  three (silver) or eight (gold), while being carried for at least half of
  that time. A silent drawer earns nothing.

What is kept: for each network that marks a habitat, a 16-bit print made
from its address and a random number unique to the device, which cannot be
turned back into the address; the name you type for a habitat; and for
badges the kind of place, a count, and for each chain already counted a
16-bit number made from that chain's spelling in the list (not from the
network that was heard). No network names or addresses are stored. The log on the card gets a `place` row (new, arrive, leave,
badge, offgrid) with the habitat's number.

## Motion and the burn-in guard

The watch's motion sensor (a QMI8658, read as an accelerometer about ten
times a second) tells three things:

- **Wrist raise.** When it comes to rest with the screen facing up, after
  being turned away or really moved (an arm brought up, or picked up off a
  desk), on battery, the screen lights. Stood upright counts too, when
  that is a new angle (so hanging from a bag does not light it at every
  pause). A knock on the table is too short to count. Plugged in it is
  ignored. The last run's tallies are printed at the next start.
- **Carried or still.** On battery the pet scans every minute while it is
  being carried, every three minutes otherwise, and every five once it has
  lain still for ten minutes. Plugged in it scans every 30 seconds
  regardless.
- **Off the grid** only counts while carried (see Places).

Once a minute the pet's area and the two strips shift a few pixels along a
slow loop, so the same pixels are not lit for months on end.

## Sleep on battery

On battery the chip sleeps whenever every task is waiting and wakes for
the next timer; plugged in it stays awake, because a sleeping chip drops
off the USB port. With the screen off it wakes about ten times a second to
read the motion sensor, buttons and touch panel, and for each scan.

How well it slept is counted. After a spell on battery the next start
prints "last time on battery: N min, asleep X%", and the card log gets a
`sleep` row with the same figures.

Three things in the board package had to be worked around for sleep
(all in `firmware/main/board_bsp.c` and `sdkconfig.defaults`):

- Its touch reader stops the program when the panel does not answer, and
  an untouched panel dozes. The firmware reads the panel itself and takes
  no answer as "not touched".
- Its audio setup leaves a microphone channel running, which blocks sleep
  entirely. The watch sets up the speaker alone, running only during a sound.
- By default every pin is disconnected during sleep; that is turned off.

## Settings files on the card

The pet's numbers can be changed without rebuilding, from plain-text files
in the `pet` folder on the memory card:

- `tuning.txt`: stomach, mood, cravings, stage lengths, the speed
  multiplier (`growth_speed = 2016` runs a life in an hour), the
  evolution thresholds, and how strongly each personality trait pulls
  (`lazy_nap = 2` means a lazy pet is twice as drawn to a nap). A `growth_speed` line here wins over the "Life
  speed" setting, which then reads "by file".
- `foods.txt`: what each food type is worth.
- `food-names.txt`: the names that make a network or device a car, a
  hotspot, a printer, earbuds, a speaker, a watch or a TV.
- `place-names.txt`: the network names that count toward place badges.
- `journal.txt`: the sentences the pet writes its journal from.

In the first two each line is `name = number`; anything after a `#` is a
comment, and lines left out keep their built-in value. At every start the
pet writes a `-defaults.txt` copy of each file beside them
(`tuning-defaults.txt` and so on): the built-in values, with a note and
the allowed range on each line. Copy one to the name above and edit the
copy.

The two name files hold lists, `name = a, b, c`:

```
earbuds = AirPods, Buds, "QC "      # quotes keep the space at the end
coffee = costa coffee, costa_wifi   # one chain and its spellings
coffee_word = cafe, tea room        # general words, once a day
```

A food or kind of place the file leaves out keeps its built-in list; one
it gives is replaced. A chain is one line, and is remembered by its first
spelling, so chains can be added, removed and reordered without
disturbing badges already earned; changing a first spelling makes it a
new chain. Up to 32 chains are remembered for each kind of place.

Every entry is checked (numbers against their range, a few against each
other, names for length). A file with any mistake is ignored as a whole and the pet runs
on the built-in values; the home screen says which file and line at
start-up, and so does the serial log. With no card or no files it runs on
the built-in values without comment.

To edit on the watch: Settings, Card, "computer"; edit the file; switch
back to "pet", which restarts and reads it. In the simulator, `--card DIR`
points at a folder standing in for the card's `pet` folder. The checks are
in `sim/pet_config_test.c`.

The journal file is one sentence to a line, since sentences have commas
in them: `list = one sentence`, with as many lines for a list as it has
sentences.

```
quiet = Hardly a nibble all day.
quiet = Thin times.
best = Oh, {food}! Lovely.     # {food} is where the food's name goes
```

There are 21 lists, each for one thing a day can be (a quiet day, a new
place, a first taste, the closing line for each trait); `journal-defaults.txt`
names them all. Sentences are 60 letters at most and in plain letters
only, because the screen's font has no accented ones. The three lists
that name a food (`new_food`, `best`, `mostly`) need `{food}` once in
every sentence, and no other list may have it.

Quiet hours stay in the settings screen. Badges stay built in: each one
is a rule in the code, so a file could only rename them. Traits are the
same six, but how strongly each one pulls is in `tuning.txt`.
`pet_replay` does not read the card files yet; it classifies with the
built-in name lists.

## Web site

`site/src` holds a small static site: a page about the pet with a browser
installer, and a manual. It is published at
<https://lowpoplabs.github.io/VirtualPet/> by `.github/workflows/pages.yml`,
which takes the `installer/` folder out of the latest release zip each time a
release is published, so the site always offers the released firmware. The pages say
`{{NAME}}` wherever the project's name belongs, and the build fills it in.

```powershell
.\site\build.ps1                      # into site\out, as Baandwidth Farms
.\site\build.ps1 -Version 1.0
```

The build fills in the name and version, copies the watch firmware from
`firmware\build-watch` (build that first) and writes `manifest.json` for
the installer. `site\out` is then a folder of plain files that any static
host can serve, GitHub Pages included. To look at it locally:
`python -m http.server --directory site\out`.

- The installer is [ESP Web Tools](https://esphome.github.io/esp-web-tools/),
  release 10.5.2, kept in `site/src/vendor/esp-web-tools` and served from
  the site itself, so the pages run no script from anywhere else. Its
  `VERSION.txt` gives the source, the hash it was checked against, and how
  to update it. It needs Chrome or Edge on a computer, and the page must
  be served over https (or from localhost).
- Both pages carry a content security policy that allows scripts only
  from the site. The one thing fetched from outside is the two fonts,
  from Google Fonts.
- The firmware is listed as its separate parts, not joined into one file.
  The pet's save sits in flash between two of them, and one joined file
  would write blanks over it at every update.
- The manual's screenshots come from the simulator: take them with
  `--screenshot` and convert with `python site\topng.py IN.bmp OUT.png`.
- The site does not show the adult forms. They are meant to be a surprise.

## Crash record

Each start logs why the chip restarted and the last few reasons. A crash
also records its message and call chain (`crash:` lines in the serial log),
and the card log gets a `start` row. This survives a restart but not a loss
of power. Decode the addresses with `xtensa-esp32s3-elf-addr2line -pfiaC -e
firmware/build-watch/<app>.elf <addresses>`.

## Status

Build stages 1 to 6 are in place: board bring-up, sensing, the pet core
(stomach, mood, actions, traits, eating moments with sound, saving),
growth (egg, hatchling, juvenile, adult forms), the screens (diet,
journal, recent finds, food-dex, badges, pet card, settings) and places
(habitats, place badges, off the grid). Battery,
carry and desk modes from stage 7 are in too. The square board has never
been flashed.

Life speed sets how long a whole life takes: about 12 weeks on "slow"
(the built-in pace), 6 on "medium" and 3 on "fast". It scales the days
and meals each stage asks for; hunger, mood and the calendar stay on real
time. Changing it does not age the pet: its age and its share of the
current stage stay as they were, and only time from then on runs at the
new speed. The same holds when `growth_speed` in `tuning.txt` changes,
because the save records the speed the pet was living at.

Preview of every form (hidden): with Dev log set to "names", hold the
"Settings" title at the top of the settings screen. With Dev log off the
hold does nothing, so nobody spoils the forms by accident. The home screen then draws the pet as each stage and
form in turn, eleven in all, moving on at each tap, with the name on the
line under it. It ends after the last one, or after a minute left alone.
Only the drawing changes; the pet itself is untouched. Nothing on screen
points to it, because the forms are meant to be a surprise.

Settings: name, brightness, sound, volume, quiet hours, clock, screen
(stays on or sleeps), pause, life speed, wrist raise, card and dev log, and "Start over" at
the bottom, which after a three second hold replaces the pet and all it
had with a new egg (settings stay). "Card: computer" lends the memory
card to a computer as a USB drive (watch only so far); while it is lent the
USB port is not the serial port, so switch back to "pet" (the device
restarts) before flashing. If the port ever stays missing, unplug the
cable, hold the power button until the screen goes dark, and plug it back
in (the watch has no reset button; a full power-off is what clears it). They are stored apart from the pet. The clock setting
changes hours and minutes only, not the date.

From stage 7, the burn-in guard, the motion sensor and sleep on battery
are in, and so are the settings files on the card for numbers and name
lists. Stage 8, generations, is in too (see Retirement and the farm).
Still to come: the remaining card files.
The pet is drawn by code in smooth shapes, not pixel-art sprites as the
plan first had it: the hatchling's look is the style, and each form is
being designed one at a time in it. The animals are the adult forms. A
juvenile is an older hatchling: bigger, its green warming toward the
Wi-Fi line (Grazer) or cooling toward the Bluetooth line (Hunter). Wi-Fi
adults have antennae and Bluetooth adults have ears; the Prism has both.
All seven adults have their looks. The Wi-Fi line is complete: the
Homebody is a highland cow with antennae for horns and a shaggy fringe,
the Roamer is a monarch butterfly whose wings slowly beat, and the
Scavenger is a coral crab with blue pincers that snap and wave. On the
Bluetooth line, also complete, the Singer is a lilac fennec fox with
huge ears and an antenna tip on its tail, the Racer is a pink hare with
very long ears, one of them wearing two slanted racing stripes, and the
Glowbug is a periwinkle dumbo octopus whose ears are the two fins on its
head, with spots and a belly that pulse with light. The Prism, for a
balanced diet, is a unicorn: ears, a horn that is its one antenna, and a
rainbow mane and tail, with a body colour that drifts through the
rainbow.

### Retirement and the farm

An adult that has lived its full life (63 days and its meals, at the slow
speed) is ready to retire. It says so once on the home screen, the bell
shows there as it does for a craving, its card says why, and the Farm
tile reads "ready to retire". Then it waits: nothing happens
until the owner opens Farm, taps "Retire", and holds the button for
three seconds.

- **The farm** keeps up to twelve retired pets, shown as small pictures
  of the form each retired as. Tap one for its name, form, generation,
  how long it lived, its meals and its traits. When a thirteenth retires
  the oldest moves on and is only counted.
- **The egg** it leaves is the next generation. It takes one of its
  parent's two traits and draws the other afresh.
- **What stays:** the farm, the badges, the habitats and place badges,
  and the settings. **What starts over:** the name, the diet and its
  history, the food-dex and the journal, since those belong to one pet
  and decide what it grows into.
- **Badges** for the line: a pet raised to retirement, a pet grown into a
  Prism, each of the six ordinary adult forms retired, and the fifth
  generation.
- "Start over" in Settings still wipes everything, the farm included.

At the moment of retiring the pet hops out of the side of the picture to
a fanfare, and its egg comes in from the other side.

Simulator extras: `--carry` (plugged in, screen sleeps when idle),
`--growth 2016` (egg to adult in about a minute) and `--travel 20` (after
20 seconds the pretend surroundings change from home to a coffee shop).

## Packaging a release

The version is `firmware/version.txt`; the firmware, the site and the zip all
read it. Build the watch firmware, then:

```powershell
python package.py        # releases\BaandwidthFarms-<version>.zip
```

It builds the site, checks that the firmware carries that version, and zips
the installer with the firmware, `INSTALL.txt`, this file, the changelog and
the licence.

## Compatibility

- **Watch:** Waveshare ESP32-S3-Touch-AMOLED-2.06. This is what releases are
  built for and tested on.
- **Square board:** Waveshare ESP32-S3-Touch-AMOLED-2.16. The firmware builds
  for it and has never been flashed; build it yourself.
- **To build:** ESP-IDF 5.5. The simulator needs Windows and Visual Studio
  Build Tools.

## Credits

[LVGL](https://lvgl.io) draws the screens, Waveshare's board support packages
drive the hardware, and [ESP Web Tools](https://esphome.github.io/esp-web-tools/)
(Apache-2.0, kept in `site/src/vendor`) is the browser installer.

## Support

Provided as-is. Bug reports welcome via GitHub Issues. No Discord, no custom
work, no promises on turnaround. If it saved you time or you enjoy it:

[![Ko-fi](https://ko-fi.com/img/githubbutton_sm.svg)](https://ko-fi.com/lowpoplabs)

## License

[MIT](LICENSE). ESP Web Tools in `site/src/vendor` keeps its own licence.
