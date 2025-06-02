#include <IRremote.hpp>

#include <EEPROM.h>
#include <Ds1302.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <Bonezegei_DHT11.h>
#include <Adafruit_Sensor.h>  // include Adafruit sensor library
#include <Adafruit_BMP280.h>

#define BMP_CS 10

//Adafruit_BMP280 bmp(BMP_CS); // hardware SPI

#define IR_RECEIVE_PIN 2
enum DisplayMode {
  DISPLAY_MODE_TIME_DATE,    //0
  DISPLAY_MODE_TEMPERATURE,  //1
  DISPLAY_MODE_HUMIDITY,     //2
  DISPLAY_MODE_PRESSURE,     //3
  DISPLAY_MODE_ALTITUDE,     //4
};

enum TempMode {
  TEMP_CELCIUS,
  TEMP_FAHRENHEIT
};

enum DateTimeMode {
  DATE_TIME,
  DATE,
  TIME
};

struct Memory {
  DisplayMode field1;
  TempMode field2;
  DateTimeMode field3;
};

Bonezegei_DHT11 dht(A0);
LiquidCrystal_I2C lcd(0x20, 16, 2);
Adafruit_BMP280 bmp280(BMP_CS);
Ds1302 rtc(5, 3, 4);

int number[11] = { 15, 82, 80, 16, 86, 84, 20, 78, 76, 12, 15 };

int firstRowType = DISPLAY_MODE_TIME_DATE;
int tempType = TEMP_CELCIUS;
int DateTimeType = DATE_TIME;
bool scrollDirection = true;  
bool isPaused = false;        
int eeAddress = 0;
bool isCalculatorMode = false;

int calculatorValue = 0;     
String calculatorInput = ""; 
char lastOperation = '\0';   
int offset = 0;

void setup() {
  Serial.begin(9600);
  while (!Serial) {
    ;  // wait for serial port to connect. Needed for native USB port only
  }
  Memory obj;
  delay(10);
  EEPROM.get(eeAddress, obj);
  Serial.println(sizeof(Memory));
  firstRowType = obj.field1; 
  tempType = obj.field2;
  DateTimeType = obj.field3;

  if (!bmp280.begin()) {

    Serial.println("Could not find a valid BMP280 sensor, check wiring!");
    while (1)
      ;
  }
  lcd.init();
  lcd.backlight();
  IrReceiver.begin(IR_RECEIVE_PIN, ENABLE_LED_FEEDBACK);
  dht.begin();

  rtc.init();

  // test if clock is halted and set a date-time (see example 2) to start it
  if (rtc.isHalted()) {
    Serial.println("RTC is halted. Setting time...");

    Ds1302::DateTime dt = {
      .year = 24,
      .month = Ds1302::MONTH_NOV,
      .day = 25,
      .hour = 22,
      .minute = 27,
      .second = 42,
      .dow = Ds1302::DOW_TUE
    };
    rtc.setDateTime(&dt);
  }
}

void loop() {
  if (IrReceiver.decode()) {
    remoteControl();
    delay(100);
    IrReceiver.resume();
  }

  if (!isPaused && !isCalculatorMode) {
    updateLCD();
  }
}

void updateLCD() {
  delay(300);
  lcd.clear();

  Ds1302::DateTime now;
  rtc.getDateTime(&now);

  dht.getData();
  String date = String(now.day) + "/" + String(now.month) + "/" + String(now.year + 2000);
  String time = (now.hour < 10 ? "0" : "") + String(now.hour) + ":" + (now.minute < 10 ? "0" : "") + String(now.minute) + ":" + (now.second < 10 ? "0" : "") + String(now.second);

  String dateTime;
  if (DateTimeType == DATE_TIME) {
    dateTime = date + " " + time;

  } else if (DateTimeType == DATE) {
    dateTime = date;

  } else {  
    dateTime = time;
  }

  float temp = dht.getTemperature();
  char type = 'C';

  if (tempType != TEMP_CELCIUS) {
    temp = (temp * 9) / 5 + 32;
    type = 'F';

  } else {
    type = 'C';
  }

  float humidity = dht.getHumidity();
  float pressure = bmp280.readPressure();
  float altitude = bmp280.readAltitude(1024);

  String firstRowText = "";
  switch (firstRowType) {
    case DISPLAY_MODE_TIME_DATE:
      firstRowText = dateTime;
      break;
    case DISPLAY_MODE_TEMPERATURE:
      firstRowText = "Temp: " + String(temp) + (char)223 + type;
      break;
    case DISPLAY_MODE_HUMIDITY:
      firstRowText = "Humidity: " + String(humidity) + "%";
      break;
    case DISPLAY_MODE_PRESSURE:
      firstRowText = "Pressure: " + String(pressure) + "hPA";
      break;
    case DISPLAY_MODE_ALTITUDE:
      firstRowText = "Altitude: " + String(altitude) + "m";
      break;
  }

  String secondRowText = "";
  switch ((firstRowType + 1) % 5) {
    case DISPLAY_MODE_TIME_DATE:
      secondRowText = dateTime.substring(offset, 16 + offset);
      break;
    case DISPLAY_MODE_TEMPERATURE:
      secondRowText = "Temp: " + String(temp) + (char)223 + type;
      break;
    case DISPLAY_MODE_HUMIDITY:
      secondRowText = "Humidity: " + String(humidity) + "%";
      break;
    case DISPLAY_MODE_PRESSURE:
      String pressureText = "Pressure: " + String(pressure, 2) + "hPA";
      secondRowText = pressureText.substring(offset, offset + 16);
      break;
    case DISPLAY_MODE_ALTITUDE:
      secondRowText = "Altitude: " + String(altitude, 2) + "m";
      break;
  }

  lcd.setCursor(0, 0);
  lcd.print(firstRowText.substring(offset, offset + 16));
  lcd.setCursor(0, 1);
  lcd.print(secondRowText);
  handleScrolling(firstRowText.length());
}

void handleScrolling(int n) {
  if (n <= 16) {
    offset = 0;  
    return;
  }

  if (scrollDirection) {
    offset++;
    if (offset >= n - 16) {     
      scrollDirection = false; 
      offset = n - 16;
    }

  }
  else {
    offset--;
    if (offset <= 0) {         
      scrollDirection = true;  
      offset = 0;
    }
  }
}

void remoteControl() {
  uint16_t command = IrReceiver.decodedIRData.command;
  Serial.println(command);

  if (isCalculatorMode) {
    handleCalculatorInput(command);
    return;
  }

  switch (command) {
    case 6:  // UP
      if (firstRowType - 1 < 0)
        firstRowType = 4;
      else firstRowType = (firstRowType - 1) % 5;
      break;

    case 22:  // DOWN
      firstRowType = (firstRowType + 1) % 5;
      break;

    case 27:  // LEFT
      if (firstRowType == DISPLAY_MODE_TIME_DATE)
        DateTimeType = (DateTimeType - 1 + 3) % 3;
      else if (firstRowType == DISPLAY_MODE_TEMPERATURE)
        tempType = (tempType - 1 + 2) % 2;
      break;

    case 90:  // RIGHT
      if (firstRowType == DISPLAY_MODE_TIME_DATE)
        DateTimeType = (DateTimeType + 1) % 3;
      else if (firstRowType == DISPLAY_MODE_TEMPERATURE)
        tempType = (tempType + 1) % 2;
      break;

    case 26:  // STOP
      isPaused = !isPaused;
      break;
    case 69:  // MENU
      isCalculatorMode = true;
      calculatorInput = "";
      lastOperation = '\0';
      calculatorValue = 0;

      int value;
      eeAddress += sizeof(Memory);
      EEPROM.get(eeAddress, value);

      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print("Calc Mode: " + String(value));
      lcd.setCursor(0, 1);
      lcd.blink_on();
      offset = 0;
      break;
    default:
      Serial.println("UNDEFINED");
  }
}

void handleCalculatorInput(uint16_t command) {

  for (int i = 0; i <= 10; i++) {
    if (command == number[i]) {     
      calculatorInput += String(i);  
      break;
    }
  }

  if (command == 17) {
    if (!(calculatorInput == NULL) && (calculatorInput[calculatorInput.length() - 1] != '+' && calculatorInput[calculatorInput.length() - 1] != '-')) {
      calculatorInput += "+";  
    }
  }

  if (command == 81) {
    if (!(calculatorInput == NULL) && (calculatorInput[calculatorInput.length() - 1] != '+' && calculatorInput[calculatorInput.length() - 1] != '-')) {
      calculatorInput += "-";  
    }
  }

  if (command == 69) {
    isCalculatorMode = false;
    lcd.clear();
    lcd.blink_off();
    return;
  }

  if (command == 5) {
    int result = executeLastOperation(calculatorInput); 
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Result: ");
    lcd.setCursor(0, 1);
    lcd.print(result);
    calculatorValue = result; 
    calculatorInput = "";
    offset = 0;
    Memory obj = {
      firstRowType,
      tempType,
      DateTimeType
    };

    EEPROM.put(eeAddress, obj);
    eeAddress += sizeof(Memory);
    EEPROM.put(eeAddress, result);
    eeAddress = 0;

    return;
  }

  calculatorScroll(calculatorInput.length());
  lcd.setCursor(0, 1);
  lcd.print(calculatorInput.substring(offset, offset + 16));
}

void calculatorScroll(int n) {
  if (n > 16) {
    if (offset < n - 16) {
      offset++;

    } else {
      offset = 0;
    }

  } else {
    offset = 0;
  }
}

int executeLastOperation(const String &input) {
  int total = 0;
  String currentNumber = "";  
  char lastOperator = '+';

  for (int i = 0; i < input.length(); i++) {
    char c = input[i];

    if (isdigit(c)) {      
      currentNumber += c; 

    } else if (c == '+' || c == '-') {
      if (!(currentNumber == NULL)) {
        int number = currentNumber.toInt(); 
        if (lastOperator == '+') {
          total += number; 

        } else if (lastOperator == '-') {
          total -= number;  
        }
        currentNumber = "";
      }
      lastOperator = c;  
    }
  }

  if (!(currentNumber == NULL)) {
    int number = currentNumber.toInt();
    if (lastOperator == '+') {
      total += number;

    } else if (lastOperator == '-') {
      total -= number;
    }
  }

  return total;  
}
