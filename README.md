# cardbuddy

[fm1buddy](https://github.com/konsumer/fm1buddy) for the [M5 Cardputer (ADV)](https://docs.m5stack.com/en/core/Cardputer-Adv): swap, back up and recover the M-VAVE FM-1's firmware, and manage its sounds, samples and songs, with a keyboard instead of a touchscreen. It's an app like any other for [M5 Launcher](https://bmorcelli.github.io/Launcher/), so the Cardputer can run other things when you don't need it.

It runs fm1buddy's own core (`lib/fm1core` there), so it does the same things the same way, and **the SD card is the same**: move a card between an fm1buddy and a cardbuddy and both see the same firmwares, backups, sounds, setups and SLOOP files.

| On the SD card | What                                                                      |
| -------------- | ------------------------------------------------------------------------- |
| `/fm1`         | firmwares (`.fwsc`)                                                       |
| `/backups`     | the FM-1's whole flash                                                    |
| `/syx`         | sounds (`.syx`, DX7 banks)                                                |
| `/setups`      | setups, fomajam's kits                                                    |
| `/sloop`       | SLOOP backups, `songs/`, `samples/`                                       |
| `/recordings`  | WAV recordings                                                            |
| `/midimap.txt` | MIDI mapping rules                                                        |
| `/fm1buddy`    | the Hub's list, install records, logs (`cardbuddy-log.txt` is this one's) |

## What it does

All of it through fm1buddy's own code (`lib/fm1core`, and `lib/fm1s3` for the ESP32-S3's USB host, Bluetooth and Wi-Fi):

- **Firmware**: the SD card's `.fwsc` files and the Hub's ([mutant.cloud](https://mutant.cloud/)), with search. Download from the Hub over Wi-Fi, install on the FM-1 (`i`), with a warning when there's no backup yet
- **Backup & recover**: all of the FM-1's flash to `/backups`, and an FM-1 that won't start brought back from a firmware or a backup (recovery mode: the FM-1 straight on the USB-C port, the flash map drawn as it goes, on both screens)
- **Sounds**: send `.syx` files, save the stock firmware's voice, keep and load fomajam's song and kits (setups), export them
- **SLOOP**: back it up, restore a backup or one project of it (`0`, `a`-`d`), save and load songs, load `.wav` files into a sample slot (`1`-`4`)
- **MIDI**: the devices on the hub, routing between them (`r`, with `/midimap.txt`), Bluetooth MIDI (`b`)
- **Keys**: the keyboard as a MIDI controller, two octaves as trackers lay them out (`z s x d c v`... and `q 2 w 3 e r`...), `-` `=` octave, `[` `]` channel
- **Settings**: theme, brightness, Bluetooth MIDI, USB MIDI on a computer, USB disk (the SD card as a computer's drive)

## Using it

- `;` `.` move up and down, `,` `/` page (left and right), `enter` opens, `` ` `` goes back (`fn` + these while typing)
- In a firmware list, just type to find one; `tab` switches between the SD card's and the Hub's
- The FM-1 and controllers go on the USB-C port through a powered USB hub (or an OTG adapter with a charging port), as with fm1buddy. For recovery, the FM-1 goes straight on the port, and cardbuddy says when to switch it on
- With a computer on the USB-C port at power-on, the port is its serial port (flashing, logs) instead

## No PSRAM

The Cardputer has 8 MB of flash and no PSRAM, where fm1buddy's board has 8 MB of it. So what fm1buddy holds in memory stays on the SD card here: a firmware package is read from its file as the FM-1 asks for it, and a backup's or a recovery's 1 MB of flash goes through a file in `/fm1buddy` (removed after). fm1buddy's simulator tests both ways (`--lowmem`: `tools/install-test.sh`, `tools/recover-test.sh`), and they come out byte for byte the same. Files too big for its memory (a long `.wav` for a sample slot, say) get an error instead of a crash.

Not tried on a Cardputer yet: how much memory is left with Wi-Fi, the USB host and Bluetooth all on (Settings > About says), and the USB-C port as the host.

Next: what the Cardputer is good at: tilt as MIDI CCs, previewing samples and recordings on its speaker, sampling from its mic.

## Building

With [PlatformIO](https://platformio.org), and fm1buddy beside this repo (`../fm1buddy`, for `lib/fm1core` and `lib/fm1s3`):

```sh
git clone https://github.com/konsumer/fm1buddy.git ../fm1buddy
pio run -t upload
```

`npm run fmt` formats it.

## License

GPL-3.0-only, as fm1buddy.
