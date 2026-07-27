/*
 * ============================================================
 *  Combined Bot: Line Follow, Wall Follow & Bluetooth Manual
 * ============================================================
 *  - Starts in AUTO Mode (Line Follow -> Wall Follow)
 *  - Use SriTu Hobby App to control.
 *  
 *  IMPROVED MODE SWITCHING:
 *  - Touch any movement arrow to TEMPORARILY override and drive manually.
 *    When you release the arrow, it goes back to Auto (Line/Wall) automatically.
 *  - Use the "Front Light" (W), "Switch" (X), or "Channel 1" (1) buttons in the 
 *    app to PERMANENTLY lock the robot into MANUAL mode. 
 *  - Turn them off (w, x, 2) to restore AUTO mode.
 * ============================================================
 */

#include <BluetoothSerial.h>
#include <NewPing.h>

BluetoothSerial SerialBT;

// ---------------- PIN DEFINITIONS ----------------

// IR line sensors
#define left_ir    34
#define center_ir  35
#define right_ir   32

// Motor pins
#define leftmotor1  18 // IN1
#define leftmotor2  17 // IN2
#define rightmotor1 22 // IN3
#define rightmotor2 21 // IN4

#define ENA 16
#define ENB 19

// Channel pins
#define CH1_PIN 2  // Channel 1 — built-in LED
#define CH2_PIN 23 // Channel 2

// Ultrasonic pins
#define LEFT_TRIG  25
#define LEFT_ECHO  26
#define FRONT_TRIG 27
#define FRONT_ECHO 14
#define RIGHT_TRIG 33
#define RIGHT_ECHO 13

#define MAX_DISTANCE 200  // max distance to ping (cm)

// ---------------- TUNING (AUTO) ----------------

#define WALL_DIST      15   // desired distance from wall (cm)
#define FRONT_STOP     25   // stop/turn if front obstacle closer than this
#define BASE_SPEED     120
#define TURN_SPEED     100

#define SWITCH_DIST    34   // distance threshold (cm) to trigger line->wall switch

// ---------------- TUNING (MANUAL) ----------------

#define MOTOR_SPEED 120 
#define MOTOR_SPEED_TUR 180 // 0-255  — normal drive speed
#define DIAGONAL_FAST 150 // outer wheel for diagonal moves
#define DIAGONAL_SLOW 150 // inner wheel for diagonal moves

// === PWM CONFIG ===
#define PWM_FREQ 4000
#define PWM_RESOLUTION 8
const int FRONT_LIMIT = 25;   // cm
const int WALL_LIMIT  = 20;   // desired wall distance
const int TOLERANCE   = 4;  

// NewPing sensor objects
NewPing sonarLeft(LEFT_TRIG, LEFT_ECHO, MAX_DISTANCE);
NewPing sonarFront(FRONT_TRIG, FRONT_ECHO, MAX_DISTANCE);
NewPing sonarRight(RIGHT_TRIG, RIGHT_ECHO, MAX_DISTANCE);

// ---------------- MODE ----------------

enum Mode { MODE_LINE, MODE_WALL, MODE_MANUAL };
Mode currentMode = MODE_LINE;       // Currently active mode
Mode autoModeState = MODE_LINE;     // Remembers whether we were in LINE or WALL
bool isHardManual = false;          // True if locked into manual via switch (W/X/1)

// ---------------- LOW-LEVEL MOTOR CONTROL ----------------

void setMotors(int leftSpeedVal, int rightSpeedVal)
{
  if (leftSpeedVal >= 0) {
    digitalWrite(leftmotor1, HIGH);
    digitalWrite(leftmotor2, LOW);
  } else {   // backward
    digitalWrite(leftmotor1, LOW);
    digitalWrite(leftmotor2, HIGH);
    leftSpeedVal = -leftSpeedVal;
  }

  if (rightSpeedVal >= 0) {  // forward
    digitalWrite(rightmotor1, HIGH);
    digitalWrite(rightmotor2, LOW);
  } else {  // backward
    digitalWrite(rightmotor1, LOW);
    digitalWrite(rightmotor2, HIGH);
    rightSpeedVal = -rightSpeedVal;
  }

  ledcWrite(ENA, constrain(leftSpeedVal, 0, 255));
  ledcWrite(ENB, constrain(rightSpeedVal, 0, 255));
}

void stopMotor()
{
  ledcWrite(ENA, 0);
  ledcWrite(ENB, 0);
  digitalWrite(leftmotor1, LOW);
  digitalWrite(leftmotor2, LOW);
  digitalWrite(rightmotor1, LOW);
  digitalWrite(rightmotor2, LOW);
}

// ---------------- AUTO MOTOR HELPERS ----------------

void forward()       { setMotors(BASE_SPEED, BASE_SPEED); }
void back()          { setMotors(-BASE_SPEED, -BASE_SPEED); }
void adjust_left()   { setMotors(-TURN_SPEED, TURN_SPEED); }
void adjust_right()  { setMotors(TURN_SPEED, -TURN_SPEED); }
void right()         { setMotors(TURN_SPEED, -TURN_SPEED); }
void right_wall()    { setMotors(TURN_SPEED -20, -TURN_SPEED);}
void left()          { setMotors(-TURN_SPEED, TURN_SPEED); }
void left_wall()     { setMotors(-TURN_SPEED, TURN_SPEED - 20);}
// ---------------- MANUAL MOTOR HELPERS ----------------

void moveForward()   { setMotors(MOTOR_SPEED, MOTOR_SPEED); }
void moveBackward()  { setMotors(-MOTOR_SPEED, -MOTOR_SPEED); }
void turnLeft()      { setMotors(-MOTOR_SPEED, MOTOR_SPEED); }
void turnRight()     { setMotors(MOTOR_SPEED, -MOTOR_SPEED); }

void forwardLeft()   { setMotors(DIAGONAL_SLOW, DIAGONAL_FAST); }
void forwardRight()  { setMotors(DIAGONAL_FAST, DIAGONAL_SLOW); }
void backwardLeft()  { setMotors(-DIAGONAL_SLOW, -DIAGONAL_FAST); }
void backwardRight() { setMotors(-DIAGONAL_FAST, -DIAGONAL_SLOW); }

// ---------------- ULTRASONIC HELPER ----------------

long readDistance(NewPing &sonar)
{
  unsigned int cm = sonar.ping_cm();
  if (cm == 0) return MAX_DISTANCE;
  return cm;
}

// ---------------- SETUP ----------------

void setup()
{
  Serial.begin(115200);
  SerialBT.begin("THE EUPORA"); // Bluetooth device name
  Serial.println("Bluetooth started — waiting for connection...");

  // IR sensors
  pinMode(left_ir, INPUT);
  pinMode(center_ir, INPUT);
  pinMode(right_ir, INPUT);

  // Motor pins
  pinMode(leftmotor1, OUTPUT);
  pinMode(leftmotor2, OUTPUT);
  pinMode(rightmotor1, OUTPUT);
  pinMode(rightmotor2, OUTPUT);

  // PWM on enable pins
  ledcAttach(ENA, PWM_FREQ, PWM_RESOLUTION);
  ledcAttach(ENB, PWM_FREQ, PWM_RESOLUTION);

  // Channel outputs
  pinMode(CH1_PIN, OUTPUT);
  pinMode(CH2_PIN, OUTPUT);
  digitalWrite(CH1_PIN, LOW);
  digitalWrite(CH2_PIN, LOW);

  stopMotor();
}

// ---------------- LINE FOLLOWING ----------------

void lineFollow()
{
  int L = digitalRead(left_ir);
  int C = digitalRead(center_ir);
  int R = digitalRead(right_ir);

  if (L == LOW && C == LOW && R == LOW)
  {
    long distLeft  = readDistance(sonarLeft);
    delay(10);
    long distRight = readDistance(sonarRight);

    if ((distLeft + distRight) < SWITCH_DIST)
    {
      Serial.println("Switching: LINE -> WALL");
      stopMotor();
      currentMode = MODE_WALL;
      autoModeState = MODE_WALL; // Remember we are in wall mode
      return; 
    }
  }
   
  if (C == LOW && L == HIGH && R == HIGH)
  {
    forward();
  }
  else if (L == LOW)
  {
    left();
  }
  else if (R == LOW)
  {
    right();
  }
  else if (C == LOW && L == LOW && R == LOW)
  //else if (C == HIGH && L == HIGH && R == HIGH)
  {
    forward();
    //back();
  }
  else if (C == HIGH && L == HIGH && R == HIGH)
  {
    back();
  }
  else
  {
    stopMotor();
  }
}

// ---------------- WALL FOLLOWING ----------------

void wallFollow()
{
  long distLeft  = readDistance(sonarLeft);
  delay(15);
  long distFront = readDistance(sonarFront);
  delay(15);
  long distRight = readDistance(sonarRight);

  int L = digitalRead(left_ir);
  int C = digitalRead(center_ir);
  int R = digitalRead(right_ir);
  if ((C == LOW && (L == HIGH && R == HIGH)) && (distLeft + distRight) > 70) 
  {
    Serial.println("Switching: WALL -> LINE");
    stopMotor();
    currentMode = MODE_LINE;
    autoModeState = MODE_LINE; // Remember we are back in line mode
    return;
  }

  if (distFront > 22 && (distLeft + distRight) > 32)
  {
    forward();
  }
  else if (distFront > 22 && ((distLeft + distRight) > 32) && (distLeft > distRight))
  {
    adjust_right();
  }
  else if (distFront > 22 &&  ((distLeft + distRight) > 32) && (distLeft < distRight))
  {
    adjust_left();
  }
  else if (distFront < 22 && distLeft > distRight)
  {
    left_wall(); 
    delay(250);
  }
  else if (distFront < 22 && distRight > distLeft)
  {
    right_wall();  
    delay(250);
  }
  else
  {
    setMotors(BASE_SPEED, BASE_SPEED);
  }

  delay(20);
}

// ---------------- MAIN LOOP ----------------

void loop()
{
  // 1. Process Bluetooth Commands First
  if (SerialBT.available()) {
    char cmd = SerialBT.read();

    switch (cmd) {
    // ── Movement (Overrides to MANUAL temporarily) ──
    case 'U': 
      if (currentMode != MODE_MANUAL) autoModeState = currentMode;
      currentMode = MODE_MANUAL; moveForward(); break;
    case 'D': 
      if (currentMode != MODE_MANUAL) autoModeState = currentMode;
      currentMode = MODE_MANUAL; moveBackward(); break;
    case 'L': 
      if (currentMode != MODE_MANUAL) autoModeState = currentMode;
      currentMode = MODE_MANUAL; turnLeft(); break;
    case 'R': 
      if (currentMode != MODE_MANUAL) autoModeState = currentMode;
      currentMode = MODE_MANUAL; turnRight(); break;

    // ── Diagonal ──
    case 'T': 
      if (currentMode != MODE_MANUAL) autoModeState = currentMode;
      currentMode = MODE_MANUAL; forwardLeft(); break;
    case 'F': 
      if (currentMode != MODE_MANUAL) autoModeState = currentMode;
      currentMode = MODE_MANUAL; forwardRight(); break;
    case 'H': 
      if (currentMode != MODE_MANUAL) autoModeState = currentMode;
      currentMode = MODE_MANUAL; backwardLeft(); break;
    case 'G': 
      if (currentMode != MODE_MANUAL) autoModeState = currentMode;
      currentMode = MODE_MANUAL; backwardRight(); break;

    // ── Stop (Reverts to Auto if not locked in Hard Manual) ──
    case 'S': 
      stopMotor();
      if (!isHardManual && currentMode == MODE_MANUAL) {
        currentMode = autoModeState; // Seamlessly resume Auto mode
      }
      break;

    // ── Mode Switching (Hard Toggle via App Buttons) ──
    // 1, W (Front Light), X (Switch) -> Lock into Manual Mode
    case '1': case 'W': case 'X': case 'V':
      isHardManual = true;
      if (currentMode != MODE_MANUAL) autoModeState = currentMode;
      currentMode = MODE_MANUAL;
      stopMotor();
      digitalWrite(CH1_PIN, HIGH);
      Serial.println(">> Mode: HARD MANUAL (Locked)");
      break;
      
    // 2, w (Front Light Off), x (Switch Off) -> Restore Auto Mode
    case '2': case 'w': case 'x': case 'v':
      isHardManual = false;
      currentMode = autoModeState;
      stopMotor();
      digitalWrite(CH1_PIN, LOW);
      Serial.println(">> Mode: AUTO RESTORED");
      break;

    default:
      break; // ignore unknown chars
    }
  }

  // 2. Execute behaviors based on current Mode
  if (currentMode == MODE_LINE)
  {
    lineFollow();
  }
  else if (currentMode == MODE_WALL)
  {
    wallFollow();
  }
}
