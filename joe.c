
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <stdlib.h>
#include <pico/stdlib.h>
#include <math.h>

#include <FreeRTOS.h>
#include <queue.h>
#include <task.h>

#include "tkjhat/sdk.h"

#include <ctype.h>




#define DEFAULT_STACK_SIZE 2048
#define CDC_ITF_TX      1
#define UNIT 200  // base time unit in milliseconds



enum state { WAITING=1,RUNNING=0};
enum state programState = WAITING;
enum mode { RECEIVING=1,SENDING=0,DECODING=2};
enum mode programMode = SENDING;


char input_buffer[100];  // Temporary input buffer
char morse_string[600] = "";
//char decoded_buffer[256];  // Adjust size as needed

QueueHandle_t morseQueue;

typedef struct {
    char symbol;
    const char* code;
} MorseEntry;

const MorseEntry morse_table[] = {
    // Letters
    {'A', ".-"},    {'B', "-..."},  {'C', "-.-."},  {'D', "-.."},   {'E', "."},
    {'F', "..-."},  {'G', "--."},   {'H', "...."},  {'I', ".."},    {'J', ".---"},
    {'K', "-.-"},   {'L', ".-.."},  {'M', "--"},    {'N', "-."},    {'O', "---"},
    {'P', ".--."},  {'Q', "--.-"},  {'R', ".-."},   {'S', "..."},   {'T', "-"},
    {'U', "..-"},   {'V', "...-"},  {'W', ".--"},   {'X', "-..-"},  {'Y', "-.--"},
    {'Z', "--.."},

    // Digits
    {'0', "-----"}, {'1', ".----"}, {'2', "..---"}, {'3', "...--"}, {'4', "....-"},
    {'5', "....."}, {'6', "-...."}, {'7', "--..."}, {'8', "---.."}, {'9', "----."},

    // Punctuation
    {',', "--..--"}, {'?', "..--.."}, {'!', "-.-.--"}, {'\'', ".----."},
    {'/', "-..-."},  {'(', "-.--."},  {')', "-.--.-"}, {'&', ".-..."},  {':', "---..."},
    {';', "-.-.-."}, {'=', "-...-"},  {'+', ".-.-."}, {'_', "..--.-"},
    {'"', ".-..-."}, {'$', "...-..-"}, {'@', ".--.-."}
};
//// Convert character to Morse codem,returns a cahr that is in morse
const char* to_morse(char c);

// Convert character to Morse code
const char* to_morse(char c) {
    c = toupper((unsigned char)c);
    for (int i = 0; i < sizeof(morse_table) / sizeof(MorseEntry); i++) {
        if (morse_table[i].symbol == c) {
            return morse_table[i].code;
        }
    }
    return ""; // unsupported character
}



// Encode input_buffer into morse_string
void encode_to_morse(const char* input) {
    morse_string[0] = '\0';  // Clear previous content
    for (size_t i = 0; i < strlen(input); i++) {
        const char* morse = to_morse(input[i]);
        strcat(morse_string, morse);
        strcat(morse_string, " ");  // Space between Morse characters
    }
}

// Convert Morse code to character
char from_morse(const char* code) {
    for (int i = 0; i < sizeof(morse_table) / sizeof(MorseEntry); i++) {
        if (strcmp(morse_table[i].code, code) == 0) {
            return morse_table[i].symbol;
        }
    }
    return '?'; // Unknown Morse code
}

void decode_from_morse(const char* morse_input, char* output_buffer) {
    output_buffer[0] = '\0';
    char token[10];
    size_t out_index = 0;

    const char* p = morse_input;
    while (*p) {
        size_t i = 0;

        // Extract Morse token
        while (*p && *p != ' ' && i < sizeof(token) - 1) {
            token[i++] = *p++;
        }
        token[i] = '\0';

        if (i > 0) {
            output_buffer[out_index++] = from_morse(token);
        }

        // Count spaces
        int space_count = 0;
        while (*p == ' ') {
            space_count++;
            p++;
        }

        // If two or more spaces, insert a space between words
        if (space_count >= 2 && out_index < sizeof(morse_string) - 1) {
            output_buffer[out_index++] = ' ';
        }
    }

    output_buffer[out_index] = '\0';
}

// Convert pitch angle to Morse symbol
char morse_from_angle(float angle) {
    if (angle > 10) {
        return '-';
    } else if (angle < -10) {
        return '.';
    } else {
        return ' ';
    }
}
void play_theme() {
    buzzer_play_tone(659, 150);  // E5
    sleep_ms(50);
    buzzer_play_tone(784, 150);  // G5
    sleep_ms(50);
    buzzer_play_tone(880, 150);  // A5
    sleep_ms(100);
    buzzer_play_tone(1046, 200); // C6
    sleep_ms(100);
    buzzer_play_tone(880, 150);  // A5
    sleep_ms(50);
    buzzer_play_tone(784, 150);  // G5
    sleep_ms(50);
    buzzer_play_tone(659, 300);  // E5
}
void print() {
        if((rand() % 6) + 1 == 6){
            play_theme(); 
                              }
                        printf("\nMorse word: %s\n", morse_string);
                          // OLED DISPLAY
                        clear_display();
                        write_text(morse_string);
                             for (int i = 0; morse_string[i] != '\0'; i++) {
                                if (morse_string[i] == '.') {
                                    printf("\nDot at index %d\n", i);
                                    toggle_led();           // LED ON
                                    sleep_ms(UNIT);         // 1 unit ON
                                    toggle_led();           // LED OFF
                                    sleep_ms(UNIT);         // 1 unit gap
                                    printf("\nLED blinked for dot\n");
                                } else if (morse_string[i]  == '-') {
                                    printf("\nDash at index %d\n", i);
                                    toggle_led();           // LED ON
                                    sleep_ms(UNIT * 3);     // 3 units ON
                                    toggle_led();           // LED OFF
                                    sleep_ms(UNIT);         // 1 unit gap
                                    printf("\nLED blinked for dash\n");

                            }else if (morse_string[i] == ' ') {
            printf("\nSpace at index %d\n", i);
            sleep_ms(UNIT * 3);     // Inter-character gap
        }
    }
}

// Calculate pitch from accelerometer data
float calculate_pitch(float ax, float ay, float az) {
    return atan2f(ax, sqrtf(ay * ay + az * az)) * 180.0f / M_PI;
}

static void btn_fxn(uint gpio, uint32_t eventMask) {
    if (gpio == BUTTON1 ) {
        programState = RUNNING;
        if(programMode != SENDING){
            programMode = SENDING;
            morse_string[0] = '\0';
        }
    }

       if (gpio == BUTTON2 ) {
        if(programMode == SENDING || programMode==DECODING){
            programMode = RECEIVING;
        printf("Now receiving \n");
        }else{
        programMode = DECODING;
        printf("Now Decoding \n");
        }
    }

}

static void sensor_task(void *arg){
    (void)arg;
    float ax, ay, az, gx, gy, gz, temp;
        // Setting up the sensor. 
    if (init_ICM42670() == 0) {
        printf("ICM-42670P initialized successfully!\n");
        if (ICM42670_start_with_default_values() != 0){
            printf("ICM-42670P could not initialize accelerometer or gyroscope");
        }
        /*int _enablegyro = ICM42670_enable_accel_gyro_ln_mode();
        printf ("Enable gyro: %d\n",_enablegyro);
        int _gyro = ICM42670_startGyro(ICM42670_GYRO_ODR_DEFAULT, ICM42670_GYRO_FSR_DEFAULT);
        printf ("Gyro return:  %d\n", _gyro);
        int _accel = ICM42670_startAccel(ICM42670_ACCEL_ODR_DEFAULT, ICM42670_ACCEL_FSR_DEFAULT);
        printf ("Accel return:  %d\n", _accel);*/
    } else {
        printf("Failed to initialize ICM-42670P.\n");
    }

   
    for(;;){

    if(programMode == SENDING){
        ICM42670_read_sensor_data(&ax, &ay, &az, &gx, &gy, &gz, &temp);
        //printf("x:%f,y:%f,z:%f,temp:%f \n",ax,ay,az,temp);
        float pitch = calculate_pitch(ax, ay, az);
        char symbol  = morse_from_angle(pitch);
        printf("Position: %.2f  symbol : %c\n", pitch , symbol);
        vTaskDelay(pdMS_TO_TICKS(500));
        xQueueSend(morseQueue, &symbol, portMAX_DELAY);
    }


        // Do not remove this
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

void receive_task(void *arg) {
    (void)arg;
    size_t index = 0;

    while (1) {
        if (programMode == RECEIVING || programMode == DECODING) {
            int c = getchar_timeout_us(0);
            if (c != PICO_ERROR_TIMEOUT) {
                if (c == '\r') continue;
                if (c == '\n') {
                    input_buffer[index] = '\0';  // End input string

                    if (programMode == RECEIVING) {
                        encode_to_morse(input_buffer);  // Convert to Morse
                        print();  // Output morse_string
                    } else if (programMode == DECODING) {
                        decode_from_morse(input_buffer, morse_string);  // Convert Morse to text
                        printf("Decoded: %s\n", morse_string);  // Output decoded text
                        print();
                    }

                    index = 0;
                } else if (index < sizeof(input_buffer) - 1) {
                    input_buffer[index++] = (char)c;
                } else {
                    index = 0;  // overflow protection
                }
            }
        }

        vTaskDelay(pdMS_TO_TICKS(10));
    }
}


// Print task
// Print task: updates OLED, LED, buffer
static void print_task(void *arg) {
    (void)arg;
    char symbol;

    for (;;) {
        if (xQueueReceive(morseQueue, &symbol, portMAX_DELAY) && programMode == SENDING) {

            if (programState == RUNNING) {
                programState = WAITING;
                int length = strlen(morse_string);


                // Append symbol to string
                if (symbol != ' ') {
                    morse_string[length] = symbol;
                    morse_string[length + 1] = '\0';
                  // Print to serial
                    printf("Symbol: %c  Buffer: %s\n", symbol, morse_string);
              
                } else {
                    if (length > 0) {
                        print();
                        }
                        morse_string[0] = '\0';

                    }
                }

                }
            }
        

        }


        

int main() {
     // Seed the random number generator with current time
    srand(time(NULL));

    stdio_init_all();
    init_hat_sdk();
    sleep_ms(300); //Wait some time so initialization of USB and hat is done.
    init_display();          // SSD1306 ready to use
    clear_display();
    gpio_init(LED1);
    gpio_set_dir(LED1, GPIO_OUT);    
    gpio_init(BUTTON1); 
    gpio_init(BUTTON2); 
    gpio_set_irq_enabled_with_callback(BUTTON1, GPIO_IRQ_EDGE_FALL, true, btn_fxn);    
    gpio_set_irq_enabled_with_callback(BUTTON2, GPIO_IRQ_EDGE_FALL, true, btn_fxn);    
    init_buzzer();      

    morseQueue = xQueueCreate(10, sizeof(char));
    if (morseQueue == NULL) {
        printf("Failed to create Morse queue\n");
        return 1;
    }
    TaskHandle_t hSensorTask, hPrintTask,hReceiveTask, hUSB = NULL;



    // Create the tasks with xTaskCreate
    BaseType_t result = xTaskCreate(sensor_task, // (en) Task function
                "sensor",                        // (en) Name of the task 
                DEFAULT_STACK_SIZE,              // (en) Size of the stack for this task (in words). Generally 1024 or 2048
                NULL,                            // (en) Arguments of the task 
                2,                               // (en) Priority of this task
                &hSensorTask);                   // (en) A handle to control the execution of this task

    if(result != pdPASS) {
        printf("Sensor task creation failed\n");
        return 0;
    }
    result = xTaskCreate(print_task,  // (en) Task function
                "print",              // (en) Name of the task 
                DEFAULT_STACK_SIZE,   // (en) Size of the stack for this task (in words). Generally 1024 or 2048
                NULL,                 // (en) Arguments of the task 
                2,                    // (en) Priority of this task
                &hPrintTask);         // (en) A handle to control the execution of this task

    if(result != pdPASS) {
        printf("Print Task creation failed\n");
        return 0;
    }
        result = xTaskCreate(receive_task,  // (en) Task function
                "receive",             // (en) Name of the task 
                DEFAULT_STACK_SIZE,   // (en) Size of the stack for this task (in words). Generally 1024 or 2048
                NULL,                 // (en) Arguments of the task 
                2,                    // (en) Priority of this task
                &hReceiveTask);         // (en) A handle to control the execution of this task

    // Start the scheduler (never returns)
    vTaskStartScheduler();
    
    // Never reach this line.
    return 0;
}


