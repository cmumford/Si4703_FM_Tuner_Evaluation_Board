//
// Original work Copyright 09.09.2011 Nathan Seidle (SparkFun)
// Modified work Copyright 11.02.2013 Aaron Weiss (SparkFun)
// Modified work Copyright 13.09.2013 Christoph Thoma
//

#include <cmath>

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <wiringPi.h>
#include <linux/i2c-dev.h>
#include <sys/ioctl.h>

#include "SparkFunSi4703.h"

namespace {

// Max powerup time, from datasheet page 13.
uint16_t MAX_POWERUP_TIME = 110;

// Delay for clock to settle - from AN230 page 9.
uint16_t CLOCK_SETTLE_DELAY = 500;

uint16_t SwapEndian(uint16_t val) {
  return (val >> 8) | (val << 8);
}

uint16_t ToLittleEndian(uint16_t val) {
  return SwapEndian(val);
}

uint16_t ToBigEndian(uint16_t val) {
  return SwapEndian(val);
}

// Determine if two float values are "equal enough" - i.e. to within some small
// value.
bool FloatsEqual(float a, float b) {
  const float epsilon = 0.02;
  return std::abs(a - b) < epsilon;
}

}  // anonymous namespace

Si4703_Breakout::Si4703_Breakout(int resetPin, int sdioPin, Region region)
    : resetPin_(resetPin), sdioPin_(sdioPin), region_(region) {
  switch (region) {
    case Region::US:
      band_ = Band_US_Europe;
      channel_spacing_ = Spacing_200kHz;
      break;
    case Region::Europe:
      band_ = Band_US_Europe;
      channel_spacing_ = Spacing_100kHz;
      break;
    case Region::Japan:
      band_ = Band_Japan_Wide;
      // TODO: verify spacing.
      channel_spacing_ = Spacing_100kHz;
      break;
  }
}

// To get the Si4703 inito 2-wire mode, SEN needs to be high and SDIO needs to
// be low after a reset. The breakout board has SEN pulled high, but also has
// SDIO pulled high. Therefore, after a normal power up the Si4703 will be in an
// unknown state. RST must be controlled
int Si4703_Breakout::powerOn() {
  wiringPiSetupGpio();  // Setup gpio access in BCM mode.

  pinMode(resetPin_, OUTPUT);  // gpio bit-banging to get 2-wire (I2C) mode.
  pinMode(sdioPin_, OUTPUT);   // SDIO is connected to A4 for I2C.

  digitalWrite(sdioPin_, LOW);    // A low SDIO indicates a 2-wire interface.
  digitalWrite(resetPin_, LOW);   // Put Si4703 into reset.
  delay(1);                       // Some delays while we allow pins to settle.
  digitalWrite(resetPin_, HIGH);  // Bring Si4703 out of reset with SDIO set to
                                  // low and SEN pulled high with on-board
                                  // resistor.
  delay(1);                       // Allow Si4703 to come out of reset.

  // Setup I2C
  const char filename[] = "/dev/i2c-1";
  if ((si4703_fd_ = open(filename, O_RDWR)) < 0) {  // Open I2C slave device.
    perror(filename);
    return FAIL;
  }

  if (ioctl(si4703_fd_, I2C_SLAVE, SI4703) < 0) {  // Set device address 0x10.
    perror("Failed to acquire bus access and/or talk to slave");
    return FAIL;
  }

  if (ioctl(si4703_fd_, I2C_PEC, 1) < 0) {  // Enable "Packet Error Checking".
    perror("Failed to enable PEC");
    return FAIL;
  }

  readRegisters();  // Read the current register set.

  // Enable the oscillator, from AN230 page 9, rev 0.61 (works).
  registers_[0x07] = 0x8100;
  updateRegisters();

  delay(CLOCK_SETTLE_DELAY);

  readRegisters();                // Read the current register set.
  registers_[POWERCFG] = 0x4001;  // Enable the IC.

  registers_[SYSCONFIG1] |= RDS;  // Enable RDS.
  if (region_ == Region::Europe)
    registers_[SYSCONFIG1] |= DE;
  registers_[SYSCONFIG2] |= band_;
  registers_[SYSCONFIG2] |= channel_spacing_;
  registers_[SYSCONFIG2] &= 0xFFF0;  // Clear volume bits.
  registers_[SYSCONFIG2] |= 0x0001;  // Set volume to lowest.
  updateRegisters();

  delay(MAX_POWERUP_TIME);

  return SUCCESS;
}

void Si4703_Breakout::powerOff() {
  readRegisters();
  registers_[POWERCFG] = 0x0000;  // Clear Enable Bit disables chip.
  updateRegisters();
}

void Si4703_Breakout::setFrequency(float frequency) {
  // See frequencyToChannel for source of equation.
  float fchannel = (frequency - minFrequency()) / channelSpacing();
  uint16_t channel = frequencyToChannel(frequency);
  if (!FloatsEqual(fchannel, channel)) {
    // The freq must be a multiple of the channel spacing and offset from the
    // minimum freq.
    return;
  }
  readRegisters();
  registers_[CHANNEL] &= 0xFE00;   // Clear out the channel bits.
  registers_[CHANNEL] |= channel;  // Mask in the new channel.
  registers_[CHANNEL] |= TUNE;     // Set the TUNE bit to start.
  updateRegisters();

  delay(60);  // Wait 60ms - you can use or skip this delay.

  // Poll to see if STC is set.
  while (true) {
    readRegisters();
    if ((registers_[STATUSRSSI] & STC) != 0)
      break;  // Tuning complete!
  }

  readRegisters();
  registers_[CHANNEL] &= ~TUNE;  // Clear the tune after a tune has completed.
  updateRegisters();

  // Wait for the si4703 to clear the STC as well.
  while (true) {
    readRegisters();
    if ((registers_[STATUSRSSI] & STC) == 0)
      break;  // Tuning complete!
  }
}

float Si4703_Breakout::seekUp() {
  return seek(SeekDirection::Up);
}

float Si4703_Breakout::seekDown() {
  return seek(SeekDirection::Down);
}

void Si4703_Breakout::setVolume(int volume) {
  readRegisters();
  if (volume < 0)
    volume = 0;
  if (volume > 15)
    volume = 15;
  registers_[SYSCONFIG2] &= 0xFFF0;  // Clear volume bits.
  registers_[SYSCONFIG2] |= volume;  // Set new volume.
  updateRegisters();                 // Update.
}

void Si4703_Breakout::readRDS(char* buffer, long timeout) {
  long endTime = millis() + timeout;
  bool completed[] = {false, false, false, false};
  int completedCount = 0;

  // Read until we get four pairs of letters, or until there is a timeout.
  while (completedCount < 4 && millis() < endTime) {
    readRegisters();

    if (registers_[STATUSRSSI] & RDSR) {
      // lowest order two bits of B are the word pair index.
      uint16_t b = registers_[RDSB];
      int index = b & 0x03;

      if (!completed[index] && b < 500) {
        completed[index] = true;
        completedCount++;
        char Dh = (registers_[RDSD] & 0xFF00) >> 8;
        char Dl = (registers_[RDSD] & 0x00FF);
        buffer[index * 2] = Dh;
        buffer[index * 2 + 1] = Dl;
      }
      delay(40);  // Wait for the RDS bit to clear.
    } else {
      delay(30);  // From AN230, using the polling method 40ms should be
                  // sufficient amount of time between checks.
    }
  }

  if (millis() >= endTime) {
    buffer[0] = '\0';
    return;
  }

  buffer[8] = '\0';
}

// Read the entire register control set from 0x00 to 0x0F.
uint8_t Si4703_Breakout::readRegisters() {
  uint16_t buffer[16];

  // Si4703 begins reading from upper byte of register 0x0A and reads to 0x0F,
  // then loops to 0x00.
  // We want to read the entire register set from 0x0A to 0x09 = 32 bytes.
  if (read(si4703_fd_, buffer, 32) != 32) {
    perror("Could not read from I2C slave device");
    return FAIL;
  }

  // We may want some time-out error here.

  // Remember, register 0x0A comes in first so we have to shuffle the array
  // around a bit.
  int i = 0;
  for (int x = 0x0A;; x++) {
    if (x == 0x10)
      x = 0;  // Loop back to zero.
    registers_[x] = ToLittleEndian(buffer[i++]);
    if (x == 0x09)
      break;  // We're done!
  }

  return SUCCESS;
}

// Write the current 9 control registers (0x02 to 0x07) to the Si4703.
// It's a little weird, you don't write an I2C address.
// The Si4703 assumes you are writing to 0x02 first, then increments.
uint8_t Si4703_Breakout::updateRegisters() {
  int i = 0;
  uint16_t buffer[6];

  // A write command automatically begins with register 0x02 so no need to send
  // a write-to address.
  // First we send the 0x02 to 0x07 control registers, first upper byte, then
  // lower byte and so on.
  // In general, we should not write to registers 0x08 and 0x09.
  for (int regSpot = 0x02; regSpot < 0x08; regSpot++)
    buffer[i++] = ToBigEndian(registers_[regSpot]);

  if (write(si4703_fd_, buffer, 12) < 12) {
    perror("Could not write to I2C slave device");
    return FAIL;
  }

  return SUCCESS;
}

void Si4703_Breakout::printRegisters() {
  int i;

  printf("Registers\tValues\n");

  for (i = 0; i < 16; i++) {
    printf("0x%02X:\t%04X\n", i, registers_[i]);
  }
}

// Seeks out the next available station.
// Returns the freq if it made it.
// Returns zero if failed.
float Si4703_Breakout::seek(SeekDirection direction) {
  readRegisters();
  // Set seek mode wrap bit.
  registers_[POWERCFG] |= SKMODE;  // Allow wrap.
  // registers_[POWERCFG] &= ~SKMODE; // Disallow wrap - if you
  // disallow wrap, you may want to tune to 87.5 first.
  if (direction == SeekDirection::Down) {
    // Seek down is the default upon reset.
    registers_[POWERCFG] &= ~SEEKUP;
  } else {
    registers_[POWERCFG] |= SEEKUP;  // Set the bit to seek up.
  }

  registers_[POWERCFG] |= SEEK;  // Start seek.
  updateRegisters();             // Seeking will now start.

  // Poll to see if STC is set.
  while (true) {
    readRegisters();
    if ((registers_[STATUSRSSI] & STC) != 0)
      break;  // Tuning complete!
  }

  readRegisters();
  // Store the value of SFBL.
  int valueSFBL = registers_[STATUSRSSI] & SFBL;
  // Clear the seek bit after seek has completed.
  registers_[POWERCFG] &= ~SEEK;
  updateRegisters();

  // Wait for the si4703 to clear the STC as well.
  while (true) {
    readRegisters();
    if ((registers_[STATUSRSSI] & STC) == 0)
      break;  // Tuning complete!
  }

  if (valueSFBL) {  // The bit was set indicating we hit a band limit or failed
                    // to find a station.
    return FAIL;
  }

  return getFrequency();
}

// Return the space between channels (in MHz).
float Si4703_Breakout::channelSpacing() const {
  switch (channel_spacing_) {
    case Spacing_200kHz:
      return 0.2;
    case Spacing_100kHz:
      return 0.1;
    case Spacing_50kHz:
      return 0.05;
  }
}

float Si4703_Breakout::minFrequency() const {
  switch (band_) {
    case Band_US_Europe:
      return 87.5;
    case Band_Japan:
      return 76.0;
  }
}

// Given the |channel| value from the READCHAN registry convert it to frequency.
float Si4703_Breakout::channelToFrequency(uint16_t channel) const {
  // This formula is from the AN230 Programmers Guide, section 3.7.1.
  // https://www.silabs.com/documents/public/application-notes/AN230.pdf
  return channelSpacing() * static_cast<float>(channel) + minFrequency();
}

// Given a |frequency| convert it to a registry CHANNEL value.
uint16_t Si4703_Breakout::frequencyToChannel(float frequency) const {
  // This formula is from the AN230 Programmers Guide, section 3.7.1.
  // https://www.silabs.com/documents/public/application-notes/AN230.pdf
  // Add a small value to account for floating point rounding errors.
  const float epsilon = 0.001f;
  return static_cast<uint16_t>(epsilon +
                               (frequency - minFrequency()) / channelSpacing());
}

float Si4703_Breakout::getFrequency() {
  readRegisters();
  // Mask out everything but the lower 10 bits.
  const int channel = registers_[READCHAN] & 0x03FF;
  return channelToFrequency(channel);
}
