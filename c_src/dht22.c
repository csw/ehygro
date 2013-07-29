#include "dht22.h"

#include <stdbool.h>

#include <wiringPi.h>

/*
 * Taken from https://github.com/technion/lol_dht22
 */

#define DATA_BITS 40
#define MAX_DATA_EDGES 85
#define MAX_EDGE_US 255
/* Must wait at least this many ms between sensor readings. */
#define SENSOR_WAIT_USEC 2000


static int try_read_dht22(int pin, int *dat);
static void wait_until_ready();
static bool checksum_ok(int *dat);
static void convert(int *dat, dht22_reading_t *reading);

static unsigned int last_read = 0;

int read_dht22(int pin, uint32_t timeout, dht22_reading_t *reading)
{
    bool success = false;
    uint32_t deadline = millis() + timeout;
    do {
        wait_until_ready();
        int dat[6] = { 0, 0, 0, 0, 0, 0 };
        int read_rc = try_read_dht22(pin, dat);
        if (read_rc == 0 && checksum_ok(dat)) {
            convert(dat, reading);
            success = true;
        }
    } while (!success && millis() < deadline);

    return success ? 0 : 1;
}

static int try_read_dht22(int pin, int *dat)
{
    // pull pin down for 18 milliseconds
    pinMode(pin, OUTPUT);
    digitalWrite(pin, LOW);
    delay(18);
    // then pull it up for 40 microseconds
    digitalWrite(pin, HIGH);
    delayMicroseconds(40); 
    // prepare to read the pin
    pinMode(pin, INPUT);

    int last_state = HIGH;
    int cur_bit = 0;

    // detect change and read data
    // wait for falling edges
    for (int edge = 0; edge < MAX_DATA_EDGES; ++edge) {
        int edge_time = 0;
        while (digitalRead(pin) == last_state) {
            delayMicroseconds(1);
            if (++edge_time == MAX_EDGE_US) {
                break;
            }
        }
        last_state = digitalRead(pin);

        if (edge_time == MAX_EDGE_US)
            break;

        // ignore first 3 transitions
        // trigger on falling edges
        if ((edge >= 4) && last_state == LOW) {
            // shift each bit into the storage bytes
            int *cur_byte = dat + (cur_bit/8);
            *cur_byte <<= 1;
            // XXX: looks like this should be more like 26?
            // TODO: rework time handling, avoid accumulated error
            if (edge_time > 16)
                *cur_byte |= 1;
            ++cur_bit;
        }
    }

    last_read = millis();

    if (cur_bit >= DATA_BITS)
        return 0;
    else
        return -1;
}

static bool checksum_ok(int *dat)
{
    return (dat[4] == ((dat[0] + dat[1] + dat[2] + dat[3]) & 0xFF));
}

static void convert(int *dat, dht22_reading_t *reading)
{
    reading->humidity = dat[0] << 8 | dat[1];
    int32_t temp = (dat[2] & 0x7F) << 8 | dat[3];
    /* check sign bit */
    if (dat[2] & 0x80)
        temp *= -1;
    reading->temp = temp;
}

static void wait_until_ready()
{
    /* Lack of error codes worries me a bit here. */
    unsigned int elapsed = millis() - last_read;
    if (elapsed < SENSOR_WAIT_USEC)
        delay(SENSOR_WAIT_USEC - elapsed);
}
