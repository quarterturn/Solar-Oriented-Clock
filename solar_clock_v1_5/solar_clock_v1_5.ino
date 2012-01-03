/* Sun Rise, Noon and Set Clock
 
 Keeps time using a DS3231 RTC chip and calculates sunrise, solar noon and sunset.
 It also displays the moon phase and of course the time.
 
 The clock is controlled via a four-way plus center button interface.
 
 The display is a 20x2 LCD or LCD-compatible VFD.
 
 Version: 1.5
 Author: Alexander Davis
 Hardwarwe: ATMEGA328
 
 Developed on a Modern Device RBBB and a Wicked Device RBBB shield.
 
 Uses the TimeLord library http://www.swfltek.com/arduino/timelord.html
 
 Digital pins used:
 13 INT/SQW from DS3231 (active-low, set internal pull-up)
 12, 11, 5, 4, 3, 2 for the LCD
 10, 9, 8, 7, 6 for buttons (active-low; set internal pull-ups to avoid external pull-down)
 
 Analog pins used:
 5, 4 i2c for DS3231
 */

// for reading flash memory
#include <avr/pgmspace.h>
// for using atmega eeprom
#include <EEPROM.h>
// debounced button library
#include <Bounce.h>
// for LCD
#include <LiquidCrystal.h>
// for using i2c interface
#include <Wire.h>

// for time calculations
#include <TimeLord.h>

// global constants
#define SQW_PIN 13
#define LCD_ROW_SIZE 20
#define LCD_LINES 2
#define SERIAL_BAUD 9600
// menus
#define TIME_DATE 0
#define TZ_LONG_LAT 1
#define DST_START_END 2
#define DISP_SCHED 3
#define SET_12_24_MODE 4
#define SET_DEFAULTS 5
// to help remember which row is time and date
#define TENS_HOURS 0
#define ONES_HOURS 1
#define TENS_MINUTES 3
#define ONES_MINUTES 4
#define TENS_SECONDS 6
#define ONES_SECONDS 7
#define TENS_MONTH 9
#define ONES_MONTH 10
#define TENS_DATE 12
#define ONES_DATE 13
#define TENS_YEAR 15
#define ONES_YEAR 16
#define THE_DAY 18
#define DONE 19
// to help remember for tz and long and lat setting
#define TZ_SIGN 0
#define TZ_TENS 1
#define TZ_ONES 2
#define LONG_SIGN 4
#define LONG_HUND 5
#define LONG_TENS 6
#define LONG_ONES 7
#define LONG_TENTHS 9
#define LONG_HUNDRTHS 10
#define LAT_SIGN 12
#define LAT_TENS 13
#define LAT_ONES 14
#define LAT_TENTHS 16
#define LAT_HUNDRTHS 17
// to help remember for dst settings
#define DST_START_MON_TENS 0
#define DST_START_MON_ONES 1
#define DST_START_WEEK 4
#define DST_START_DAY 7
#define DST_END_MON_TENS 9
#define DST_END_MON_ONES 10
#define DST_END_WEEK 13
#define DST_END_DAY 16
#define DST_ENABLE 18
// to help remember the display schedule settings
#define SCHED_BRIGHT 2
#define SCHED_DIM 9
#define SCHED_OFF 14
// to help remember the 12/24 hour settings
#define MODE_12_24_HOUR 12
// EEPROM memory locations for stored value
#define EE_BIG_MODE 0
#define EE_TIME_ZONE 1
#define EE_LAT_L 20
#define EE_LAT_U 21
#define EE_LONG_L 25
#define EE_LONG_U 26
#define EE_BRIGHT_HR 3
#define EE_DIM_HR 4
#define EE_OFF_HR 5
#define EE_DST_MON_START 6
#define EE_DST_DOW_START 7
#define EE_DST_WEEK_START 8
#define EE_DST_MON_END 9
#define EE_DST_DOW_END 10
#define EE_DST_WEEK_END 11
#define EE_DST_CHANGE_HOUR 12
#define EE_TIME_MODE 13
#define EE_DST_ENABLE 2

//i2c address of ds3231
#define DS3231_I2C_ADDRESS 0x68

// button setup - bounce objects
// 10 msec debounce interval
Bounce rightButton = Bounce(10, 10);
Bounce upButton = Bounce(9, 10);
Bounce downButton = Bounce(8, 10);
Bounce centerButton = Bounce(7, 10);
Bounce leftButton = Bounce(6, 10);
Bounce timeReady = Bounce(13, 10);

// global variables
byte currentByte;

// time, date and location are split up to places
// to allow them to be set one digit at a time

// latitude +/- 180.00
// really a .2 percision float but we store in eeprom as an int
int latitude;
// longitude +/- 90.00
// really a .2 percision float but we store in eeprom as a long
int longitude;
//  moon phase 0-1
float moonPhase;
// bright display start hour
byte brightHour;
// dim display start hour
byte dimHour;
// off display start hour
byte offHour;
// daylight savings time start and stop
byte dstMonStart;
byte dstDowStart;
byte dstWeekStart;
byte dstMonEnd;
byte dstDowEnd;
byte dstWeekEnd;
byte dstChangeHr;
byte dstEnable;
// timezone
int timezone;
// 12 hour mode flag
byte is12Hour;

// day of week
byte day;
  
// array offsets
// 0 second
// 1 minute
// 2 hour
// 3 day
// 4 month
// 5 year

// sunrise
byte sunRise[6];
// sunset
byte sunSet[6];
// noon
byte theNoon[6];
// dst time
byte theTime[6];

// if the sun does not rise (and therefore set either)
boolean sunWillRise = true;

// upper and lower bytes for reading/writing a 2 byte int from EEPROM
byte bu,
     bl;
     
// cursor position
byte colPos = 0;
byte rowPos = 0;

// 3x2 big character time display mode
byte bigMode = 0;

// track if display is on or off
// default is on
byte displayOn = 1;

// 3x2 big character arrays
// an array of characters for each line
// each character is 3 blocks across plus a space
// 32 is an ascii space
//                0         1          2         3          4        5          6        7          8         9
char bn12[] = {0,1,2,32, 1,2,32,32, 6,6,2,32, 6,6,2,32, 3,4,7,32, 3,6,6,32, 0,6,6,32, 1,1,2,32, 0,6,2,32, 0,6,2,32};
char bn22[] = {3,4,5,32, 4,7,4,32, 3,4,4,32, 4,4,5,32, 32,32,7,32, 4,4,5,32, 3,4,5,32, 32,32,7,32, 3,4,5,32, 32,32,7,32};

// the value under the cursor
byte currentValue;

// position mask
// controls where the cursor is allowed to be during setting mode
byte menuMask[5][20] = {{1,1,0,1,1,0,1,1,0,1,1,0,1,1,0,1,1,0,1,1},
                        {1,1,1,0,1,1,1,1,0,1,1,0,1,1,1,0,1,1,0,1},
                        {1,1,0,0,1,0,0,1,0,1,1,0,0,1,0,0,1,0,1,1},
                        {0,0,1,0,0,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1},
                        {0,0,0,0,0,0,0,0,0,0,0,0,1,0,0,0,0,0,0,1}
};
                        
// value mask
// controls how large each value can be 0-9
byte valueMask[4][20] = {
  {2,9,0,5,9,0,5,9,0,1,9,0,3,9,0,9,9,0,7,1},
  {1,1,9,0,1,1,9,9,0,9,9,0,1,9,9,0,9,9,0,1},
  {1,9,0,0,4,0,0,7,0,1,9,0,0,4,0,0,7,0,1,1},
  {0,0,2,9,0,0,0,0,0,2,9,0,0,0,2,9,0,0,0,1}
};

// menu values
// temorarily stores values so that the user can change them one character at a time
byte menuValues[20];

// configure the LCD pins
LiquidCrystal lcd(12, 11, 5, 4, 3, 2);

// the number of setting menus including done
#define NUM_MENUS 6

// strings to display stored in FLASH
// menu strings
prog_char menu0[] PROGMEM = "SET STD TIME & DATE";
prog_char menu1[] PROGMEM = "SET TZ, LONG & LAT";
prog_char menu2[] PROGMEM = "SET DST START & END";
prog_char menu3[] PROGMEM = "SET DISPLAY SCHEDULE";
prog_char menu4[] PROGMEM = "SET 12/24 HR MODE";
prog_char menu5[] PROGMEM = "SET DEFAULTS";
prog_char menu6[] PROGMEM = "DONE";
prog_char menu7[] PROGMEM = "Solar Clock    1.5";
prog_char menu8[] PROGMEM = "Alex Davis 1/2/2012";

// an array of menu strings stored in FLASH
PROGMEM  const char *menuStrSet[] = {
  menu0,
  menu1,
  menu2,
  menu3,
  menu4,
  menu5,
  menu6,
  menu7,
  menu8
};

// a string for PROGMEM values
char currentString[20];

// create a TimeLord object
TimeLord myLord;

// setup
void setup()
{

  // set up the serial port
  Serial.begin(SERIAL_BAUD);
  
  // button setup
  // enable internal pull-ups
  pinMode(6, INPUT);
  digitalWrite(6, HIGH);
  pinMode(7, INPUT);
  digitalWrite(7, HIGH);
  pinMode(8, INPUT);
  digitalWrite(8, HIGH);
  pinMode(9, INPUT);
  digitalWrite(9, HIGH);
  pinMode(10, INPUT);
  digitalWrite(10, HIGH);
  pinMode(13, INPUT);
  digitalWrite(13, HIGH);
  
  // lood the big font custom characters to the lcd
  lcdCustomChars();

  // read the settings from the EEPROM
  bigMode = EEPROM.read(EE_BIG_MODE);
  // combine the upper and lower bytes
  // into one two-byte int
  bu = EEPROM.read(EE_LAT_U);
  bl = EEPROM.read(EE_LAT_L);
  latitude = (int)word (bu, bl);
  bu = EEPROM.read(EE_LONG_U);
  bl = EEPROM.read(EE_LONG_L);
  longitude = (int)word (bu, bl);
  brightHour = EEPROM.read(EE_BRIGHT_HR);
  dimHour = EEPROM.read(EE_DIM_HR);
  offHour = EEPROM.read(EE_OFF_HR);
  // timezone and dst info
  dstMonStart = EEPROM.read(EE_DST_MON_START);
  dstDowStart = EEPROM.read(EE_DST_DOW_START);
  dstWeekStart = EEPROM.read(EE_DST_WEEK_START);
  dstMonEnd = EEPROM.read(EE_DST_MON_END);
  dstDowEnd = EEPROM.read(EE_DST_DOW_END);
  dstWeekEnd = EEPROM.read(EE_DST_WEEK_END);
  dstChangeHr = EEPROM.read(EE_DST_CHANGE_HOUR);
  dstEnable = EEPROM.read(EE_DST_ENABLE);
  is12Hour = EEPROM.read(EE_TIME_MODE);
  timezone = EEPROM.read(EE_TIME_ZONE);
  if (timezone > 127)
  {
    timezone = (256 - timezone) * -1;
  }
  
  // configure the LCD size for 20x2
  lcd.begin(LCD_ROW_SIZE, 2);
  
  // clear the lcd
  lcd.clear();

  // set brightness to 75%
  lcd.vfdDim(2);
  
  // display version and contact info
  intro();

  // move the LCD cursor to home
  lcd.home();

  // clear /EOSC bit
  // Sometimes necessary to ensure that the clock
  // keeps running on just battery power. Once set,
  // it shouldn't need to be reset but it's a good
  // idea to make sure.
  Wire.begin();
  Wire.beginTransmission(DS3231_I2C_ADDRESS); // address DS3231
  Wire.write(0x0E); // select register
  Wire.write(0b00011100); // write register bitmap, bit 7 is /EOSC
  Wire.endTransmission();

  // enable the SQW pin output
  // the function sets it to 1 HZ
  SQWEnable();
  
    // TimeLord library configuration
  // set lat and long
  // requires floats so cast to float and move decimal place 2 to left
  myLord.Position(((float)latitude) / 100, ((float)longitude) / 100);
  // set timezone
  myLord.TimeZone(timezone * 60);
  // set dst rules
  myLord.DstRules(dstMonStart, dstWeekStart, dstMonEnd, dstWeekEnd, 60);
  
  // grab the date and time for use in sun calculations
  getDate();
  getTime();
  day = myLord.DayOfWeek(theTime);
  
  // set the sunRise array
  sunRise[0] = 0;
  sunRise[1] = 0;
  sunRise[2] = 0;
  sunRise[3] = theTime[3];
  sunRise[4] = theTime[4];
  sunRise[5] = theTime[5];
 
  // call the SunRise method
  sunWillRise = myLord.SunRise(sunRise);
  
  // set the sunSet array
  sunSet[0] = 0;
  sunSet[1] = 0;
  sunSet[2] = 0;
  sunSet[3] = theTime[3];
  sunSet[4] = theTime[4];
  sunSet[5] = theTime[5];
  
  // call the sunSet method
  myLord.SunSet(sunSet);
  
  // call the MoonPhase method
  moonPhase = myLord.MoonPhase(sunRise);
  
  // calculate noon from sunrise and sunset times
  calculateNoon();
  
  // convert time to dst if enabled
  if (dstEnable)
  {
    myLord.DST(theTime);
    myLord.DST(sunRise);
    myLord.DST(sunSet);
    myLord.DST(theNoon);
  }
}

//---------------------------------------------------------------------------------------------//
// main loop
//---------------------------------------------------------------------------------------------//
void loop()
{
  // DS3231 sends 1 Hz 50% duty cycle signal to pin 13
  // trigger on the edge of going low
  if (timeReady.update())
  {
    if (timeReady.fallingEdge())
    {
      if (bigMode)
      {
        displayBigTimeAndDate();
      }
      else
      {
        displayTimeAndDate();
      }
    }
  }

  // center button is set
  if (centerButton.update())
  {
    if ((centerButton.read()) == LOW)
    {
      setMenu();
    }
  }
  
  // toggle big or small display mode based on up button
  if (upButton.update() && (bigMode == 0))
  {
    if ((upButton.read()) == LOW)
    {
      bigMode = 1;
      EEPROM.write(EE_BIG_MODE, 1);
      lcd.clear();
    }
  }
  if (upButton.update() && (bigMode == 1))
  {
    if ((upButton.read()) == LOW)
    {
      bigMode = 0;
      EEPROM.write(EE_BIG_MODE, 0);
      lcd.clear();
    }
  }
  // toggle display on or off based on down button
  // we don't track this in eeprom
  // the idea is you turn it off temporarily
  // for example you go to bed early and want the display off
  if (downButton.update())
  {
    if ((downButton.read()) == LOW)
    {
      if (displayOn)
      {
        // turn off the display
        lcd.noDisplay();
        displayOn = 0;
      }
      else
      {
        // turn on the display
        lcd.display();
        displayOn = 1;
      }
    }
  }
}

//---------------------------------------------------------------------------------------------//
// function displaySettingData
// prints data in menuValues based on the menu index
// to the lcd
//---------------------------------------------------------------------------------------------//
void displaySettingData(byte menuNum)
{  
  // write the menu values to the display
  for (colPos = 0; colPos < LCD_ROW_SIZE; colPos++)
  {
    // force the cursor to where we want
    // each time we write it moves to the right
    lcd.setCursor(colPos, rowPos);
    if (menuMask[menuNum][colPos])
    {
      // convert the int to a char and send it to the lcd
      currentByte = menuValues[colPos];
      // handle the printing of +/- for the tz, long and lat menu
      if ((menuNum == 1) && ((colPos == 0) || (colPos == 4) || (colPos == 12)))
      {
        if (currentByte == 1)
        {
          lcd.write('+');
        }
        if (currentByte == 0)
        {
          lcd.write('-');
        }
      }
      // otherwise just print whatever is in the array
      else
      {
        lcd.write(48 + currentByte);
      }
    }
  }
  // move the cursor to the bottom line left side
  lcd.setCursor(0,1);
}

//---------------------------------------------------------------------------------------------//
// function decValue
// decreases a value in the curPos position of menuValues
// for the menu number passed
// also updates currentValue
// expects: a byte, menuNUm
// returns: nothing
//---------------------------------------------------------------------------------------------//
void decValue(byte menuNum)
{
  char buf[12];
  
  if (currentValue > 0)
  {
    currentValue--;
    // handle the printing of +/- based on 1/0
    // for the tz, long & lat menu
    if ((menuNum == TZ_LONG_LAT) && ((colPos == TZ_SIGN) || (colPos == LONG_SIGN) || (colPos == LAT_SIGN)))
    {
      if (currentValue == 1)
      {
        lcd.write('+');
      }
      if (currentValue == 0)
      {
        lcd.write('-');
      }
    }
    // otherwise just print whatever is in the array
    else
    {
      // need to add a space after the value for DISP_SCHED
      // to clear going from double to single digit
      if (menuNum == DISP_SCHED)
      {
        // convert the value to a string and display
        lcd.print(itoa(currentValue, buf, 10));
        lcd.print(" ");
      }
      else
      {
        // convert the value to a string and display
        lcd.print(itoa(currentValue, buf, 10));
      }
    }
    // move the cursor back since write moves it to the right
    lcd.setCursor(colPos,rowPos);
    // update the menu value array
    menuValues[colPos] = currentValue;
  }
}

//---------------------------------------------------------------------------------------------//
// function moveRight
// moves the cursor right to the next possible editable value
// based on curPos position of menuMask
// for the menu number passed
// also updates currentValue at that position
//---------------------------------------------------------------------------------------------//
void moveRight(byte menuNum)
{
  if (colPos < (LCD_ROW_SIZE  - 1))
  {
    colPos++;
    // skip over positions not allowed to be set per menuMask
    // until we get to an allowed position or the end of the lcd
    while ((colPos < (LCD_ROW_SIZE - 1)) && !(menuMask[menuNum][colPos]))
    {
      colPos++;
    }
    lcd.setCursor(colPos, rowPos);
    currentValue = menuValues[colPos];
  }
}

//---------------------------------------------------------------------------------------------//
// function moveLeft
// moves the cursor left to the next possible editable value
// based on curPos position of menuMask
// for the menu number passed
// also updates currentValue at that position
//---------------------------------------------------------------------------------------------//
void moveLeft(byte menuNum)
{
  if (colPos > 0)
  {
    colPos--;
    // skip over positions not allowed to be set per menuMask
    // until we get to an allowed position or the end of the lcd
    while ((colPos > 0) && !(menuMask[menuNum][colPos]))
    {
      colPos--;
    }
    lcd.setCursor(colPos, rowPos);
    currentValue = menuValues[colPos];
  }
}

//---------------------------------------------------------------------------------------------//
// function setButton
// writes or ignores the setting information
// for the menu number passed
// returns the status 1 or 0
//---------------------------------------------------------------------------------------------//
byte setButton(byte menuNum)
{
  if (colPos == DONE)
  {
    return 1;
  }
  else
  {
    return 0;
  }
}

//---------------------------------------------------------------------------------------------//
// function setTimeDate
// sets the time and date
//---------------------------------------------------------------------------------------------//
void setTimeDate()
{
  byte setStatus = 0;
  
  // get the time and date
  // so they will be in std time format
  getDate();
  getTime();

  // clear the lcd
  lcd.clear();

  // move the LCD cursor to home
  lcd.home();

  // print the set time prompt to the display
  lcd.print("SET TIME AND DATE");

  // move the cursor to the bottom line left side
  lcd.setCursor(0,1);

  // turn on the cursor
  lcd.cursor();

  // set the array indexes to the same position
  rowPos = 1;
  colPos = 0;

  // reset the array
  memset(menuValues, 0, (sizeof(menuValues)/sizeof(menuValues[0])));
  
  // array offsets
  // 0 second
  // 1 minute
  // 2 hour
  // 3 date
  // 4 month
  // 5 year

  // load the time and date values into the setting array
  // we will break them up into digits as that is how they are stored
  // tens hours
  menuValues[TENS_HOURS] = theTime[2] / 10;
  // ones hours
  menuValues[ONES_HOURS] = theTime[2] % 10;
  // tens minutes
  menuValues[TENS_MINUTES] = theTime[1] / 10;
  // ones minutes
  menuValues[ONES_MINUTES] = theTime[1] % 10;
  // tens seconds
  menuValues[TENS_SECONDS] = theTime[0] / 10;
  // ones seconds
  menuValues[ONES_SECONDS] = theTime[0] % 10;
  // tens month
  menuValues[TENS_MONTH] = theTime[4] / 10;
  // ones month
  menuValues[ONES_MONTH] = theTime[4] % 10;
  // tens day
  menuValues[TENS_DATE] = theTime[3] / 10;
  // ones day
  menuValues[ONES_DATE] = theTime[3] % 10;
  // tens year
  menuValues[TENS_YEAR] = theTime[5] / 10;
  // ones year
  menuValues[ONES_YEAR] = theTime[5] % 10;
  // the day of week
  menuValues[THE_DAY] = day;

  // write the current time and date data to the lcd
  displaySettingData(TIME_DATE);

  // time setting symbols
  lcd.setCursor(2,1);
  lcd.write(':');
  lcd.setCursor(5,1);
  lcd.write(':');
  lcd.setCursor(11,1);
  lcd.write('/');
  lcd.setCursor(14,1);
  lcd.write('/');
  lcd.setCursor(19,1);
  lcd.write('*');

  // move the cursor to the bottom line left side
  lcd.setCursor(0,1);

  // set currentValue to match the cursor position
  currentValue = menuValues[0];
  colPos = 0;

  while (1)
  {
    // if the down button is pressed,
    // decement the value so long as it is not out of range
    // and it is in a position allowed to be changed
    if ((downButton.update()) && (menuMask[TIME_DATE][colPos]))
    {
      if (downButton.read() == LOW)
      {
        decValue(TIME_DATE);
      }
    }

    // if the up button is pressed,
    // increment the value so long as it is not more than the value mask
    if (upButton.update() && menuMask[TIME_DATE][colPos])
    {
      if (upButton.read() == LOW)
      {
        // more logic to keep values in range
        switch (colPos)
        {
          case TENS_HOURS:
            if (checkHourValue())
            {
              currentValue++;
            }
            break;
          case ONES_HOURS:
            if (checkHourValue())
            {
              currentValue++;
            }
            break;
          case TENS_MINUTES:
            if (checkMinuteValue())
            {
              currentValue++;
            }
            break;
          case ONES_MINUTES:
            if (checkMinuteValue())
            {
              currentValue++;
            }
            break;
          case TENS_SECONDS:
            if (checkSecondValue())
            {
              currentValue++;
            }
            break;
          case ONES_SECONDS:
            if (checkSecondValue())
            {
              currentValue++;
            }
            break;
          case TENS_MONTH:
            if (checkMonthValue())
            {
              currentValue++;
            }
            break;
          case ONES_MONTH:
            if (checkMonthValue())
            {
              currentValue++;
            }
            break;
          case TENS_DATE:
            if (checkDayValue())
            {
              currentValue++;
            }
            break;
          case ONES_DATE:
            if (checkDayValue())
            {
              currentValue++;
            }
            break;
          case TENS_YEAR:
            if (checkYearValue())
            {
              currentValue++;
            }
            break;
          case ONES_YEAR:
            if (checkYearValue())
            {
              currentValue++;
            }
            break;
          case THE_DAY:
            if (currentValue < 7)
            {
              currentValue++;
            }
            break;
          case DONE:
            if (currentValue < 1)
            {
              currentValue++;
            }
            break;
          // otherwise don't do anything
          default:
            break;
        }
      }
      lcd.write(48 + currentValue);
      // move the cursor back since write moves it to the right
      lcd.setCursor(colPos,rowPos);
      // update the menu value array
      menuValues[colPos] = currentValue;
    }

    // go right on right button,
    // unless we are at the end of the array
    if (rightButton.update())
    {
      if (rightButton.read() == LOW)
      {
        moveRight(TIME_DATE);
      }
    }

    // go left on left button
    // unless we are at the end of the array
    if (leftButton.update())
    {
      if (leftButton.read() == LOW)
      {
        moveLeft(TIME_DATE);
      }
    }

    // if the middle button is pressed,
    // call function to test if we are on the set field
    // and return the status of 1 (set) or 0 (ignore)
    if ((centerButton.update()) && setButton(TIME_DATE))
    {
      if (centerButton.read() == LOW)
      {
        if (currentValue == 1)
        {
          rtcSetTimeDate();
        }
        lcd.noCursor();
        return;
      }
    }
  }
}

//---------------------------------------------------------------------------------------------//
// function setTzLongLat
// sets the time and date
//---------------------------------------------------------------------------------------------//
void setTzLongLat()
{
  // holds the set status
  byte setStatus = 0;
  // holds the values before writing to the eeprom
  byte tmpTz;
  int tmpLong;
  int tmpLat;

  // clear the lcd
  lcd.clear();

  // move the LCD cursor to home
  lcd.home();

  // print the prompt to the display
  lcd.print(" TZ    LONG    LAT");

  // move the cursor to the bottom line left side
  lcd.setCursor(0,1);

  // turn on the cursor
  lcd.cursor();

  // set the array indexes to the same position
  rowPos = 1;
  colPos = 0;

  // reset the array
  memset(menuValues, 0, (sizeof(menuValues)/sizeof(menuValues[0])));
  // timezone sign
  if (timezone < 0)
  {
    menuValues[TZ_SIGN] = 0;
    timezone = timezone * -1;
  }
  else
  {
    menuValues[TZ_SIGN] = 1;
  }
  // timezone tens
  menuValues[TZ_TENS] = timezone / 10;
  // timezone ones
  menuValues[TZ_ONES] = timezone % 10;
  // longitude sign
  if (longitude < 0)
  {
    menuValues[LONG_SIGN] = 0;
    longitude = longitude * -1;
  }
  else
  {
    menuValues[LONG_SIGN] = 1;
  }
  // logitude and latitude are stored in the eeprom as a dword - two bytes
  // we store it that way so we can use div and mod, which only work on int
  // longitude hundreds
  menuValues[LONG_HUND] = longitude / 10000;
  // logitude tens
  menuValues[LONG_TENS] = (longitude % 10000) / 1000;
  // longitude ones
  menuValues[LONG_ONES] = (longitude % 1000) / 100;
  // longitude tenths
  menuValues[LONG_TENTHS] = (longitude % 100) / 10;
  // longitude hundredths
  menuValues[LONG_HUNDRTHS] = longitude % 10;
  // latitude sign
  if (latitude < 0)
  {
    menuValues[LAT_SIGN] = 0;
    latitude = latitude * -1;
  }
  else
  {
    menuValues[LAT_SIGN] = 1;
  }
  // latitude tens
  menuValues[LAT_TENS] = latitude / 1000;
  // latitude ones
  menuValues[LAT_ONES] = (latitude % 1000) / 100;
  // latitude tenths
  menuValues[LAT_TENTHS] = (latitude % 100) / 10;
  // latitude hundredths
  menuValues[LAT_HUNDRTHS] = latitude % 10;

  // write the current setting data to the lcd
  displaySettingData(TZ_LONG_LAT);

  // setting symbols
  lcd.setCursor(8,1);
  lcd.write('.');
  lcd.setCursor(15,1);
  lcd.write('.');
  lcd.setCursor(19,1);
  lcd.write('*');

  // move the cursor to the bottom line left side
  lcd.setCursor(0,1);

  // set currentValue to match the cursor position
  currentValue = menuValues[0];
  colPos = 0;

  while (1)
  {
    // if the down button is pressed,
    // decement the value so long as it is not out of range
    // and it is in a position allowed to be changed
    if ((downButton.update()) && (menuMask[TZ_LONG_LAT][colPos]))
    {
      if (downButton.read() == LOW)
      {
        decValue(TZ_LONG_LAT);
      }
    }

    // if the up button is pressed,
    // increment the value so long as it is not more than the value mask
    if (upButton.update() && menuMask[TZ_LONG_LAT][colPos])
    {
      if (upButton.read() == LOW)
      {
        if (currentValue < valueMask[TZ_LONG_LAT][colPos])
        {
          // more logic to keep values in range
          switch (colPos)
          {    
            // tz value checks
            // in each case the check is to see
            // that all pieces add up to less than 12
          case TZ_TENS:
            if (checkTzValue())
            {
              currentValue++;
            }
            break;         
            // tz ones position
          case TZ_ONES:
            if (checkTzValue())
            {
              currentValue++;
            }
            break;
            //longitude value checks
            // in each case the check is to see
            // that all pieces add up to less than 18000
          case LONG_HUND:
            if (checkLongValue())
            {
              currentValue++;
            }
            break;
          case LONG_TENS:
            if (checkLongValue())
            {
              currentValue++;
            }
            break;
          case LONG_ONES:
            if (checkLongValue())
            {
              currentValue++;
            }
            break;
          case LONG_TENTHS:
            if (checkLongValue())
            {
              currentValue++;
            }
            break;
          case LONG_HUNDRTHS:
            if (checkLongValue())
            {
              currentValue++;
            }
            break;
            // latitude value checks
            // in each case the check is to see
            // that all pieces add up to less than 9000
          case LAT_TENS:
            if (checkLatValue())
            {
              currentValue++;
            }
            break;
          case LAT_ONES:
            if (checkLatValue())
            {
              currentValue++;
            }
            break;
          case LAT_TENTHS:
            if (checkLatValue())
            {
              currentValue++;
            }
            break;
          case LAT_HUNDRTHS:
            if (checkLatValue())
            {
              currentValue++;
            }
            break;
            // otherwise just follow the valueMask value for colPos
          default:
            currentValue++;
          }
          // handle the printing of +/- based on 1/0
          if ((colPos == 0) || (colPos == 4) || (colPos == 12))
          {
            if (currentValue == 1)
            {
              lcd.write('+');
            }
            if (currentValue == 0)
            {
              lcd.write('-');
            }
          }
          // otherwise just print whatever is in the array
          else
          {
            lcd.write(48 + currentValue);
          }
          // move the cursor back since write moves it to the right
          lcd.setCursor(colPos,rowPos);
          // update the menu value array
          menuValues[colPos] = currentValue;
        }
        // handle the printing of +/- based on 1/0
        if ((colPos == 0) || (colPos == 4) || (colPos == 12))
        {
          if (currentValue == 1)
          {
            lcd.write('+');
          }
          if (currentValue == 0)
          {
            lcd.write('-');
          }
        }
        // otherwise just print whatever is in the array
        else
        {
          lcd.write(48 + currentValue);
        }
        // move the cursor back since write moves it to the right
        lcd.setCursor(colPos,rowPos);
        // update the menu value array
        menuValues[colPos] = currentValue;
      }
    }

    // go right on right button,
    // unless we are at the end of the array
    if (rightButton.update())
    {
      if (rightButton.read() == LOW)
      {
        moveRight(TZ_LONG_LAT);
      }
    }

    // go left on left button
    // unless we are at the end of the array
    if (leftButton.update())
    {
      if (leftButton.read() == LOW)
      {
        moveLeft(TZ_LONG_LAT);
      }
    }

    // if the middle button is pressed,
    // call function to test if we are on the set field
    // and return the status of 1 (set) or 0 (ignore)
    if ((centerButton.update()) && setButton(TZ_LONG_LAT))
    {
      if (centerButton.read() == LOW)
      {
        if (currentValue == 1)
        {
          // reassemble the values, add sign (if needed) and write to EEPROM
          tmpTz = (menuValues[TZ_TENS] * 10) + menuValues[TZ_ONES];
          tmpLong = (menuValues[LONG_HUND] * 10000) + (menuValues[LONG_TENS] * 1000) + (menuValues[LONG_ONES] * 100) + (menuValues[LONG_TENTHS] * 10) + menuValues[LONG_HUNDRTHS];
          tmpLat = (menuValues[LAT_TENS] * 1000) + (menuValues[LAT_ONES] * 100) + (menuValues[LAT_TENTHS] * 10) + menuValues[LAT_HUNDRTHS];
          if (menuValues[TZ_SIGN] == 0)
          {
            tmpTz = tmpTz * -1;
          }
          if (menuValues[LONG_SIGN] == 0)
          {
            tmpLong = tmpLong * -1;
          }
          if (menuValues[LAT_SIGN] == 0)
          {
            tmpLat = tmpLat * -1;
          }
          // write the timezone to the eeprom
          EEPROM.write(EE_TIME_ZONE, tmpTz);
          // write the longitude to the eeprom
          bu = highByte(tmpLong);
          bl = lowByte(tmpLong);
          EEPROM.write(EE_LONG_U, bu);
          EEPROM.write(EE_LONG_L, bl); 
          // write the latitude to the eeprom
          bu = highByte(tmpLat);
          bl = lowByte(tmpLat);
          EEPROM.write(EE_LAT_U, bu);
          EEPROM.write(EE_LAT_L, bl);
          
          // update the globals with the temp values
          timezone = tmpTz;
          longitude = tmpLong;
          latitude = tmpLat;
          
        }
        // turn off the cursor
        lcd.noCursor();
        // exit the set tz, long and lat function
        return;
      }
    } 
  }
}

//---------------------------------------------------------------------------------------------//
// function setDstStartEnd
// sets the DST start and end dates
//---------------------------------------------------------------------------------------------//
void setDstStartEnd()
{
  byte setStatus = 0;

  // clear the lcd
  lcd.clear();

  // move the LCD cursor to home
  lcd.home();

  // print the set time prompt to the display
  lcd.print("SM SW SD EM EW ED ?");

  // move the cursor to the bottom line left side
  lcd.setCursor(0,1);

  // turn on the cursor
  lcd.cursor();

  // set the array indexes to the same position
  rowPos = 1;
  colPos = 0;

  // reset the array
  memset(menuValues, 0, (sizeof(menuValues)/sizeof(menuValues[0])));

  // load the time and date values into the setting array
  // we will break them up into digits as that is how they are stored
  // start tens month
  menuValues[DST_START_MON_TENS] =  dstMonStart / 10;
  // start ones month
  menuValues[DST_START_MON_ONES] = dstMonStart % 10;
  // start tens day
  menuValues[DST_START_DAY] = dstDowStart;
  // start ones day
  menuValues[DST_START_WEEK] = dstWeekStart;
  // end tens month
  menuValues[DST_END_MON_TENS] = dstMonEnd / 10;
  // end ones month
  menuValues[DST_END_MON_ONES] = dstMonEnd % 10;
  // end tens day
  menuValues[DST_END_DAY] = dstDowEnd;
  // end ones day
  menuValues[DST_END_WEEK] = dstWeekEnd;
  // enable
  menuValues[DST_ENABLE] = dstEnable;

  // write the current dst data to the lcd
  displaySettingData(DST_START_END);

  // time setting symbols
  lcd.setCursor(19,1);
  lcd.write('*');

  // move the cursor to the bottom line left side
  lcd.setCursor(0,1);

  // set currentValue to match the cursor position
  currentValue = menuValues[0];
  colPos = 0;

  while (1)
  {
    // if the down button is pressed,
    // decement the value so long as it is not out of range
    // and it is in a position allowed to be changed
    if ((downButton.update()) && (menuMask[DST_START_END][colPos]))
    {
      if (downButton.read() == LOW)
      {
        decValue(DST_START_END);
      }
    }

    // if the up button is pressed,
    // increment the value so long as it is not more than the value mask
    if (upButton.update() && menuMask[DST_START_END][colPos])
    {
      if (upButton.read() == LOW)
      {
        // more logic to keep values in range
        switch (colPos)
        {
          // limit start month to 12
          case DST_START_MON_TENS:
            if (((menuValues[DST_START_MON_TENS] * 10) + menuValues[DST_START_MON_ONES]) < 12)
            {
              currentValue++;
            }
            break;
          case DST_START_MON_ONES:
            if (((menuValues[DST_START_MON_TENS] * 10) + menuValues[DST_START_MON_ONES]) < 12)
            {
              currentValue++;
            }
            break;
            // limit end month to 12 
          case DST_START_WEEK:
            if (currentValue < 4)
            {
              currentValue++;
            }
            break;
          case DST_START_DAY:
            if (currentValue < 7)
            {
              currentValue++;
            }
            break;
          case DST_END_MON_TENS:
            if (((menuValues[DST_END_MON_TENS] * 10) + menuValues[DST_END_MON_ONES]) < 12)
            {
              currentValue++;
            }
            break;
          case DST_END_MON_ONES:
            if (((menuValues[DST_END_MON_TENS] * 10) + menuValues[DST_END_MON_ONES]) < 12)
            {
              currentValue++;
            }
            break;
          case DST_END_WEEK:
            if (currentValue < 4)
            {
              currentValue++;
            }
            break;
          case DST_END_DAY:
            if (currentValue < 7)
            {
              currentValue++;
            }
            break;
          case DST_ENABLE:
            if (currentValue < 1)
            {
              currentValue++;
            }
            break;
          case DONE:
            if (currentValue < 1)
            {
              currentValue++;
            }
            break;
            // otherwise do nothing
          default:
            currentValue++;
        }
        lcd.write(48 + currentValue);
        // move the cursor back since write moves it to the right
        lcd.setCursor(colPos,rowPos);
        // update the menu value array
        menuValues[colPos] = currentValue;
      }
    }

    // go right on right button,
    // unless we are at the end of the array
    if (rightButton.update())
    {
      if (rightButton.read() == LOW)
      {
        moveRight(DST_START_END);
      }
    }

    // go left on left button
    // unless we are at the end of the array
    if (leftButton.update())
    {
      if (leftButton.read() == LOW)
      {
        moveLeft(DST_START_END);
      }
    }

    // if the middle button is pressed,
    // call function to test if we are on the set field
    // and return the status of 1 (set) or 0 (ignore)
    if ((centerButton.update()) && setButton(DST_START_END))
    {
      if (centerButton.read() == LOW)
      {
        if (currentValue == 1)
        {
          // reconstruct the values
          dstMonStart = (menuValues[DST_START_MON_TENS] * 10) + menuValues[DST_START_MON_ONES];
          dstDowStart = menuValues[DST_START_DAY];
          dstWeekStart =  menuValues[DST_START_WEEK];
          dstMonEnd = (menuValues[DST_END_MON_TENS] * 10) + menuValues[DST_END_MON_ONES];
          dstDowEnd = menuValues[DST_END_DAY];
          dstWeekEnd =  menuValues[DST_END_WEEK];
          dstEnable = menuValues[DST_ENABLE];
          
          // write the dst change info to the eeprom
          EEPROM.write(EE_DST_MON_START, dstMonStart);
          EEPROM.write(EE_DST_DOW_START, dstDowStart);
          EEPROM.write(EE_DST_WEEK_START, dstWeekStart);
          EEPROM.write(EE_DST_MON_END, dstMonEnd);
          EEPROM.write(EE_DST_DOW_END, dstDowEnd);
          EEPROM.write(EE_DST_WEEK_END, dstWeekEnd);
          // update dst enable flag
          dstEnable = menuValues[DST_ENABLE];
          // write the dst enable flag to eeprom
          EEPROM.write(EE_DST_ENABLE, dstEnable);
  
          myLord.TimeZone(timezone * 60);
          myLord.DstRules(dstMonStart, dstWeekStart, dstMonEnd, dstWeekEnd, 60);
        }
      }             
      // turn off the cursor
      lcd.noCursor();
      // exit the set tz, long and lat function
      return;
    } 
  }
}

//---------------------------------------------------------------------------------------------//
// function setDispSched
// displays the choices for setting schedule for bright, dim and off display
//---------------------------------------------------------------------------------------------//
void setDispSched()
{
  byte setStatus = 0;
  
  char buf[12];

  // clear the lcd
  lcd.clear();

  // move the LCD cursor to home
  lcd.home();
  
  // reset the array
  memset(menuValues, 0, (sizeof(menuValues)/sizeof(menuValues[0])));
  
  // load the array with the current values
  menuValues[SCHED_BRIGHT] = brightHour;
  menuValues[SCHED_DIM] = dimHour;
  menuValues[SCHED_OFF] = offHour;

  // print the set time prompt to the display
  lcd.print("BRIGHT  DIM  OFF");

  // move the cursor to the bottom line left side
  lcd.setCursor(0,1);

  // turn on the cursor
  lcd.cursor();

  // set the array indexes to the same position
  rowPos = 1;
  colPos = 0;
  
  // write the current data to the display 
  // we're storing the whole value in one array byte
  // so we can't use displaySettingData()
  lcd.setCursor(2,1);
  lcd.print(brightHour, DEC);
  lcd.setCursor(9,1);
  lcd.print(dimHour, DEC);
  lcd.setCursor(14,1);
  lcd.print(offHour, DEC);
  lcd.setCursor(19,1);
  lcd.print('*');

  // move the cursor to the bottom line left side
  lcd.setCursor(0,1);

  // set currentValue to match the cursor position
  currentValue = menuValues[0];
  colPos = 0;

  while (1)
  {
    // if the down button is pressed,
    // decement the value so long as it is not out of range
    // and it is in a position allowed to be changed
    if ((downButton.update()) && (menuMask[DISP_SCHED][colPos]))
    {
      if (downButton.read() == LOW)
      {
        decValue(DISP_SCHED);
      }
    }

    // if the up button is pressed,
    // increment the value so long as it is not more than the value mask
    if (upButton.update() && menuMask[DISP_SCHED][colPos])
    {
      if (upButton.read() == LOW)
      {
        // more logic to keep values in range
        switch (colPos)
        {    
          // display schedule checks
          // bright time check
          case SCHED_BRIGHT:
            if (currentValue < 23)
            {
              currentValue++;
            }
            break;
          // dim time check
          case SCHED_DIM:
            if (currentValue < 23)
            {
              currentValue++;
            }
            break;
          // off time check
          case SCHED_OFF:
            if (currentValue < 23)
            {
              currentValue++;
            }
            break;
          // set or not position - done
          case DONE:
            if (currentValue < 1)
            {
              currentValue++;
            }
            break;
          // otherwise do nothing
          default:
            break;
        }
  
        // convert the value to a string and display
        lcd.print(itoa(currentValue, buf, 10));
        lcd.print(" ");
        // move the cursor back since write moves it to the right
        lcd.setCursor(colPos,rowPos);
        // update the menu value array
        menuValues[colPos] = currentValue;
      }
    }
 
    // go right on right button,
    // unless we are at the end of the array
    if (rightButton.update())
    {
      if (rightButton.read() == LOW)
      {
        moveRight(DISP_SCHED);
      }
    }

    // go left on left button
    // unless we are at the end of the array
    if (leftButton.update())
    {
      if (leftButton.read() == LOW)
      {
        moveLeft(DISP_SCHED);
      }
    }

    // if we are on the set position
    // middle button sets if 1 and discards if 0
    if ((centerButton.update()) && setButton(DISP_SCHED))
    {
      if (centerButton.read() == LOW)
      {
        if (currentValue == 1)
        {
          // refresh the current values with the array values
          brightHour = menuValues[SCHED_BRIGHT];
          dimHour = menuValues[SCHED_DIM];
          offHour = menuValues[SCHED_OFF];
          
          // write the values to the eeprom
          EEPROM.write(EE_BRIGHT_HR, brightHour);
          EEPROM.write(EE_DIM_HR, dimHour);
          EEPROM.write(EE_OFF_HR, offHour);
          
          // turn off the cursor
          lcd.noCursor();
          // exit the set display schedule
          return;
          
        }             
        // turn off the cursor
        lcd.noCursor();
        // exit the set display schedule menu
        return;
      }
    } 
  }
}

//---------------------------------------------------------------------------------------------//
// function set1224Mode
// displays the choices for 12/24 hour display mode
//---------------------------------------------------------------------------------------------//
void set1224Mode()
{
  byte setStatus = 0;

  // clear the lcd
  lcd.clear();

  // move the LCD cursor to home
  lcd.home();
  
  // reset the array
  memset(menuValues, 0, (sizeof(menuValues)/sizeof(menuValues[0])));

  // print the set time prompt to the display
  lcd.print("USE 12 HOUR MODE");

  // move the cursor to the bottom line left side
  lcd.setCursor(0,1);

  // turn on the cursor
  lcd.cursor();

  // set the array indexes to the same position
  rowPos = 1;
  colPos = 0;
  
  // write the current data to the display 
  lcd.setCursor(12,1);
  if (is12Hour)
  {
    lcd.print("YES");
    menuValues[MODE_12_24_HOUR] = 1;
  }
  else
  {
    lcd.print("NO ");
    menuValues[MODE_12_24_HOUR] = 0;
  }
  lcd.setCursor(19,1);
  lcd.print('*');

  // move the cursor to the bottom line left side
  lcd.setCursor(0,1);

  // set currentValue to match the cursor position
  currentValue = menuValues[0];
  colPos = 0;

  while (1)
  {
    // if the down button is pressed,
    // decement the value so long as it is not out of range
    // and it is in a position allowed to be changed
    if ((downButton.update()) && (menuMask[SET_12_24_MODE][colPos]))
    {
      if (downButton.read() == LOW)
      {
        // more logic to keep values in range
        switch (colPos)
        {    
          // 12 hour enable position
          case MODE_12_24_HOUR:
            if (currentValue > 0)
            {
              currentValue--;
              lcd.print("NO ");
              lcd.setCursor(colPos,rowPos);
            }
            break;
          // set or not position - done
          case DONE:
            if (currentValue > 0)
            {
              currentValue++;
              lcd.print("0");
              lcd.setCursor(colPos,rowPos);
            }
            break;
          // otherwise do nothing
          default:
            break;
        }
      }
    }
    
    // if the up button is pressed,
    // increment the value so long as it is not more than the value mask
    if (upButton.update() && menuMask[SET_12_24_MODE][colPos])
    {
      if (upButton.read() == LOW)
      {
        // more logic to keep values in range
        switch (colPos)
        {    
          // 12 hour enable position
          case MODE_12_24_HOUR:
            if (currentValue < 1)
            {
              currentValue++;
              lcd.print("YES");
              lcd.setCursor(colPos,rowPos);
            }
            break;
          // set or not position - done
          case DONE:
            if (currentValue < 1)
            {
              currentValue++;
              lcd.print("1");
              lcd.setCursor(colPos,rowPos);
            }
            break;
          // otherwise do nothing
          default:
            break;
        }
      }
    }
 
    // go right on right button,
    // unless we are at the end of the array
    if (rightButton.update())
    {
      if (rightButton.read() == LOW)
      {
        moveRight(SET_12_24_MODE);
      }
    }

    // go left on left button
    // unless we are at the end of the array
    if (leftButton.update())
    {
      if (leftButton.read() == LOW)
      {
        moveLeft(SET_12_24_MODE);
      }
    }

    // if we are on the set position
    // middle button sets if 1 and discards if 0
    if ((centerButton.update()) && setButton(SET_12_24_MODE))
    {
      if (centerButton.read() == LOW)
      {
        if (currentValue == 1)
        {
          is12Hour = menuValues[MODE_12_24_HOUR];
          // write the values to the eeprom
          EEPROM.write(EE_TIME_MODE, is12Hour);
          // turn off the cursor
          lcd.noCursor();
          // exit the set display schedule
          return;       
        }             
        // turn off the cursor
        lcd.noCursor();
        // exit the set display schedule menu
        return;
      }
    } 
    menuValues[colPos] = currentValue;   
  }
}


//---------------------------------------------------------------------------------------------//
// function setMenu
// displays the choices for setting the clock
//---------------------------------------------------------------------------------------------//
void setMenu()
{
  byte menuNum = 0;
  
  // turn on the lcd
  lcd.display();

  // clear the lcd
  lcd.clear();

  // move the LCD cursor to home
  lcd.home();

  // default to time and date menu
  strcpy_P(currentString, (char*)pgm_read_word(&(menuStrSet[menuNum])));

  // print the menu to the LCD
  lcd.print(currentString);


  // loop until exit
  while (1)
  {
    // down button goes to previous menu unless already there
    if (downButton.update() && (menuNum > 0))
    {
      if (downButton.read() == LOW)
      {
        menuNum--;
        strcpy_P(currentString, (char*)pgm_read_word(&(menuStrSet[menuNum])));
        lcd.clear();
        lcd.home();
        lcd.print(currentString);
      }
    }
    // up button goes to next menu unless already at last one
    if (upButton.update() && (menuNum < NUM_MENUS))
    {
      if (upButton.read() == LOW)
      {
        menuNum++;
        strcpy_P(currentString, (char*)pgm_read_word(&(menuStrSet[menuNum])));
        lcd.clear();
        lcd.home();
        lcd.print(currentString);
      }
    }
    // center button selects the current menu choice
    if (centerButton.update())
    {
      if (centerButton.read() == LOW)
      {
        switch(menuNum)
        {
          // set time and date menu
          // 
        case TIME_DATE:
          setTimeDate();
          lcd.clear();
          return;
          break;
        case TZ_LONG_LAT:
          setTzLongLat();
          lcd.clear();
          return;
          break;
        case DST_START_END:
          setDstStartEnd();
          lcd.clear();
          return;
          break;
        case DISP_SCHED:
          setDispSched();
          lcd.clear();
          return;
          break;
        case SET_12_24_MODE:
          set1224Mode();
          lcd.clear();
          return;
          break;
        case SET_DEFAULTS:
          setDefaults();
          lcd.clear();
          return;
          break;
          // exit setting mode
        case 6:
          lcd.clear();
          return;
          break;
        }
      }
    } 
  } 
}

//---------------------------------------------------------------------------------------------//
// function rtcSetTimeDate
// sets the time and date on the DS3231 RTC
// uses the global menuValues for index TIME_DATE to re-assemble each digit into the data
//---------------------------------------------------------------------------------------------//
void rtcSetTimeDate()
{ 
  // array offsets
  // 0 second
  // 1 minute
  // 2 hour
  // 3 date
  // 4 month
  // 5 year
  
  // reassemble the values
  theTime[2] = 10 * menuValues[TENS_HOURS] + menuValues[ONES_HOURS];
  theTime[1] = 10 * menuValues[TENS_MINUTES] + menuValues[ONES_MINUTES];
  theTime[0] = 10 * menuValues[TENS_SECONDS] + menuValues[ONES_SECONDS];
  theTime[4] = 10 * menuValues[TENS_MONTH] + menuValues[ONES_MONTH];
  theTime[3] = 10 * menuValues[TENS_DATE] + menuValues[ONES_DATE];
  theTime[5] = 10 * menuValues[TENS_YEAR] + menuValues[ONES_YEAR];
  day = menuValues[THE_DAY];

  // send the values to the RTC
  setDate();
  setTime();
  
  // set the sunRise array
  sunRise[0] = 0;
  sunRise[1] = 0;
  sunRise[2] = 0;
  sunRise[3] = theTime[3];
  sunRise[4] = theTime[4];
  sunRise[5] = theTime[5];
 
  // call the SunRise method
  sunWillRise = myLord.SunRise(sunRise);
  
  // set the sunSet array
  sunSet[0] = 0;
  sunSet[1] = 0;
  sunSet[2] = 0;
  sunSet[3] = theTime[3];
  sunSet[4] = theTime[4];
  sunSet[5] = theTime[5];
  
  // call the sunSet method
  myLord.SunSet(sunSet);
  
  // call the MoonPhase method
  moonPhase = myLord.MoonPhase(sunRise);
  
  // calculate noon from sunrise and sunset times
  calculateNoon();
  
  // convert time to dst if enabled
  if (dstEnable)
  {
    myLord.DST(theTime);
    myLord.DST(sunRise);
    myLord.DST(sunSet);
    myLord.DST(theNoon);
  }
}

//---------------------------------------------------------------------------------------------//
// function setDate
// sets the date on the DS3231 RTC
// uses the globals day date month year
//---------------------------------------------------------------------------------------------//
void setDate()
{
  // array offsets
  // 0 second
  // 1 minute
  // 2 hour
  // 3 date
  // 4 month
  // 5 year
  
  Wire.beginTransmission(DS3231_I2C_ADDRESS);
  Wire.write(3);
  Wire.write(decToBcd(day));
  Wire.write(decToBcd(theTime[3]));
  Wire.write(decToBcd(theTime[4]));
  Wire.write(decToBcd(theTime[5]));
  Wire.endTransmission();
}

//---------------------------------------------------------------------------------------------//
// function getDate
// gets the date from the DS3231 RTC
// uses the globals day date month year
//---------------------------------------------------------------------------------------------//
void getDate()
{
  // array offsets
  // 0 second
  // 1 minute
  // 2 hour
  // 3 date
  // 4 month
  // 5 year
  
  Wire.beginTransmission(DS3231_I2C_ADDRESS);
  Wire.write(3); //set register to 3 (day)
  Wire.endTransmission();
  Wire.requestFrom(DS3231_I2C_ADDRESS, 4); //get 5 bytes (day,date,month,year,control)
  while(Wire.available())
  {
    day = bcdToDec(Wire.read());
    theTime[3] = bcdToDec(Wire.read());
    theTime[4] = bcdToDec(Wire.read());
    theTime[5] = bcdToDec(Wire.read());
  }
}

//---------------------------------------------------------------------------------------------//
// function setTime
// sets the time on the DS3231 RTC
// uses the global theTime[]
//---------------------------------------------------------------------------------------------//
void setTime()
{
  // array offsets
  // 0 second
  // 1 minute
  // 2 hour
  // 3 date
  // 4 month
  // 5 year
  
  Wire.beginTransmission(DS3231_I2C_ADDRESS);
  Wire.write((byte)0x0);
  Wire.write(decToBcd(theTime[0]));
  Wire.write(decToBcd(theTime[1]));
  Wire.write(decToBcd(theTime[2]));
  Wire.endTransmission();
}

//---------------------------------------------------------------------------------------------//
// function getTime
// gets the time from the DS3231 RTC
// uses the global theTime[]
//---------------------------------------------------------------------------------------------//
void getTime()
{
  // array offsets
  // 0 second
  // 1 minute
  // 2 hour
  // 3 date
  // 4 month
  // 5 year
  
  Wire.beginTransmission(DS3231_I2C_ADDRESS);
  Wire.write((byte)0x0); //set register to 0
  Wire.endTransmission();
  Wire.requestFrom(DS3231_I2C_ADDRESS, 3); //get 3 bytes (seconds, minutes, hours)
  while(Wire.available())
  {
    theTime[0] = bcdToDec(Wire.read() & 0x7f);
    theTime[1] = bcdToDec(Wire.read());
    theTime[2] = bcdToDec(Wire.read() & 0x3f);
  }
}

//---------------------------------------------------------------------------------------------//
// function displayTimeAndDate
// displays the time and date on the first line of the LCD
// uses the globals theTime[]
//---------------------------------------------------------------------------------------------//
void displayTimeAndDate()
{
  char buf[12];
  
  byte tmpMin,
       tmpHr;

  // get the time
  getTime();
  // get the date
  getDate();
  // compute the day of week
  day = myLord.DayOfWeek(theTime);
  
  // convert time to dst if enabled
  if (dstEnable)
  {
    myLord.DST(theTime);
  }

  // start at upper left
  lcd.setCursor(0, 0);
  
  // array offsets
  // 0 second
  // 1 minute
  // 2 hour
  // 3 date
  // 4 month
  // 5 year
  
  // test for display mode change time
  // if you set them all the same you should stay in bright mode
  // off
  if ((theTime[1] == 0) && (theTime[0] == 0) && (theTime[2] == offHour))
  {
    lcd.noDisplay();
  }  
  // dim
  if ((theTime[1] == 0) && (theTime[0] == 0) && (theTime[2] == dimHour))
  {
    lcd.display();
    lcd.vfdDim(3);
  }
  // bright
  if ((theTime[1] == 0) && (theTime[0] == 0) && (theTime[2] == brightHour))
  {
    // turn on the display
    lcd.display();
    lcd.vfdDim(0);
  }
  
  tmpHr = theTime[2];
  // convert to 12 hour time if set
  if (is12Hour)
  {
    // convert 0 to 12
    if (tmpHr == 0)
    {
      tmpHr = 12;
    }
    // if greater than 12 subtract 12
    if (tmpHr > 12)
    {
      tmpHr = tmpHr - 12;
    }
  }
  
  // print the hour
  // pad with a zero if less than ten hours 
  // and 12 hour time is not set 
  if ((tmpHr < 10) && !(is12Hour))
  {
    lcd.print("0");
  }
  // if 12 hour time is set pad with space
  // if the tens hour is less than one
  if ((tmpHr < 10) && (is12Hour))
  {
    lcd.print(" ");
  }
    
  lcd.print(itoa(tmpHr, buf, 10));
  lcd.print(":");

  // print the minutes
  // pad with a zero if less than ten minutes
  if (theTime[1] < 10)
  {
    lcd.print("0");
  }
  lcd.print(itoa(theTime[1], buf, 10));
  lcd.print(":");

  // print the seconds
  // pad with a zero if less than ten seconds
  if (theTime[0] < 10)
  {
    lcd.print("0");
  }
  lcd.print(itoa(theTime[0], buf, 10));

  lcd.setCursor(9, 0);

  // print the day of the week
  switch (day) {
  case 1:
    lcd.print("Su");
    break;
  case 2:
    lcd.print("Mo");
    break;
  case 3:
    lcd.print("Tu");
    break;
  case 4:
    lcd.print("We");
    break;
  case 5:
    lcd.print("Th");
    break;
  case 6:
    lcd.print("Fr");
    break;
  case 7:
    lcd.print("Sa");
    break;
  }

  lcd.setCursor(12,0);

  // print the month
  // pad with a zero if less than ten
  if (theTime[4] < 10)
  {
    lcd.print("0");
  }  
  lcd.print(itoa(theTime[4], buf, 10));
  lcd.print("/");

  // print the date
  // pad with a zero if less than ten
  if (theTime[3] < 10)
  {
    lcd.print("0");
  }
  lcd.print(itoa(theTime[3], buf, 10)); 

  // I decided not to have the year displayed
  // uncomment below if you want it

  //lcd.print("/"); 

  // print the year
  // pad with a zero if less than ten
  //  if (year < 10)
  //  {
  //    lcd.print("0");
  //  }
  //  lcd.print(itoa(year, buf, 10));
  //  Serial.println("Done displaying time");
  
  // move the cursor to the bottom line left side
  lcd.setCursor(0,1);
  
  // update the solar values
  updateSolar();
  
  // print the sunrise, sunset and noon if the sun rises
  if (sunWillRise)
  {
    // sunrise up triangle
    // theTime[2]
    lcd.write(31);
    tmpHr = sunRise[2];
    
    // convert to 12 hour time if set
    if (is12Hour)
    {
      // convert 0 to 12
      if (tmpHr == 0)
      {
        tmpHr = 12;
      }
      // if greater than 12 subtract 12
      if (tmpHr > 12)
      {
        tmpHr = tmpHr - 12;
      }
    }
    
    if ((tmpHr < 10) && !(is12Hour))
    {
      lcd.print('0');
    }
    if ((tmpHr < 10) && (is12Hour))
    {
      lcd.print(" ");
    }
    
    lcd.print(tmpHr, DEC);
    // minutes
    tmpMin = sunRise[1];
    if (tmpMin < 10)
    {
      lcd.print('0');
    }
    lcd.print(tmpMin, DEC);
    
    // space
    lcd.write(' ');
    
    // sunset down triangle
    // theTime[2]
    lcd.write(28);
    tmpHr = sunSet[2];
    
    // convert to 12 hour time if set
    if (is12Hour)
    {
      // convert 0 to 12
      if (tmpHr == 0)
      {
        tmpHr = 12;
      }
      // if greater than 12 subtract 12
      if (tmpHr > 12)
      {
        tmpHr = tmpHr - 12;
      }
    }
    
    if ((tmpHr < 10) && !(is12Hour))
    {
      lcd.print('0');
    }
    if ((tmpHr < 10) && (is12Hour))
    {
      lcd.print(" ");
    }
    
    lcd.print(tmpHr, DEC);
    // minutes
    tmpMin = sunSet[1];
    if (tmpMin < 10)
    {
      lcd.print('0');
    }
    lcd.print(tmpMin, DEC);
    
    // space
    lcd.print(' ');
    
    // noon
    lcd.write(148);
   
    tmpHr = theNoon[2];
    
    // convert to 12 hour time if set
    if (is12Hour)
    {
      // convert 0 to 12
      if (tmpHr == 0)
      {
        tmpHr = 12;
      }
      // if greater than 12 subtract 12
      if (tmpHr > 12)
      {
        tmpHr = tmpHr - 12;
      }
    }
    
    if ((tmpHr < 10) && !(is12Hour))
    {
      lcd.print('0');
    }
    if ((tmpHr < 10) && (is12Hour))
    {
      lcd.print(" ");
    }
    
    lcd.print(tmpHr, DEC);
    
    // minutes
    if (theNoon[1] < 10)
    {
      lcd.print('0');
    }
    lcd.print(theNoon[1], DEC);    
  }
  // otherwise signify the sun is not rising
  else
  {
    lcd.write(31);
    lcd.print("--:-- ");
    lcd.write(28);
    lcd.print("--:-- ");
    lcd.write(148);
    lcd.print("--:-- ");
  }
  
  // space
  lcd.print(' ');
  
  // moon phase
  if (moonPhase == 0)
  {
    lcd.write(149);
  }
  if ((moonPhase < 0.15) && (moonPhase > 0))
  {
    lcd.write(24);
  }
  if ((moonPhase < 0.25) && (moonPhase >= 0.15))
  {
    lcd.write(23);
  }
  if ((moonPhase < 0.35) && (moonPhase >= 0.25))
  {
    lcd.write(22);
  }
  if ((moonPhase < 0.45) && (moonPhase >= 0.35))
  {
    lcd.write(21);
  }
  if ((moonPhase < 0.55) && (moonPhase >= 0.45))
  {
    lcd.write(20);
  }
  if ((moonPhase < 0.65) && (moonPhase >= 0.55))
  {
    lcd.write(19);
  }
  if ((moonPhase < 0.75) && (moonPhase >= 0.65))
  {
    lcd.write(18);
  }
  if ((moonPhase < 0.85) && (moonPhase >= 0.75))
  {
    lcd.write(17);
  }
  if ((moonPhase < 0.95) && (moonPhase >= 0.85))
  {
    lcd.write(16);
  }
  if (moonPhase >= 0.95)
  {
    lcd.write(149);
  }
}

//---------------------------------------------------------------------------------------------//
// function displayBigTimeAndDate
// displays the time and date in big 2x3 block characters on the LCD
// uses the globals theTime[]
//---------------------------------------------------------------------------------------------//
void displayBigTimeAndDate()
{
  // digits 0-3 of the display in big numbers, left to right
  byte digit;
  // column loop counter
  byte col;
  // array to hold hours and minutes as digits
  byte hm[4];
  // to hold the time for 12 hour
  byte tmpHr;

  char buf[12];

  // get the time
  getTime();
  // get the date
  getDate();

  // convert time to dst if enabled
  if (dstEnable)
  {
    myLord.DST(theTime);
  }
  
  // array offsets
  // 0 second
  // 1 minute
  // 2 hour
  // 3 date
  // 4 month
  // 5 year
  
  tmpHr = theTime[2];
  // convert to 12 hour time if set
  if (is12Hour)
  {
    // convert 0 to 12
    if (tmpHr == 0)
    {
      tmpHr = 12;
    }
    // if greater than 12 subtract 12
    if (tmpHr > 12)
    {
      tmpHr = tmpHr - 12;
    }
  }
  
  // tens hours
  hm[0] = tmpHr / 10;
  // ones hours
  hm[1] = tmpHr % 10;
  // tens minutes
  hm[2] = theTime[1] / 10;
  // ones minutes
  hm[3] = theTime[1] % 10;
  
  // test for display mode change time
  // if all are set the same it should stay bright
  // off
  if ((theTime[1] == 0) && (theTime[0] == 0) && (theTime[2] == offHour))
  {
    lcd.noDisplay();
  }
  // dim
  if ((theTime[1] == 0) && (theTime[0] == 0) && (theTime[2] == dimHour))
  {
    lcd.display();
    lcd.vfdDim(3);
  } 
  // bright
  if ((theTime[1] == 0) && (theTime[0] == 0) && (theTime[2] == brightHour))
  {
    // turn on the display
    lcd.display();
    lcd.vfdDim(1);
  }
  
  // update the solar data
  updateSolar();
  
  // start at upper left
  lcd.setCursor(0, 0);

  // display 4 digits of time
  for (digit = 0; digit < 4; digit++)
  {
    for (col = 0; col < 3; col++)
    { 
      // 12 hour mode
      if (is12Hour)
      {
        // top row
        lcd.setCursor((col + digit * 4), 0);
        // four spaces if the first digit is 0
        if ((digit == 0) && (hm[0] == 0))
        {
          lcd.write(' ');
        }
        // otherwise print the custom characters for the digit
        else
        {
          lcd.write(bn12[col + hm[digit] * 4]);
        }
        // bottom row
        lcd.setCursor((col + digit * 4), 1);
        // four spaces if the first digit is 0
        if ((digit == 0) && (hm[0] == 0))
        {
          lcd.write(' ');
        }
        else
        {
          lcd.write(bn22[col + hm[digit] * 4]);
        }
      }
      if (!(is12Hour))
      {
        // top row
        lcd.setCursor((col + digit * 4), 0);
        lcd.write(bn12[col + hm[digit] * 4]);
        // bottom row
        lcd.setCursor((col + digit * 4), 1);
        lcd.write(bn22[col + hm[digit] * 4]);
      }
    }
  }

  // display colons
  lcd.setCursor(7, 0);
  lcd.write(165);
  lcd.setCursor(7, 1);
  lcd.write(165);

  // print the seconds
  // pad with a zero if less than ten theTime[0]
  lcd.setCursor(16,0);
  if (theTime[0] < 10)
  {
    lcd.print("0");
  }
  lcd.print(itoa(theTime[0], buf, 10));

  // print the month
  // pad with a zero if less than ten
  lcd.setCursor(16, 1);
  if (theTime[4] < 10)
  {
    lcd.print("0");
  }  
  lcd.print(itoa(theTime[4], buf, 10));

  // print the date
  // pad with a zero if less than ten
  if (theTime[3] < 10)
  {
    lcd.print("0");
  }
  lcd.print(itoa(theTime[3], buf, 10));
}

//---------------------------------------------------------------------------------------------//
// function decToBcd
// Convert normal decimal numbers to binary coded decimal
//---------------------------------------------------------------------------------------------//
byte decToBcd(byte val)
{
  return ( (val/10*16) + (val%10) );
}

//---------------------------------------------------------------------------------------------//
// function bdctoDec
// Convert binary coded decimal to normal decimal numbers
//---------------------------------------------------------------------------------------------//
byte bcdToDec(byte val)
{
  return ( (val/16*10) + (val%16) );
}

//---------------------------------------------------------------------------------------------//
// function SQWEnable
// enables output on the SQW pin
//---------------------------------------------------------------------------------------------//
void SQWEnable()
{
  Wire.beginTransmission(DS3231_I2C_ADDRESS);
  Wire.write(0x0e);
  Wire.write((byte)0x0);
  Wire.endTransmission();
}

//---------------------------------------------------------------------------------------------//
// function lcdCustomChars
// defines the character blocks for the big font
//---------------------------------------------------------------------------------------------//
void lcdCustomChars()
{
  // big number data
  // the lowest line for the LR and LL characters does not
  // work correctly on my Noritake VFD as they are hardwired to be a cursor line
  // this should probably be moved to the flash via PROGMEM
  byte LT[8] =
  {
    B00111,
    B01111,
    B11111,
    B11111,
    B11111,
    B11111,
    B11111,
    B11111
  };
  byte UB[8] =
  {
    B11111,
    B11111,
    B11111,
    B00000,
    B00000,
    B00000,
    B00000,
    B00000
  };
  byte RT[8] =
  {
    B11100,
    B11110,
    B11111,
    B11111,
    B11111,
    B11111,
    B11111,
    B11111
  };
  byte LL[8] =
  {
    B11111,
    B11111,
    B11111,
    B11111,
    B11111,
    B11111,
    B01111,
    B00111
  };
  byte LB[8] =
  {
    B00000,
    B00000,
    B00000,
    B00000,
    B00000,
    B11111,
    B11111,
    B11111
  };
  byte LR[8] =
  {
    B11111,
    B11111,
    B11111,
    B11111,
    B11111,
    B11111,
    B11110,
    B00000
  };
  byte MB[8] =
  {
    B11111,
    B11111,
    B11111,
    B00000,
    B00000,
    B00000,
    B11111,
    B11111
  };
  byte block[8] =
  {
    B11111,
    B11111,
    B11111,
    B11111,
    B11111,
    B11111,
    B11111,
    B11111
  };

  lcd.createChar(0,LT);
  lcd.createChar(1,UB);
  lcd.createChar(2,RT);
  lcd.createChar(3,LL);
  lcd.createChar(4,LB);
  lcd.createChar(5,LR);
  lcd.createChar(6,MB);
  lcd.createChar(7,block);

//  // counter
//  int cCount = 0;
//
//  // string to hold custom char data from flash
//  char theCustChar[8];
//  
//  // use PROGMEM
//  prog_char custChar0[] PROGMEM = 
//  {
//    B00111,
//    B01111,
//    B11111,
//    B11111,
//    B11111,
//    B11111,
//    B11111,
//    B11111
//  };
//  prog_char custChar1[] PROGMEM =
//  {
//    B11111,
//    B11111,
//    B11111,
//    B00000,
//    B00000,
//    B00000,
//    B00000,
//    B00000
//  };
//  prog_char custChar2[] PROGMEM =
//  {
//    B11100,
//    B11110,
//    B11111,
//    B11111,
//    B11111,
//    B11111,
//    B11111,
//    B11111
//  };
//  prog_char custChar3[] PROGMEM =
//  {
//    B11111,
//    B11111,
//    B11111,
//    B11111,
//    B11111,
//    B11111,
//    B01111,
//    B00111
//  };
//  prog_char custChar4[] PROGMEM =
//  {
//    B00000,
//    B00000,
//    B00000,
//    B00000,
//    B00000,
//    B11111,
//    B11111,
//    B11111
//  };
//  prog_char custChar5[] PROGMEM =
//  {
//    B11111,
//    B11111,
//    B11111,
//    B11111,
//    B11111,
//    B11111,
//    B11110,
//    B00000
//  };
//  prog_char custChar6[] PROGMEM =
//  {
//    B11111,
//    B11111,
//    B11111,
//    B00000,
//    B00000,
//    B00000,
//    B11111,
//    B11111
//  };
//  prog_char custChar7[] PROGMEM =
//  {
//    B11111,
//    B11111,
//    B11111,
//    B11111,
//    B11111,
//    B11111,
//    B11111,
//    B11111
//  };
//  
//  // an array of menu strings stored in FLASH
//  PROGMEM  const char *custCharSet[] = {
//    custChar0,
//    custChar1,
//    custChar2,
//    custChar3,
//    custChar4,
//    custChar5,
//    custChar6,
//    custChar7
//  };
//  
//  // read the strings out of flash and into the display custom character RAM
//  for (cCount = 0; cCount > 8; cCount++);
//  { 
//    strcpy_P(theCustChar, (char*)pgm_read_word(&(custCharSet[cCount])));
//    lcd.createChar(cCount,theCustChar);
//  }

}

//---------------------------------------------------------------------------------------------//
// function checkHourValue
// checks menuValues[] for hours recomibined are less than 24
// returns 1 if less than, 0 otherwise
//---------------------------------------------------------------------------------------------//
byte checkHourValue()
{ 
  // test the value mask first
  if (currentValue < valueMask[TIME_DATE][colPos])
  {
    // if we add one to the tens will it exceed 24?
    if ((colPos == TENS_HOURS) && ((10 * (1 + menuValues[TENS_HOURS]) + menuValues[ONES_HOURS]) < 24))
    {
      return 1;
    }
    // if we add one to the ones will it exceed 24?
    else if ((colPos == ONES_HOURS) && ((10 * menuValues[TENS_HOURS] + menuValues[ONES_HOURS] + 1) < 24))
    {
      return 1;
    }
    else
    {
      return 0;
    }
  }
  else
  {
    return 0;
  }
}

//---------------------------------------------------------------------------------------------//
// function checkMinuteValue
// checks menuValues[] for minutes recomibined are less than 59
// returns 1 if less than, 0 otherwise
//---------------------------------------------------------------------------------------------//
byte checkMinuteValue()
{
  // test the value mask first
  if (currentValue < valueMask[TIME_DATE][colPos])
  {
    // if we add one to the tens will it exceed 60?
    if ((colPos == TENS_MINUTES) && ((10 * (1 + menuValues[TENS_MINUTES]) + menuValues[ONES_MINUTES]) < 60))
    {
      return 1;
    }
    // if we add one to the ones will it exceed 60?
    else if ((colPos == ONES_MINUTES) && ((10 * menuValues[TENS_MINUTES] + menuValues[ONES_MINUTES] + 1) < 60))
    {
      return 1;
    }
    else
    {
      return 0;
    }
  }
  else
  {
    return 0;
  }
}

//---------------------------------------------------------------------------------------------//
// function checkSecondValue
// checks menuValues[] for seconds recomibined are less than 59
// returns 1 if less than, 0 otherwise
//---------------------------------------------------------------------------------------------//
byte checkSecondValue()
{
  // test the value mask first
  if (currentValue < valueMask[TIME_DATE][colPos])
  {
    // if we add one to the tens will it exceed 59?
    if ((colPos == TENS_SECONDS) && ((10 * (1 + menuValues[TENS_SECONDS]) + menuValues[ONES_SECONDS]) < 60))
    {
      return 1;
    }
    // if we add one to the ones will it exceed 59?
    else if ((colPos == ONES_SECONDS) && ((10 * menuValues[TENS_SECONDS] + menuValues[ONES_SECONDS] + 1) < 60))
    {
      return 1;
    }
    else
    {
      return 0;
    }
  }
  else
  {
    return 0;
  }
}

//---------------------------------------------------------------------------------------------//
// function checkMonthValue
// checks menuValues[] for months recomibined are less than 12
// returns 1 if less than, 0 otherwise
//---------------------------------------------------------------------------------------------//
byte checkMonthValue()
{
  // test the value mask first
  if (currentValue < valueMask[TIME_DATE][colPos])
  {
    // if we add one to the tens will it exceed 12?
    if ((colPos == TENS_MONTH) && ((10 * (1 + menuValues[TENS_MONTH]) + menuValues[ONES_MONTH]) < 13))
    {
      return 1;
    }
    // if we add one to the ones will it exceed 12?
    else if ((colPos == ONES_MONTH) && ((10 * menuValues[TENS_MONTH] + menuValues[ONES_MONTH] + 1) < 13))
    {
      return 1;
    }
    else
    {
      return 0;
    }
  }
  else
  {
    return 0;
  }
}

//---------------------------------------------------------------------------------------------//
// function checkDayValue
// checks menuValues[] for days recomibined are less than 31
// returns 1 if less than, 0 otherwise
//---------------------------------------------------------------------------------------------//
byte checkDayValue()
{
  // test the value mask first
  if (currentValue < valueMask[TIME_DATE][colPos])
  {
    // note
    // if the user wants to set February 31 they can do it
    
    // if we add one to the tens will it exceed 31?
    if ((colPos == TENS_DATE) && ((10 * (1 + menuValues[TENS_DATE]) + menuValues[ONES_DATE]) < 32))
    {
      return 1;
    }
    // if we add one to the ones will it exceed 31?
    else if ((colPos == ONES_DATE) && ((10 * menuValues[TENS_DATE] + menuValues[ONES_DATE] + 1) < 32))
    {
      return 1;
    }
    else
    {
      return 0;
    }
  }
  else
  {
    return 0;
  }
}

//---------------------------------------------------------------------------------------------//
// function checkYearValue
// checks menuValues[] for years recomibined are less than 99
// returns 1 if less than, 0 otherwise
//---------------------------------------------------------------------------------------------//
byte checkYearValue()
{
  // test the value mask first
  if (currentValue < valueMask[TIME_DATE][colPos])
  {
    // both digits can be 9 so the test is much easier
    // just see if it combines to less than 99
    if ((10 * menuValues[TENS_YEAR] + menuValues[ONES_YEAR]) < 99)
    {
      return 1;
    }
    else
    {
      return 0;
    }
  }
  else
  {
    return 0;
  }
}

//---------------------------------------------------------------------------------------------//
// function checkTzValue
// checks menuValues[] recomibined are less than 12
// I think this is easier than figuring out the logic of how each digit interacts
// when checking more than two digits
// we don't care about the sign
// returns 1 if less than, 0 otherwise
//---------------------------------------------------------------------------------------------//
byte checkTzValue()
{
  int currTz;

  currTz = (menuValues[TZ_TENS] * 10) + menuValues[TZ_ONES];

  if (currTz < 12)
  {
    return 1;
  }
  else
  {
    return 0;
  }
}

//---------------------------------------------------------------------------------------------//
// function checkLongValue
// checks menuValues[] recomibined are less than 18000
// longitude is stored as a two byte int, not a float
// we don't care about the sign
// returns 1 if less than, 0 otherwise
//---------------------------------------------------------------------------------------------//
byte checkLongValue()
{
  int currLong;

  currLong = (menuValues[LONG_HUND] * 10000) + (menuValues[LONG_TENS] * 1000) + (menuValues[LONG_ONES] * 100) + (menuValues[LONG_TENTHS] * 10) + menuValues[LONG_HUNDRTHS];

  if (currLong < 18000)
  {
    return 1;
  }
  else
  {
    return 0;
  }
}
//---------------------------------------------------------------------------------------------//
// function checkLatValue
// checks menuValues[] recomibined are less than 9000
// latitude is stored as a two byte int, not a float
// we don't care about the sign
// returns 1 if less than, 0 otherwise
//---------------------------------------------------------------------------------------------//
byte checkLatValue()
{
  int currLat;

  currLat = (menuValues[LAT_TENS] * 1000) + (menuValues[LAT_ONES] * 100) + (menuValues[LAT_TENTHS] * 10) + menuValues[LAT_HUNDRTHS];
  
  if (currLat < 9000)
  {
    return 1;
  }
  else
  {
    return 0;
  }
}

//---------------------------------------------------------------------------------------------//
// function setDefaults
// sets default values for stored settings in the eeprom
//---------------------------------------------------------------------------------------------//
void setDefaults()
{
  int defLat = 3587;
  int defLong = -7878;
  
  // timezone and dst info
  EEPROM.write(EE_TIME_ZONE, -5);
  EEPROM.write(EE_DST_MON_START, 3);
  EEPROM.write(EE_DST_DOW_START, 1);
  EEPROM.write(EE_DST_WEEK_START, 1);
  EEPROM.write(EE_DST_MON_END, 10);
  EEPROM.write(EE_DST_DOW_END, 1);
  EEPROM.write(EE_DST_WEEK_END, 1);
  EEPROM.write(EE_DST_CHANGE_HOUR, 2);
  EEPROM.write(EE_DST_ENABLE, 0);
  
      
  // set regular display mode
  EEPROM.write(EE_BIG_MODE, 0);
  bigMode = 0;
  
  // set the latitude
  bu = highByte(defLat);
  bl = lowByte(defLat);  
  EEPROM.write(EE_LAT_U, bu);
  EEPROM.write(EE_LAT_L, bl);
  // set the longitude
  bu = highByte(defLong);
  bl = lowByte(defLong);
  EEPROM.write(EE_LONG_U, bu);
  EEPROM.write(EE_LONG_L, bl);
  // set the bright hour
  EEPROM.write(EE_BRIGHT_HR, 6);
  // set the dim hour
  EEPROM.write(EE_DIM_HR, 21);
  // set the off hour
  EEPROM.write(EE_OFF_HR, 1);

  // automatic dst changeover flag
  EEPROM.write(EE_DST_ENABLE, 1);
  
  // set 24-hour mode
  EEPROM.write(EE_TIME_MODE, 0);
  
  lcd.clear();
  lcd.home();
  lcd.print("DEFAULTS SET");
  delay(2000);
  lcd.clear();
}

//---------------------------------------------------------------------------------------------//
// function calculateNoon
// calculates noon from sunRise and sunSet
//---------------------------------------------------------------------------------------------//
void calculateNoon()
{
  int setMin,
      riseMin,
      aMin;
      
  // sunset time in minutes from 0000
  setMin = sunSet[2] * 60 + sunSet[1];
 
  // sunrise time in minutes from 0000
  riseMin = sunRise[2] * 60 + sunRise[1];
 
  // take the average between sunrise and sunset
  aMin = (setMin + riseMin) / 2;
  
  theNoon[2] = aMin / 60;
  theNoon[1] = aMin % 60;
  
  // fill in the date from sunRise
  theNoon[3] = sunRise[3];
  theNoon[4] = sunRise[4];
  theNoon[5] = sunRise[5];
  
}


//---------------------------------------------------------------------------------------------//
// function intro()
// displays version info
//---------------------------------------------------------------------------------------------//
 void intro()
{ 
  lcd.clear();
  lcd.home();
  // default to time and date menu
  strcpy_P(currentString, (char*)pgm_read_word(&(menuStrSet[7])));
  lcd.print(currentString);
  // move the cursor to the bottom line left side
  lcd.setCursor(0,1);
  strcpy_P(currentString, (char*)pgm_read_word(&(menuStrSet[8])));
  lcd.print(currentString);
  delay(3000);
  lcd.clear();
}

//---------------------------------------------------------------------------------------------//
// function updateSolar()
// updates solar information
//---------------------------------------------------------------------------------------------//
void updateSolar()
{
  // update the sunrise, noon, sunset and moon twice daily
  //if (((theTime[1] == 0) && (theTime[0] == 0)) && ((theTime[2] == 17) || (theTime[2] == 4)))
  if ((theTime[1] == 0) && (theTime[0] == 0) && (theTime[2] == 0))
  {
    // set the sunRise array
    sunRise[0] = 0;
    sunRise[1] = 0;
    sunRise[2] = 0;
    sunRise[3] = theTime[3];
    sunRise[4] = theTime[4];
    sunRise[5] = theTime[5];
    
    // call the SunRise method and get the return result
    // so we can tell if the sun actually rises
    sunWillRise = myLord.SunRise(sunRise);
    
    // set the sunSet array
    sunSet[0] = 0;
    sunSet[1] = 0;
    sunSet[2] = 0;
    sunSet[3] = theTime[3];
    sunSet[4] = theTime[4];
    sunSet[5] = theTime[5];
    
    // call the sunSet method
    myLord.SunSet(sunSet);
    
    // call the MoonPhase method
    moonPhase = myLord.MoonPhase(sunRise);
    
    // calculate noon
    calculateNoon();
    
    // convert time to dst if enabled
    if (dstEnable)
    {
      myLord.DST(sunRise);
      myLord.DST(sunSet);
      myLord.DST(theNoon);
    }
  }
}
