
#include <stdio.h>
#include <string.h>

#include <pico/stdlib.h>
#include <math.h>

#include <FreeRTOS.h>
#include <queue.h>
#include <task.h>

#include "tkjhat/sdk.h"



#define DEFAULT_STACK_SIZE 2048
#define CDC_ITF_TX      1



enum state { WAITING=1,RUNNING=0};
enum state programState = WAITING;

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

// Calculate pitch from accelerometer data
float calculate_pitch(float ax, float ay, float az) {
    return atan2f(ax, sqrtf(ay * ay + az * az)) * 180.0f / M_PI;
}

static void btn_fxn(uint gpio, uint32_t eventMask) {
    if (gpio == BUTTON1 ) {
        programState = RUNNING;
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


        ICM42670_read_sensor_data(&ax, &ay, &az, &gx, &gy, &gz, &temp);
        float pitch = calculate_pitch(ax, ay, az);
        char symbol  = morse_from_angle(pitch);
        printf("Position: %.2f  symbol : %c\n", pitch , symbol);
        vTaskDelay(pdMS_TO_TICKS(500));
        xQueueSend(morseQueue, &symbol, portMAX_DELAY);


        // Do not remove this
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
// Print task
static void print_task(void *arg) {
    (void)arg;
    char symbol;

    while (1) {
        if (xQueueReceive(morseQueue, &symbol, portMAX_DELAY)) {
            if (programState == RUNNING) {
                programState = WAITING;
                int length = strlen(morse_string);

                if (symbol != ' ') {
                    morse_string[length] = symbol;
                    morse_string[length + 1] = '\0';
                    printf("Buffer: %s\n", morse_string);
                } else {
                    if (length > 0) {
                        printf("\nMorse word: %s\n", morse_string);
                        morse_string[0] = '\0';
                    }
                }
            } 
        }

        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}



int main() {

    stdio_init_all();
    init_hat_sdk();
    sleep_ms(300); //Wait some time so initialization of USB and hat is done.
    init_display();          // SSD1306 ready to use
    gpio_init(BUTTON1); 
    gpio_set_irq_enabled_with_callback(BUTTON1, GPIO_IRQ_EDGE_FALL, true, btn_fxn);              

    morseQueue = xQueueCreate(10, sizeof(char));
    if (morseQueue == NULL) {
        printf("Failed to create Morse queue\n");
        return 1;
    }
    TaskHandle_t hSensorTask, hPrintTask, hUSB = NULL;



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

    // Start the scheduler (never returns)
    vTaskStartScheduler();
    
    // Never reach this line.
    return 0;
}
