# KassutronicsQuantizer
Firmware for the Kassutronics Quantizer module.

Documentation and installation instructions can be found here: https://github.com/kassu/kassutronics/tree/master/documentation/Quantizer

There are also [instructions for programming the firmware using an Arduino Uno](https://github.com/kassu/kassutronics/blob/master/documentation/Quantizer/Programming%20using%20an%20Arduino%20Uno.md)

## Changes in this fork

This fork is based on [kassu/KassutronicsQuantizer](https://github.com/kassu/KassutronicsQuantizer). It changes the following:

### CV control of gate length
When a CV input is assigned to gate length, the CV now shifts the gate length set in the menu instead of replacing it.

- 0V gives the gate length selected in the menu.
- Each ~1.33V moves the gate length by one menu step. Over ±5V that is about 4 steps down or up.
- Between steps the gate length changes smoothly.
- The result is limited to the shortest (~1ms) and longest (~2.08s) gate length.

Example: with the menu on step 6 (147ms), −5V gives ~18ms and +5V gives ~1.2s.

In the original firmware the menu setting was ignored in this mode.

### Note flash
In the normal scale display, when a channel outputs a new note, that note's LED goes dark for ~80ms. This shows which notes are being played. If both channels play the same note, it still flashes.

### Startup animation
At power-up the note LEDs light one after another, ~1 second in total. This also makes it easy to confirm that new firmware was flashed.

### MIDI out
The quantized notes are sent as MIDI on the TX pin of the serial header J2 (pin 5). Channel A plays on MIDI channel 1, channel B on channel 2.

- 0V is MIDI note 36, and each volt is one octave. The top of the output range (+5.3V) is note 100. Outputs below -3V would be under MIDI note 0 and are not sent.
- The status byte is sent with every message (no running status), so a receiver that is connected later picks up at the next note.
- A note starts when the gate turns on and stops when it turns off. Velocity is always 100.
- With legato on, the new note is sent before the old one is released, so mono synths slide instead of retriggering.

Wiring to a MIDI jack:

| J2 pin | Connect to |
|---|---|
| 3 (+5V) | 220Ω to DIN pin 4 (TRS type A: ring) |
| 5 (TX) | 220Ω to DIN pin 5 (TRS type A: tip) |
| 1 (GND) | DIN pin 2 (TRS: sleeve) |

A schematic of the MIDI out and MIDI in circuits is in `KassutronicsQuantizer/midi_circuit/midi_circuit.kicad_sch` (KiCad).

Leave DTR (pin 6) unconnected. MIDI uses the UART, so it can't be enabled together with `DEBUGPRINT`. Disconnect the MIDI cable before uploading through J2. To turn MIDI out off, comment out `#define MIDI_OUT` in `KassutronicsQuantizer.ino`.

### MIDI in: root note and octave
MIDI notes received on the RX pin of the serial header J2 (pin 4) set the root note and the octave. The scale is rotated by the note played: C leaves the scale as entered, D moves it up two semitones, and so on. Enter the scale with C as the root and the MIDI note then picks the key.

- The octave of the note transposes both outputs in whole octaves, centered on middle C. Notes 60–71 (middle C up to the B above it) give no shift, 48–59 shift one octave down, 72–83 one octave up, and so on. Middle C is MIDI note 60; some gear labels it C3, some C4.
- The octave shift does not sound when the MIDI note arrives. Each channel picks it up at its next note: the next trigger, or with nothing in TRIG, the next time the input moves to a different note.
- Any MIDI channel works.
- Root and octave stay until the next note on. Note offs are ignored.
- The MIDI root adds to the rotation set in the menu and by CV. The scale display shows the result.
- The MIDI octave adds to the transpose set in the menu and by CV. If the result is outside the output range, the output holds its last note.
- Root and octave are not saved. After power-up the root is C with no octave shift until a note arrives.

MIDI in needs an optocoupler. The usual 6N138 circuit:

| 6N138 pin | Connect to |
|---|---|
| 2 (anode) | 220Ω to DIN pin 4 (TRS type A: ring). 270Ω also works |
| 3 (cathode) | DIN pin 5 (TRS type A: tip) |
| 5 (GND) | J2 pin 1 (GND) |
| 6 (output) | J2 pin 4 (RX), and 470Ω to +5V |
| 7 | 4.7kΩ to GND |
| 8 (VCC) | J2 pin 3 (+5V) |

Put a 1N4148 across pins 2 and 3, cathode to pin 2. Leave DIN pin 2 unconnected. If tip and ring are swapped, nothing is received. Disconnect the MIDI input before uploading through J2. To turn MIDI in off, comment out `#define MIDI_IN` in `KassutronicsQuantizer.ino`.

### Default trigger delay
The trigger delay (Shift+9) now defaults to 3, about 3ms, instead of 0. With 0 the input is sampled about 0.2ms after the trigger, which is before the pitch CV of some sequencers has settled, so each trigger picks up the previous note.

### Channel B mirror
Channel B can quantize channel A's input, so it works as a second voice. Press Shift+8 (quantize mode menu), then key 11 to toggle. LED 11 is lit when mirroring is on. The setting is saved.

- IN B is ignored while mirroring is on. The module can't detect whether IN B is patched, so turn this on only when you want B to follow A.
- With nothing in TRIG B, B updates together with A: same timing and gates, including repeated notes.
- With a clock in TRIG B, B samples A's input on B's own clock.
- B's transpose, offset and CV settings still apply, e.g. set transpose B to 7 semitones for a parallel fifth.

### Fixes
- Gate length could overflow at long settings with CV applied, which could stop the gate from firing.
- An invalid quantizer result could read past the end of a lookup table.
- The flash timer is updated with interrupts disabled, so a new flash can't be cut short.

### Gate length reference
Gate length for each menu step (key), measured on the module:

| Step | Gate length |
|---:|---:|
| 0 | 1ms |
| 1 | 10ms |
| 2 | 17.7ms |
| 3 | 30ms |
| 4 | 51ms |
| 5 | 86.5ms |
| 6 | 147ms |
| 7 | 250ms |
| 8 | 424ms |
| 9 | 720ms |
| 10 | 1.22s |
| 11 | 2.08s |

### Flashing
The sketch builds with the board set to **Arduino Uno**. When programming through an Uno running the ArduinoISP sketch:

1. Select **Tools → Programmer → Arduino as ISP**.
2. Use **Sketch → Upload Using Programmer** (Ctrl+Shift+U).

A plain **Upload** (Ctrl+U) writes the firmware to the Uno itself, not to the quantizer. In the verbose upload log, the avrdude command should contain `-cstk500v1` and `-b19200`. If it contains `-carduino`, the firmware went to the Uno.

Flashing this way erases the whole chip. Unless the EESAVE fuse is set, that includes the saved settings and scales, so the module starts from the defaults afterwards.
