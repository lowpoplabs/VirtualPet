# Wi-Fi and Bluetooth virtual pet: build plan

Nov 17, 2026 · LowPopLabs

## Overview

The pet is an invented critter that lives on an ESP32-S3 touch board and eats the Wi-Fi networks and Bluetooth devices it detects nearby. It combines Pocket Tank's look with pwnagotchi's personality, and it never connects to anything.

- **Scanning only:** passive Wi-Fi scans and active Bluetooth LE scanning, which asks nearby devices for their name using a new random address each scan. Passive Bluetooth scanning was tried first and could not tell speakers, earbuds or cars from unknown devices. No promiscuous mode, deauth, handshake capture, connecting or pairing.
- **Privacy:** a device or network name can show on screen while it is being eaten. Only food-type counts, badges and scrambled habitat fingerprints are saved. No MAC addresses or network names are stored.
- **One pet at a time:** it grows and evolves by diet over about three months, then retires to a farm and leaves an egg.
- **Built to share:** rules, text and art live in editable files on the microSD card, so others can change or add to them without reflashing.

## Hardware

The two targets are nearly the same board inside, so one codebase covers both.

|  | [ESP32-S3-Touch-AMOLED-2.06](https://docs.waveshare.com/ESP32-S3-Touch-AMOLED-2.06) (watch) | [ESP32-S3-Touch-AMOLED-2.16](https://docs.waveshare.com/ESP32-S3-Touch-AMOLED-2.16) (board) |
| --- | --- | --- |
| Chip | ESP32-S3R8, 8MB PSRAM | ESP32-S3R8, 8MB PSRAM |
| Flash | 32MB | 16MB |
| Screen | 2.06 inch AMOLED, 410 x 502 | 2.16 inch AMOLED, 480 x 480 |
| Display driver | CO5300 over QSPI | CO5300 over QSPI |
| Touch chip | FT3168 | CST9220 |
| Motion sensor | QMI8658 6-axis | QMI8658 6-axis |
| Clock chip | PCF85063 | PCF85063 |
| Power chip | AXP2101 | AXP2101 |
| Also on board | microSD slot, speaker, microphones, battery header | microSD slot, speaker, microphones, battery header |

Specs are from Waveshare's product pages for the [2.06](https://www.waveshare.com/product/esp32-related/displays/amoled/esp32-s3-touch-amoled-2.06.htm) and the [2.16](https://www.waveshare.com/product/iot-communication/short-range-wireless/esp32-s3-touch-amoled-2.16.htm).

Design consequences:

- **Layout:** the pet lives in a centered 400 x 400 square that fits both screens. The watch's extra height holds the status strips.
- **Storage:** all firmware and built-in data must fit the 16MB board.
- **Touch:** two small touch drivers behind one interface. Everything above that is shared.
- **Look:** black background with a bright critter, which suits AMOLED and saves power.
- **Bluetooth:** the ESP32-S3 has Bluetooth LE only, no Bluetooth Classic. Cars and speakers are detected only when they advertise over LE, so those foods will be rarer.
- **Carry modes:** on a wrist, or hung on a backpack with a USB power bank plugged in.

## Sensing and food

Every detected network or device is classified into a food type with a value. Anything matched by name is a guess, so the name lists must be easy to extend.

### Wi-Fi foods

| Food | Detected by | Value |
| --- | --- | --- |
| Secured network | WPA2 flag | 1 |
| WPA3 network | WPA3 flag | 2 |
| Open network | No encryption | 2, junk food |
| Printer | Name (HP-Print, DIRECT-, EPSON) | 2 |
| Phone hotspot | Name (iPhone, Galaxy, Pixel) | 3 |
| Hidden network | Blank name | Random 1 to 5 |
| Car hotspot | Name (car brands) | 5 |

### Bluetooth foods

| Food | Detected by | Value |
| --- | --- | --- |
| Unknown device | Nothing matched | 0.5, crumbs |
| Phone | Manufacturer ID | 1 |
| Watch or band | Appearance field | 2 |
| Earbuds | Appearance field or name | 2 |
| Tracker tag | Service ID | 2 |
| TV or media box | Appearance field or name | 2 |
| Speaker | Name (JBL, Bose, Sonos) | 3 |
| Car | Name (SYNC, Uconnect) | 5 |

### Eating rules

- **Stomach:** a 0 to 20 scale that drains about 1 per hour.
- **Diminishing returns:** each food type gives full value the first time in a day, then half, then a quarter. This also handles phones that change their address every few minutes.
- **Junk food:** open networks fill the stomach, but too many in a day makes the pet queasy.
- **Variety bonus:** a mood boost for each different food type eaten that day.
- **Batching:** in a busy place the pet eats in batches with a minimum gap between eating moments.
- **Fresh air:** no networks or devices for an hour while the motion sensor shows walking boosts mood instead of filling the stomach.

## The pet

One invented critter grows through four stages over about three months, and its diet decides which adult form it becomes.

### Life stages

| Stage | Lasts | Notes |
| --- | --- | --- |
| Egg | Under an hour | Hatches after its first handful of meals |
| Hatchling | About 1 week | Same for every pet: round body, two stubby antennae, big eyes |
| Juvenile | About 2 weeks | Splits into Grazer (Wi-Fi) or Hunter (Bluetooth) |
| Adult | About 9 weeks | Form set by dominant food, then retires to the farm |

### Forms

| Form | Branch | Diet that produces it | Look |
| --- | --- | --- | --- |
| Grazer | Juvenile | Mostly Wi-Fi | An older hatchling: bigger, longer antennae, warming to yellow-green |
| Hunter | Juvenile | Mostly Bluetooth | An older hatchling: bigger, slimmer, cooling to soft blue |
| Homebody | Grazer adult | The same few networks every day | Highland cow: ginger coat, antennae for horns, shaggy fringe |
| Roamer | Grazer adult | Many different networks | Monarch butterfly: orange wings that beat, slim amber body, antennae |
| Scavenger | Grazer adult | Open networks and hotspots | Crab: coral with blue pincers that snap and wave, uneven antennae |
| Singer | Hunter adult | Speakers and earbuds | Fennec fox: lilac, huge ears, bushy tail with an antenna tip |
| Racer | Hunter adult | Cars | Hare: pink, very long ears, slanted racing stripes on one ear |
| Glowbug | Hunter adult | Phones and wearables | Dumbo octopus: periwinkle, ear fins that flap, rippling arms, pulsing light |
| Prism | Special | Balanced across everything | Unicorn: ears, a horn for an antenna, rainbow mane and tail, shifting color |
| Static | Special | Underfed | Scrawny, flickering, recovers with meals |

### Evolution rules

- **Branch points:** every meal adds its value to a running total for that food's group.
- **Juvenile split:** whichever of Wi-Fi or Bluetooth has more points.
- **Adult form:** the food group with the biggest share inside that branch. Homebody and Roamer are separated by how many different habitats the Wi-Fi points came from.
- **Prism:** no single group above about a quarter of the total, and at least 10 food types tasted.
- **Static:** stomach near empty for two days straight. It is a recoverable state, and the pet never dies of neglect.

### Growth rules

- **Meals and time both count:** each stage needs a minimum number of meals as well as days, so a pet left in a drawer pauses.
- **Speed setting:** one multiplier in config, so a whole life can run in an hour for testing.
- **Generations:** a retired adult moves to the farm, and the new egg inherits one trait from it. The owner decides when: a pet that has finished its adult life waits until it is retired from the Farm screen. The farm holds twelve; after that the oldest moves on. Badges, habitats, place badges and settings carry over; the name, diet, food-dex and journal start fresh with each egg.

## Brain

Behavior runs on a state machine with three additions that keep it from feeling scripted. A small on-device AI model is a stretch goal.

- **Weighted choices:** each possible action is scored by mood, hunger and surroundings, then one is picked with some randomness.
- **Personality traits:** each egg gets random traits such as lazy, greedy or curious, which shift those scores so no two pets act alike.
- **Template pools:** journal lines and thoughts are assembled from a few hundred text fragments, so repeats are rare.
- **Swappable picker:** the step that chooses the next action is one replaceable piece, so a trained model can take its place later without touching anything else.

## Engagement

The core hook is that going somewhere new feeds the pet something it cannot get at home. A three-month life gives the owner time to plan enrichment trips.

### Daily loop

- **Live reactions:** the pet sniffs when food appears, perks up in busy places and gets bored at home.
- **Cravings:** it asks for something it is short on, such as "wants something musical". Satisfying a craving gives a bigger boost than a normal meal.
- **Diet readout:** a chart of the week's food, so gaps are obvious.
- **Evolution nudges:** during the juvenile stage, hints show which adult form it is drifting toward. The owner can nudge back: a favourite food group picked on the pet's card counts several times over toward the form, so every form stays within reach where there is nearly all Wi-Fi or nearly all Bluetooth.
- **Journal:** an end-of-day entry in the pet's voice, such as "went for a drive today".
- **Food-dex:** every food type ever eaten, with silhouettes for the undiscovered ones.
- **Habitats:** the pet recognizes home versus somewhere new from the mix of networks around it, and the owner can name a habitat when it is discovered.

### Badges

Badges belong to the owner and carry across generations. Each pet also keeps its own smaller record. Badges are display only for now.

| Type | Examples |
| --- | --- |
| Device types | First of each food, all Wi-Fi foods, all Bluetooth foods, full food-dex |
| Quantities | 100 phones eaten, 1,000 meals, 50 foods in one day |
| Places | Coffee shops, hotels, stores, airports, in-flight Wi-Fi, trains, libraries, universities, hospitals, museums, parks, restaurants, public safety (fire, police and rescue stations) |
| Parks | Named visitor-center networks where they exist, as top-tier badges |
| Off the grid | No signals for an hour while walking, with wilderness tiers |
| Rarity | First car, three rare foods in one day, a hidden network worth 5 |
| Care | A week without going hungry, a perfectly balanced day, a pet raised to retirement |
| Lineage | Each adult form raised once, a Prism, five generations |

Place badges come from matching network names on the device, and only the badge is saved. Tiers come from counts: one coffee shop is bronze, ten different hotel brands is gold.

Things to avoid: punishing streaks and death from neglect. A gentle streak that only adds bonuses is fine.

## Screens, controls and sound

The home screen carries almost everything, because the device gets glanced at for a second or two.

### Home screen

- **Pet:** centered on black, animated by mood.
- **Top strip:** a small clock in the corner and the battery level.
- **Bottom strip:** stomach bar, mood icon and the current craving.
- **Scanning cue:** the pet sniffs or its antennae twitch while a scan runs.
- **Eating moment:** the food pops in with a short label such as "speaker: JBL Flip", then the pet eats it. The name is shown and not saved.

### Navigation

| Gesture | Goes to |
| --- | --- |
| Swipe left | Diet: the week's food chart and evolution hints |
| Swipe right | Journal: today's entry first, older days below |
| Swipe up | Menu grid: food-dex, badges, farm, pet info, settings |
| Swipe down | Recent finds: the last few meals |
| Tap the pet | It reacts and gets a small mood boost, with a cooldown |
| Long press the pet | Its card: name, age, stage, traits |
| Side button | Screen on and off |
| Tap or wrist raise | Wake the screen |

Nothing depends on more than one button, so the same controls work on both boards.

### Sound

- **Sniff cue:** a short chirp a second or two before the munch, so there is time to look.
- **By rarity:** a soft munch for common food, a brighter jingle for rare food, a fanfare for a new food-dex entry or badge.
- **Throttling:** a minimum gap between sounds in busy places.
- **Levels:** off, rare finds only, or everything.
- **Quiet hours:** silent overnight by default.

### Settings from the start

Brightness, sound level, quiet hours, set the time, name the pet, backpack screen always-on, and pause for when the device sits on a shelf.

## Art

The pet is drawn by code in smooth, rounded shapes, with eyes that carry the mood in the way pwnagotchi, EMO, Vector and Wall-E do. The hatchling's look sets the style. An earlier version of this plan called for 96 x 96 pixel-art sprites; that was dropped because the smooth hatchling looked better, and a smooth hatchling growing into a pixel-art adult would look like two different pets.

- **One form at a time:** each form is designed on its own in the hatchling's style, starting from an animal or idea, and approved from a picture before it goes on the device. The Homebody is a highland cow: ginger coat, antennae swept out like horns, and a shaggy fringe between them. The Singer is a fennec fox: lilac, with huge ears and a bushy tail that ends in an antenna tip. The Roamer is a monarch butterfly: orange wings with pale spots that slowly beat, and the hatchling's antennae. The Scavenger is a crab: wide and coral, with a blue crab's blue on its pincers, leg tips and back, and pincers that snap as it eats and wave when it plays. A tin-can shell and a spiral shell were both tried for it and dropped, because neither read clearly at a glance. The Racer is a hare: pink, with very long ears blown to one side, two slanted white racing stripes across one ear, whiskers, a puff of a tail and long back feet. The Glowbug is a dumbo octopus: periwinkle, with two short ear-shaped fins tilted up from its head that flap, six rippling arms in place of feet, and spots and a belly that pulse with light. It was chosen over a bush baby so that the Bluetooth line (fox, hare, octopus) is as varied as the Wi-Fi line (cow, butterfly, crab). The Prism is a unicorn: pointed ears, a striped gold horn that is its one antenna with the glowing tip on top, and a rainbow forelock, mane and tail, on a body whose color drifts through the rainbow. An axolotl, a dragon and a chameleon were drawn alongside it and the unicorn was chosen.
- **Antennae for Wi-Fi, ears for Bluetooth:** every Wi-Fi adult has antennae in some form and every Bluetooth adult has ears. The Prism has both.
- **Animals are the adult forms:** the seven adults each get an animal of their own. A juvenile is not one: it is an older hatchling, bigger and with its color drifting toward its line, so the stage reads as growing up and the adult is the reveal.
- **Shared base, own features:** every form starts from the same round body, feet, eyes and mouth, set by a few numbers (color, size, antennae, ears). A form with a look of its own adds its features on top, such as the cow's horns, fringe and muzzle.
- **Eyes and mouth drawn live:** they blink, squint, widen, track, chew and yawn without a frame per expression, and work the same on every form.
- **Starter animations:** idle, blink, sniff, eat, happy and sleepy.
- **Silhouette first:** each form should be recognizable from its outline alone.
- **Color per branch:** greens and warm tones for the Wi-Fi line, purples and pinks for the Bluetooth line.
- **Edges:** dark outlines disappear on a black background, so shapes are solid color with no outline.
- **In the firmware, not on the card:** the art is code, so a new form is a code change and not an image file. This gives up the idea of anyone adding a form in a pixel editor.

## Power

Two modes switch automatically, because the power chip can tell when the board is plugged in.

|  | Plugged in (backpack) | On battery (wrist) |
| --- | --- | --- |
| Scanning | About every 30 seconds, alternating Wi-Fi and Bluetooth | A short burst every few minutes, sleeping in between |
| Screen | Always on and dimmed, brightens on events; can be turned off in settings | Off after about 15 seconds |
| Wake | Tap | Tap, button or wrist raise |
| Extras | Burn-in guard: the pet wanders and the strips shift slightly | Catch-up replay of meals eaten while the screen was off |

### Motion states

The motion sensor is combined with how fast nearby networks change. Battery mode scans more when moving and backs off when still.

| State | Step rhythm | Networks |
| --- | --- | --- |
| Still | No | Same every scan |
| Walking | Yes | Changing slowly |
| Riding | No | Changing quickly |

### Both modes

- **Night:** during quiet hours the pet sleeps and scans rarely.
- **Low battery:** drowsy around 15%, then it saves and stops scanning around 5%.
- **Saving:** progress is written after each batch of meals and every few minutes.
- **Dead battery:** time is treated as paused, so the pet wakes where it left off. If the clock chip lost the time, the pet asks for it to be set.
- **Radio sharing:** Wi-Fi and Bluetooth share one radio, so scans alternate.

## Config on the card

Anything a contributor might want to change lives in plain-text files on the microSD card, in a format such as JSON.

- **On the card:** food name patterns, food values, evolution thresholds, stage lengths and the speed multiplier, how strongly each personality trait pulls, the journal's sentences, and place-name patterns. The art is not on the card (see Art).
- **Not on the card, by decision:** badge definitions and the kinds of food and trait themselves. Each is a rule in the code, so a file could only rename them, and that was not worth a file.
- **In the firmware:** a built-in copy of the defaults, so the pet works with no card or a broken file.
- **Validation:** a file with an error falls back to the defaults and shows a small warning, so a typo cannot break the pet.
- **Regional lists:** the starter place patterns will lean toward US chains, and others can add their own.

## Build order

Each stage ends with something that runs. Sensing goes early because detection quality and battery life are the biggest unknowns.

1. **Board bring-up:** screen, touch, card, sound and power chip working on both boards, plus a desktop simulator for fast testing. Pocket Tank's repo, which has an LVGL and SDL2 simulator, is a reference for ideas only: it was MIT-licensed up to v0.3.9.2 and has been under the PolyForm Noncommercial License since, so none of its code is copied here.
2. **Sensing:** scans turned into food events and logged to the card. Carry it for a week to tune the food list and measure battery life.
3. **Pet core:** stomach, mood, the state machine, the hatchling with code-drawn eyes, the eating moment with sound, and saving.
4. **Growth:** stages, evolution rules, personality traits and the speed setting.
5. **Screens:** diet, journal, food-dex, badges and settings.
6. **Places:** network-name badges, habitats and off the grid.
7. **Power and polish:** the two power modes, burn-in guard and config validation, then a public release.
8. **Generations:** retirement, the farm and inherited traits. Built, with a short farewell: the pet hops out of the picture and its egg comes in.

## Stretch goals

- **AI brain:** train a small on-device model to replace the action picker. It doubles as a first model-training project.
- **Cosmetics:** parts or farm decorations unlocked by badges.
- **Pet meetups:** two devices that detect each other trade a food or leave a visitor in each other's farm.
- **Owner's Wi-Fi:** an opt-in connection to a network the owner sets up, for clock sync and firmware updates. Off by default, and the only case where the pet connects to anything.

## Open questions

None of these block starting stage 1.

- [x] Name: the project is Baandwidth Farms, spelled with two a's for the pun. Each owner names their own critter. No sheep is planned; one may turn up later if it fits.
- [x] Eyes are drawn by code, and so is the body: smooth shapes, no sprites (see Art).
- [ ] Measure real battery life on the watch. No numbers exist yet, and they decide how aggressive battery mode must be.
- [ ] Check whether the Anker power bank stays on at this low a draw, or needs its trickle-charge mode.
- [ ] Measure how many cars and speakers actually advertise over Bluetooth LE, since that sets how reachable Racer and Singer are.
- [ ] Collect real network and device names to build the starter pattern lists.
- [x] Firmware framework: ESP-IDF with LVGL. No sprite file format is needed, since the art is drawn by code.
- [ ] Tune the numbers in play: food values, stomach drain, stage meal minimums and evolution thresholds.
- [ ] Decide where the Wi-Fi password is kept for the stretch goal, so it is not in a plain text file on the card.
