#include <WiFi.h>
#include <WebSocketsServer.h>
#include <FastLED.h>
#include <ESP32Servo.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// ============================================================================
// WIFI CONFIGURATION (UPDATE THESE)
// ============================================================================
const char* ssid = "(TinkerSpace)";
const char* password = "123tinkerspace";

WebSocketsServer webSocket(81);

// HARDWARE PIN DEFINITIONS
constexpr uint8_t PIN_LDR         = 32; 
constexpr uint8_t PIN_BUTTON      = 27; 
constexpr uint8_t PIN_TRIG        = 5;  
constexpr uint8_t PIN_ECHO        = 18; 
constexpr uint8_t PIN_SERVO       = 19; 
constexpr uint8_t PIN_WS2812B     = 4;  
constexpr uint8_t PIN_BUZZER      = 26; 
constexpr uint8_t PIN_CAR_LED     = 25; 
constexpr uint8_t PIN_HOUSE_LED   = 33; 

// OLED Display Settings
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET    -1
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

constexpr uint8_t   NUM_LEDS              = 13;
constexpr uint8_t   LED_BRIGHTNESS        = 100; 
constexpr uint8_t   LDR_DETECT_STATE      = LOW; 

constexpr uint32_t GRACE_PERIOD_MS        = 1000;  
constexpr uint32_t CONTINUOUS_PENALTY_MS = 5000;  
constexpr uint32_t FLASH_COOLDOWN_MS      = 2000;  
constexpr uint32_t GATE_TIMEOUT_MS        = 15000; 

CRGB leds[NUM_LEDS];
Servo gateServo;

float karmaScore = 0.0;
const float PENALTY_PER_FLASH = 1.5;
const float REWARD_PER_FLASH  = 1.0;
float currentDistanceCm = 999.0f;

enum FlashState { WAITING_FOR_LIGHT, GRACE_PERIOD, BLINDED_BY_LIGHT, COOLDOWN };
FlashState currentFlashState = WAITING_FOR_LIGHT;

uint32_t graceStartTime = 0;
uint32_t darknessStartTime = 0;
uint32_t blindedStartTime = 0; 
uint32_t lastOledUpdate = 0;

bool isPunishmentTimeoutActive = false;
uint32_t punishmentStartTime = 0;

void webSocketEvent(uint8_t num, WStype_t type, uint8_t * payload, size_t length) {
    if (type == WStype_TEXT) {
        String msg = "";
        for(size_t i = 0; i < length; i++) msg += (char)payload[i];
        msg.trim();
        if (msg.startsWith("SET_KARMA:")) {
            karmaScore = msg.substring(10).toFloat();
            if (karmaScore >= 0.0) isPunishmentTimeoutActive = false; 
        }
    }
}

void setup() {
    Serial.begin(115200);
    
    pinMode(PIN_LDR, INPUT); 
    pinMode(PIN_BUTTON, INPUT_PULLUP); 
    pinMode(PIN_TRIG, OUTPUT);
    pinMode(PIN_ECHO, INPUT);
    pinMode(PIN_BUZZER, OUTPUT);
    pinMode(PIN_CAR_LED, OUTPUT);
    pinMode(PIN_HOUSE_LED, OUTPUT);
    digitalWrite(PIN_TRIG, LOW);

    Wire.begin(21, 22);
    if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
        Serial.println(F("SSD1306 allocation failed"));
    }
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(0,0);
    display.println(F("Connecting WiFi..."));
    display.display();

    WiFi.begin(ssid, password);
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
    }
    
    display.clearDisplay();
    display.setCursor(0,0);
    display.println(F("WiFi Connected!"));
    display.print(WiFi.localIP());
    display.display();
    delay(2000);

    webSocket.begin();
    webSocket.onEvent(webSocketEvent);

    FastLED.addLeds<WS2812B, PIN_WS2812B, GRB>(leds, NUM_LEDS);
    FastLED.setBrightness(LED_BRIGHTNESS);
    FastLED.clear(); FastLED.show();

    ESP32PWM::allocateTimer(0);
    gateServo.setPeriodHertz(50);
    gateServo.attach(PIN_SERVO, 500, 2400);
    gateServo.write(90); 
}

float getDistance() {
    digitalWrite(PIN_TRIG, LOW); delayMicroseconds(2);
    digitalWrite(PIN_TRIG, HIGH); delayMicroseconds(10);
    digitalWrite(PIN_TRIG, LOW);
    unsigned long duration = pulseIn(PIN_ECHO, HIGH, 25000);
    if (duration == 0) return 400.0f;
    return (duration * 0.0343f) / 2.0f;
}

void updateOled(int level, bool buzzerOn) {
    display.clearDisplay();
    display.setCursor(0, 0);
    display.setTextSize(1);
    display.print(F("IP: "));
    display.println(WiFi.localIP());
    display.print(F("KARMA: "));
    display.println(karmaScore, 1);
    display.print(F("DIST: "));
    display.print(currentDistanceCm, 1);
    display.println(F(" cm"));
    display.print(F("LEVEL: "));
    display.println(level);
    display.drawFastHLine(0, 36, 128, SSD1306_WHITE);
    display.setCursor(0, 42);
    if (buzzerOn) {
        display.setTextSize(2);
        display.println(F("ALARM!!"));
    } else {
        display.setTextSize(1);
        display.println(isPunishmentTimeoutActive ? F("GATE LOCKED!") : F("SYSTEM NORMAL"));
    }
    display.display();
}

void loop() {
    webSocket.loop();
    uint32_t now = millis();

    currentDistanceCm = getDistance();
    bool beamDetected = (digitalRead(PIN_LDR) == LDR_DETECT_STATE); 
    bool isButtonPressed = (digitalRead(PIN_BUTTON) == LOW); 

    // LIGHT LOGIC
    if (currentFlashState == WAITING_FOR_LIGHT) {
        if (beamDetected) { currentFlashState = GRACE_PERIOD; graceStartTime = now; }
    } else if (currentFlashState == GRACE_PERIOD) {
        if (!beamDetected) { currentFlashState = WAITING_FOR_LIGHT; } 
        else if (now - graceStartTime >= GRACE_PERIOD_MS) {
            if (isButtonPressed) karmaScore += REWARD_PER_FLASH; 
            else karmaScore -= PENALTY_PER_FLASH; 
            currentFlashState = BLINDED_BY_LIGHT; blindedStartTime = now; 
        }
    } else if (currentFlashState == BLINDED_BY_LIGHT) {
        if (!beamDetected) { currentFlashState = COOLDOWN; darknessStartTime = now; } 
        else if (now - blindedStartTime >= CONTINUOUS_PENALTY_MS) {
            if (isButtonPressed) karmaScore += REWARD_PER_FLASH; 
            else karmaScore -= PENALTY_PER_FLASH; 
            blindedStartTime = now; 
        }
    } else if (currentFlashState == COOLDOWN) {
        if (beamDetected) { currentFlashState = BLINDED_BY_LIGHT; blindedStartTime = now; } 
        else if (now - darknessStartTime >= FLASH_COOLDOWN_MS) { currentFlashState = WAITING_FOR_LIGHT; }
    }

    if (karmaScore > 0.0) karmaScore = 0.0;
    if (karmaScore < -100.0) karmaScore = -100.0;

    bool carAtGate = (currentDistanceCm > 0 && currentDistanceCm < 15.0);
    bool carEngineOn = true, housePowerOn = true, buzzerOn = false;
    int targetGateAngle = 90; 
    CRGB targetLedColor = CRGB::Black;

    int currentLevel = 0;
    if (karmaScore > -16.5) currentLevel = 0;
    else if (karmaScore > -33.0) currentLevel = 1;
    else if (karmaScore > -50.0) currentLevel = 2;
    else if (karmaScore > -66.0) currentLevel = 3;
    else if (karmaScore > -83.0) currentLevel = 4;
    else if (karmaScore > -99.0) currentLevel = 5;
    else currentLevel = 6;

    if (currentLevel >= 1) carEngineOn = false; 
    if (currentLevel >= 2) {
        targetLedColor = (currentDistanceCm < 10) ? CRGB::Green : CRGB::Red;
    } else {
        if (currentDistanceCm > 20) targetLedColor = CRGB::Green;
        else if (currentDistanceCm > 10) targetLedColor = CRGB::Yellow;
        else targetLedColor = CRGB::Red;
    }
    if (currentLevel >= 3 && carAtGate && !isPunishmentTimeoutActive) {
        isPunishmentTimeoutActive = true;
        punishmentStartTime = now;
    }
    if (currentLevel >= 4) housePowerOn = false; 
    if (currentLevel >= 5) targetLedColor = CRGB::Black; 
    if (currentLevel >= 6 && carAtGate) buzzerOn = true; 

    if (isPunishmentTimeoutActive) {
        targetGateAngle = 90; 
        if (now - punishmentStartTime >= GATE_TIMEOUT_MS) isPunishmentTimeoutActive = false;
    } else {
        targetGateAngle = carAtGate ? 0 : 90;          
    }

    digitalWrite(PIN_CAR_LED, carEngineOn ? HIGH : LOW);
    digitalWrite(PIN_HOUSE_LED, housePowerOn ? HIGH : LOW);
    gateServo.write(targetGateAngle);
    for (int i = 0; i < NUM_LEDS; i++) leds[i] = targetLedColor;
    FastLED.show();

    if (buzzerOn) {
        if ((now / 150) % 2 == 0) tone(PIN_BUZZER, 3500); 
        else tone(PIN_BUZZER, 2000); 
    } else {
        noTone(PIN_BUZZER);
    }

    if (now - lastOledUpdate >= 100) {
        lastOledUpdate = now;
        updateOled(currentLevel, buzzerOn);

        // Broadcast telemetry to connected Wi-Fi web clients
        String payload = "KARMA:" + String(karmaScore, 1) + ",DIST:" + String(currentDistanceCm, 1) + 
                         ",LVL:" + String(currentLevel) + ",BUZZ:" + String(buzzerOn ? 1 : 0) + 
                         ",LIGHT:" + String(beamDetected ? 1 : 0);
        webSocket.broadcastTXT(payload);
    }
    delay(50);
}