#include "LiquidTWI.h"

/*
LiquidTWI High Performance i2c LCD driver
by Matt Falcon (FalconFour) / http://falconfour.com
modified by Stephanie Maks / http://planetstephanie.net
Adapted and optimized from LiquidCrystal bundled with Arduino

Version 1.2

Changelog:
* 1.2 / January 2012
  Modified by Stephanie Maks to re-introduce legacy support along with
  support for the new Arduino 1.0 IDE.
* 1.1.s / December 2011:
  Modified by Stephanie Maks to drop legacy support and work with the
  Arduino 1.0 IDE. Also includes the bugfix for the startup/init problem.
* 1.1 / 6-17-2011:
  Oops... initial bugchecking was done on one program (performance test).
  LCD_DISPLAYCONTROL functions were sending bit 7 (backlight) to the LCD as
  part of the command, invalidating most commands and setting the cursor to
  address 0 instead of doing their function. That also screwed up the
  initialization routine something fierce. A miracle this slipped by testing.
* 1.0 / 6-12-2011:
  Initial release

Distributed with as free-will a license as is available given this code's long
associative chain of licenses (LiquidCrystal -> Arduino -> Wiring -> ...?). Use
it in any way you feel fit, profit or free, provided it fits in the licenses of
its associated works.

Usage:
Attach CLK pin to ANALOG 5 pin (remember: analog pins double as digital!),
Attach DAT pin to ANALOG 4 pin,
Give it 5V using    5V     pin,
Then,  GND it with  GND    pin, and you're all set!

LiquidTWI lcd(0);
void setup() { lcd.begin(16,2); }
void loop() { lcd.print("Happy!"); delay(500); lcd.clear(); delay(500); }

If you changed the i2c address of the board (multiple LCDs? I dig it!), set
"lcd(0)" to your new 3-bit address.
If you have more than a 16x2 LCD, change "lcd.begin(16,2)" to reflect the
columns and rows of your LCD.
*/

#include <stdio.h>
#include <string.h>
#include <inttypes.h>
#include <Wire.h>
#if ARDUINO >= 100
  #include "Arduino.h"
#else
  #include "WProgram.h"
#endif

// When the display powers up, it is configured as follows:
//
// 1. Display clear
// 2. Function set: 
//    DL = 1; 8-bit interface data 
//    N = 0; 1-line display 
//    F = 0; 5x8 dot character font 
// 3. Display on/off control: 
//    D = 0; Display off 
//    C = 0; Cursor off 
//    B = 0; Blinking off 
// 4. Entry mode set: 
//    I/D = 1; Increment by 1 
//    S = 0; No shift 
//
// Note, however, that resetting the Arduino doesn't reset the LCD, so we
// can't assume that its in that state when a sketch starts (and the
// LiquidTWI constructor is called). This is why we save the init commands
// for when the sketch calls begin(), except configuring the expander, which
// is required by any setup.

LiquidTWI::LiquidTWI(uint8_t i2cAddr) {
	if (i2cAddr > 7) i2cAddr = 7;
	_i2cAddr = i2cAddr; // transfer this function call's number into our internal class state
	_displayfunction = LCD_4BITMODE | LCD_1LINE | LCD_5x8DOTS; // in case they forget to call begin() at least we have something
}

void LiquidTWI::begin(uint8_t cols, uint8_t lines, uint8_t dotsize) {
	// SEE PAGE 45/46 FOR INITIALIZATION SPECIFICATION!
	// according to datasheet, we need at least 40ms after power rises above 2.7V
	// before sending commands. Arduino can turn on way befer 4.5V so we'll wait 50
	delay(50);

	Wire.begin();
	// first thing we do is get the GPIO expander's head working straight, with a boatload of junk data.
	Wire.beginTransmission(MCP23008_ADDRESS | _i2cAddr);
#if ARDUINO >= 100
	Wire.write((byte)MCP23008_IODIR);
	Wire.write((byte)0xFF);
	Wire.write((byte)0x00);
	Wire.write((byte)0x00);
	Wire.write((byte)0x00);
	Wire.write((byte)0x00);
	Wire.write((byte)0x00);
	Wire.write((byte)0x00);
	Wire.write((byte)0x00);
	Wire.write((byte)0x00);
	Wire.write((byte)0x00);
#else
	Wire.send(MCP23008_IODIR);
	Wire.send(0xFF);
	Wire.send(0x00);
	Wire.send(0x00);
	Wire.send(0x00);
	Wire.send(0x00);
	Wire.send(0x00);
	Wire.send(0x00);
	Wire.send(0x00);
	Wire.send(0x00);
	Wire.send(0x00);
#endif
	Wire.endTransmission();

	// now we set the GPIO expander's I/O direction to output since it's soldered to an LCD output.
	Wire.beginTransmission(MCP23008_ADDRESS | _i2cAddr);
#if ARDUINO >= 100
	Wire.write((byte)MCP23008_IODIR);
	Wire.write((byte)0x00); // all output: 00000000 for pins 1...8
#else
	Wire.send(MCP23008_IODIR);
	Wire.send(0x00); // all output: 00000000 for pins 1...8
#endif
	Wire.endTransmission();

	if (lines > 1) {
		_displayfunction |= LCD_2LINE;
	}
	_numlines = lines;
	_currline = 0;

	// for some 1 line displays you can select a 10 pixel high font
	if ((dotsize != 0) && (lines == 1)) {
		_displayfunction |= LCD_5x10DOTS;
	}

	//put the LCD into 4 bit mode
	// start with a non-standard command to make it realize we're speaking 4-bit here
	// per LCD datasheet, first command is a single 4-bit burst, 0011.
        //-----
        //  we cannot assume that the LCD panel is powered at the same time as
        //  the arduino, so we have to perform a software reset as per page 45
        //  of the HD44780 datasheet - (kch)
        //-----
        // bit pattern for the burstBits function is
        //
        //  7   6   5   4   3   2   1   0
        // LT  D7  D6  D5  D4  EN  RS  n/c
        //-----
        burstBits(B10011100); // send LITE D4 D5 high with enable
        burstBits(B10011000); // send LITE D4 D5 high with !enable
        burstBits(B10011100); //
        burstBits(B10011000); //
        burstBits(B10011100); // repeat twice more
        burstBits(B10011000); //
        burstBits(B10010100); // send D4 low and LITE D5 high with enable
        burstBits(B10010000); // send D4 low and LITE D5 high with !enable
	delay(5); // this shouldn't be necessary, but sometimes 16MHz is stupid-fast.
	
	command(LCD_FUNCTIONSET | _displayfunction); // then send 0010NF00 (N=lines, F=font)
	delay(5); // for safe keeping...
	command(LCD_FUNCTIONSET | _displayfunction); // ... twice.
	delay(5); // done!

	// turn on the LCD with our defaults. since these libs seem to use personal preference, I like a cursor.
	_displaycontrol = LCD_DISPLAYON;  
	display();
	// clear it off
	clear();

	_displaymode = LCD_ENTRYLEFT | LCD_ENTRYSHIFTDECREMENT;
	// set the entry mode
	command(LCD_ENTRYMODESET | _displaymode);

	setBacklight(HIGH); // turn the backlight on if so equipped.
}

/********** high level commands, for the user! */
void LiquidTWI::clear()
{
	command(LCD_CLEARDISPLAY);  // clear display, set cursor position to zero
	delayMicroseconds(2000);  // this command takes a long time!
}

void LiquidTWI::home()
{
	command(LCD_RETURNHOME);  // set cursor position to zero
	delayMicroseconds(2000);  // this command takes a long time!
}

void LiquidTWI::setCursor(uint8_t col, uint8_t row)
{
	int row_offsets[] = { 0x00, 0x40, 0x14, 0x54 };
	if ( row > _numlines ) row = _numlines - 1;    // we count rows starting w/0
	command(LCD_SETDDRAMADDR | (col + row_offsets[row]));
}

// Turn the display on/off (quickly)
void LiquidTWI::noDisplay() {
	_displaycontrol &= ~LCD_DISPLAYON;
	command(LCD_DISPLAYCONTROL | _displaycontrol);
}
void LiquidTWI::display() {
	_displaycontrol |= LCD_DISPLAYON;
	command(LCD_DISPLAYCONTROL | _displaycontrol);
}

// Turns the underline cursor on/off
void LiquidTWI::noCursor() {
	_displaycontrol &= ~LCD_CURSORON;
	command(LCD_DISPLAYCONTROL | _displaycontrol);
}
void LiquidTWI::cursor() {
	_displaycontrol |= LCD_CURSORON;
	command(LCD_DISPLAYCONTROL | _displaycontrol);
}

// Turn on and off the blinking cursor
void LiquidTWI::noBlink() {
	_displaycontrol &= ~LCD_BLINKON;
	command(LCD_DISPLAYCONTROL | _displaycontrol);
}
void LiquidTWI::blink() {
	_displaycontrol |= LCD_BLINKON;
	command(LCD_DISPLAYCONTROL | _displaycontrol);
}

// These commands scroll the display without changing the RAM
void LiquidTWI::scrollDisplayLeft(void) {
	command(LCD_CURSORSHIFT | LCD_DISPLAYMOVE | LCD_MOVELEFT);
}
void LiquidTWI::scrollDisplayRight(void) {
	command(LCD_CURSORSHIFT | LCD_DISPLAYMOVE | LCD_MOVERIGHT);
}

// This is for text that flows Left to Right
void LiquidTWI::leftToRight(void) {
	_displaymode |= LCD_ENTRYLEFT;
	command(LCD_ENTRYMODESET | _displaymode);
}

// This is for text that flows Right to Left
void LiquidTWI::rightToLeft(void) {
	_displaymode &= ~LCD_ENTRYLEFT;
	command(LCD_ENTRYMODESET | _displaymode);
}

// This will 'right justify' text from the cursor
void LiquidTWI::autoscroll(void) {
	_displaymode |= LCD_ENTRYSHIFTINCREMENT;
	command(LCD_ENTRYMODESET | _displaymode);
}

// This will 'left justify' text from the cursor
void LiquidTWI::noAutoscroll(void) {
	_displaymode &= ~LCD_ENTRYSHIFTINCREMENT;
	command(LCD_ENTRYMODESET | _displaymode);
}

// Allows us to fill the first 8 CGRAM locations
// with custom characters
void LiquidTWI::createChar(uint8_t location, uint8_t charmap[]) {
	location &= 0x7; // we only have 8 locations 0-7
	command(LCD_SETCGRAMADDR | (location << 3));
	for (int i=0; i<8; i++) {
		write(charmap[i]);
	}
}

/*********** mid level commands, for sending data/cmds */
inline void LiquidTWI::command(uint8_t value) {
	send(value, LOW);
}
#if ARDUINO >= 100
inline size_t LiquidTWI::write(uint8_t value) {
	send(value, HIGH);
	return 1;
}
#else
inline void LiquidTWI::write(uint8_t value) {
	send(value, HIGH);
}
#endif
/************ low level data pushing commands **********/

// Allows to set the backlight, if the LCD backpack is used
void LiquidTWI::setBacklight(uint8_t status) {
	bitWrite(_displaycontrol,3,status); // flag that the backlight is enabled, for burst commands
	burstBits((_displaycontrol & LCD_BACKLIGHT)?0x80:0x00);
}

// write either command or data, burst it to the expander over I2C.
void LiquidTWI::send(uint8_t value, uint8_t mode) {
	// BURST SPEED, OH MY GOD
	// the (now High Speed!) I/O expander pinout
	// RS pin = 1
	// Enable pin = 2
	// Data pin 4 = 3
	// Data pin 5 = 4
	// Data pin 6 = 5
	// Data pin 7 = 6
	byte buf;
	// crunch the high 4 bits
	buf = (value & B11110000) >> 1; // isolate high 4 bits, shift over to data pins (bits 6-3: x1111xxx)
	if (mode) buf |= 3 << 1; // here we can just enable enable, since the value is immediately written to the pins
	else buf |= 2 << 1; // if RS (mode), turn RS and enable on. otherwise, just enable. (bits 2-1: xxxxx11x)
	buf |= (_displaycontrol & LCD_BACKLIGHT)?0x80:0x00; // using DISPLAYCONTROL command to mask backlight bit in _displaycontrol
	burstBits(buf); // bits are now present at LCD with enable active in the same write
	// no need to delay since these things take WAY, WAY longer than the time required for enable to settle (1us in LCD implementation?)
	buf &= ~(1<<2); // toggle enable low
	burstBits(buf); // send out the same bits but with enable low now; LCD crunches these 4 bits.
	// crunch the low 4 bits
	buf = (value & B1111) << 3; // isolate low 4 bits, shift over to data pins (bits 6-3: x1111xxx)
	if (mode) buf |= 3 << 1; // here we can just enable enable, since the value is immediately written to the pins
	else buf |= 2 << 1; // if RS (mode), turn RS and enable on. otherwise, just enable. (bits 2-1: xxxxx11x)
	buf |= (_displaycontrol & LCD_BACKLIGHT)?0x80:0x00; // using DISPLAYCONTROL command to mask backlight bit in _displaycontrol
	burstBits(buf);
	buf &= ~( 1 << 2 ); // toggle enable low (1<<2 = 00000100; NOT = 11111011; with "and", this turns off only that one bit)
	burstBits(buf);
}

void LiquidTWI::burstBits(uint8_t value) {
	// we use this to burst bits to the GPIO chip whenever we need to. avoids repetative code.
	Wire.beginTransmission(MCP23008_ADDRESS | _i2cAddr);
#if ARDUINO >= 100
	Wire.write(MCP23008_GPIO);
	Wire.write(value); // last bits are crunched, we're done.
#else
	Wire.send(MCP23008_GPIO);
	Wire.send(value); // last bits are crunched, we're done.
#endif
	while (Wire.endTransmission()) ;
}