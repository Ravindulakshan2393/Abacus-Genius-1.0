#include <Keypad.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <DFRobotDFPlayerMini.h>

// === LCD Setup ===
LiquidCrystal_I2C lcd(0x27, 16, 2);

// === Keypad Setup ===
const byte ROWS = 4;
const byte COLS = 4;
char keys[ROWS][COLS] = {
  {'1','2','3','A'},
  {'4','5','6','B'},
  {'7','8','9','C'},
  {'*','0','#','D'}
};
byte rowPins[ROWS] = {23,22,45,46};
byte colPins[COLS] = {47,49,25,48};
Keypad keypad = Keypad(makeKeymap(keys), rowPins, colPins, ROWS, COLS);

// === Encoder Pins ===
#define R1_BIT0 26
#define R1_BIT1 27
#define R1_BIT2 28
#define R1_BIT3 29
#define R2_BIT0 30
#define R2_BIT1 31
#define R2_BIT2 32
#define R2_BIT3 33
#define R3_BIT0 34
#define R3_BIT1 35
#define R3_BIT2 36
#define R3_BIT3 37
#define R4_BIT0 38
#define R4_BIT1 39
#define R4_BIT2 40
#define R4_BIT3 41

// === Touch & PIR ===
#define TOUCH_PIN 43
#define PIR_PIN 42
unsigned long lastMotionTime = 0;
unsigned long lastBeadMotionTime = 0;
const unsigned long DISPLAY_TIMEOUT = 20000; // 20 sec

// === Extra Buttons ===
#define BTN_LIGHT   A11
#define BTN_READNUM A9
#define BTN_PAUSE   A8
#define BTN_MUSIC   A10

// === LED Pattern ===
#define LED_PIN 8
#define VIBRATION_PIN 6  // Pin connected to the coin vibration motor

bool ledOn = false;
bool buttonState = LOW;
int lastButtonState = LOW;
unsigned long lastDebounceTime = 0;
unsigned long buttonPressStart = 0;
bool waitingForLongPress = false;
int pattern = 0;  // 0=steady,1-3=blink speeds
unsigned long previousMillis = 0;
bool ledState = false;
bool correctFeedbackActive = false;
unsigned long correctFeedbackStart = 0;
const unsigned long correctFeedbackDuration = 3000; // 3s
const unsigned long debounceDelay = 50;

// === Music System Variables ===
bool numberSpoken = false;
unsigned long resultTime = 0;
int lastTouchState = LOW;
unsigned long debounceTime2 = 0;
unsigned long lastStableTime = 0;
const unsigned long stableDelay = 1500;
bool isMusicSystemOn = false;
bool musicButtonPreviouslyPressed = false;
unsigned long musicButtonPressTime = 0;
unsigned long musicLastDebounceTime = 0;
bool longPressHandled = false;
int currentTrack = 32;

// === Display Control ===
bool displayOn = true;

// === Modes & Vars ===
enum Mode { MENU_MODE, AUTO_MODE, MANUAL_MODE, SHOW_RESULT };
Mode currentMode = MENU_MODE;
bool manualBeadPhase = false;
bool questionShown = false;
bool answerChecked = false;

int num1,num2;
char operation;
String manualProblem = "";
int beadValue=0, lastBeadValue=-1;

// --- Bead Timing ---
unsigned long lastBeadUpdate=0;

// --- Helpers ---
int readRod(int b0,int b1,int b2,int b3){
  int val=(digitalRead(b3)<<3)|(digitalRead(b2)<<2)|(digitalRead(b1)<<1)|digitalRead(b0);
  return 15-val;
}

int readTotalBeadValue(){
  int unit = constrain(readRod(R1_BIT0, R1_BIT1, R1_BIT2, R1_BIT3), 0, 9);
  int ten = constrain(readRod(R2_BIT0, R2_BIT1, R2_BIT2, R2_BIT3), 0, 9);
  int hundred = constrain(readRod(R3_BIT0, R3_BIT1, R3_BIT2, R3_BIT3), 0, 9);
  int thousand = constrain(readRod(R4_BIT0, R4_BIT1, R4_BIT2, R4_BIT3), 0, 9);
  return unit + (ten*10) + (hundred*100) + (thousand*1000);
}

// === DFPlayer ===
DFRobotDFPlayerMini dfplayer;

// === Functions for LED Pattern ===
void blinkLED(unsigned long interval) {
  unsigned long currentMillis = millis();
  if (currentMillis - previousMillis >= interval) {
    previousMillis = currentMillis;
    ledState = !ledState;
    digitalWrite(LED_PIN, ledState ? HIGH : LOW);
  }
}

// === DFPlayer Helpers ===
void playTrackAndWait(int track) {
  if (track == 36) dfplayer.volume(20);  // "correct"
  else dfplayer.volume(30);
  dfplayer.play(track);
  delay(800);
}

void sayBelowThousand(int num) {
  if (num <= 20) { playTrackAndWait(num + 1); return; }
  if (num < 100) {
    int tens = num / 10;
    int ones = num % 10;
    playTrackAndWait(tens + 19);
    if (ones > 0) playTrackAndWait(ones + 1);
    return;
  }
  int hundreds = num / 100;
  int remainder = num % 100;
  playTrackAndWait(hundreds + 1);
  playTrackAndWait(30); // "hundred"
  if (remainder > 0) {
    if (remainder <= 20) playTrackAndWait(remainder + 1);
    else {
      int tens = remainder / 10;
      int ones = remainder % 10;
      playTrackAndWait(tens + 19);
      if (ones > 0) playTrackAndWait(ones + 1);
    }
  }
}

void sayNumber(int num) {
  if (num == 0) { playTrackAndWait(1); return; }
  int thousands = num / 1000;
  int remainder = num % 1000;
  if (thousands > 0) {
    playTrackAndWait(thousands + 1);
    playTrackAndWait(29); // "thousand"
    if (remainder > 0) {
      if (remainder < 100) sayBelowThousand(remainder);
      else { playTrackAndWait(31); sayBelowThousand(remainder); }
    }
    return;
  }
  sayBelowThousand(num);
}

// === Display Helpers ===
void wakeDisplay() { if (!displayOn) { lcd.backlight(); displayOn = true; } }
void sleepDisplay() { if (displayOn) { lcd.noBacklight(); displayOn = false; } }


// Add this function to send JSON formatted data
void sendToESP8266(String type, String question = "", int userAnswer = 0, int correctAnswer = 0) {
  String json = "{";
  json += "\"type\":\"" + type + "\"";
  
  if (question.length() > 0) {
    json += ",\"question\":\"" + question + "\"";
  }
  
  if (type == "answer") {
    json += ",\"userAnswer\":" + String(userAnswer);
    json += ",\"correctAnswer\":" + String(correctAnswer);
    json += ",\"correct\":" + String(userAnswer == correctAnswer ? "true" : "false");
  }
  
  json += "}";
  
  Serial1.println(json);   // Send to ESP8266
  Serial.println("Sent to ESP8266: " + json);  // Debug
}

// --- Auto Question ---
void generateAutoQuestion(){
  do{
    num1=random(1,1000);
    num2=random(1,1000);
    operation=(random(0,2)==0)?'+':'-';
    if(operation=='-' && num2>num1){ int t=num1; num1=num2; num2=t; }
  }while((operation=='+' && num1+num2>9000) || (operation=='-' && num1-num2<0));
  
  lcd.clear();
  lcd.setCursor(0,0);
  lcd.print(num1); lcd.print(" "); lcd.print(operation); lcd.print(" "); lcd.print(num2); lcd.print(" =");
  lcd.setCursor(0,1); lcd.print("Your Ans: 0");
  
  // Send question to web interface
  String questionStr = String(num1) + " " + String(operation) + " " + String(num2);
  sendToESP8266("question", questionStr);
  
  questionShown=true; answerChecked=false; lastBeadValue=-2; numberSpoken=false;
}



// Modified checkAutoAnswer function
void checkAutoAnswer() {
  int correctAnswer = (operation=='+') ? (num1+num2) : (num1-num2);
  int userAnswer = readTotalBeadValue();

  // Send answer result to web interface
  String questionStr = String(num1) + " " + String(operation) + " " + String(num2);
  sendToESP8266("answer", questionStr, userAnswer, correctAnswer);

  lcd.clear();
  lcd.setCursor(0,0);
  lcd.print(num1); lcd.print(" "); lcd.print(operation); lcd.print(" "); 
  lcd.print(num2); lcd.print(" = "); lcd.print(userAnswer);
  lcd.setCursor(0,1);

  if (userAnswer == correctAnswer) {
    lcd.print("Correct!");
    playTrackAndWait(36);
    if (ledOn) { correctFeedbackActive = true; correctFeedbackStart = millis(); }
  } else {
    lcd.print("Wrong! Ans:"); lcd.print(correctAnswer);
    playTrackAndWait(37);
    digitalWrite(VIBRATION_PIN, HIGH);
    delay(500);
    digitalWrite(VIBRATION_PIN, LOW);
  }

  questionShown=false; answerChecked=true;
  currentMode=SHOW_RESULT;
  resultTime=millis();
}

// --- Menu ---
void showMenu(){
  lcd.clear();
  lcd.setCursor(0,0);
  lcd.print("Abacus Genius1.0");
  lcd.setCursor(2, 1);
  lcd.print("Welcome User!");
  delay(3500);
  lcd.clear();
  lcd.setCursor(0,0); lcd.print("Select Mode:");
  lcd.setCursor(0,1); lcd.print("1:Auto 2:Manual");
  currentMode=MENU_MODE;
}

void setup(){
  lcd.init(); lcd.backlight();
  pinMode(TOUCH_PIN, INPUT);
  pinMode(PIR_PIN, INPUT);
  pinMode(BTN_LIGHT, INPUT); // external pull-down
  pinMode(BTN_READNUM, INPUT_PULLUP);
  pinMode(BTN_PAUSE, INPUT_PULLUP);
  pinMode(BTN_MUSIC, INPUT); // external pull-down
  pinMode(LED_PIN, OUTPUT); digitalWrite(LED_PIN, LOW);
  pinMode(VIBRATION_PIN, OUTPUT);
  digitalWrite(VIBRATION_PIN, LOW); // make sure motor is off initially

  int rodPins[] = { R1_BIT0,R1_BIT1,R1_BIT2,R1_BIT3,
                    R2_BIT0,R2_BIT1,R2_BIT2,R2_BIT3,
                    R3_BIT0,R3_BIT1,R3_BIT2,R3_BIT3,
                    R4_BIT0,R4_BIT1,R4_BIT2,R4_BIT3 };
  for(int i=0;i<16;i++) pinMode(rodPins[i],INPUT);

  randomSeed(analogRead(A0));

  Serial.begin(9600);
  Serial1.begin(9600);  // For ESP8266 comms
  Serial2.begin(9600); // TX2/RX2 for DFPlayer Mini on Mega
  if(!dfplayer.begin(Serial2)){
    lcd.clear(); lcd.print("DFPlayer error!"); while(true);
  }
  dfplayer.volume(25);

  showMenu();
}

void loop(){
  char key = keypad.getKey();
  beadValue = readTotalBeadValue();

  // --- Touch/PIR Sleep ---
  bool touch = digitalRead(TOUCH_PIN);
  bool pir = digitalRead(PIR_PIN);
  if(touch) wakeDisplay();
  if(displayOn && millis()-lastMotionTime>=DISPLAY_TIMEOUT && millis()-lastBeadMotionTime>=DISPLAY_TIMEOUT) sleepDisplay();

  // --- Global Menu Button '*' ---
  if(key=='*'){
    showMenu();
    return;
  }

  // --- Menu ---
  if(currentMode==MENU_MODE && key){
    if(key=='1'){ currentMode=AUTO_MODE; generateAutoQuestion(); return; }
    if(key=='2'){ currentMode=MANUAL_MODE; manualProblem=""; manualBeadPhase=false; lcd.clear(); lcd.setCursor(0,0); lcd.print("Enter problem:"); return; }
  }

  // --- Manual Mode Input ---
  if(currentMode==MANUAL_MODE && !manualBeadPhase && key){
    if(key>='0' && key<='9'){ manualProblem+=key; lcd.setCursor(manualProblem.length()-1,1); lcd.print(key); }
    else if(key=='A'){ manualProblem+="+"; lcd.setCursor(manualProblem.length()-1,1); lcd.print("+"); }
    else if(key=='B'){ manualProblem+="-"; lcd.setCursor(manualProblem.length()-1,1); lcd.print("-"); }
    else if(key=='C'){ manualBeadPhase=true; lcd.setCursor(manualProblem.length(),1); lcd.print("="); lastBeadValue=-1; }
    else if(key=='D'){ // Enter pressed
        if(!manualBeadPhase){ manualBeadPhase=true; lcd.setCursor(manualProblem.length(),1); lcd.print("="); lastBeadValue=-1; }
    }
    else if(key=='#' && manualProblem.length()>0){ manualProblem.remove(manualProblem.length()-1); lcd.setCursor(manualProblem.length(),1); lcd.print(" "); }
  }

  // --- Manual Bead Phase ---
  if(currentMode==MANUAL_MODE && manualBeadPhase){
    lcd.setCursor(manualProblem.length()+1,1); lcd.print(beadValue);
    if(key=='D'){ // Enter after beads
      int correctAns=0;
      if(manualProblem.indexOf('+')>0){ int split=manualProblem.indexOf('+'); correctAns=manualProblem.substring(0,split).toInt()+manualProblem.substring(split+1).toInt();}
      else if(manualProblem.indexOf('-')>0){ int split=manualProblem.indexOf('-'); correctAns=manualProblem.substring(0,split).toInt()-manualProblem.substring(split+1).toInt();}
      
      sendToESP8266("answer", manualProblem, beadValue, correctAns);
      
      lcd.clear(); lcd.setCursor(0,0); lcd.print(manualProblem); lcd.print("="); lcd.print(correctAns);
      lcd.setCursor(0,1); lcd.print("You: "); lcd.print(beadValue); lcd.print(beadValue==correctAns?" OK":" Wrong");

      // --- Say number first ---
      sayNumber(beadValue);

      // --- Then play correct/wrong sound ---
      if(beadValue == correctAns) playTrackAndWait(36);  // correct
      else {
        playTrackAndWait(37);  // wrong
        digitalWrite(VIBRATION_PIN, HIGH);
        delay(500);
        digitalWrite(VIBRATION_PIN, LOW);
      }

      manualBeadPhase=false; answerChecked=true;
    }
  }

  // --- Auto Mode ---
  if(currentMode==AUTO_MODE && questionShown){
    if(key=='D'){ checkAutoAnswer(); lastBeadValue=-1; numberSpoken=false; lastStableTime=millis(); }

    if(!answerChecked){
      int beads=readTotalBeadValue();
      if(beads!=lastBeadValue){ numberSpoken=false; lastBeadValue=beads; lastStableTime=millis(); lastMotionTime=millis(); wakeDisplay(); }
      if(!numberSpoken && (millis()-lastStableTime>=stableDelay)){ lcd.setCursor(10,1); lcd.print("    "); lcd.setCursor(10,1); lcd.print(beads); sayNumber(beads); numberSpoken=true; }
    }
  }

  // --- LED System ---
  int reading = digitalRead(BTN_LIGHT);
  if(reading != lastButtonState) lastDebounceTime = millis();
  if((millis()-lastDebounceTime) > debounceDelay){
    if(reading != buttonState){
      buttonState = reading;
      if(buttonState==HIGH){ buttonPressStart=millis(); waitingForLongPress=true; }
      else{
        unsigned long pressDuration=millis()-buttonPressStart;
        if(waitingForLongPress){
          if(pressDuration>=2000){ ledOn=!ledOn; if(ledOn) pattern=0; }
          else{ if(ledOn){ pattern++; if(pattern>3) pattern=0; } }
          waitingForLongPress=false;
        }
      }
    }
  }
  lastButtonState=reading;

  // --- LED Execution ---
  if(!ledOn) digitalWrite(LED_PIN,LOW);
  else {
    if(correctFeedbackActive){
      if(millis()-correctFeedbackStart<correctFeedbackDuration) blinkLED(70);
      else correctFeedbackActive=false;
    } else {
      switch(pattern){
        case 0: digitalWrite(LED_PIN,HIGH); break;
        case 1: blinkLED(800); break;
        case 2: blinkLED(600); break;
        case 3: blinkLED(400); break;
      }
    }
  }

  // --- Touch & PIR ---
  bool currentTouchState = digitalRead(TOUCH_PIN);
  if(currentTouchState!=lastTouchState && (millis()-debounceTime2)>debounceDelay){
    debounceTime2=millis();
    if(currentTouchState==HIGH){ wakeDisplay(); lastMotionTime=millis(); }
  }
  lastTouchState=currentTouchState;
  if(digitalRead(PIR_PIN)==HIGH){ lastMotionTime=millis(); wakeDisplay(); }
  if(millis()-lastMotionTime>=DISPLAY_TIMEOUT) sleepDisplay();

  // --- Music Button Handling ---
  bool musicReading = digitalRead(BTN_MUSIC);
  unsigned long now = millis();
  if (musicReading == HIGH && !musicButtonPreviouslyPressed) {
    musicButtonPressTime = now;
    musicButtonPreviouslyPressed = true;
    longPressHandled = false;
    Serial.println("Music Button pressed");
  }
  if (musicReading == HIGH && musicButtonPreviouslyPressed) {
    static unsigned long lastPrintTime = 0;
    if(now - lastPrintTime >= 500){
      float secondsHeld = (now - musicButtonPressTime)/1000.0;
      Serial.print("Held for: "); Serial.print(secondsHeld,1); Serial.println(" sec");
      lastPrintTime = now;
      if(!longPressHandled && (now - musicButtonPressTime >= 2000)){
        isMusicSystemOn = !isMusicSystemOn;
        longPressHandled = true;
        if(isMusicSystemOn) Serial.println("Music System ON (long press)");
        else { Serial.println("Music System OFF (long press)"); dfplayer.stop(); }
      }
    }
  }
  if (musicReading == LOW && musicButtonPreviouslyPressed){
    unsigned long pressDuration = now - musicButtonPressTime;
    if(!longPressHandled && pressDuration < 2000){
      if(isMusicSystemOn){
        dfplayer.play(currentTrack);
        Serial.print("Playing track "); Serial.println(currentTrack);
        currentTrack++;
        if(currentTrack > 35) currentTrack = 32;
      }
    }
    musicButtonPreviouslyPressed = false;
  }

  // --- ReadNum / Pause ---
  if(digitalRead(BTN_READNUM)==LOW){ sayNumber(readTotalBeadValue()); delay(300); }
  if(digitalRead(BTN_PAUSE)==LOW){ delay(300); }

  // --- Quiz logic ---
  if(currentMode==SHOW_RESULT){
    if(millis()-resultTime>=3000){ currentMode=AUTO_MODE; generateAutoQuestion(); lastBeadValue=-1; numberSpoken=false; lastStableTime=millis(); }
    return;
  }
}