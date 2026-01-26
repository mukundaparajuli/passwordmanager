#include <Arduino.h>

// function declaration
int myFunction(int, int);

void setup() {
  // initialize serial first
  Serial.begin(9600);

  // call the function
  int result = myFunction(2, 3);

  // print the result
  Serial.print("The result is: ");
  Serial.println(result);
}

void loop() {
  // nothing here for now
}

// function definition
int myFunction(int x, int y) { return x + y; }
