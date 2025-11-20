/**
 * Morse Code Project with FreeRTOS on Raspberry Pi Pico
 * -----------------------------------------------------
 * Features:
 *  - Encode ASCII text to Morse
 *  - Decode Morse back to ASCII
 *  - Use accelerometer pitch to generate Morse symbols
 *  - Output via LED, OLED, and buzzer
 *  - FreeRTOS tasks for sensor, input, and output
 *
 * Improvements:
 *  - Safer buffer handling
 *  - Clearer enums and constants
 *  - Proper task priorities
 *  - Queue handling with timeout
 *  - Function headers for clarity
 */

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

// -------------------- Constants --------------------
#define DEFAULT_STACK_SIZE 2048
#define CDC_ITF_TX         1
#define UNIT               200   // base time unit in ms

// Morse timing constants
#define DOT_UNITS          1
#define DASH_UNITS         3
#define LETTER_GAP         3
#define WORD_GAP           7

// Angle threshold for accelerometer input
#define ANGLE_THRESHOLD    10.0f

// Task priorities
#define PRIORITY_SENSOR    3
#define PRIORITY_RECEIVE   2
#define PRIORITY_PRINT     1

// -------------------- Enums --------------------
enum state { WAITING = 0, RUNNING = 1 };
enum mode  { SENDING = 0, RECEIVING = 1, DECODING = 2 };
enum mode2  { OFF = 0, ON = 1};

enum state programState = WAITING;
enum mode  programMode  = SENDING;
enum mode2  usbMode = OFF;

// -------------------- Buffers --------------------
char input_buffer[100];  
char morse_string[600] = "";
char uart_rx_buffer[600];
// -------------------- FreeRTOS Queue --------------------
QueueHandle_t morseQueue;

// -------------------- Morse Table --------------------
typedef struct {
    char symbol;
    const char* code;
} MorseEntry;

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

// -------------------- Function Implementations --------------------
const char* to_morse(char c) {
    c = toupper((unsigned char)c);
    for (int i = 0; i < sizeof(morse_table) / sizeof(MorseEntry); i++) {
        if (morse_table[i].symbol == c) return morse_table[i].code;
    }
    return ""; 
}

void encode_to_morse(const char* input) {
    morse_string[0] = '\0';
    for (size_t i = 0; i < strlen(input); i++) {
        const char* morse = to_morse(input[i]);
        strncat(morse_string, morse, sizeof(morse_string) - strlen(morse_string) - 1);
        strncat(morse_string, " ", sizeof(morse_string) - strlen(morse_string) - 1);
    }
}

char from_morse(const char* code) {
    for (int i = 0; i < sizeof(morse_table) / sizeof(MorseEntry); i++) {
        if (strcmp(morse_table[i].code, code) == 0) return morse_table[i].symbol;
    }
    return '?';
}

void decode_from_morse(const char* morse_input, char* output_buffer) {
    output_buffer[0] = '\0';
    char token[10];
    size_t out_index = 0;
    const char* p = morse_input;

    while (*p) {
        size_t i = 0;
        while (*p && *p != ' ' && i < sizeof(token) - 1) token[i++] = *p++;
        token[i] = '\0';
        if (i > 0) output_buffer[out_index++] = from_morse(token);

        int space_count = 0;
        while (*p == ' ') { space_count++; p++; }
        if (space_count >= 2 && out_index < sizeof(morse_string) - 1) output_buffer[out_index++] = ' ';
    }
    output_buffer[out_index] = '\0';
}

char morse_from_angle(float angle) {
    if (angle > ANGLE_THRESHOLD) return '-';
    else if (angle < -ANGLE_THRESHOLD) return '.';
    else return ' ';
}

void play_theme(void) {
    buzzer_play_tone(659, 150); vTaskDelay(pdMS_TO_TICKS(50));
    buzzer_play_tone(784, 150); vTaskDelay(pdMS_TO_TICKS(50));
    buzzer_play_tone(880, 150); vTaskDelay(pdMS_TO_TICKS(100));
    buzzer_play_tone(1046, 200); vTaskDelay(pdMS_TO_TICKS(100));
    buzzer_play_tone(880, 150); vTaskDelay(pdMS_TO_TICKS(50));
    buzzer_play_tone(784, 150); vTaskDelay(pdMS_TO_TICKS(50));
    buzzer_play_tone(659, 300);
}
// Send a string to the other Pico via UART
void send_string_to_pico(const char *msg) {
    // uart_puts automatically sends until '\0'
    uart_puts(uart0, msg);

    // Optionally send a newline so the other Pico knows the string ended
    uart_putc(uart0, '\n');

    printf("Sent to other Pico: %s\n", msg);
}
void print_morse_output(void) {
    if ((rand() % 6) + 1 == 6) play_theme();
    printf("\nMorse word: %s\n", morse_string);
    clear_display();
    write_text(morse_string);

    for (int i = 0; morse_string[i] != '\0'; i++) {
        if (morse_string[i] == '.') {
            toggle_led(); vTaskDelay(pdMS_TO_TICKS(UNIT * DOT_UNITS)); // FIX
            toggle_led(); vTaskDelay(pdMS_TO_TICKS(UNIT));            // FIX (Inter-element gap)
        } else if (morse_string[i] == '-') {
            toggle_led(); vTaskDelay(pdMS_TO_TICKS(UNIT * DASH_UNITS)); // FIX
            toggle_led(); vTaskDelay(pdMS_TO_TICKS(UNIT));             // FIX (Inter-element gap)
        } else if (morse_string[i] == ' ') {
            // Word gap: 7 units total, 1 unit is already accounted for by the preceding element
            vTaskDelay(pdMS_TO_TICKS(UNIT * (WORD_GAP - 1)));         // FIX 
        }
    }
}

float calculate_pitch(float ax, float ay, float az) {
    return atan2f(ax, sqrtf(ay * ay + az * az)) * 180.0f / M_PI;
}

static void btn_fxn(uint gpio, uint32_t eventMask) {
    if (gpio == BUTTON1) {
        programState = RUNNING;
        if (programMode != SENDING) {
            programMode = SENDING;
            morse_string[0] = '\0';
        }
    }
    if (gpio == BUTTON2) {
        if (programMode == SENDING && usbMode==OFF) {
            programMode = RECEIVING;
            printf("Now receiving, use ASCII\n");
        } else if(programMode==RECEIVING) {
            programMode = DECODING;
            printf("Now decoding, use Morse\n");
        }else if(programMode==DECODING){
            usbMode = ON;
            programMode=SENDING;
            printf("Now listening and sending to another pico device via UART\n");
        }else if(usbMode==ON){
            usbMode=OFF;
            printf("Not listening or sending to another device.\n");
        }
    }
}

static void sensor_task(void *arg) {
    (void)arg;
    float ax, ay, az, gx, gy, gz, temp;

    // Initialize sensor
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
            ICM42670_read_sensor_data(&ax, &ay, &az, &gx, &gy, &gz, &temp); 
            float pitch = calculate_pitch(ax, ay, az);
            printf("X:%f,Y:%f,Z:%f \n",ax,ay,az);
            char symbol = morse_from_angle(pitch);
            printf("Pitch: %.2f  Symbol: %c\n", pitch, symbol);

            // Send symbol to queue safely
            if (xQueueSend(morseQueue, &symbol, pdMS_TO_TICKS(100)) != pdPASS) {
                printf("Queue full, symbol dropped\n");
            }
        }
        vTaskDelay(pdMS_TO_TICKS(500)); // consistent delay
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
                    input_buffer[index] = '\0';

                    if (programMode == RECEIVING) {
                        encode_to_morse(input_buffer);
                        print_morse_output();
                    } else if (programMode == DECODING) {
                        decode_from_morse(input_buffer, morse_string);
                        printf("Decoded: %s\n", morse_string);
                        print_morse_output();
                    }
                    index = 0;
                } else if (index < sizeof(input_buffer) - 1) {
                    input_buffer[index++] = (char)c;
                } else {
                    index = 0; // overflow protection
                }
            }
        }
        // ---- NEW: USB mode via UART ----
        if (usbMode == ON) {
            if (uart_is_readable(uart0)) {
                int c = uart_getc(uart0);
                if (c == '\r') continue;
                if (c == '\n') {
                    uart_rx_buffer[index] = '\0'; // Use the dedicated buffer
                    printf("Received from other Pico: %s\n", uart_rx_buffer);
                    
                    // To display/output received data, you must copy it
                    strncpy(morse_string, uart_rx_buffer, sizeof(morse_string));
                    morse_string[sizeof(morse_string) - 1] = '\0';
                    
                    print_morse_output(); // Output the received Morse string

                    index = 0;
                } else if (index < sizeof(uart_rx_buffer) - 1) {
                    uart_rx_buffer[index++] = (char)c;
                } else {
                    index = 0; // overflow protection
                }
            }
}
    
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

static void print_task(void *arg) {
    (void)arg;
    char symbol;

    for (;;) {
        if (xQueueReceive(morseQueue, &symbol, portMAX_DELAY) && programMode == SENDING) {
            if (programState == RUNNING) {
                programState = WAITING;
                int length = strlen(morse_string);

                if (symbol != ' ') {
                    morse_string[length] = symbol;
                    morse_string[length + 1] = '\0';
                    printf("Symbol: %c  Buffer: %s\n", symbol, morse_string);
                } else {
                    if (length > 0 && usbMode!=ON) {
                        print_morse_output();
                    }else if (length > 0 && usbMode==ON){
                        morse_string[length]= '\r';
                        send_string_to_pico(morse_string);

                    }
                    morse_string[0] = '\0';
                }
            }
        }
    }
}

int main(void) {
    srand(time(NULL)); // seed RNG

    stdio_init_all();
    init_hat_sdk();
    sleep_ms(300); // allow USB + hat init
    init_display();
    clear_display();
    gpio_init(LED1);
    gpio_set_dir(LED1, GPIO_OUT);
    gpio_init(BUTTON1);
    gpio_init(BUTTON2);
    gpio_set_irq_enabled_with_callback(BUTTON1, GPIO_IRQ_EDGE_FALL, true, btn_fxn);
    gpio_set_irq_enabled_with_callback(BUTTON2, GPIO_IRQ_EDGE_FALL, true, btn_fxn);
    init_buzzer();
    uart_init(uart0, 115200);

    // Configure GPIO pins for UART0
    gpio_set_function(0, GPIO_FUNC_UART); // GP0 = TX
    gpio_set_function(1, GPIO_FUNC_UART); // GP1 = RX


    morseQueue = xQueueCreate(10, sizeof(char));
    if (morseQueue == NULL) {
        printf("Failed to create Morse queue\n");
        return 1;
    }

    TaskHandle_t hSensorTask, hPrintTask, hReceiveTask;

    if (xTaskCreate(sensor_task, "sensor", DEFAULT_STACK_SIZE, NULL,
                    PRIORITY_SENSOR, &hSensorTask) != pdPASS) {
        printf("Sensor task creation failed\n");
        return 0;
    }

    if (xTaskCreate(print_task, "print", DEFAULT_STACK_SIZE, NULL,
                    PRIORITY_PRINT, &hPrintTask) != pdPASS) {
        printf("Print task creation failed\n");
        return 0;
    }

    if (xTaskCreate(receive_task, "receive", DEFAULT_STACK_SIZE, NULL,
                    PRIORITY_RECEIVE, &hReceiveTask) != pdPASS) {
        printf("Receive task creation failed\n");
        return 0;
    }

    // Start scheduler (never returns)
    vTaskStartScheduler();
    return 0; // should never reach
}
