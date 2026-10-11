/* Midi.ino - MIDI output and input
 * Added 2026-09-29 (fork)
 * Modified 2026-10-03: MIDI input sets the root note and octave
 * Modified 2026-10-11: status byte sent with every message, 0V is MIDI note 36
 *
 * MIDI out (MIDI_OUT)
 *
 * Sends MIDI notes on the hardware UART TX pin (PD1), which is pin 5 of the
 * serial header J2. Channel A plays on MIDI channel 1, channel B on channel 2.
 *
 * Notes follow the gate outputs: a note starts when the gate turns on and
 * stops when it turns off. When the gate stays on for a new note (legato), the
 * new note is sent before the old note is released, so mono synths slide
 * instead of retriggering.
 *
 * The UART handles the bit timing, so the ADC interrupt only has to put bytes
 * in a buffer. The buffer is sent by the UART data register empty interrupt.
 *
 * MIDI in (MIDI_IN)
 *
 * Receives MIDI notes on the hardware UART RX pin (PD0), which is pin 4 of the
 * serial header J2. A note on, on any MIDI channel, sets the root note: the
 * scale is rotated by the note's pitch class (C = no rotation), on top of the
 * rotation set in the menu and by CV. The note's octave transposes both
 * channels in whole octaves, centered on middle C: notes 60-71 give no shift,
 * 48-59 one octave down, 72-83 one octave up, and so on. A channel picks up the
 * new octave at its next note (a trigger, or a different note in free running
 * mode), not when the MIDI note arrives. Root and octave stay until the next
 * note on, note offs are ignored. They are not saved.
 */
#if defined(MIDI_OUT) || defined(MIDI_IN)

#ifdef DEBUGPRINT
#error "MIDI and DEBUGPRINT both use the UART, enable only one"
#endif

void setupMidi() {
  UBRR0 = 31; // 16MHz / (16 * 31250) - 1, exact
  UCSR0A = 0;
  UCSR0C = _BV(UCSZ01) | _BV(UCSZ00); // 8N1
  byte ucsr0b = 0;
  #ifdef MIDI_OUT
  ucsr0b |= _BV(TXEN0);
  #endif
  #ifdef MIDI_IN
  sbi(PORTD, 0); // Pullup on RX, so the line idles high with nothing connected
  ucsr0b |= _BV(RXEN0) | _BV(RXCIE0);
  #endif
  UCSR0B = ucsr0b;
}

#endif

#ifdef MIDI_OUT

const byte midibase = 36;      // MIDI note number for 0V (36 = C2)
const byte midizero = 63;      // Output value at 0V (the range is bipolar, see keyboard mode in Core.ino)
const byte midivelocity = 100;
const byte midinone = 255;     // No note playing

// Transmit ring buffer. Only the ADC interrupt writes, only the UDRE interrupt reads.
#define MIDI_BUFSIZE 64 // Must be a power of 2
byte midibuf[MIDI_BUFSIZE];
volatile byte midihead = 0;
volatile byte miditail = 0;


// MIDI note currently playing on each channel
byte midinote[2] = {midinone, midinone};

byte midiFree() {
  return (miditail - midihead - 1) & (MIDI_BUFSIZE - 1);
}

void midiPut(byte data) {
  midibuf[midihead] = data;
  midihead = (midihead + 1) & (MIDI_BUFSIZE - 1);
}

// Queue a note message. Note off is sent as note on with velocity 0.
// The status byte is sent with every message (no running status), so a receiver that
// is connected later or misses a byte is back in sync at the next note.
// Caller must check there is room for 3 bytes.
void midiSendNote(byte channel, byte note, byte velocity) {
  midiPut(0x90 | channel);
  midiPut(note);
  midiPut(velocity);
  sbi(UCSR0B, UDRIE0);
}

// Called from processChannel when gate i turns on (or stays on for a new note)
void midiGateOn(byte i, byte outval) {
  // Outputs below MIDI note 0 are not sent
  if (outval < midizero - midibase) {
    return;
  }
  byte note = outval - (midizero - midibase);
  if (note > 127 || note == midinote[i]) {
    return;
  }
  /* A note on needs room for itself, a legato note off, and the note offs of
   * both channels afterwards. Note offs then always fit, so a full buffer can
   * drop notes but never leave a note stuck. */
  if (midiFree() < 12) {
    return;
  }
  midiSendNote(i, note, midivelocity);
  if (midinote[i] != midinone) {
    midiSendNote(i, midinote[i], 0);
  }
  midinote[i] = note;
}

// Called from processChannel when gate i turns off
void midiGateOff(byte i) {
  if (midinote[i] == midinone) {
    return;
  }
  midiSendNote(i, midinote[i], 0);
  midinote[i] = midinone;
}

ISR(USART_UDRE_vect) {
  if (miditail == midihead) {
    cbi(UCSR0B, UDRIE0);
  } else {
    UDR0 = midibuf[miditail];
    miditail = (miditail + 1) & (MIDI_BUFSIZE - 1);
  }
}

#endif

#ifdef MIDI_IN

// Runs for every received byte. Keeps the ADC interrupt waiting, so it has to be short.
ISR(USART_RX_vect) {
  static byte status = 0;      // Running status
  static bool havenote = false; // First data byte of a note on has been received
  static byte note = 0;

  // The error flags must be read before the data
  byte error = UCSR0A & (_BV(FE0) | _BV(DOR0));
  byte data = UDR0;

  if (error) {
    // Lost track of the message boundaries, wait for the next status byte
    status = 0;
  } else if (data >= 0xF8) {
    // Realtime messages (clock etc.) can appear anywhere and don't affect running status
  } else if (data >= 0x80) {
    status = data;
    havenote = false;
  } else if ((status & 0xF0) == 0x90) {
    if (!havenote) {
      note = data;
      havenote = true;
    } else {
      havenote = false;
      // Velocity 0 is a note off
      if (data > 0) {
        byte root = mod12(note);
        // note - root is the start of the octave, 60 is middle C
        transposesemitonesMIDI = (note - root) - 60;
        if (root != rotatesemitonesMIDI) {
          rotatesemitonesMIDI = root;
          updateRotation();
        }
      }
    }
  }
}

#endif
