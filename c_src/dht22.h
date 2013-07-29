#ifndef _DHT22_H_
#define _DHT22_H_

#include <stdint.h>

/* 
 * This stores a successfully-read sensor reading whose checksum has
 * been verified. Humidity is in tenths of a percent (relative
 * humidity), and temperature is in tenths of a degree C.
 */
typedef struct dht22_reading {
    uint32_t humidity;
    int32_t temp;
} dht22_reading_t;

int read_dht22(int pin, uint32_t timeout, dht22_reading_t *reading);

#endif /* _DHT22_H_ */
