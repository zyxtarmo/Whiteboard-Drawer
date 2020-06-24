#define PI 3.1415926535897932384626433832795

#include <Wire.h>
#include <Adafruit_MotorShield.h>
#include <AccelStepper.h>

typedef float coord_t;

const float SPOOL_RAD = 5; // in mm
const int TICKS_PER_ROT = 200;
const float TICKS_PER_MM = TICKS_PER_ROT / (2 * PI * SPOOL_RAD);

// All of these in mm
const coord_t LEFT_X = 0;
const coord_t LEFT_Y = 1000;
const coord_t RIGHT_X = 500;
const coord_t RIGHT_Y = 1000;

// Also in mm
const coord_t OFFSET_X = 50;
const coord_t OFFSET_Y = 50;

const coord_t HOME_X = (LEFT_X + RIGHT_X) / 2;
const coord_t HOME_Y = RIGHT_Y / 2;

const coord_t DEF_VEL = static_cast<coord_t>(50 * TICKS_PER_MM);

Adafruit_MotorShield shield;
Adafruit_StepperMotor *leftStepper = shield.getStepper(TICKS_PER_ROT, 1);
Adafruit_StepperMotor *rightStepper = shield.getStepper(TICKS_PER_ROT, 2);

AccelStepper left([]() {
    leftStepper->step(FORWARD, DOUBLE);
}, []() {
    leftStepper->step(BACKWARD, DOUBLE);
});
AccelStepper right([]() {
    rightStepper->step(FORWARD, DOUBLE);
}, []() {
    rightStepper->step(BACKWARD, DOUBLE);
});

/**
    Calculates the target position of each stepper, in units of steps. x and y are in mm.
*/
void calculateTargetSteps(coord_t x, coord_t y, int& l1, int& l2) {
    l1 = static_cast<int>(sqrt(sq(x - OFFSET_X - LEFT_X) + sq(y + OFFSET_Y - LEFT_Y)) * TICKS_PER_MM);
    l2 = static_cast<int>(sqrt(sq(x + OFFSET_X - RIGHT_X) + sq(y + OFFSET_Y - RIGHT_Y)) * TICKS_PER_MM);
}

/**
    Calculates the current position of the marker, in units of mm
*/
void getPosition(coord_t& x, coord_t& y) {
    float l1 = left.currentPosition() / TICKS_PER_MM; // length of left cord
    float l2 = right.currentPosition() / TICKS_PER_MM; // length of right cord

    coord_t o = OFFSET_X * 2; // distance between anchor points
    coord_t w = RIGHT_X - LEFT_X; // width of whiteboard

    // derived from geometry/math
    float fx = sq(l1) - sq(l2) + sq(o) + sq(w) - 2 * o * w;
    fx /= 2 * (w - o);

    float fy = LEFT_Y - sqrt(sq(l1) - sq(fx));

    x = (coord_t)fx + OFFSET_X;
    y = (coord_t)fy - OFFSET_Y;
}

void zero() {
    int stepsToMove = static_cast<int>(TICKS_PER_MM * sqrt(sq(RIGHT_X - LEFT_X) + sq(RIGHT_Y)));
    left.move(-stepsToMove);
    left.setSpeed(DEF_VEL);
    left.runSpeedToPosition();
    right.move(-stepsToMove);
    right.setSpeed(DEF_VEL);
    right.runSpeedToPosition();

    left.setCurrentPosition(0);
    right.setCurrentPosition(0);

    int l, r;
    calculateTargetSteps(HOME_X, HOME_Y, l, r);
    left.move(l);
    left.setSpeed(DEF_VEL);
    left.runSpeedToPosition();
    right.move(r);
    right.setSpeed(DEF_VEL);
    right.runSpeedToPosition();
}

float getSpoolVel(float velX, float velY, float fromSpoolX, float fromSpoolY) {
    float mag = hypot(fromSpoolX, fromSpoolY);
    fromSpoolX /= mag;
    fromSpoolY /= mag;
    return velX * fromSpoolX + velY * fromSpoolY;
}

void setPosition(coord_t x, coord_t y, coord_t vel) {
    coord_t currX, currY;
    int lTarget, rTarget;
    calculateTargetSteps(x, y, lTarget, rTarget);
    bool done = left.currentPosition() == lTarget && right.currentPosition() == rTarget;
    while (!done) {
        getPosition(currX, currY);
        // create a velocity vector pointing in the desired direction
        float velX = x - currX;
        float velY = y - currY;
        float mag = hypot(velX, velY);
        velX *= vel / mag;
        velY *= vel / mag;

        // the ROC of the spool is the dot product of the velocity vector with the normalized vector from the spool to the anchor
        float fromLX = x - OFFSET_X - LEFT_X;
        float fromLY = y + OFFSET_Y - LEFT_Y;
        float leftVel = getSpoolVel(velX, velY, fromLX, fromLY);

        float fromRX = x + OFFSET_X - RIGHT_X;
        float fromRY = y + OFFSET_Y - RIGHT_Y;
        float rightVel = getSpoolVel(velX, velY, fromRX, fromRY);

        Serial.print("Pos=(" + String(currX) + "," + currY + "), Target=(" + x + "," + y + "), Speed=" + vel + ", ");
        Serial.print("Speeds: Left=");
        Serial.print(leftVel);
        Serial.print(", Right=");
        Serial.println(rightVel);

        bool lDone = left.currentPosition() == lTarget;
        bool rDone = right.currentPosition() == rTarget;

        if (lDone) {
            left.stop();
        } else {
            left.setSpeed(leftVel * TICKS_PER_MM);
            left.runSpeed();
        }

        if (rDone) {
            right.stop();
        } else {
            right.setSpeed(rightVel * TICKS_PER_MM);
            right.runSpeed();
        }
        done = lDone && rDone;
    }

    left.stop();
    right.stop();
}

void move(coord_t x, coord_t y, coord_t vel) {
    coord_t currX, currY;
    getPosition(currX, currY);
    setPosition(currX + x, currY + y, vel);
}

void setToolUp(bool up) {
    // TODO: Implement
}

void setup() {
    Serial.begin(9600);
    shield.begin();

    // configure motor inversions
    left.setPinsInverted(true, false, false);
    right.setPinsInverted(false, false, false);
}

coord_t targetX = 0;
coord_t targetY = 0;
coord_t targetZ = 0;

#define numParts 5
void loop() {
    // wait for data to become available
    while (Serial.available() == 0) {
        delay(10);
    }

    // read command from serial
    char command[20] = {};
    size_t len = Serial.readBytesUntil('\n', command, (sizeof command) - 1);
    command[len] = '\0'; // make sure string is null terminated
    Serial.print("Recieved command: ");
    Serial.println(command);

    // Tokenize into space delimited parts
    char* parts[numParts] = {};
    char* part = strtok(command, " \n");
    for (size_t i = 0; i < numParts && part != nullptr; i++) {
        parts[i] = part;
        part = strtok(nullptr, " \n");
    }

    // Execute g-code command
    char* type = parts[0];
    if (type == nullptr) {
        Serial.println("Invalid command!");
    } else if (strcmp(type, "G28") == 0) { // If it's a home command
        targetX = HOME_X;
        targetY = HOME_Y;
        targetZ = 0;
        setToolUp(true);
        zero();
    } else if (type[0] == 'G') {
        for (int i = 1; i <= 3 && parts[i] != nullptr; i++) {
            if (parts[i][0] == 'X') {
                targetX = static_cast<coord_t>(strtod(&parts[i][1], nullptr));
            } else if (parts[i][0] == 'Y') {
                targetY = static_cast<coord_t>(strtod(&parts[i][1], nullptr));
            } else if (parts[i][0] == 'Z') {
                targetZ = static_cast<coord_t>(strtod(&parts[i][1], nullptr));
            }
        }
        setToolUp(targetZ >= 0);
        long code = strtol(&type[1], nullptr, 10);
        if (code == 0 || code == 1) {
            setPosition(targetX, targetY, DEF_VEL);
        } else if (code == 2) {
            // CW circular interp
        } else if (code == 3) {
            // CCW circular interp
        } else {
            Serial.println("Unsupported g-code!");
        }
    } else {
        Serial.println("Invalid command!");
    }
}