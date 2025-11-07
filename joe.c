#include <stdio.h>

enum state { WAITING=1,RUNNING=0};
enum state programState = WAITING;

// Tehtävä 3: Valoisuuden globaali muuttuja
// Exercise 3: Global variable for ambient light
uint32_t position ;
int main() {
    char joe[5] = "mama";
    printf("Hello, %s !\n",joe);
    printf("joe %s is %d  \n",joe,43)  ;
   
    return 0;
}
// teri ma ka bsdk
/* helllaaaaaa bitches */

void sensorYask() {
    // Function implementation goes here
    position = get_data();
    printf("Program is running. Position: %u\n", postion);
    delay(250);

}


void buttonTask() {
    // Function implementation goes here
    if (is_button_pressed()) {
        programState = RUNNING;
    }
}

void printTask() {
    // Function implementation goes here
    if (programState == RUNNING) {
        programState = WAITING;
        char symbol = morse_converter(position);
        printf("Program is running. Position: %c\n", symbol);
    } else {
        printf("Program is waiting.\n");
    }
}

char morse_converter(uint32_t pos) {
    // Function implementation goes here
    // Convert position to Morse code symbol
    if(pos > 120) {
        return '.'; // Placeholder return value
    }else if(pos >240) {
        return '-'; // Placeholder return value
    }else {
        return ' '; // Placeholder return value
    }
}