#include "who_take_human_face.hpp"

#include "esp_log.h"
#include "esp_camera.h"

#include "dl_image.hpp"
#include "fb_gfx.h"

#include "human_face_detect_msr01.hpp"
#include "human_face_detect_mnp01.hpp"

#include "../led/who_led.h"

#include "who_ai_utils.hpp"

using namespace std;
using namespace dl;

static const char *TAG = "take_human_face";

static QueueHandle_t xQueueFrameI = NULL;
static QueueHandle_t xQueueEvent = NULL;
static QueueHandle_t xQueueFrameO = NULL;
static QueueHandle_t xQueueResult = NULL;

static bool gReturnFB = true;

SemaphoreHandle_t xMutex;

extern "C" void send_photo(uint8_t* pdata, int size);

void printHeapState()
{
	char str[64];
	size_t size = esp_get_free_heap_size();
	printf("Total free heap space: %d\n", size);
	size = heap_caps_get_largest_free_block(1 << 2);
	printf("The largest free block size: %d\n", size);
}

static uint8_t HalfToHex(uint8_t data)
{
	data &= 0x0F;
	if (data < 10)
		return data + 0x30;
	return data - 10 + 0x41;
}

typedef struct
{
    //void *req;
    size_t len;
    //vector<uint8_t> buf;
    vector<uint8_t>* pbuf;
} jpg_chunking_t;

static size_t jpg_encode_stream(void *arg, size_t index, const void *data, size_t len)
{
    jpg_chunking_t *j = (jpg_chunking_t *)arg;
    if (!index)
    {
        j->len = 0;
    }
	
    /*if (len)
    {
        printf("reserve to %u\n", j->pbuf->size() + len * 2);
        j->pbuf->reserve(j->pbuf->size() + len * 2);
    }*/

    //printf("id: %d, length: %d. Buf size: %d, capacity: %d, max size: %u\n", index, len, j->pbuf->size(), j->pbuf->capacity(), j->pbuf->max_size());
	//printf("jpg_encode_stream. j->len:%d,\tlen:%d\n", j->len, len);
	for (size_t i = 0; i < len; ++i)
	{
        uint8_t d = ((uint8_t *)data)[i];
        j->pbuf->push_back(d);
        /*uint8_t h = HalfToHex(d >> 4);
        uint8_t l = HalfToHex(d);
        j->pbuf->push_back(h);
        j->pbuf->push_back(l);*/
	}
    //printf("..\n");
    j->len += len;
    return len;
}

static void task_process_handler(void *arg)
{
    camera_fb_t *frame = NULL;
    HumanFaceDetectMSR01 detector(0.3F, 0.3F, 10, 0.3F);
    HumanFaceDetectMNP01 detector2(0.4F, 0.3F, 10);
    static std::vector<uint8_t> img_buf;

    while (true)
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

                auto start = (unsigned long) (esp_timer_get_time() / 1000ULL);
                jpg_chunking_t jchunk = {0};
                jchunk.pbuf = &img_buf;
                jchunk.pbuf->reserve(65 * 1024);
                printf("Start time: %llu\n", esp_timer_get_time() / 1000ULL);
                frame2jpg_cb(frame, 80, jpg_encode_stream, &jchunk);
                printf("Frame len: %d, width: %d, height: %d\n", frame->len, frame->width, frame->height);
#if 0
                printf("<---start--->\n");
                for (size_t i = 0; i < jchunk.len/* * 2*/; ++i)
                {
                    //printf("%c", jchunk.pbuf->at(i));
                    printf("%02X", jchunk.buf.at(i));
                }
                printf("\n");
                printf("<---end--->\n");
#endif
                printHeapState();
                unsigned long time = (unsigned long) (esp_timer_get_time() / 1000ULL) - start;
                printf("Encoding time: %lu\n", time);
                printf("Data length: %u\n", jchunk.len * 2);
                blink_led(LED_ON_3ms);
                send_photo(jchunk.pbuf->data(), jchunk.pbuf->size());
                jchunk.pbuf->clear();
            }

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
}
