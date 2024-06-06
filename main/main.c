#include <stdio.h>
#include <stdbool.h>
#include "nvs_flash.h"
#include "esp_websocket_client.h"
#include <cJSON.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "driver/gpio.h"
#include <inttypes.h>
#include <time.h>
#include "DFRobotDFPlayerMini.h"
#include "Wifi.h"

static char *TAG = "main";

#define Previous 38    // Play/Pause
#define PP 2
#define Next 36
#define BLINK_GPIO 15

int test = -1; 
int mm = 0,ss = 0;
bool dem = false;

bool isPlaying = false;
bool isRepeat = false;
bool isShuffle = false;
bool isBtnOccur = false;

uint8_t buttonPP_previous = 0;
uint8_t buttonNext_previous = 0;
uint8_t buttonPrevious_previous = 1;

uint8_t curAudioId = 0;
char **audio;
uint8_t lengthAudio = 0;
uint64_t timer = 0;

esp_websocket_client_handle_t client;
uint16_t counter = 0;
uint16_t counterLim = 0;
esp_timer_handle_t periodic_timer;
SemaphoreHandle_t timerSemaphore;
portMUX_TYPE timerMux = portMUX_INITIALIZER_UNLOCKED;
bool isTimerRunning = false;

void websocket_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data);
void websocket_app_start();
cJSON *payload(char *action, bool all);
void sendRequest(char *action, bool all);
void handleAction(char *action, uint64_t timeStamp);
static void periodic_timer_callback(void* arg);
void checkStateTask();
void ButtonOccur();
void generateAudioName();

void app_main(void)
{
    //Config button
    // Input
    gpio_set_direction(Next,GPIO_MODE_INPUT);
	gpio_set_direction(PP,GPIO_MODE_INPUT);
	gpio_set_direction(Previous,GPIO_MODE_INPUT);
    gpio_set_direction(BLINK_GPIO,GPIO_MODE_OUTPUT);

    // Set input pullup
    gpio_set_pull_mode(Next, GPIO_PULLUP_ONLY);
    gpio_set_pull_mode(PP, GPIO_PULLUP_ONLY);
    gpio_set_pull_mode(Previous, GPIO_PULLUP_ONLY);
    //-------------------------------------    

    //Check UART DF
    vTaskDelay(500/ portTICK_PERIOD_MS);
    while(1) {
		bool ret = DF_begin(17, 18, true); //TX: 17, RX: 18
        vTaskDelay(500/ portTICK_PERIOD_MS);
		ESP_LOGI(TAG, "DF_begin=%d", ret);
		if (ret) break;
		vTaskDelay(200/ portTICK_PERIOD_MS);
	}
    // Check done ----------------------------------------

    //Config DFP
    vTaskDelay(300/ portTICK_PERIOD_MS);
    ESP_LOGI(TAG, "DFPlayer Mini online.");
    generateAudioName();
    vTaskDelay(300/ portTICK_PERIOD_MS);
	DF_volume(30); //Set volume value. From 0 to 30

    vTaskDelay(100/ portTICK_PERIOD_MS);
    DF_EQ(DFPLAYER_EQ_POP);

    vTaskDelay(100/ portTICK_PERIOD_MS);
    //------------------------------------

    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK( err );
    ESP_ERROR_CHECK(esp_netif_init());

    //timer config
    const esp_timer_create_args_t periodic_timer_args = {
            .callback = &periodic_timer_callback,
            .name = "periodic"
    };

    //timer, semephore timer 
    ESP_ERROR_CHECK(esp_timer_create(&periodic_timer_args, &periodic_timer));
    timerSemaphore = xSemaphoreCreateBinary();

    //require event loop to check wifi and websocket event
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    
    //Start websocket
    const char *gateway = "ws://nhacnkd.online:8080";
    initialise_wifi();
    vTaskDelay(2000/ portTICK_PERIOD_MS);
    websocket_app_start(gateway);\
    xTaskCreate(ButtonOccur, "Xu_ly_nut_nhan",4096, NULL, 1, NULL);
    xTaskCreate(checkStateTask, "checkStateTask", 2048, NULL, 1, NULL);
    
}

void generateAudioName() {
    vTaskDelay(200 / portTICK_PERIOD_MS);
    lengthAudio = 255;
    DF_play(lengthAudio);

    vTaskDelay(200 / portTICK_PERIOD_MS);
        int x = DF_readCurrentFileNumber(DFPLAYER_DEVICE_SD);
        if(x!= -1){
            lengthAudio = x; //Lay lai bai hat
        }

        //----------------------------------
        vTaskDelay(200 / portTICK_PERIOD_MS);
        DF_play(0);
        isPlaying = true;
        isBtnOccur = true;
        //--------------------------
        
    vTaskDelay(100 / portTICK_PERIOD_MS);
	ESP_LOGI(TAG, "Num audio file: %d", lengthAudio);

    audio = (char**)malloc(lengthAudio * sizeof(char*));
    if (audio) {
        for (uint8_t i = 0; i < lengthAudio; ++i) {
            audio[i] = (char *)malloc(4 * sizeof(char));
            if (!audio[i]) {
                ESP_LOGI(TAG, "Allocate mem for audio is fail");
                exit(1);
            }

            snprintf((char *)audio[i], 4, "%03d", i + 1);
        }
    } else {
        ESP_LOGI(TAG, "Allocate mem for audio is fail");
        exit(1);
    }
}

void websocket_app_start(const char *gateway) {
    esp_websocket_client_config_t websocket_cfg = {};
    websocket_cfg.uri = gateway;
    websocket_cfg.reconnect_timeout_ms = 2000;

    client = esp_websocket_client_init(&websocket_cfg);
    esp_websocket_register_events(client, WEBSOCKET_EVENT_ANY, websocket_event_handler, (void *)client);
    esp_websocket_client_start(client);
}

void websocket_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data) {
    esp_websocket_event_data_t *data = (esp_websocket_event_data_t *)event_data;

    switch (event_id) {
    case WEBSOCKET_EVENT_CONNECTED:
        ESP_LOGI(TAG, "Websocket connected");
        sendRequest("join", true);
        break;
    case WEBSOCKET_EVENT_DISCONNECTED:
        ESP_LOGI(TAG, "Websocket disconnected");
        break;
    case WEBSOCKET_EVENT_DATA:
        cJSON *json;
        json = cJSON_Parse(data->data_ptr);
        if (data->data_len == 0 || json == NULL) {
            return;
        }
        if (cJSON_IsObject(json)) {
            cJSON *sender = cJSON_GetObjectItemCaseSensitive(json, "sender");
            if (sender != NULL && cJSON_IsString(sender) && !strcmp(sender->valuestring, "server")) {

                ESP_LOGI(TAG, "Received=%.*s", data->data_len, (char *)data->data_ptr);

                cJSON *action = cJSON_GetObjectItemCaseSensitive(json, "action");
                if (action != NULL && cJSON_IsString(action)) {
                    cJSON *data = cJSON_GetObjectItemCaseSensitive(json, "data");
                    if (data != NULL) {
                        cJSON *time = cJSON_GetObjectItemCaseSensitive(data, "time_stamp");
                        if (time != NULL)
                            handleAction(action->valuestring, time->valueint);
                        else 
                            handleAction(action->valuestring, 0);
                    } else 
                        handleAction(action->valuestring, 0);
                }
            }
        }

        cJSON_Delete(json);
        break;
    }
}

cJSON *payload(char *action, bool all) {
    cJSON *json, *data, *audioArr;
    json = cJSON_CreateObject();
    data = cJSON_CreateObject();
    audioArr = cJSON_CreateArray();

    if (all) {
      cJSON_AddBoolToObject(data, "isPlaying", isPlaying);
      cJSON_AddBoolToObject(data, "isRepeat", isRepeat);
      cJSON_AddBoolToObject(data, "isShuffle", isShuffle);
      cJSON_AddNumberToObject(data, "curAudioId", curAudioId);
      cJSON_AddNumberToObject(data, "timer", timer);

      for (uint8_t i = 0; i < lengthAudio; ++i) {
        cJSON_AddItemToArray(audioArr, cJSON_CreateString(audio[i]));
      }
      cJSON_AddItemToObject(data, "audio", audioArr);
      cJSON_AddItemToObject(json, "data", data);
    }

    cJSON_AddStringToObject(json, "action", action);
    cJSON_AddStringToObject(json, "sender", "speaker");

    char *js = cJSON_PrintUnformatted(json);
    cJSON_Delete(json);

    return js;
}

void sendRequest(char *action, bool all) {
    if (esp_websocket_client_is_connected(client)) {
      const char *json = payload(action, all);
      esp_websocket_client_send_text(client, json, strlen(json), portMAX_DELAY);
    }
}

static void periodic_timer_callback(void* arg)
{
    portENTER_CRITICAL_ISR(&timerMux);
    counter++;
    portEXIT_CRITICAL_ISR(&timerMux);
    xSemaphoreGiveFromISR(timerSemaphore, NULL);
}

void handleAction(char *action, uint64_t timeStamp) {

    if (strstr(action, "timer")) {
        uint64_t timerVal;
        sscanf(action, "timer %llu", &timerVal);
        timer = timerVal;

        counter = 0;
        counterLim = timerVal - timeStamp;
        if (counterLim > 0) {
            ESP_LOGI(TAG, "counterLim: %d", counterLim);
            if (isTimerRunning) {
                ESP_ERROR_CHECK(esp_timer_stop(periodic_timer));
                ESP_ERROR_CHECK(esp_timer_start_periodic(periodic_timer, 1000000));
            } else {
                isTimerRunning = true;
                ESP_ERROR_CHECK(esp_timer_start_periodic(periodic_timer, 1000000));
            }
        } 
        
    } else if (strstr(action, "play")) {
        int playId;
        sscanf(action, "play %d", &playId);

        if (playId == curAudioId) {
            isPlaying = true;
            vTaskDelay(100 / portTICK_PERIOD_MS);
            DF_start();

        } else if (playId >= 0 && playId < lengthAudio) {
            curAudioId = (uint8_t)playId;
            //play audio
            isPlaying = true;
            DF_play(curAudioId+1);

        } else {
            return;
        }

    } else if (!strcmp(action, "pause")) {
        isPlaying = false;
        DF_pause();

    } else if (!strcmp(action, "next")) {
        curAudioId = curAudioId+1 >= lengthAudio ? 0 : curAudioId+1;

        //next audio
        DF_play(curAudioId+1);
        //update state var
        isPlaying = true;

    } else if (!strcmp(action, "previous")) {
        curAudioId = curAudioId-1 < 0 ? lengthAudio-1 : curAudioId-1;
        //pre audio
        DF_play(curAudioId+1);

        //update state var
        isPlaying = true;

    } else if (!strcmp(action, "repeat")) {
        //update state var
        isRepeat = true;

    }else if (!strcmp(action, "cancel repeat")) {
        //update state var
        isRepeat = false;

    } else if (!strcmp(action, "shuffle")) {
        //shuffle
        isShuffle = true;
        
    } else if (!strcmp(action, "cancel shuffle")) {
        //cancel shuffle
        isShuffle = false;
        
    } else {
        return;
    }

    sendRequest(action, true);
}



void checkStateTask() {
    while(1) {
        if (xSemaphoreTake(timerSemaphore, 0) == pdTRUE) {
            ESP_LOGI(TAG, "counter: %d", counter);
            if (counter >= counterLim) {
                ESP_ERROR_CHECK(esp_timer_stop(periodic_timer));
                // vTaskDelay(3000/portTICK_PERIOD_MS);
                counter = 0;
                counterLim = 0;
                isTimerRunning = false;

                //pause audio
                isPlaying = false;
                DF_pause();
                sendRequest("audio", true);
            }
        }

        if (DF_available()) {
			ESP_LOGI(TAG, "DF available");
			uint8_t type = DF_readType();
			if (type == DFPlayerPlayFinished) {
				ESP_LOGI(TAG, "DF play finished");

                if (isRepeat) {
                    isPlaying = true;
                    DF_play(curAudioId+1);

                } else if (isShuffle) {
                    uint8_t id_rand;
                    do {
                        id_rand = rand() % lengthAudio;
                    } while (id_rand == curAudioId);

                    curAudioId = id_rand;
                    isPlaying = true;
                    
                    DF_play(curAudioId+1);
                } else {
                    isPlaying = false;
                }
                
                sendRequest("audio", true);
			}
		 }
    }

    vTaskDelete( NULL );
}

void ButtonOccur(){
    while(1) {
        //Next button
        uint8_t buttonNext_current = gpio_get_level(Next);
		if(buttonNext_current == 0 && buttonNext_previous == 1){
			vTaskDelay(20/ portTICK_PERIOD_MS);
			uint8_t buttonNext_current = gpio_get_level(Next);

			if(buttonNext_current == 0){
                curAudioId = curAudioId+1 >= lengthAudio ? 0 : curAudioId+1;
                DF_play(curAudioId+1);
				printf("Next\n");
				isPlaying = true;
                isBtnOccur = true;
			}
		}
		buttonNext_previous = buttonNext_current;

        //Play or Pause button
        uint8_t buttonPP_current = gpio_get_level(PP);
		if(buttonPP_current == 0 && buttonPP_previous == 1){
			vTaskDelay(20/ portTICK_PERIOD_MS);
			uint8_t buttonPP_current = gpio_get_level(PP);

			if(buttonPP_current == 0){
                if(isPlaying == false){
                    DF_start();
			        printf("Play\n");
                    isPlaying = true;
                } else {
                    DF_pause();
                    printf("Pause\n");
                    isPlaying = false;
                }

                isBtnOccur = true;
			}
		}
		buttonPP_previous = buttonPP_current;

        //Previous button    
        uint8_t buttonPrevious_current = gpio_get_level(Previous);
		if(buttonPrevious_current == 0 && buttonPrevious_previous == 1){
			vTaskDelay(20/ portTICK_PERIOD_MS);
			uint8_t buttonPrevious_current = gpio_get_level(Previous);

			if(buttonPrevious_current == 0){
                curAudioId = curAudioId-1 < 0 ? lengthAudio-1 : curAudioId-1;
                DF_play(curAudioId+1);
				printf("Previous\n");
				isPlaying = true;

                isBtnOccur = true;
			}
		}
		buttonPrevious_previous = buttonPrevious_current;

        gpio_set_level (BLINK_GPIO, (isPlaying)? 1:0);

        if (isBtnOccur) {
            isBtnOccur = false;
            sendRequest("audio", true);
        }
    }

    vTaskDelete( NULL );
}