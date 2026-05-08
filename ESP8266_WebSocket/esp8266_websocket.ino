// ESP8266 Enhanced WebSocket + JSON Handler
#include <ESP8266WiFi.h>
#include <WebSocketsServer.h>
#include <ESP8266WebServer.h>
#include <ArduinoJson.h>

const char* ssid = "ESP8266_AP";
const char* password = "12345678";

ESP8266WebServer server(80);
WebSocketsServer webSocket = WebSocketsServer(8884);

String inputBuffer = "";
String currentQuestion = "";

// --- Simple evaluator: handles a+b, a-b, a*b, a/b
int evaluateExpression(String expr) {
  int result = 0;
  if (expr.indexOf('+') > 0) {
    int a = expr.substring(0, expr.indexOf('+')).toInt();
    int b = expr.substring(expr.indexOf('+') + 1).toInt();
    result = a + b;
  } else if (expr.indexOf('-') > 0) {
    int a = expr.substring(0, expr.indexOf('-')).toInt();
    int b = expr.substring(expr.indexOf('-') + 1).toInt();
    result = a - b;
  } else if (expr.indexOf('*') > 0) {
    int a = expr.substring(0, expr.indexOf('*')).toInt();
    int b = expr.substring(expr.indexOf('*') + 1).toInt();
    result = a * b;
  } else if (expr.indexOf('/') > 0) {
    int a = expr.substring(0, expr.indexOf('/')).toInt();
    int b = expr.substring(expr.indexOf('/') + 1).toInt();
    if (b != 0) result = a / b;
  }
  return result;
}

void handleArduinoMessage(String message) {
  message.trim();
  
  // Check if it's JSON from Arduino
  if (message.startsWith("{")) {
    DynamicJsonDocument doc(1024);
    DeserializationError error = deserializeJson(doc, message);
    
    if (error) {
      Serial.println("JSON parse error: " + String(error.c_str()));
      return;
    }
    
    String type = doc["type"];
    
    if (type == "question") {
  // New question generated
  currentQuestion = doc["question"].as<String>();  // ✅ FIXED
  String response = "{\"type\":\"question\",\"question\":\"" + currentQuestion + "\"}";
  webSocket.broadcastTXT(response);
  Serial.println("Question sent to web: " + response);
}

    } else if (type == "answer") {
      // Answer check result
      String question = doc["question"];
      int userAnswer = doc["userAnswer"];
      int correctAnswer = doc["correctAnswer"];
      bool correct = doc["correct"];
      
      String response = "{\"type\":\"result\",\"question\":\"" + question + 
                       "\",\"userAnswer\":" + String(userAnswer) +
                       ",\"correctAnswer\":" + String(correctAnswer) +
                       ",\"correct\":" + String(correct ? "true" : "false") + "}";
      
      webSocket.broadcastTXT(response);
      Serial.println("Result sent to web: " + response);
      
    } else if (type == "timer_start") {
      // Timer started
      String response = "{\"type\":\"timer_start\",\"timerActive\":true}";
      webSocket.broadcastTXT(response);
      Serial.println("Timer start sent to web: " + response);
      
    } else if (type == "timer_stop") {
      // Timer stopped
      int finalTime = doc["finalTime"];
      String response = "{\"type\":\"timer_stop\",\"timerActive\":false,\"finalTime\":" + String(finalTime) + "}";
      webSocket.broadcastTXT(response);
      Serial.println("Timer stop sent to web: " + response);
      
    } else if (type == "timer_update") {
      // Timer update
      String currentTime = doc["currentTime"];
      int seconds = doc["seconds"];
      String response = "{\"type\":\"timer_update\",\"currentTime\":\"" + currentTime + "\",\"seconds\":" + String(seconds) + "}";
      webSocket.broadcastTXT(response);
      Serial.println("Timer update sent to web: " + response);
    }
  } else {
    // Handle legacy format (equation=answer)
    int equalIndex = message.indexOf('=');
    if (equalIndex > 0) {
      String leftExpr = message.substring(0, equalIndex);
      int userAnswer = message.substring(equalIndex + 1).toInt();
      int correctAnswer = evaluateExpression(leftExpr);
      bool isCorrect = (userAnswer == correctAnswer);

      String response = "{\"type\":\"legacy\",\"equation\":\"" + message +
                        "\",\"correct\":" + (isCorrect ? "true" : "false") +
                        ",\"answer\":" + String(correctAnswer) + "}";

      Serial.println("Legacy format processed: " + response);
      webSocket.broadcastTXT(response);
    }
  }
}

void setup() {
  Serial.begin(9600);  // Match Arduino Mega baud rate (9600 from your Mega code)
  WiFi.softAP(ssid, password);

  Serial.println("ESP8266 WiFi AP started");
  Serial.print("IP Address: ");
  Serial.println(WiFi.softAPIP());

  server.begin();
  webSocket.begin();

  Serial.println("WebSocket server started on port 8884");

  // Basic HTTP test page
  server.on("/", []() {
    server.send(200, "text/html", 
      "<h2>AbacusGenius ESP8266</h2>"
      "<p>WebSocket: ws://192.168.4.1:8884</p>"
      "<p>Status: Running</p>");
  });
}

void loop() {
  server.handleClient();
  webSocket.loop();

  if (Serial.available()) {
    inputBuffer = Serial.readStringUntil('\n');
    
    if (inputBuffer.length() > 0) {
      Serial.println("Received from Arduino: " + inputBuffer);
      handleArduinoMessage(inputBuffer);
    }
  }
}