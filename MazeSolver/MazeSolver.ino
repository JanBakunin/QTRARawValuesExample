#include <QTRSensors.h>
#include "Direction.h"

const int threshold = 400;

// pid loop vars
const float proportionalConst = 0.2f;
const float derivateConst = 1.0f;

const int maxMotorSpeed = 150;

Direction path[300];
unsigned int pathLength;
unsigned int pathPositionInLaterRun;

int drivePastDelay = 300; // tune value in mseconds motors will run past intersection to align wheels for turn NOT TESTED

const unsigned char ledPins[] = { 4, 5, 6, 7 };
unsigned char sensorPins[] = { 0, 1, 2, 3, 4, 5 };
QTRSensorsAnalog qtra(sensorPins, sizeof(sensorPins), 4, 2);
unsigned int sensorValues[sizeof(sensorPins)];

Direction direction = forward;

unsigned int position;
int lastError;

bool isEachDiversionOnCrossing[3];

long diversionCheckingStartTime;
bool isDiversionCheckRunning;

bool isFirstRun = true;


#pragma region "Initialization"
void setup()
{
	//init motor pins
	pinMode(12, OUTPUT);
	pinMode(13, OUTPUT);

	//init LEDs
	for (unsigned char i = 0; i < sizeof(ledPins); i++)
	{
		pinMode(ledPins[i], OUTPUT);
	}

	Serial.begin(9600);

	delay(500);

	calibrate();

	Serial.println('\n');
	delay(500);
}


void calibrate()
{
	// make half-turns to have values for black and white without holding it
	for (int i = 0; i <= 100; i++)
	{
		if (i == 0 || i == 60)
		{
			moveBothMotors(maxMotorSpeed, backward, maxMotorSpeed, forward);
		}

		else if (i == 20 || i == 100)
		{
			moveBothMotors(maxMotorSpeed, forward, maxMotorSpeed, backward);
		}

		qtra.calibrate();
	}

	while (sensorValues[2] < threshold)
	{
		position = qtra.readLine(sensorValues);
	}

	moveBothMotors(0, forward, 0, forward);

	// print the calibration minimum values measured when emitters were on
	for (int i = 0; i < sizeof(sensorPins); i++)
	{
		Serial.print(qtra.calibratedMinimumOn[i]);
		Serial.print(' ');
	}
	Serial.println();

	// print the calibration maximum values measured when emitters were on
	for (int i = 0; i < sizeof(sensorPins); i++)
	{
		Serial.print(qtra.calibratedMaximumOn[i]);
		Serial.print(' ');
	}

	delay(300);
}

#pragma endregion

void loop()
{
	// stop driving when 1 is received over serial port
	// and restart when 2 is received
	switch (Serial.read())
	{
	case '1':
		direction = none;
		break;
	case '2':
		direction = forward;
		break;
	}

	printSensorValues();

	// boilerplate code
	// TODO declare and find good pin
	// if button pressed start the simplified path
	if (digitalRead(/*unused pin*/ 10) == HIGH)
	{
		isFirstRun = false;
	}

	drive();
}

#pragma region "DrivingPart"

void drive()
{
	// update position and sensorValues
	position = qtra.readLine(sensorValues);

	// if the time for the last step before turn is over
	if (direction == diversionChecking
		&& millis() > diversionCheckingStartTime + drivePastDelay)
	{
		decideWhatDirection();
	}

	turnOffAllLeds();

	int motorSpeed;
	int posPropotionalToMid;

	switch (direction)
	{
	case diversionChecking:
		lightLed(1);

		moveBothMotors(maxMotorSpeed, forward, maxMotorSpeed, forward);
		checkForDiversions();
		break;

	case none:
		moveBothMotors(0, forward, 0, forward);
		break;

	case backward:
		lightLed(2);
		lightLed(3);

		moveBothMotors(maxMotorSpeed, forward, maxMotorSpeed, backward);
		checkForNewLineOnSide(right);
		path[pathLength] = backward;
		simplifyMaze();
		break;

	case left:
		lightLed(2);

		moveBothMotors(maxMotorSpeed, backward, maxMotorSpeed, forward);
		checkForNewLineOnSide(left);

		if (isFirstRun)
		{
			path[pathLength] = left;
			pathLength++;
			simplifyMaze();
		}
		else
		{
			pathPositionInLaterRun++;
		}
		break;

	case forward:
		lightLed(0);

		posPropotionalToMid = position - 2500;

		motorSpeed = proportionalConst * posPropotionalToMid + derivateConst * (posPropotionalToMid - lastError);
		lastError = posPropotionalToMid;

		moveBothMotors(maxMotorSpeed - motorSpeed, forward, maxMotorSpeed + motorSpeed, forward);

		checkForDiversions();
		if (isEachDiversionOnCrossing[left] || isEachDiversionOnCrossing[right])
		{
			if (isFirstRun)
			{
				direction = diversionChecking;
				startFurtherDiversionCheckingTime();
			}
			else
			{
				direction = path[pathPositionInLaterRun];
				pathPositionInLaterRun++;
			}
		}
		else if (isDeadEnd())
		{
			direction = backward;
		}

		if (isFirstRun)
		{
			path[pathLength] = forward;
			pathLength++;
			simplifyMaze();
		}
		break;

	case right:
		lightLed(3);

		moveBothMotors(maxMotorSpeed, forward, maxMotorSpeed, backward);
		checkForNewLineOnSide(right);

		if (isFirstRun)
		{
			path[pathLength] = right;
			pathLength++;
			simplifyMaze();
		}
		else
		{
			pathPositionInLaterRun++;
		}
		break;
	}
}

void lightLed(unsigned char index)
{
	digitalWrite(ledPins[index], HIGH);
}

void turnOffAllLeds()
{
	for (unsigned char i = 0; i < sizeof(ledPins); i++)
	{
		digitalWrite(ledPins[i], LOW);
	}
}

bool isDeadEnd()
{
	for (unsigned char i = 0; i < sizeof(sensorPins); i++)
	{
		if (sensorValues[i] > threshold)
		{
			return false;
		}
	}
	return true;
}

void checkForNewLineOnSide(Direction side)
{
	if (sensorValues[side == left ? 0 : sizeof(sensorPins) - 1] > threshold)
	{
		while (sensorValues[side == left ? 2 : sizeof(sensorPins) - 3] < threshold)
		{
			position = qtra.readLine(sensorValues);

			moveBothMotors(maxMotorSpeed, side == left ? backward : forward, maxMotorSpeed, side == left ? forward : backward);
		}

		direction = forward;
	}
}

void moveMotorOnSide(Direction side, Direction orientation, int speed)
{
	speed = max(min(speed, 180), 0);

	digitalWrite(side == left ? 13 : 12, orientation == forward ? HIGH : LOW);
	analogWrite(side == left ? 3 : 11, speed);
}

void moveBothMotors(int speedLeft, Direction orientationLeft, int speedRight, Direction orientationRight)
{
	moveMotorOnSide(left, orientationLeft, speedLeft);
	moveMotorOnSide(right, orientationRight, speedRight);
}

void checkForDiversions()
{
	if (sensorValues[sizeof(sensorPins) - 1] > threshold)
	{
		isEachDiversionOnCrossing[right] = true;
	}
	if (sensorValues[0] > threshold)
	{
		isEachDiversionOnCrossing[left] = true;
	}
}

void decideWhatDirection()
{
	// Check if there is a way up front
	for (unsigned char i = 1; i < sizeof(sensorPins) - 1; i++)
	{
		if (sensorValues[i] > threshold)
		{
			isEachDiversionOnCrossing[forward] = true;
			break;
		}
	}

	// Go left preferably
	if (isEachDiversionOnCrossing[left] == true)
	{
		direction = left;
	}
	else if (isEachDiversionOnCrossing[forward] == true)
	{
		direction = forward;
	}
	else
	{
		direction = right;
	}

	// Reset for next crossing
	for (unsigned char i = 0; i < sizeof(isEachDiversionOnCrossing); i++)
	{
		isEachDiversionOnCrossing[i] = false;
	}
}

void startFurtherDiversionCheckingTime()
{
	diversionCheckingStartTime = millis();
}

//LBR = B
//LBS = R
//RBL = B
//SBL = R
//SBS = B
//LBL = S
void simplifyMaze()
{
	if (pathLength < 3 || path[pathLength - 2] != backward)
	{
		return;
	}

	int totalAngle = 0;

	for (unsigned char i = 1; i <= 3; i++)
	{
		switch (path[pathLength - i])
		{
		case right:
			totalAngle += 90;
			break;
		case left:
			totalAngle += 270;
			break;
		case backward:
			totalAngle += 180;
			break;
		}
	}

	// Get the angle as a number between 0 and 360 degrees.
	totalAngle = totalAngle % 360;

	// Replace all of those turns with a single one.
	switch (totalAngle)
	{
	case 0:
		path[pathLength - 3] = forward;
		break;
	case 90:
		path[pathLength - 3] = right;
		break;
	case 180:
		path[pathLength - 3] = backward;
		break;
	case 270:
		path[pathLength - 3] = left;
		break;
	}

	// The path is now two steps shorter.
	pathLength -= 2;
}

#pragma endregion

void printSensorValues()
{
	for (unsigned char i = 0; i < sizeof(sensorPins); i++)
	{
		Serial.print(sensorValues[i]);
		Serial.print('\t');
	}

	Serial.print(" linePosition: ");
	Serial.println((int)position - 2500);
}

void printPath()
{
	Serial.println("+++++++++++++++++");
	for (unsigned int i = 0; i < pathLength; i++)
	{
		Serial.println(path[i]);
	}
	Serial.println("+++++++++++++++++");
}
