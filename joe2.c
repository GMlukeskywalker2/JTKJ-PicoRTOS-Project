
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




#define DEFAULT_STACK_SIZE 2048
#define CDC_ITF_TX      1
#define UNIT 200  // base time unit in milliseconds



enum state { WAITING=1,RUNNING=0};
enum state programState = WAITING;
enum mode { RECEIVING=1,SENDING=0};
enum mode programMode = SENDING;


char morse_string[100] = "";
QueueHandle_t morseQueue;

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
        if(programMode == RECEIVING){
            programMode = SENDING;
            morse_string[0] = '\0';
        }
    }

       if (gpio == BUTTON2 && programMode == SENDING) {
        programMode = RECEIVING;
        printf("Now receiving \n");
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
        if (programMode == RECEIVING) {
            int c = getchar_timeout_us(0);
            if (c != PICO_ERROR_TIMEOUT) {
                if (c == '\r') continue;
                if (c == '\n') {
                    morse_string[index] = '\0';
                    index = 0;
                    print();  // Only print after full line received
                } else if (index < sizeof(morse_string) - 1) {
                    morse_string[index++] = (char)c;
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
