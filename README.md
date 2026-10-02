# Tally Counter for Flipper Zero

A simple tally counter app built for [Momentum](https://momentum-fw.dev) firmware
(it should also run on official firmware with a matching API version).

## Controls

| Button            | Action                           |
| ----------------- | -------------------------------- |
| OK / Up           | Add the current step             |
| Down              | Subtract the current step        |
| Left / Right      | Change the step size (1, 5, 10, 100) |
| Hold OK           | Reset to 0                       |
| Hold Up / Down    | Count continuously               |
| Back              | Save and exit                    |

The count and step size are saved to `/ext/apps_data/tally_counter/count.txt`.

## Building

Install [ufbt](https://github.com/flipperdevices/flipperzero-ufbt), point it at the
Momentum SDK, then build:

```sh
pip install ufbt
ufbt update --index-url=https://up.momentum-fw.dev/firmware/directory.json --channel=release
ufbt            # builds dist/tally_counter.fap
ufbt launch     # builds, installs and runs on a USB-connected Flipper
```

You can also copy `dist/tally_counter.fap` to `/ext/apps/Tools/` on the SD card
using qFlipper.
