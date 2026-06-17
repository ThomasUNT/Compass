#include <WiFi.h>
#include <WebServer.h>
#include "CompassHardware.h"

// --- Network Setup ---
const char* ssid = "Frontier4640";
const char* password = "3084930036";
WebServer server(80);

// --- Hardware Instance ---
CompassHardware compass;

// --- The Master Alphabet Dictionary ---
// Format: { 'character', angle_float, useInnerRing_bool, ledIndex_int }
const int ALPHABET_SIZE = 26;
const TargetInstruction ALPHABET_TABLE[ALPHABET_SIZE] = {
    {'a', 0.0, false, 0},
    {'b', 340.0, false, 1},
    {'c', 320.0, false, 2},
    {'d', 300.0, false, 3},
    {'e', 280.0, false, 4},
    {'f', 260.0, false, 5},
    {'g', 240.0, false, 6},
    {'h', 220.0, false, 7},
    {'i', 200.0, false, 8},
    {'j', 180.0, false, 9},
    {'k', 160.0, false, 10},
    {'l', 140.0, false, 11},
    {'m', 120.0, false, 12},
    {'n', 100.0, false, 13},
    {'o', 80.0, false, 14},
    {'p', 60.0, false, 15},
    {'q', 40.0, false, 16},
    {'r', 20.0, false, 17},
    {'s', 0.0, true, 18},
    {'t', 315.0, true, 19},
    {'u', 270.0, true, 20},
    {'v', 225.0, true, 21},
    {'w', 180.0, true, 22},
    {'x', 135.0, true, 23},
    {'y', 90.0,  true,  24},
    {'z', 45.0,  true,  25} 
};

// --- Finite State Machine Variables ---
enum SystemState {
    STATE_IDLE,
    STATE_NEXT_LETTER,
    STATE_MOVING,
    STATE_WAITING_FOR_USER
};

SystemState currentState = STATE_IDLE;
String wordQueue = "";
String currentWordDisplay = "None"; // Just for the web UI

unsigned long pauseStartTime = 0;
const unsigned long PAUSE_DURATION_MS = 2000; // How long to stay on a letter

// --- Function Prototypes ---
void processWordQueue();
bool lookupInstruction(char c, TargetInstruction &outInstruction);
void handleRoot();
void handleSet();
void handleNext();
void handleStatus();
void handleSerialInput();
void printIpBanner();
String cleanInputString(String raw);

void setup() {
    Serial.begin(115200);
    Serial.println("\n=================================");
    Serial.println("ALTARIN COMPASS");
    Serial.println("=================================");

    // 1. Boot the hardware subsystem
    compass.begin();

    // 2. Connect WiFi
    WiFi.begin(ssid, password);
    Serial.print("Connecting to WiFi");
    unsigned long wifiStart = millis();

    while (WiFi.status() != WL_CONNECTED && millis() - wifiStart < 15000) {
        delay(250);
        Serial.print(".");
    }
    Serial.println();

    if (WiFi.status() == WL_CONNECTED) {
        printIpBanner();
    } else {
        Serial.println("WiFi connection timed out. Serial control active.");
    }

    // 3. Start Web Server
    server.on("/", HTTP_GET, handleRoot);
    server.on("/set", HTTP_GET, handleSet);
    server.on("/next", HTTP_GET, handleNext);
    server.on("/status", HTTP_GET, handleStatus);
    server.begin();
    Serial.println("HTTP server started.");
}

void loop() {
    // 1. Check for incoming web/serial commands
    server.handleClient();
    handleSerialInput();

    // 2. Critically update the motor/LED tracking loops every frame
    compass.update();

    // 3. Run the async word-spelling sequence
    processWordQueue();
}

// ==========================================
// FINITE STATE MACHINE
// ==========================================
void processWordQueue() {
    switch (currentState) {
        
        case STATE_IDLE:
            if (wordQueue.length() > 0) {
                Serial.println("[FSM] Word received. Switching to NEXT_LETTER.");
                currentState = STATE_NEXT_LETTER;
            }
            break;

        case STATE_NEXT_LETTER:
            if (wordQueue.length() > 0) {
                char nextChar = wordQueue.charAt(0);
                wordQueue.remove(0, 1); // Pop the character off the front

                TargetInstruction instr;
                if (lookupInstruction(nextChar, instr)) {
                    Serial.print("[FSM] Target found! Spelling letter: '");
                    Serial.print(nextChar);
                    Serial.println("'");

                    compass.setTarget(instr);
                    currentState = STATE_MOVING;
                } else {
                    Serial.print("[FSM] Character '");
                    Serial.print(nextChar);
                    Serial.println("' not found in table. Skipping.");
                    // If character wasn't in dictionary, skip it and loop back
                    currentState = STATE_NEXT_LETTER; 
                }
            } else {
                // Word finished
                Serial.println("[FSM] Entire word completed. Returning to IDLE.");
                currentWordDisplay = "Done.";
                currentState = STATE_IDLE;
            }
            break;

        case STATE_MOVING:
            if (compass.hasReachedTarget()) {
                Serial.println("[FSM] Hardware reports target reached. Waiting for DM...");
                currentState = STATE_WAITING_FOR_USER;
            }
            break;

        case STATE_WAITING_FOR_USER:
            break;
    }
}

// ==========================================
// HELPER FUNCTIONS
// ==========================================
bool lookupInstruction(char c, TargetInstruction &outInstruction) {
    for (int i = 0; i < ALPHABET_SIZE; i++) {
        if (ALPHABET_TABLE[i].letter == c) {
            outInstruction = ALPHABET_TABLE[i];
            return true;
        }
    }
    return false;
}

String cleanInputString(String raw) {
    String clean = "";
    raw.toLowerCase();
    
    // Strip everything that isn't a-z
    for (int i = 0; i < raw.length(); i++) {
        char c = raw.charAt(i);
        if (c >= 'a' && c <= 'z') {
            clean += c;
        }
    }
    return clean;
}

// ==========================================
// WEB & SERIAL HANDLERS
// ==========================================
void handleRoot() {
    // The HTML is now injected with JavaScript to handle the audio and looping updates
    String page = R"rawliteral(
    <!doctype html><html><head>
    <meta name='viewport' content='width=device-width,initial-scale=1'>
    <style>
        body{font-family:Arial;padding:16px;max-width:540px;margin:auto;background:#111;color:#fff;}
        input{font-size:20px;padding:8px;width:200px;text-transform:lowercase;border:none;border-radius:4px;}
        button{font-size:20px;padding:8px 16px;background:#00ffff;color:#000;border:none;border-radius:4px;cursor:pointer;font-weight:bold;}
        .btn-next{background:#ff0055; color:#fff; width:100%; margin-top:15px; padding:16px;}
        .box{background:#222;color:#0ff;padding:16px;border-radius:8px;margin-top:20px;}
    </style></head><body>
    <h2>Compass Controller</h2>
    
    <form onsubmit='sendWord(event)'>
        Word: <input type='text' id='wordInput' autofocus>
        <button type='submit'>Spell</button>
    </form>
    
    <div class='box'>
        <p><b>Currently Spelling:</b> <span id='currentText'>None</span></p>
        <p><b>Queue Remaining:</b> <span id='queueText'></span></p>
    </div>

    <button type='button' class='btn-next' onclick='sendNext()'>NEXT LETTER &raquo;</button>
    
    <script>
        // Paste your custom audio URLs here!
        const startSound = new Audio('https://raw.githubusercontent.com/ThomasUNT/Compass/main/Moving.wav');
        const stopSound = new Audio('https://raw.githubusercontent.com/ThomasUNT/Compass/main/TargetReached.wav');
        
        let lastState = -1;

        function sendWord(e) {
            e.preventDefault();
            let w = document.getElementById('wordInput').value;
            fetch('/set?word=' + w);
            document.getElementById('wordInput').value = '';
        }

        function sendNext() {
            fetch('/next');
        }

        // This loop checks the hardware status every 300 milliseconds
        setInterval(() => {
            fetch('/status')
            .then(response => response.json())
            .then(data => {
                document.getElementById('currentText').innerText = data.current;
                document.getElementById('queueText').innerText = data.queue;

                // If we just entered the MOVING state (2)
                if (lastState !== 2 && data.state === 2) {
                    startSound.currentTime = 0;
                    startSound.play().catch(err => console.log("Audio play blocked by browser", err));
                }
                
                // If we just entered the WAITING state (3)
                if (lastState === 2 && data.state === 3) {
                    stopSound.currentTime = 0;
                    stopSound.play().catch(err => console.log("Audio play blocked by browser", err));
                }
                
                lastState = data.state;
            });
        }, 300);
    </script>
    </body></html>
    )rawliteral";

    server.send(200, "text/html", page);
}

void handleStatus() {
    String json = "{";
    json += "\"state\":" + String(currentState) + ",";
    json += "\"current\":\"" + currentWordDisplay + "\",";
    json += "\"queue\":\"" + wordQueue + "\"";
    json += "}";
    server.send(200, "application/json", json);
}

void handleSet() {
    if (server.hasArg("word")) {
        String input = server.arg("word");
        String sanitized = cleanInputString(input);
        
        if (sanitized.length() > 0) {
            wordQueue = sanitized;
            currentWordDisplay = sanitized;
            currentState = STATE_NEXT_LETTER;
            Serial.print("New word queued: ");
            Serial.println(wordQueue);
        }
    }
    // Redirect instantly back to root
    server.sendHeader("Location", "/", true);
    server.send(302, "text/plain", "");
}

void handleNext() {
    Serial.println("[Web Server] 'Next' button clicked.");
    if (currentState == STATE_WAITING_FOR_USER || currentState == STATE_MOVING) {
        Serial.println("[FSM] Manual advance triggered. Forcing next letter.");
        currentState = STATE_NEXT_LETTER;
    }
    else {
        Serial.print("[FSM] Next pressed but ignored. Current state code: ");
        Serial.println(currentState);
    }

    server.sendHeader("Location", "/", true);
    server.send(302, "text/plain", "");
}

void handleSerialInput() {
    if (Serial.available() > 0) {
        String input = Serial.readStringUntil('\n');
        input.trim();
        if (input == "restart") ESP.restart();
        
        String sanitized = cleanInputString(input);
        if (sanitized.length() > 0) {
            wordQueue = sanitized;
            currentWordDisplay = sanitized;
            currentState = STATE_NEXT_LETTER;
            Serial.print("New word queued via Serial: ");
            Serial.println(wordQueue);
        }
    }
}

void printIpBanner() {
    Serial.println("=================================");
    Serial.print("Web Interface: http://");
    Serial.println(WiFi.localIP());
    Serial.println("=================================\n");
}