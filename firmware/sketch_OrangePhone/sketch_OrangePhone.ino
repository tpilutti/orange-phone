/*
 * Orange Phone
 * ------------
 * Plays saved voicemail messages through the handset of an old wall phone.
 *
 * How it works:
 *   The Arduino's power runs through a reed switch on the phone's hook
 *   lever. Lifting the handset powers the Arduino up; hanging up cuts the
 *   power. So every time someone picks up the phone, the sketch starts
 *   fresh: it shuffles the list of messages into a random order and then
 *   plays them one after another for as long as the handset stays off the
 *   hook.
 *
 * Hardware:
 *   - Arduino Uno (ATmega328)
 *   - Adafruit Wave Shield v1.1
 *       DAC on pins 2-5, SD card on SPI pins 10-13
 *       (set by the WaveHC library, nothing to configure here)
 *   - Shield audio output wired to the handset earpiece
 *   - Analog pin A0 left unconnected (read as random noise to seed the shuffle)
 *
 * SD card:
 *   FAT16, with the WAV files listed in VCnames[] in the root folder.
 *   Files must be uncompressed PCM, mono, 22 kHz or less (see README).
 *
 * Library:
 *   WaveHC 1.0.3 (Adafruit / William Greiman), with one setting changed:
 *   DVOLUME is set to 1 in WaveHC.h (see README). This sketch doesn't use
 *   software volume, so the change has no audible effect here.
 *
 * Built December 2021. The SD card setup and helper functions follow the
 * WaveHC example sketches and Adafruit's Wave Shield tutorial.
 */

// #include <FatReader.h>
// #include <SdReader.h>
#include <WaveUtil.h>   // putstring() / putstring_nl(): print strings stored in flash to save RAM
#include <WaveHC.h>     // WAV playback, SD card and FAT filesystem classes

// ---------------------------------------------------------------------------
// Global objects for the SD card, its filesystem, and the audio player
// ---------------------------------------------------------------------------
SdReader card;    // This object holds the information for the card
FatVolume vol;    // This holds the information for the partition on the card
FatReader root;   // This holds the information for the filesystem on the card
FatReader f;      // This holds the information for the file we're play

WaveHC wave;      // This is the only wave (audio) object, since we will only play one at a time

// ---------------------------------------------------------------------------
// Playlist
// ---------------------------------------------------------------------------
// These are the voice messages in character array format
// Filenames must match the files on the SD card exactly, in 8.3 format
// (up to 8 characters, a dot, then WAV). To add or remove a message,
// edit this list AND update VClistLength and VCplayorder[] below to match.
char *VCnames[] = {"IMJACK.WAV",
                   "JBUGCOLL.WAV",
                   "JLAPTOP.WAV",
                   "JPINEWOD.WAV",
                   "JTRAIN.WAV",
                   "JWORKSHP.WAV",
                   "KFRIENDS.WAV",
                   "KCHINESE.WAV",
                   "KCOUCH.WAV",
                   "KGRAVES.WAV",
                   "KPOOL.WAV",
                   "KSAYNHI.WAV",
                   "KTREAT.WAV",
                   "KDBOBADY.WAV"
                  };


// Number of entries in VCnames[]. Must be updated by hand if the list changes.
byte VClistLength = 14;

// Play order, as indexes into VCnames[]. Starts in order (0..13) and is
// shuffled in setup(), so this must also contain one entry per message.
byte VCplayorder[] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13};


// ---------------------------------------------------------------------------
// setup(): runs once each time the handset is lifted (power on).
// Initializes the SD card, finds the FAT partition, opens the root folder,
// then shuffles the play order. If any SD step fails, it prints an error to
// the serial monitor and stops.
// ---------------------------------------------------------------------------
void setup() {
  // byte i;   //not sure if used

  // set up serial port
  // Serial output is only for debugging with the USB cable connected;
  // the phone works the same without it.
  Serial.begin(9600);

  putstring_nl("\nWave test!");  // say we woke up!

  // --- Step 1: start the SD card ---
  //  if (!card.init(true)) { //play with 4 MHz spi if 8MHz isn't working for you
  if (!card.init()) {         //play with 8 MHz spi (default faster!)
    putstring_nl("Card init. failed!");  // Something went wrong, lets print out why
    sdErrorCheck();
    while(1);                            // then 'halt' - do nothing!
  }

  // enable optimize read - some cards may timeout. Disable if you're having problems
  card.partialBlockRead(true);

  // --- Step 2: find a FAT partition on the card ---
// Now we will look for a FAT partition!
  uint8_t part;
  for (part = 0; part < 5; part++) {     // we have up to 5 slots to look in
    if (vol.init(card, part))
      break;                             // we found one, lets bail
  }
  if (part == 5) {                       // if we ended up not finding one  :(
    putstring_nl("No valid FAT partition!");
    sdErrorCheck();      // Something went wrong, lets print out why
    while(1);                            // then 'halt' - do nothing!
  }

  // Lets tell the user about what we found
  putstring("Using partition ");
  Serial.print(part, DEC);
  putstring(", type is FAT");
  Serial.println(vol.fatType(),DEC);     // FAT16 or FAT32?

  // --- Step 3: open the root folder, where the WAV files live ---
  // Try to open the root directory
  if (!root.openRoot(vol)) {
    putstring_nl("Can't open root dir!"); // Something went wrong,
    while(1);                             // then 'halt' - do nothing!
  }

  // Whew! We got past the tough parts.
  putstring_nl("Got through SD card init");


  // --- Step 4: shuffle the play order ---
  // set up the random playlist based on the
  // int n = sizeof(NumberArray) / sizeof(NumberArray[0]); //number of elements

  // use the floating analog read port to change the seed each time it is run so that
  // when the arduino boots, the shuffle list won't be same each reboot.
  randomSeed(analogRead(0));

  // shuffle VCplayorder
  // Swap each position with a randomly chosen position.
  for (byte i = 0; i < VClistLength; i++)
  {
    byte j = random(0, VClistLength);
    byte t = VCplayorder[i];
    VCplayorder[i] = VCplayorder[j];
    VCplayorder[j] = t;
  }
  // shuffled result
  // Print the shuffled order to the serial monitor (debugging only).
  Serial.println("shuffled");
  for (byte i = 0; i < VClistLength; i++)
  {
    Serial.println(VCplayorder[i]);
  }

  Serial.println("exiting setup()");

} // end setup()



// ---------------------------------------------------------------------------
// loop(): plays every message once in the shuffled order, then repeats the
// same order. Hanging up cuts the power, which stops playback; the next
// pickup reboots the Arduino and gets a new shuffle.
// ---------------------------------------------------------------------------
void loop()
{
  // play through the list of wav-files in VCnames using the shuffled VCplayorder vector
  for (byte i = 0; i < VClistLength; i++)
  {
    // Serial.println("Playing: "); Serial.print(VCnames[VCplayorder[i]]);
    playcomplete(VCnames[VCplayorder[i]]);   // waits until this message finishes
  }


  // playcomplete("KFRIENDS.WAV");

} // end loop()


/////////////////////////////////// HELPERS
// (Adapted from the WaveHC examples and the Adafruit Wave Shield tutorial.)


// Prints the SD card's error code and data to the serial monitor and halts.
// Returns immediately if there's no error.
void sdErrorCheck(void)
{
  if (!card.errorCode()) return;
  putstring("\n\rSD I/O error: ");
  Serial.print(card.errorCode(), HEX);
  putstring(", ");
  Serial.println(card.errorData(), HEX);
  while(1);
}


// Plays a full file from beginning to end with no pause.
// Blocks (waits) until the file has finished playing.
void playcomplete(char *name) {
  // call our helper to find and play this name
  playfile(name);
  while (wave.isplaying) {
  // do nothing while its playing
  }
  // now its done playing
}

// Starts playing a file and returns right away; playback continues in the
// background (driven by a timer interrupt in the WaveHC library).
// If the file is missing or isn't a valid WAV, it prints a message and
// returns without playing, so playcomplete() simply moves on to the next one.
void playfile(char *name) {
  // see if the wave object is currently doing something
  if (wave.isplaying) {// already playing something, so stop it!
    wave.stop(); // stop it
  }
  // look in the root directory and open the file
  if (!f.open(root, name)) {
    putstring("Couldn't open file "); Serial.print(name); return;
  }
  // OK read the file and turn it into a wave object
  if (!wave.create(f)) {
    putstring_nl("Not a valid WAV"); return;
  }

  // ok time to play! start playback
  putstring_nl("Playing "); Serial.print(name);
  wave.play();
}
