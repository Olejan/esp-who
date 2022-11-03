#include "who_take_human_face.hpp"

#include "esp_log.h"
#include "esp_camera.h"

#include "dl_image.hpp"
#include "fb_gfx.h"

#include "human_face_detect_msr01.hpp"
#include "human_face_detect_mnp01.hpp"
#include "face_recognition_tool.hpp"

#include "../led/who_led.h"

#if CONFIG_MFN_V1
#if CONFIG_S8
#include "face_recognition_112_v1_s8.hpp"
#elif CONFIG_S16
#include "face_recognition_112_v1_s16.hpp"
#endif
#endif

#include "who_ai_utils.hpp"

using namespace std;
using namespace dl;

static const char *TAG = "take_human_face";

static QueueHandle_t xQueueFrameI = NULL;
static QueueHandle_t xQueueEvent = NULL;
static QueueHandle_t xQueueFrameO = NULL;
static QueueHandle_t xQueueResult = NULL;

static recognizer_state_t gEvent = DETECT;
static bool gReturnFB = true;
static face_info_t recognize_result;

SemaphoreHandle_t xMutex;

typedef enum
{
    SHOW_STATE_IDLE,
    SHOW_STATE_DELETE,
    SHOW_STATE_RECOGNIZE,
    SHOW_STATE_ENROLL,
} show_state_t;

#define RGB565_MASK_RED 0xF800
#define RGB565_MASK_GREEN 0x07E0
#define RGB565_MASK_BLUE 0x001F
#define FRAME_DELAY_NUM 16

static void rgb_print(camera_fb_t *fb, uint32_t color, const char *str)
{
    fb_gfx_print(fb, (fb->width - (strlen(str) * 14)) / 2, 10, color, str);
}

static int rgb_printf(camera_fb_t *fb, uint32_t color, const char *format, ...)
{
    char loc_buf[64];
    char *temp = loc_buf;
    int len;
    va_list arg;
    va_list copy;
    va_start(arg, format);
    va_copy(copy, arg);
    len = vsnprintf(loc_buf, sizeof(loc_buf), format, arg);
    va_end(copy);
    if (len >= sizeof(loc_buf))
    {
        temp = (char *)malloc(len + 1);
        if (temp == NULL)
        {
            return 0;
        }
    }
    vsnprintf(temp, len + 1, format, arg);
    va_end(arg);
    rgb_print(fb, color, temp);
    if (len > 64)
    {
        free(temp);
    }
    return len;
}

void printHeapState()
{
	char str[64];
	size_t size = esp_get_free_heap_size();
	printf("Total free heap space: %d\n", size);
	size = heap_caps_get_largest_free_block(1 << 2);
	printf("The largest free block size: %d\n", size);
}

typedef struct
{
    //void *req;
    size_t len;
} jpg_chunking_t;

unsigned long stat = 0;

static size_t jpg_encode_stream(void *arg, size_t index, const void *data, size_t len)
{
    jpg_chunking_t *j = (jpg_chunking_t *)arg;
    if (!index)
    {
        j->len = 0;
    }
	
	if (j->len == 0)
	{
		stat = (unsigned long) (esp_timer_get_time() / 1000ULL);
	}
#if 1//0
	//printf("jpg_encode_stream. j->len:%d,\tlen:%d\n", j->len, len);
	for (size_t i = 0; i < len; ++i)
	{
		/*if (((const uint8_t *)data)[i] < 0x10)
			printf("0");*/
		printf("%02X", ((const uint8_t *)data)[i]);
		//if ((i + 1) % 32 == 0) printf("\n");
	}
    //printf(", ");
#endif
    /*if (httpd_resp_send_chunk(j->req, (const char *)data, len) != ESP_OK)
    {
        return 0;
    }*/
    j->len += len;
	if (len == 0)
	{
        blink_led(LED_ON_3ms);
        printf("\n");
        printf("<---end--->\n");
        printHeapState();
		unsigned long time = (unsigned long) (esp_timer_get_time() / 1000ULL) - stat;
		printf("Encoding time: %lu\n", time);
	}
    return len;
}

static void task_process_handler(void *arg)
{
    camera_fb_t *frame = NULL;
    HumanFaceDetectMSR01 detector(0.3F, 0.3F, 10, 0.3F);
    HumanFaceDetectMNP01 detector2(0.4F, 0.3F, 10);

#if CONFIG_MFN_V1
#if CONFIG_S8
    FaceRecognition112V1S8 *recognizer = new FaceRecognition112V1S8();
#elif CONFIG_S16
    FaceRecognition112V1S16 *recognizer = new FaceRecognition112V1S16();
#endif
#endif
    show_state_t frame_show_state = SHOW_STATE_IDLE;
    recognizer_state_t _gEvent;
    recognizer->set_partition(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY, "fr");
    int partition_result = recognizer->set_ids_from_flash();

    while (true)
    {
        xSemaphoreTake(xMutex, portMAX_DELAY);
        _gEvent = gEvent;
        //gEvent = DETECT;
        xSemaphoreGive(xMutex);

        if (_gEvent)
        {
            bool is_detected = false;

            if (xQueueReceive(xQueueFrameI, &frame, portMAX_DELAY))
            {
/*#if TWO_STAGE_ON
                std::list<dl::detect::result_t> &detect_candidates = detector.infer((uint16_t *)frame->buf, {(int)frame->height, (int)frame->width, 3});
                std::list<dl::detect::result_t> &detect_results = detector2.infer((uint16_t *)frame->buf, {(int)frame->height, (int)frame->width, 3}, detect_candidates);
#else
                std::list<dl::detect::result_t> &detect_results = detector.infer((uint16_t *)frame->buf, {(int)frame->height, (int)frame->width, 3});
#endif*/
                auto st = esp_timer_get_time();
                std::list<dl::detect::result_t> &detect_results = detector.infer((uint16_t *)frame->buf, {(int)frame->height, (int)frame->width, 3});
                auto t1 = esp_timer_get_time() - st;

                if (detect_results.size() == 1)
                    is_detected = true;

                if (is_detected)
                {
                    printf("detect face time: %llums\n", t1 / 1000ULL);
                    blink_led(LED_ON_3ms);

                    printf("<---start--->\n");
					jpg_chunking_t jchunk = {0};
					frame2jpg_cb(frame, 80, jpg_encode_stream, &jchunk);
					
					/*printf("Detected. gEvent is %d\n", _gEvent);
					printf("Frame format: %d\n", frame->format);
                    switch (_gEvent)
                    {
                    case ENROLL:
						printf("Enroll -->\n");
                        recognizer->enroll_id((uint16_t *)frame->buf, {(int)frame->height, (int)frame->width, 3}, detect_results.front().keypoint, "", true);
                        ESP_LOGW("ENROLL", "ID %d is enrolled", recognizer->get_enrolled_ids().back().id);
                        frame_show_state = SHOW_STATE_ENROLL;
						gEvent = DETECT;
                        break;

                    case RECOGNIZE:
						printf("Recognize -->\n");
                        recognize_result = recognizer->recognize((uint16_t *)frame->buf, {(int)frame->height, (int)frame->width, 3}, detect_results.front().keypoint);
                        print_detection_result(detect_results);
                        if (recognize_result.id > 0)
                            ESP_LOGI("RECOGNIZE", "Similarity: %f, Match ID: %d", recognize_result.similarity, recognize_result.id);
                        else
                            ESP_LOGE("RECOGNIZE", "Similarity: %f, Match ID: %d", recognize_result.similarity, recognize_result.id);
                        frame_show_state = SHOW_STATE_RECOGNIZE;
						gEvent = DETECT;
                        break;

                    case DELETE:
						printf("Delete -->\n");
                        vTaskDelay(10);
                        recognizer->delete_id(true);
                        ESP_LOGE("DELETE", "% d IDs left", recognizer->get_enrolled_id_num());
                        frame_show_state = SHOW_STATE_DELETE;
						gEvent = DETECT;
                        break;

                    default:
                        break;
                    }*/
                }

                /*if (frame_show_state != SHOW_STATE_IDLE)
                {
                    static int frame_count = 0;
                    switch (frame_show_state)
                    {
                    case SHOW_STATE_DELETE:
                        rgb_printf(frame, RGB565_MASK_RED, "%d IDs left", recognizer->get_enrolled_id_num());
                        break;

                    case SHOW_STATE_RECOGNIZE:
                        if (recognize_result.id > 0)
                            rgb_printf(frame, RGB565_MASK_GREEN, "ID %d", recognize_result.id);
                        else
                            rgb_print(frame, RGB565_MASK_RED, "who ?");
                        break;

                    case SHOW_STATE_ENROLL:
                        rgb_printf(frame, RGB565_MASK_BLUE, "Enroll: ID %d", recognizer->get_enrolled_ids().back().id);
                        break;

                    default:
                        break;
                    }

                    if (++frame_count > FRAME_DELAY_NUM)
                    {
                        frame_count = 0;
                        frame_show_state = SHOW_STATE_IDLE;
                    }
                }*/

                if (detect_results.size())
                {
#if !CONFIG_IDF_TARGET_ESP32S3
                    print_detection_result(detect_results);
#endif
                    draw_detection_result((uint16_t *)frame->buf, frame->height, frame->width, detect_results);
                }
            }

            if (xQueueFrameO)
            {

                xQueueSend(xQueueFrameO, &frame, portMAX_DELAY);
            }
            else if (gReturnFB)
            {
                esp_camera_fb_return(frame);
            }
            else
            {
                free(frame);
            }

            if (xQueueResult && is_detected)
            {
                xQueueSend(xQueueResult, &recognize_result, portMAX_DELAY);
            }
        }
    }
}

static void task_event_handler(void *arg)
{
    recognizer_state_t _gEvent;
    while (true)
    {
        xQueueReceive(xQueueEvent, &(_gEvent), portMAX_DELAY);
        xSemaphoreTake(xMutex, portMAX_DELAY);
		if (gEvent != _gEvent)
			printf("Event is %d\n", _gEvent);
        gEvent = _gEvent;
        xSemaphoreGive(xMutex);
    }
}

void register_take_human_face(const QueueHandle_t frame_i,
								const QueueHandle_t event,
								const QueueHandle_t result,
								const QueueHandle_t frame_o,
								const bool camera_fb_return)
{
    xQueueFrameI = frame_i;
    xQueueFrameO = frame_o;
    xQueueEvent = event;
    xQueueResult = result;
    gReturnFB = camera_fb_return;
    xMutex = xSemaphoreCreateMutex();

    xTaskCreatePinnedToCore(task_process_handler, TAG, 4 * 1024, NULL, 5, NULL, 0);
    if (xQueueEvent)
        xTaskCreatePinnedToCore(task_event_handler, TAG, 4 * 1024, NULL, 5, NULL, 1);
}
