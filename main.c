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
#include <ctype.h>          // Character handling (toupper, isdigit, etc.)v

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
// state machine and mode selection

enum state { WAITING = 0, RUNNING = 1 };           // Sending state machine
enum mode  { SENDING = 0, RECEIVING = 1, DECODING = 2 }; // Program mode selection
enum mode2 { OFF = 0, ON = 1 };                    // USB/UART link mode (OFF or ON)

enum state programState = WAITING;                 // Current sending state
enum mode  programMode  = SENDING;                 // Start in sending mode
enum mode2 uartMode = OFF;                          // UART communication initially OFF

// -------------------- Buffers --------------------
// Buffers for incoming ASCII and Morse data
char input_buffer[100];
char morse_string[600] = "";
char uart_rx_buffer[600];                           // Buffer to store received UART data

// -------------------- FreeRTOS Queue --------------------
// Queue for sending IMU-detected Morse symbols between sensor and print tasks
QueueHandle_t morseQueue;

// -------------------- Morse Table --------------------
// Struct that maps ASCII symbols to their Morse strings
typedef struct {
    char symbol;
    const char* code;
} MorseEntry;

// Lookup table for Morse encoding/decoding. Done by AI Tier2.
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

// -------------------- Function Headers -------------------- (TASK 1,2,3)
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

//Sends a string via UART to other connected Pico
void send_string_to_pico(const char *msg);

/** Main entry point */
int main(void);

// -------------------- Functions -------------------- (NEED TO ADD)

// Convert a single ASCII character to its Morse string,Tier2
const char* to_morse(char c) {
    c = (char)toupper((unsigned char)c);  // Convert lowercase to uppercase safely
    for (size_t i = 0; i < sizeof(morse_table) / sizeof(MorseEntry); i++) {
        if (morse_table[i].symbol == c) return morse_table[i].code; // Found match
    }
    return ""; // Return empty string if character not found
}

// Encode an entire ASCII string to Morse code,Tier 2
void encode_to_morse(const char* input) {
    morse_string[0] = '\0'; // Clear previous Morse string
    size_t in_len = strlen(input);
    for (size_t i = 0; i < in_len; i++) {
        char ch = input[i];

        // Treat whitespace in input as a word gap: decoder expects >=2 spaces for word boundary.
        if (isspace((unsigned char)ch)) {
            size_t rem = sizeof(morse_string) - strlen(morse_string) - 1;
            if (rem >= 2) {
                strncat(morse_string, "  ", rem); // add two spaces to mark word gap
            }
            continue;
        }

        const char* morse = to_morse(ch); // Convert one char
        if (morse[0] == '\0') {
            // Unknown character, skip it
            continue;
        }

        // Append Morse for the character, then a single space to separate letters.
        size_t rem = sizeof(morse_string) - strlen(morse_string) - 1;
        if (rem > 0) strncat(morse_string, morse, rem);
        rem = sizeof(morse_string) - strlen(morse_string) - 1;
        if (rem > 0) strncat(morse_string, " ", rem);
    }
}

// Convert a Morse string token to its ASCII character,Tier 2
char from_morse(const char* code) {
    for (int i = 0; i < sizeof(morse_table) / sizeof(MorseEntry); i++) {
        if (strcmp(morse_table[i].code, code) == 0) return morse_table[i].symbol; // Match found
    }
    return '?'; // Unknown symbol
}

// Decode a full Morse string into ASCII text ,Tier 2
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

// Convert IMU pitch angle to Morse symbol (Tier 1)
char morse_from_angle(float angle) {
    if (angle > ANGLE_THRESHOLD) return '-';   // Dash for positive tilt
    else if (angle < -ANGLE_THRESHOLD) return '.'; // Dot for negative tilt
    else return ' ';                           // Neutral, no symbol
}

// Play a short buzzer melody Tier2
void play_theme(void) {
    buzzer_play_tone(659, 150); vTaskDelay(pdMS_TO_TICKS(50));
    buzzer_play_tone(784, 150); vTaskDelay(pdMS_TO_TICKS(50));
    buzzer_play_tone(880, 150); vTaskDelay(pdMS_TO_TICKS(100));
    buzzer_play_tone(1046, 200); vTaskDelay(pdMS_TO_TICKS(100));
    buzzer_play_tone(880, 150); vTaskDelay(pdMS_TO_TICKS(50));
    buzzer_play_tone(784, 150); vTaskDelay(pdMS_TO_TICKS(50));
    buzzer_play_tone(659, 300);
}

// Send a string to the other Pico via UART (Tier 3)
void send_string_to_pico(const char *msg) {

    uart_puts(uart0, msg);     // Send full string over UART
    uart_putc(uart0, '\n');    // Add newline so receiver knows message ended

    printf("Sent to other Pico: %s\n", msg);  // Debug print to USB terminal
}
// Print Morse string to OLED, LED, and buzzer (Tier 2)
void print_morse_output(void) {
   if ((rand() % 3) == 0)  play_theme();
    printf("\nMorse word: %s\n", morse_string);
    clear_display();
    write_text(morse_string);                      // OLED display output

 for (int i = 0; morse_string[i] != '\0'; i++) {                    // Loop through each Morse symbol in the string
    if (morse_string[i] == '.') {
        toggle_led(); vTaskDelay(pdMS_TO_TICKS(UNIT * DOT_UNITS)); // LED on for dot duration
        toggle_led(); vTaskDelay(pdMS_TO_TICKS(UNIT));             // Quick LED off break
    } else if (morse_string[i] == '-') {
        toggle_led(); vTaskDelay(pdMS_TO_TICKS(UNIT * DASH_UNITS)); // LED on for dash duration
        toggle_led(); vTaskDelay(pdMS_TO_TICKS(UNIT));              // Quick LED off break
    } else if (morse_string[i] == ' ') {
        vTaskDelay(pdMS_TO_TICKS(UNIT * (WORD_GAP - 1)));          // Longer delay for space between words
    }
    }
}

// Calculate pitch angle from accelerometer readings Tier1
float calculate_pitch(float ax, float ay, float az) {
    return atan2f(ax, sqrtf(ay * ay + az * az)) * 180.0f / M_PI; // Convert to degrees
}
// -------------------- Button Interrupt Handler -------------------- (Tier 2 and 3)
// Handles BUTTON1 and BUTTON2 presses 
static void btn_fxn(uint gpio, uint32_t eventMask) {
    /* Button interrupt handler
     * - BUTTON1: start sending state (records next IMU symbols)
     * - BUTTON2: cycle through modes:
     *     SENDING -> RECEIVING  (accept ASCII from USB)
     *     RECEIVING -> DECODING (accept Morse from USB)
     *     DECODING -> enable UART/USB relay (uartMode = ON) and return to SENDING
     *     if uartMode == ON -> turn UART relay OFF
     *
     */
    /* Simple per-button debounce: ignore events that occur within
     * DEBOUNCE_MS milliseconds of the previous event for the same button.
     * This prevents multiple mode changes from a single physical press.
     */
        const uint32_t DEBOUNCE_MS = 200;
        static uint32_t last_press_time_button1 = 0;
        static uint32_t last_press_time_button2 = 0;
        uint32_t now = to_ms_since_boot(get_absolute_time());

        if (gpio == BUTTON1) {
            if ((now - last_press_time_button1) < DEBOUNCE_MS) return;
            last_press_time_button1 = now;

            /* Mark that we should record the next sequence of symbols.
             * Ensure we are in SENDING mode and clear any prior Morse buffer.
             */
            programState = RUNNING;
            if (programMode != SENDING) {
                programMode = SENDING;
                morse_string[0] = '\0';
            }
            return;
        }

        if (gpio == BUTTON2) {
            if ((now - last_press_time_button2) < DEBOUNCE_MS) return;
            last_press_time_button2 = now;

            /* Cycle modes when BUTTON2 is pressed:
             * - If currently sending and UART relay is off, switch to RECEIVING (ASCII input).
             * - If currently RECEIVING, switch to DECODING (Morse-to-text).
             * - If currently DECODING, enable UART relay and go to SENDING so IMU input
             *   will be sent to the other Pico.
             * - If UART relay is already ON, turn it OFF.
             */
            if (programMode == SENDING && uartMode == OFF) {
                programMode = RECEIVING;
                printf("Now receiving, use ASCII\n");
            } else if (programMode == RECEIVING) {
                programMode = DECODING;
                printf("Now decoding, use Morse\n");
            } else if (programMode == DECODING) {
                uartMode = ON;
                programMode = SENDING;
                printf("Now listening and sending to another pico device via UART\n");
            } else if (uartMode == ON) {
                /* Fallback case: turn UART relay off if it was on. */
                uartMode = OFF;
                printf("Not listening or sending to another device.\n");
            }
            return;
        }
    }

// -------------------- Sensor Task --------------------

// Reads IMU, calculates pitch, and sends Morse symbols (TASK 1)
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

// (Tier 2)
// ...existing code...
void receive_task(void *arg) {
    (void)arg;
    size_t usb_index = 0;   // USB (stdio) buffer write position
    size_t uart_index = 0;  // UART buffer write position

    // Ensure buffers are initialized
    input_buffer[0] = '\0';
    uart_rx_buffer[0] = '\0';

    while (1) {
        // --- USB input (commands / ASCII / Morse) ---
        {
            int c = getchar_timeout_us(0); // non-blocking read from USB stdio
            if (c != PICO_ERROR_TIMEOUT) {
                if (c == '\r') {
                    /* ignore CR */
                } else if (c == '\n') {
                    input_buffer[usb_index] = '\0';

                    // Global commands handled from USB regardless of mode
                    if (strcmp(input_buffer, ".clear") == 0) {
                        printf("\x1b[2J\x1b[H"); // ANSI clear
                        usb_index = 0;
                        input_buffer[0] = '\0';
                    } else if (strcmp(input_buffer, ".exit") == 0) {
                        printf("Exiting program...\n");
                        vTaskEndScheduler(); // attempt to stop FreeRTOS
                        exit(0);
                    } else if (programMode == RECEIVING) {
                        // ASCII -> Morse, support verbatim sections __...__
                        morse_string[0] = '\0';
                        const char *p = input_buffer;
                        while (*p) {
                            if (p[0] == '_' && p[1] == '_') {
                                p += 2;
                                while (*p && !(p[0] == '_' && p[1] == '_')) {
                                    size_t rem = sizeof(morse_string) - strlen(morse_string) - 1;
                                    if (rem > 0) strncat(morse_string, p, 1);
                                    p++;
                                }
                                if (*p) p += 2;
                                size_t rem = sizeof(morse_string) - strlen(morse_string) - 1;
                                if (rem > 0) strncat(morse_string, " ", rem);
                                continue;
                            }

                            if (isspace((unsigned char)*p)) {
                                size_t rem = sizeof(morse_string) - strlen(morse_string) - 1;
                                if (rem >= 2) strncat(morse_string, "  ", rem);
                                p++;
                                continue;
                            }

                            const char *m = to_morse(*p++);
                            if (m[0]) {
                                size_t rem = sizeof(morse_string) - strlen(morse_string) - 1;
                                if (rem > 0) strncat(morse_string, m, rem);
                                rem = sizeof(morse_string) - strlen(morse_string) - 1;
                                if (rem > 0) strncat(morse_string, " ", rem);
                            }
                        }
                        print_morse_output();
                        morse_string[0] = '\0';
                    } else if (programMode == DECODING) {
                        // Morse -> ASCII, support verbatim __...__
                        if (strstr(input_buffer, "__")) {
                            morse_string[0] = '\0';
                            const char *p = input_buffer;
                            while (*p) {
                                if (p[0] == '_' && p[1] == '_') {
                                    p += 2;
                                    while (*p && !(p[0] == '_' && p[1] == '_')) {
                                        size_t rem = sizeof(morse_string) - strlen(morse_string) - 1;
                                        if (rem > 0) strncat(morse_string, p, 1);
                                        p++;
                                    }
                                    if (*p) p += 2;
                                    continue;
                                }

                                char token[10] = {0};
                                size_t ti = 0;
                                while (*p && *p != ' ' && ti < sizeof(token) - 1) token[ti++] = *p++;
                                token[ti] = '\0';
                                if (ti > 0) {
                                    char dec = from_morse(token);
                                    size_t rem = sizeof(morse_string) - strlen(morse_string) - 1;
                                    if (rem > 0) {
                                        char tmp[2] = { dec, '\0' };
                                        strncat(morse_string, tmp, rem);
                                    }
                                }

                                int sp = 0;
                                while (*p == ' ') { sp++; p++; }
                                if (sp >= 2) {
                                    size_t rem = sizeof(morse_string) - strlen(morse_string) - 1;
                                    if (rem > 0) strncat(morse_string, " ", rem);
                                }
                            }
                        } else {
                            decode_from_morse(input_buffer, morse_string);
                        }
                        printf("Decoded: %s\n", morse_string);
                        print_morse_output();
                        morse_string[0] = '\0';
                    }

                    usb_index = 0;
                    input_buffer[0] = '\0';
                } else {
                    if (usb_index < sizeof(input_buffer) - 1) {
                        input_buffer[usb_index++] = (char)c;
                    } else {
                        // overflow: reset
                        usb_index = 0;
                        input_buffer[0] = '\0';
                    }
                }
            }
        }

        // --- UART input from other Pico (when enabled) ---
        if (uartMode == ON) {
            if (uart_is_readable(uart0)) {
                int c = uart_getc(uart0);
                if (c == '\r') {
                    /* ignore */
                } else if (c == '\n') {
                    uart_rx_buffer[uart_index] = '\0';
                    printf("Received from other Pico: %s\n", uart_rx_buffer);

                    // Copy safely into morse_string and display
                    strncpy(morse_string, uart_rx_buffer, sizeof(morse_string));
                    morse_string[sizeof(morse_string) - 1] = '\0';
                    print_morse_output();
                    morse_string[0] = '\0';

                    uart_index = 0;
                    uart_rx_buffer[0] = '\0';
                } else {
                    if (uart_index < sizeof(uart_rx_buffer) - 1) {
                        uart_rx_buffer[uart_index++] = (char)c;
                    } else {
                        uart_index = 0;
                        uart_rx_buffer[0] = '\0';
                    }
                }
            }
        }

        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
// ...existing
// -------------------- Print Task -------------------- (Tier 2 and 3)
static void print_task(void *arg) {
    (void)arg;                                   
    char symbol;                                 // Symbol received from the queue

    for (;;) {
        if (xQueueReceive(morseQueue, &symbol, portMAX_DELAY) && programMode == SENDING) {  // Wait for next Morse symbol
            if (programState == RUNNING) {       // Only record symbols when active
                programState = WAITING;          // Reset state until next button press
                int length = strlen(morse_string); // Current size of the Morse message

                if (symbol != ' ') {             // If it's a dot or dash
                    morse_string[length] = symbol;       // Add symbol to buffer
                    morse_string[length + 1] = '\0';     // Close string properly
                    printf("Symbol: %c  Buffer: %s\n", symbol, morse_string); // Debug output
                } else {                          // Space means "end of letter/word"
                    if (length > 0 && uartMode != ON) {   // If not in USB→UART mode
                        print_morse_output();     // Show Morse on LED/OLED/buzzer
                    } else if (length > 0 && uartMode == ON) {  // If UART sending is active
                        morse_string[length] = '\r';     // Add end marker for UART
                        send_string_to_pico(morse_string); // Send to the other Pico
                    }
                    morse_string[0] = '\0';       // Clear buffer for next message
                }
            }
        }
    }
}


// -------------------- Main Function -------------------- (Tier 1,2,3)

// Program entry point
int main(void) {
    srand(time(NULL));            // Seed random number generator

    stdio_init_all();             // Initialize USB serial
    init_hat_sdk();               // Initialize JTKJ Hat
    sleep_ms(300);                // Wait for USB + Hat to initialize
    init_display();               // Initialize OLED display
    clear_display();              // Clear display at startup

 gpio_init(LED1);                                      // Initialize onboard LED
gpio_set_dir(LED1, GPIO_OUT);                         // Set LED as output
gpio_init(BUTTON1);                                   // Initialize button 1
gpio_init(BUTTON2);                                   // Initialize button 2
gpio_set_irq_enabled_with_callback(BUTTON1, GPIO_IRQ_EDGE_FALL, true, btn_fxn); // Attach interrupt to button 1
gpio_set_irq_enabled_with_callback(BUTTON2, GPIO_IRQ_EDGE_FALL, true, btn_fxn); // Attach interrupt to button 2
init_buzzer();                                        // Initialize buzzer
uart_init(uart0, 115200);                             // Initialize UART0 at 115200 baud

// Configure GPIO pins for UART0
gpio_set_function(0, GPIO_FUNC_UART);                 // GP0 = TX
gpio_set_function(1, GPIO_FUNC_UART);                 // GP1 = RX

morseQueue = xQueueCreate(10, sizeof(char));         // Create a FreeRTOS queue to hold Morse symbols
if (morseQueue == NULL) {
    printf("Failed to create Morse queue\n");        // Error if queue creation fails
    return 1;
}

TaskHandle_t hSensorTask, hPrintTask, hReceiveTask;  // Task handles for FreeRTOS

// Create sensor task
if (xTaskCreate(sensor_task, "sensor", DEFAULT_STACK_SIZE, NULL,
                PRIORITY_SENSOR, &hSensorTask) != pdPASS) {
    printf("Sensor task creation failed\n");
    return 0;
}

// Create print/output task
if (xTaskCreate(print_task, "print", DEFAULT_STACK_SIZE, NULL,
                PRIORITY_PRINT, &hPrintTask) != pdPASS) {
    printf("Print task creation failed\n");
    return 0;
}

// Create receive/input task
if (xTaskCreate(receive_task, "receive", DEFAULT_STACK_SIZE, NULL,
                PRIORITY_RECEIVE, &hReceiveTask) != pdPASS) {
    printf("Receive task creation failed\n");
    return 0;
}

// Start FreeRTOS scheduler (this function never returns)
vTaskStartScheduler();
return 0; // Should never reach here
}
