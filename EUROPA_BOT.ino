/*
 * THE EUROPA Bot: Line Follow, Wall Follow and Bluetooth Manual
 * Starts in AUTO Mode (Line Follow to Wall Follow)
 * Use SriTu Hobby App to control.
 * 
 * Mode Switching:
 * Touch any movement arrow to temporarily override and drive manually.
 * When you release the arrow it goes back to Auto mode automatically.
 * Use Front Light (W), Switch (X), or Channel 1 (1) buttons in app
 * to lock the robot into MANUAL mode.
 * Turn them off (w, x, 2) to restore AUTO mode.
 */

#include <BluetoothSerial.h>
#include <NewPing.h>

BluetoothSerial SerialBT;

// Pin definitions

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
#define CH1_PIN 2  // Channel 1 built in LED
#define CH2_PIN 23 // Channel 2

// Ultrasonic pins
#define LEFT_TRIG  25
#define LEFT_ECHO  26
#define FRONT_TRIG 27
#define FRONT_ECHO 14
#define RIGHT_TRIG 33
#define RIGHT_ECHO 13

#define MAX_DISTANCE 200  // max distance to ping in cm

// Tuning auto mode

#define WALL_DIST      15   // desired distance from wall in cm
#define FRONT_STOP     25   // stop or turn if front obstacle closer than this
#define BASE_SPEED     120
#define TURN_SPEED     100

#define SWITCH_DIST    34   // distance threshold in cm to trigger line to wall switch

// Tuning manual mode

#define MOTOR_SPEED 120 
#define MOTOR_SPEED_TUR 180 // 0 to 255 normal drive speed
#define DIAGONAL_FAST 150 // outer wheel for diagonal moves
#define DIAGONAL_SLOW 150 // inner wheel for diagonal moves

// PWM configuration
#define PWM_FREQ 4000
#define PWM_RESOLUTION 8
const int FRONT_LIMIT = 25;   // cm
const int WALL_LIMIT  = 20;   // desired wall distance
const int TOLERANCE   = 4;  

// Sensor objects
NewPing sonarLeft(LEFT_TRIG, LEFT_ECHO, MAX_DISTANCE);
NewPing sonarFront(FRONT_TRIG, FRONT_ECHO, MAX_DISTANCE);
NewPing sonarRight(RIGHT_TRIG, RIGHT_ECHO, MAX_DISTANCE);

// Mode variables

enum Mode { MODE_LINE, MODE_WALL, MODE_MANUAL };
Mode currentMode = MODE_LINE;       // Currently active mode
Mode autoModeState = MODE_LINE;     // Remembers whether we were in line or wall mode
bool isHardManual = false;          // True if locked into manual via switch

// Motor control functions

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

// Auto mode motor helpers

void forward()       { setMotors(BASE_SPEED, BASE_SPEED); }
void back()          { setMotors(-BASE_SPEED, -BASE_SPEED); }
void adjust_left()   { setMotors(-TURN_SPEED, TURN_SPEED); }
void adjust_right()  { setMotors(TURN_SPEED, -TURN_SPEED); }
void right()         { setMotors(TURN_SPEED, -TURN_SPEED); }
void right_wall()    { setMotors(TURN_SPEED -20, -TURN_SPEED);}
void left()          { setMotors(-TURN_SPEED, TURN_SPEED); }
void left_wall()     { setMotors(-TURN_SPEED, TURN_SPEED - 20);}

// Manual mode motor helpers

void moveForward()   { setMotors(MOTOR_SPEED, MOTOR_SPEED); }
void moveBackward()  { setMotors(-MOTOR_SPEED, -MOTOR_SPEED); }
void turnLeft()      { setMotors(-MOTOR_SPEED, MOTOR_SPEED); }
void turnRight()     { setMotors(MOTOR_SPEED, -MOTOR_SPEED); }

void forwardLeft()   { setMotors(DIAGONAL_SLOW, DIAGONAL_FAST); }
void forwardRight()  { setMotors(DIAGONAL_FAST, DIAGONAL_SLOW); }
void backwardLeft()  { setMotors(-DIAGONAL_SLOW, -DIAGONAL_FAST); }
void backwardRight() { setMotors(-DIAGONAL_FAST, -DIAGONAL_SLOW); }

// Ultrasonic helper function

long readDistance(NewPing &sonar)
{
  unsigned int cm = sonar.ping_cm();
  if (cm == 0) return MAX_DISTANCE;
  return cm;
}

// Setup function

void setup()
{
  Serial.begin(115200);
  SerialBT.begin("THE EUROPA"); // Bluetooth device name
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

// Line following function

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

// Wall following function

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

// Main loop

void loop()
{
  // Process Bluetooth commands first
  if (SerialBT.available()) {
    char cmd = SerialBT.read();

    switch (cmd) {
    // Movement temporary manual override
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

    // Diagonal movement
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

    // Stop command reverts to auto if not locked in hard manual
    case 'S': 
      stopMotor();
      if (!isHardManual && currentMode == MODE_MANUAL) {
        currentMode = autoModeState; // Resume auto mode
      }
      break;

    // Mode switching via app buttons
    // 1, W, X, V lock into manual mode
    case '1': case 'W': case 'X': case 'V':
      isHardManual = true;
      if (currentMode != MODE_MANUAL) autoModeState = currentMode;
      currentMode = MODE_MANUAL;
      stopMotor();
      digitalWrite(CH1_PIN, HIGH);
      Serial.println(">> Mode: HARD MANUAL (Locked)");
      break;
      
    // 2, w, x, v restore auto mode
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

  // Execute behaviors based on current mode
  if (currentMode == MODE_LINE)
  {
    lineFollow();
  }
  else if (currentMode == MODE_WALL)
  {
    wallFollow();
  }
}
