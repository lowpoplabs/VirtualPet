# Changelog

All notable changes to Baandwidth Farms (a virtual pet that eats the Wi-Fi and Bluetooth it
hears nearby, by LowPopLabs). The version is the firmware's, in `firmware/version.txt`; the
simulator, the site and the release zip carry the same number.

## 0.4.16 - 2026-11-26

Nothing changes on the watch.

### Added
- **Site:** how to install the Android app, under "Install it", beside the watch's
  installer. The app is released from its own repository,
  [VirtualPet-Android](https://github.com/lowpoplabs/VirtualPet-Android).

### Changed
- **Site:** "What's next" is now two lists, one for the ESP32 watch (tuning, battery life,
  a GPS and 4G LTE add-on board) and one for Android (tuning, GPS, cellular, a Wear OS
  app). It has a link at the top of both pages.

## 0.4.15 - 2026-11-26

### Changed
- **The pet's breathing no longer ticks at the bottom.** It used to touch its lowest point
  for a moment and spring back; it now rests there as long as it does at the top. The
  movement is the same size.
- **On a device that draws the pet bigger than the watch does,** the pet's movements
  (breathing, hopping, looking about, sniffing and the rest) now go a pixel of that screen
  at a time, where they went in jumps of two or three. Nothing changes on the watch from
  this.

### Fixed
- **`look_sheet`,** the simulator's artwork tool, took every picture at the first moment
  of a movement whatever time it was asked for. It now pictures the moment asked for, and
  the same arguments always give the same picture.

## 0.4.14 - 2026-11-26

Nothing changes on the watch. This release is for other devices that run the same pet.

### Added
- **The preview of every form** can be started by a device that draws its own screens. It
  is refused unless Dev log is on, as on the watch, where it is a hold on the Settings
  title.
- **The farm's small pictures of retired pets** are now a list of rounded boxes that such a
  device can draw itself at any size. The watch's farm page draws from the same list and
  looks exactly as before.

## 0.4.13 - 2026-11-26

Nothing changes on the watch. This release is for other devices that run the same pet.

### Added
- **The pet at another size.** A device that shows the pet's square alone can say how big
  that square is. The pet is then drawn at that size, as smooth as on the watch, and not
  enlarged afterwards. The watch draws it at 400 pixels exactly as before.
- **Simulator:** `--pet-only 800` shows the pet's square at 800 pixels, and `look_sheet`
  takes a size as its last argument.

### Changed
- **`look_sheet` counts its time** and no longer measures it, so the same arguments give
  the same picture nearly every time.

## 0.4.12 - 2026-11-26

Nothing changes on the watch. This release is for other devices that run the same pet.

### Added
- **A device that draws its own screens** (a phone) can read the pet, its settings, its
  recent finds and a day's record, and can get the worked-out lines the pages show (the
  mood, the growth lines, "none this week", the farm's sentence, the line under each menu
  tile) from the same code the watch's pages use, so the two cannot come to say different
  things. See `app/include/pet_ui.h`.
- **The same device can do what the pages' buttons do:** step a setting, rename the pet,
  choose its favourite food, name a place, retire the pet and start over.
- **A pet-only screen.** A device can ask for the pet's square alone, with no strips, no
  line under the pet, no pages and no swipes. A tap still pats the pet.
- **Word of what happens.** A device can be told each find line and each news line as it
  appears, when the line goes, and when something the pages show has changed.
- **Simulator:** `--pet-only` shows the pet's square alone and prints each of these. A new
  test, `pet_ui_test`, runs the whole app that way.

### Changed
- **A name that is too long** is now cut on a whole character. The watch's keyboard only
  offers plain letters, so nothing changes there.

## 0.4.11 - 2026-11-25

Nothing changes on the watch.

### Changed
- **Site:** in "What's next", the Android app has moved from "Being thought about" to
  "Being worked on now". The Wear OS view of it is still being thought about.

## 0.4.10 - 2026-11-25

### Changed
- **What the pet does with a network or a device once it is heard** (what food it is,
  eaten once a day, the scrambled prints and place names that say where it is, the counts
  of one scan) has moved out of the watch's own code into the part shared with other
  devices, so they cannot drift apart. The watch eats, settles and logs as before.
- **In a very busy place,** with more than 64 Bluetooth devices in one scan, the count of
  devices heard no longer counts some of them several times.

## 0.4.9 - 2026-11-25

Nothing changes on the watch. This release is for other devices that run the same pet.

### Added
- **A device that looks after its own screen** (a phone) can say so. The pet then never
  dims or darkens the screen for being left alone, and Settings leaves out "Screen",
  "Wrist raise" and "Touch", which are about that.
- **Settings leaves out rows a device cannot use:** "Brightness" where the pet cannot set
  it, and "Wrist raise" where there is no motion sensor.
- **Dev log wording** can be given by the device, for one whose log holds no names: the
  word the row shows when on, and the paragraph that explains it.

## 0.4.8 - 2026-11-25

### Changed
- **Riding** is now told by comparing each scan with the last few, not with every network
  the pet has ever met. Two short drives from home had read as "carried" throughout,
  because the roads near home were full of networks it already knew; a pet would have
  seen fewer rides the longer it lived. The prints it compares are scrambled and kept in
  memory only.
- **On a ride the pet scans every 30 seconds** on battery instead of every minute,
  starting from the first scan in which the networks have changed. A minute is a long way
  in a car. A low battery and quiet hours still come first.
- **Dev log:** motion rows add `fixed=` and `same=`.

## 0.4.7 - 2026-11-23

### Added
- **More trackers.** Chipolo tags, Samsung tags not yet set up, tags on Google's Find Hub
  network, and any maker's tag that is away from its owner are now eaten as trackers
  and no longer as crumbs. Earbuds left behind are still earbuds.
- **Apple watches and phones** are told apart a little better: a watch looking for its
  phone is a watch, and a phone offering its hotspot is a phone.
- **LE Audio earbuds,** headphones and hearing aids that send no name are eaten as earbuds.
- **Dev log:** a service's data now shows the byte that says what kind of beacon it is,
  as `u=FEAA/40`. It says nothing about which beacon.

## 0.4.6 - 2026-11-23

### Changed
- **Car Wi-Fi is car food.** A car's hotspot used to count with the phone hotspots, toward
  the Scavenger, while only cars heard over Bluetooth led to the Racer; out on the road
  most car food is Wi-Fi. It is now in the cars group: it shows there on the diet chart,
  weighs on the Hunter's side as a hatchling, and leads to the Racer. A living pet's past
  car meals count the new way at once; days already on the diet chart stay as drawn.
  The "cars" favourite now covers it, and "hotspots" no longer does.

## 0.4.5 - 2026-11-23

### Changed
- **Naming the pet** has moved from Settings to the pet's own card (hold the pet): the
  pencil beside its name opens the keyboard.
- **Manual:** the seven animals are now named, with what to raise each one on and how to
  get a different one next time. What they look like is still left to find out.

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
