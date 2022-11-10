#ifndef _APP_
#define _APP_

#ifdef __cplusplus
extern "C" {
#endif



void wifi_init();
void register_post();
void send_photo(uint8_t* pdata, int size);

#ifdef __cplusplus
}
#endif

#endif //_APP_
