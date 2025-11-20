// All the Libraries used in the program: 

#include <stdio.h>          // Standard input/output functions (printf, etc.)
#include <string.h>         // String handling functions (strlen, strncat, strcmp)
#include <time.h>           // Time-related functions (used here for random seed)
#include <stdlib.h>         // General utilities (rand, srand, malloc, free)
#include <pico/stdlib.h>    // Raspberry Pi Pico basics (GPIO, sleep, UART)
#include <math.h>           // Math functions (atan2f, sqrt, M_PI)
#include <FreeRTOS.h>       // FreeRTOS core functions for tasks and scheduling
#include <queue.h>          // FreeRTOS queues for safe task communication
#include <task.h>           // FreeRTOS task creation and management
#include "tkjhat/sdk.h"     // JTKJ Hat SDK (LEDs, buttons, display, buzzer)
#include <ctype.h>          // Character handling (toupper, isdigit, etc.)

// -------------------- Constants --------------------
// Basic timing and LED/serial definitions
#define DEFAULT_STACK_SIZE 2048
#define CDC_ITF_TX         1
#define UNIT               200   // base Morse timing unit in ms

// Morse timing units
#define DOT_UNITS          1
#define DASH_UNITS         3
#define LETTER_GAP         3
#define WORD_GAP           7

// Angle threshold for deciding dot/dash
#define ANGLE_THRESHOLD    10.0f

// Task priorities (higher = more important)
#define PRIORITY_SENSOR    3
#define PRIORITY_RECEIVE   2
#define PRIORITY_PRINT     1

// -------------------- Enums --------------------
// Program state machine for sending handling
enum state { WAITING = 0, RUNNING = 1 };
enum mode  { SENDING = 0, RECEIVING = 1, DECODING = 2 };

// Current program mode/state
enum state programState = WAITING;
enum mode  programMode  = SENDING;

// -------------------- Buffers --------------------
// Buffers for incoming ASCII and Morse data
char input_buffer[100];
char morse_string[600] = "";

// -------------------- FreeRTOS Queue --------------------
// Queue for sending IMU-detected Morse symbols between sensor and print tasks
QueueHandle_t morseQueue;

// -------------------- Morse Table --------------------
// Struct that maps ASCII symbols to their Morse strings
typedef struct {
    char symbol;
    const char* code;
} MorseEntry;

// Lookup table for Morse encoding/decoding. Done by AI.
const MorseEntry morse_table[] = {
    {'A', ".-"}, {'B', "-..."}, {'C', "-.-."}, {'D', "-.."}, {'E', "."},
    {'F', "..-."}, {'G', "--."}, {'H', "...."}, {'I', ".."}, {'J', ".---"},
    {'K', "-.-"}, {'L', ".-.."}, {'M', "--"}, {'N', "-."}, {'O', "---"},
    {'P', ".--."}, {'Q', "--.-"}, {'R', ".-."}, {'S', "..."}, {'T', "-"},
    {'U', "..-"}, {'V', "...-"}, {'W', ".--"}, {'X', "-..-"}, {'Y', "-.--"},
    {'Z', "--.."},
    {'0', "-----"}, {'1', ".----"}, {'2', "..---"}, {'3', "...--"}, {'4', "....-"},
    {'5', "....."}, {'6', "-...."}, {'7', "--..."}, {'8', "---.."}, {'9', "----."},
    {',', "--..--"}, {'?', "..--.."}, {'!', "-.-.--"}, {'\'', ".----."},
    {'/', "-..-."}, {'(', "-.--."}, {')', "-.--.-"}, {'&', ".-..."}, {':', "---..."},
    {';', "-.-.-."}, {'=', "-...-"}, {'+', ".-.-."}, {'_', "..--.-"},
    {'"', ".-..-."}, {'$', "...-..-"}, {'@', ".--.-."}
};

// -------------------- Function Headers --------------------
/** Convert character to Morse code string */
const char* to_morse(char c);

/** Encode ASCII string into Morse string */
void encode_to_morse(const char* input);

/** Convert Morse code string to ASCII character */
char from_morse(const char* code);

/** Decode Morse string into ASCII output */
void decode_from_morse(const char* morse_input, char* output_buffer);

/** Convert pitch angle to Morse symbol */
char morse_from_angle(float angle);

/** Play buzzer theme */
void play_theme(void);

/** Print Morse string to OLED, LED, and buzzer */
void print_morse_output(void);

/** Calculate pitch from accelerometer data */
float calculate_pitch(float ax, float ay, float az);

/** Button interrupt handler */
static void btn_fxn(uint gpio, uint32_t eventMask);

/** Sensor task: reads accelerometer and sends Morse symbols */
static void sensor_task(void *arg);

/** Receive task: handles user input (ASCII or Morse) */
void receive_task(void *arg);

/** Print task: consumes queue and outputs Morse */
static void print_task(void *arg);

/** Main entry point */
int main(void);


// -------------------- Functions --------------------

// Convert a single ASCII character to its Morse string
const char* to_morse(char c) {
    c = toupper((unsigned char)c);  // Convert lowercase to uppercase
    for (int i = 0; i < sizeof(morse_table) / sizeof(MorseEntry); i++) {
        if (morse_table[i].symbol == c) return morse_table[i].code; // Found match
    }
    return ""; // Return empty string if character not found
}

// Encode an entire ASCII string to Morse code
void encode_to_morse(const char* input) {
    morse_string[0] = '\0'; // Clear previous Morse string
    for (size_t i = 0; i < strlen(input); i++) {
        const char* morse = to_morse(input[i]); // Convert one char
        strncat(morse_string, morse, sizeof(morse_string) - strlen(morse_string) - 1); // Append Morse
        strncat(morse_string, " ", sizeof(morse_string) - strlen(morse_string) - 1);     // Space between letters
    }
}

// Convert a Morse string token to its ASCII character
char from_morse(const char* code) {
    for (int i = 0; i < sizeof(morse_table) / sizeof(MorseEntry); i++) {
        if (strcmp(morse_table[i].code, code) == 0) return morse_table[i].symbol; // Match found
    }
    return '?'; // Unknown symbol
}

// Decode a full Morse string into ASCII text
void decode_from_morse(const char* morse_input, char* output_buffer) {
    output_buffer[0] = '\0';  // Clear output buffer
    char token[10];            // Temporary buffer for one Morse letter
    size_t out_index = 0;
    const char* p = morse_input;

    while (*p) {
        size_t i = 0;
        // Extract one Morse token until space or end
        while (*p && *p != ' ' && i < sizeof(token) - 1) token[i++] = *p++;
        token[i] = '\0';
        if (i > 0) output_buffer[out_index++] = from_morse(token); // Convert token to ASCII

        // Count spaces to detect word boundaries
        int space_count = 0;
        while (*p == ' ') { space_count++; p++; }
        if (space_count >= 2 && out_index < sizeof(morse_string) - 1) output_buffer[out_index++] = ' '; // Add space
    }
    output_buffer[out_index] = '\0'; // Null-terminate final string
}

// Convert IMU pitch angle to Morse symbol
char morse_from_angle(float angle) {
    if (angle > ANGLE_THRESHOLD) return '-';   // Dash for positive tilt
    else if (angle < -ANGLE_THRESHOLD) return '.'; // Dot for negative tilt
    else return ' ';                           // Neutral, no symbol
}

// Play a short buzzer melody
void play_theme(void) {
    buzzer_play_tone(659, 150); sleep_ms(50);
    buzzer_play_tone(784, 150); sleep_ms(50);
    buzzer_play_tone(880, 150); sleep_ms(100);
    buzzer_play_tone(1046, 200); sleep_ms(100);
    buzzer_play_tone(880, 150); sleep_ms(50);
    buzzer_play_tone(784, 150); sleep_ms(50);
    buzzer_play_tone(659, 300);
}

// Print Morse string to terminal, OLED, and LED/buzzer
void print_morse_output(void) {
    if ((rand() % 6) + 1 == 6) play_theme();      // Randomly play melody
    printf("\nMorse word: %s\n", morse_string);   // Terminal output
    clear_display();
    write_text(morse_string);                      // OLED display output

    // Blink LED and apply timing for dots/dashes/spaces
    for (int i = 0; morse_string[i] != '\0'; i++) {
        if (morse_string[i] == '.') {
            toggle_led(); sleep_ms(UNIT * DOT_UNITS);
            toggle_led(); sleep_ms(UNIT);
        } else if (morse_string[i] == '-') {
            toggle_led(); sleep_ms(UNIT * DASH_UNITS);
            toggle_led(); sleep_ms(UNIT);
        } else if (morse_string[i] == ' ') {
            sleep_ms(UNIT * WORD_GAP);
        }
    }
}

// Calculate pitch angle from accelerometer readings
float calculate_pitch(float ax, float ay, float az) {
    return atan2f(ax, sqrtf(ay * ay + az * az)) * 180.0f / M_PI; // Convert to degrees
}

// Handles BUTTON1 and BUTTON2 presses
static void btn_fxn(uint gpio, uint32_t eventMask) {
    if (gpio == BUTTON1) {
        programState = RUNNING;          // Set state to running
        if (programMode != SENDING) {
            programMode = SENDING;       // Switch to sending mode
            morse_string[0] = '\0';      // Clear Morse buffer
        }
    }
    if (gpio == BUTTON2) {
        if (programMode == SENDING || programMode == DECODING) {
            programMode = RECEIVING;      // Switch to receiving mode
            printf("Now receiving, use ASCII\n");
        } else {
            programMode = DECODING;       // Switch to decoding mode
            printf("Now decoding, use Morse\n");
        }
    }
}

// -------------------- Sensor Task --------------------

// Reads IMU, calculates pitch, and sends Morse symbols
static void sensor_task(void *arg) {
    (void)arg;
    float ax, ay, az, gx, gy, gz, temp;

    // Initialize ICM-42670 sensor
    if (init_ICM42670() == 0) {
        printf("ICM-42670P initialized successfully!\n");
        if (ICM42670_start_with_default_values() != 0) {
            printf("ICM-42670P could not initialize accelerometer or gyroscope\n");
        }
    } else {
        printf("Failed to initialize ICM-42670P.\n");
    }

    for (;;) {
        if (programMode == SENDING) {
            ICM42670_read_sensor_data(&ax, &ay, &az, &gx, &gy, &gz, &temp); // Read sensor
            float pitch = calculate_pitch(ax, ay, az);                       // Calculate tilt
            char symbol = morse_from_angle(pitch);                          // Convert to dot/dash
            printf("Pitch: %.2f  Symbol: %c\n", pitch, symbol);

            // Send symbol to queue (with timeout)
            if (xQueueSend(morseQueue, &symbol, pdMS_TO_TICKS(100)) != pdPASS) {
                printf("Queue full, symbol dropped\n");  // Queue overflow
            }
        }
        vTaskDelay(pdMS_TO_TICKS(500)); // Wait before next reading
    }
}

// -------------------- Receive Task --------------------

// Handles incoming ASCII or Morse from workstation
void receive_task(void *arg) {
    (void)arg;
    size_t index = 0;

    while (1) {
        if (programMode == RECEIVING || programMode == DECODING) {
            int c = getchar_timeout_us(0);          // Non-blocking read
            if (c != PICO_ERROR_TIMEOUT) {
                if (c == '\r') continue;            // Ignore carriage return
                if (c == '\n') {                     // End of line
                    input_buffer[index] = '\0';      // Terminate string

                    if (programMode == RECEIVING) {
                        encode_to_morse(input_buffer); // Encode ASCII to Morse
                        print_morse_output();          // Show output
                    } else if (programMode == DECODING) {
                        decode_from_morse(input_buffer, morse_string); // Decode Morse to ASCII
                        printf("Decoded: %s\n", morse_string);
                        print_morse_output();          // Show output
                    }
                    index = 0;                        // Reset buffer
                } else if (index < sizeof(input_buffer) - 1) {
                    input_buffer[index++] = (char)c;  // Store character
                } else {
                    index = 0;                        // Prevent overflow
                }
            }
        }
        vTaskDelay(pdMS_TO_TICKS(10));               // Small delay to yield CPU
    }
}

// -------------------- Print Task --------------------

// Receives symbols from queue and prints them using LED/display/Serial
static void print_task(void *arg) {
    (void)arg;
    char symbol;

    for (;;) {
        if (xQueueReceive(morseQueue, &symbol, portMAX_DELAY) && programMode == SENDING) {
            if (programState == RUNNING) {
                programState = WAITING;             // Reset state after sending
                int length = strlen(morse_string);

                if (symbol != ' ') {
                    morse_string[length] = symbol;   // Append symbol
                    morse_string[length + 1] = '\0';
                    printf("Symbol: %c  Buffer: %s\n", symbol, morse_string);
                } else {
                    if (length > 0) {
                        print_morse_output();        // Print when space detected
                    }
                    morse_string[0] = '\0';         // Clear buffer
                }
            }
        }
    }
}

// -------------------- Main Function --------------------

// Program entry point
int main(void) {
    srand(time(NULL));            // Seed random number generator

    stdio_init_all();             // Initialize USB serial
    init_hat_sdk();               // Initialize JTKJ Hat
    sleep_ms(300);                // Wait for USB + Hat to initialize
    init_display();               // Initialize OLED display
    clear_display();              // Clear display at startup

    // Initialize onboard LED
    gpio_init(LED1);
    gpio_set_dir(LED1, GPIO_OUT);

    // Initialize buttons and attach interrupt handlers
    gpio_init(BUTTON1);
    gpio_init(BUTTON2);
    gpio_set_irq_enabled_with_callback(BUTTON1, GPIO_IRQ_EDGE_FALL, true, btn_fxn);
    gpio_set_irq_enabled_with_callback(BUTTON2, GPIO_IRQ_EDGE_FALL, true, btn_fxn);

    init_buzzer();                // Initialize buzzer

    // Create a FreeRTOS queue for Morse symbols
    morseQueue = xQueueCreate(10, sizeof(char));
    if (morseQueue == NULL) {
        printf("Failed to create Morse queue\n");
        return 1;
    }

    // Task handles
    TaskHandle_t hSensorTask, hPrintTask, hReceiveTask;

    // Create sensor task (reads IMU)
    if (xTaskCreate(sensor_task, "sensor", DEFAULT_STACK_SIZE, NULL,
                    PRIORITY_SENSOR, &hSensorTask) != pdPASS) {
        printf("Sensor task creation failed\n");
        return 0;
    }

    // Create print task (outputs Morse)
    if (xTaskCreate(print_task, "print", DEFAULT_STACK_SIZE, NULL,
                    PRIORITY_PRINT, &hPrintTask) != pdPASS) {
        printf("Print task creation failed\n");
        return 0;
    }

    // Create receive task (USB input)
    if (xTaskCreate(receive_task, "receive", DEFAULT_STACK_SIZE, NULL,
                    PRIORITY_RECEIVE, &hReceiveTask) != pdPASS) {
        printf("Receive task creation failed\n");
        return 0;
    }

    // Start FreeRTOS scheduler (will run tasks forever)
    vTaskStartScheduler();
    return 0; // Should never reach here
}
