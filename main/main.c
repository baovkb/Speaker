#include <stdio.h>
#include <stdbool.h>
#include "Wifi.h"
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

#include "ssd1306.h"
#include "font8x8_basic.h"

static char *TAG = "main";
#define tag "SSD1306"

SSD1306_t dev;
int center, top, bottom;
char lineChar[20];

#define UP 15    // Play/Pause
#define Next 5
#define DOWN 2
#define Previous 14

#define DEFAULT 1
int x = 1;
int value = 1;
uint8_t max_width = 0;

bool up = false;
bool down = false;

int test = -1; 
int mm = 0,ss = 0;
bool dem = false;

bool isPlaying = false;
bool isRepeat = false;
bool isShuffle = false;
bool isWait = false;
bool isReset = true;
bool isAvai = true;

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
void PrintOLED();
void CheckStateButton();
void generateAudioName();

void ConfigOLED();
void StateAudio();

void app_main(void)
{
    //Config button
    gpio_set_direction(UP,GPIO_MODE_INPUT);
	gpio_set_direction(Next,GPIO_MODE_INPUT);
	gpio_set_direction(Previous,GPIO_MODE_INPUT);
    gpio_set_direction(DOWN,GPIO_MODE_INPUT);
    gpio_set_pull_mode(DOWN, GPIO_PULLUP_ONLY);

    vTaskDelay(200/ portTICK_PERIOD_MS);

    //Check UART DF
    while(1) {
		bool ret = DF_begin(CONFIG_TX_GPIO, CONFIG_RX_GPIO, true);
		ESP_LOGI(TAG, "DF_begin=%d", ret);
		if (ret) break;
		vTaskDelay(200/ portTICK_PERIOD_MS);
	}
    // Check done ----------------------------------------

    vTaskDelay(300/ portTICK_PERIOD_MS);
    generateAudioName();

    ESP_LOGI(TAG, "DFPlayer Mini online.");
	ESP_LOGI(TAG, "Play first track on 01 folder.");
    vTaskDelay(300/ portTICK_PERIOD_MS);
	DF_volume(30); //Set volume value. From 0 to 30

    vTaskDelay(200/ portTICK_PERIOD_MS);

    DF_EQ(DFPLAYER_EQ_POP);

    vTaskDelay(200/ portTICK_PERIOD_MS);

    //Config OLED
    ConfigOLED();
    //Done ------------------------

    //Config 
    const char *ssid = "TrungPhat";
    const char *password = "30090610";
    const char *gateway = "ws://nhacnkd.online:8080";

    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
      ESP_ERROR_CHECK(nvs_flash_erase());
      ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

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
    
    connectWifi(ssid, password);
    websocket_app_start(gateway);\
    xTaskCreate(ButtonOccur, "Xu_ly_nut_nhan",4096, NULL, 1, NULL);
    xTaskCreate(checkStateTask, "checkStateTask", 2048, NULL, 1, NULL);
    
    // xTaskCreate(PrintOLED, "In man hinh", 2048, NULL, 1, NULL);
    // xTaskCreate(CheckStateButton, "Trang thai nut nhan", 2048, NULL,1,NULL);
}

void ConfigOLED(){
    ESP_LOGI(tag, "INTERFACE is i2c");
	ESP_LOGI(tag, "CONFIG_SDA_GPIO=%d",CONFIG_SDA_GPIO);
	ESP_LOGI(tag, "CONFIG_SCL_GPIO=%d",CONFIG_SCL_GPIO);
	ESP_LOGI(tag, "CONFIG_RESET_GPIO=%d",CONFIG_RESET_GPIO);
	i2c_master_init(&dev, CONFIG_SDA_GPIO, CONFIG_SCL_GPIO, 12);

    ESP_LOGI(tag, "Panel is 128x64");
	ssd1306_init(&dev, 128, 64);

	top = 2;
	center = 3;
	bottom = 8;
	
    vTaskDelay(1000 / portTICK_PERIOD_MS);
	ssd1306_clear_screen(&dev, false);
	ssd1306_contrast(&dev, 0xff);

    vTaskDelay(1000 / portTICK_PERIOD_MS);

    //DONE CONFIG
	int i=0, value = 0,x = 1;
	char s[7][20];
	char s1[7][20];


    //Hien thi playlist default
    ssd1306_display_text(&dev, 0, "Bai hat: ", 9, false);
    ssd1306_display_text(&dev, 1, "       Unknown ", 15, false);
    //---------------------------------------
    if (lengthAudio < 6) {
        max_width = lengthAudio;
    } else {
        max_width = 8;
    }

	for(int i= 2; i < max_width; i= i + 1){
		sprintf(s[i], "      %d      ", i-1);

		if(i == 2){
			vTaskDelay(10 / portTICK_PERIOD_MS);
			ssd1306_display_text(&dev, 2, s[i], 15, true);
		} else {
			vTaskDelay(10 / portTICK_PERIOD_MS);
			ssd1306_display_text(&dev, i, s[i], 15, false);
		}
	}

    value = DEFAULT;
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
    // ESP_LOGI(TAG, "action: %s", action);

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
            //ESP_ERROR_CHECK(esp_timer_restart(periodic_timer, 1000000));
        } 
        
    } else if (strstr(action, "play")) {
        int playId;
        sscanf(action, "play %d", &playId);

        if (playId == curAudioId) {
            isPlaying = true;
            vTaskDelay(100 / portTICK_PERIOD_MS);
            DF_start();
            isAvai = true;

        } else if (playId >= 0 && playId < lengthAudio) {
            curAudioId = (uint8_t)playId;
            //play audio
            isPlaying = true;
            DF_play(curAudioId+1);
            isAvai = true;

        } else {
            return;
        }

    } else if (!strcmp(action, "pause")) {
        isPlaying = false;
        DF_pause();
        isAvai = true;

    } else if (!strcmp(action, "next")) {
        curAudioId = curAudioId+1 >= lengthAudio ? 0 : curAudioId+1;

        //next audio
        DF_play(curAudioId+1);
        //update state var
        isPlaying = true;
        isAvai = true;

    } else if (!strcmp(action, "previous")) {
        curAudioId = curAudioId-1 < 0 ? lengthAudio-1 : curAudioId-1;
        //pre audio
        DF_play(curAudioId+1);

        //update state var
        isPlaying = true;
        isAvai = true;

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

void generateAudioName() {
    lengthAudio = DF_readFileCounts(DFPLAYER_DEVICE_SD);
    vTaskDelay(200 / portTICK_PERIOD_MS);
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

void checkStateTask() {
    while(1) {
        if (xSemaphoreTake(timerSemaphore, 0) == pdTRUE) {
            ESP_LOGI(TAG, "counter: %d", counter);
            if (counter >= counterLim) {
                ESP_ERROR_CHECK(esp_timer_stop(periodic_timer));
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
            isAvai = false;
			uint8_t type = DF_readType();
			if (type == DFPlayerPlayFinished) {
				ESP_LOGI(TAG, "DF play finished");
                dem = true;
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
    bool isBtnOccur = false;

    while(1) {
        //Play or Pause button
        uint8_t buttonPP_current = gpio_get_level(Next);
		if(buttonPP_current == 0 && buttonPP_previous == 1){
			vTaskDelay(30/ portTICK_PERIOD_MS);
			uint8_t buttonPP_current = gpio_get_level(Next);

			if(buttonPP_current == 0){

                if(curAudioId != (value-1)){
                    DF_play(value);
                    curAudioId = value - 1;
                    isPlaying = true;
                    isAvai = true;

                    printf("Play\n");
                } else {
                    if(!isPlaying ){
                        DF_start();
                        isPlaying = true;
                        isAvai = true;

                        printf("Start\n");
                    } else if(isPlaying){
                        DF_pause();
                        isPlaying = false;
                        isAvai = true;

                        printf("Pause\n");
                    }
                }

                isBtnOccur = true;
			}
		}
		buttonPP_previous = buttonPP_current;
        
        //------------------------------------------

        //Next button
		uint8_t buttonNext_current = gpio_get_level(UP);
		if(buttonNext_current == 0 && buttonNext_previous == 1){
			vTaskDelay(20/ portTICK_PERIOD_MS);
			uint8_t buttonNext_current = gpio_get_level(UP);

			if(buttonNext_current == 0){
				up = true;
			}
		}
		buttonNext_previous = buttonNext_current;

		//------------------------------------------

        //Previous button    
        uint8_t buttonPrevious_current = gpio_get_level(DOWN);
		if(buttonPrevious_current == 1 && buttonPrevious_previous == 0){
			vTaskDelay(5/ portTICK_PERIOD_MS);
			uint8_t buttonPrevious_current = gpio_get_level(DOWN);

			if(buttonPrevious_current == 1){
                down = true;
			}
		}
		buttonPrevious_previous = buttonPrevious_current;

		//----------------------------------------------

        if (isBtnOccur) {
            isBtnOccur = false;
            sendRequest("audio", true);
        }


        //---------MENU-------------------------
        vTaskDelay(10 / portTICK_PERIOD_MS);

		if(up){
            value++;
        } else if(down){
            value--;
        }

		if(value > lengthAudio){
			value = 1;
		} else if(value == 0 && down){
			value = lengthAudio;
		}

		up = false;
		down = false;

        StateAudio();

        //--------------END MENU-------------------------


    }

    vTaskDelete( NULL );
}

void StateAudio(){
    char s1[20];

    if(isAvai){
        if(isPlaying){
            ssd1306_display_text(&dev, 0, "Dang phat: ", 11, false);
        } else {
            ssd1306_display_text(&dev, 0, "Tam ngung: ", 11, false);
        }
    } else {
        ssd1306_display_text(&dev, 0, "Het bai:   ", 11, false);
    }
    sprintf(s1, "  %d           ", curAudioId+1);
    ssd1306_display_text(&dev, 1, s1, 16, false);

    if(x != value){
            x = value;
            char s[7][20];
            for(int i=2; i<max_width; i= i + 1){
                if(x < 1){
                    x = lengthAudio + value;
                } else if (x > lengthAudio) {
                    x = 1;
                }
                sprintf(s[i], "      %d      ", x);

                if(i == 2){
                    ssd1306_display_text(&dev, 2, s[i], 15, true);
                } else {
                    ssd1306_display_text(&dev, i, s[i], 15, false);
                }

                x++;
            }

            x  = value;
            vTaskDelay(10 / portTICK_PERIOD_MS);
        }
}
