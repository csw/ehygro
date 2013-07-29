#include <assert.h>
#include <sched.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <unistd.h>

#include <wiringPi.h>

#include "erl_interface.h"
#include "ei.h"

#include "dht22.h"
#include "locking.h"

static int init();
static int init_rt();

static int run();
static int read_msg(int *index);

static int read_exact(char *buf, int len);
static int write_exact(char *buf, int len);
static int error(const char *msg);

/* Globals */

#define BUFSIZE 1024
#define LENGTH_BYTES 2

static int dht22_pin = 7;

static int exit_code = 0;
static int lock_fd;

static char iobuf[BUFSIZE];

static int init()
{
    lock_fd = open_lockfile(LOCKFILE);
    assert(lock_fd);

    // this aborts on error unless the env var WIRINGPI_CODES is set
    int rc = wiringPiSetup();
    assert(rc == 0);

    if (init_rt() != 0)
        return -1;

    if (setuid(getuid()) != 0) {
        perror("Failed to relinquish privileges");
        return -1;
    }

    erl_init(NULL, 0);

    return 0;
}

static int init_rt()
{
    struct sched_param sp;
    memset(&sp, 0, sizeof(sp));

    sp.sched_priority = sched_get_priority_max(SCHED_FIFO);
    if (sp.sched_priority < 0) {
        perror("getting max scheduler priority failed");
        return -1;
    }

    if (sched_setscheduler(0, SCHED_FIFO, &sp) != 0) {
        perror("setting real-time scheduler failed");
        return -1;
    }

    if (mlockall(MCL_CURRENT | MCL_FUTURE) != 0) {
        perror("mlockall failed");
        return -1;
    }

    return 0;
}

static int run()
{
    int index;

    while (read_msg(&index) > 0) {
        int version, arity;

        if (ei_decode_version(iobuf, &index, &version) != 0)
            return error("Decoding version failed!");

        if (ei_decode_tuple_header(iobuf, &index, &arity) != 0)
            return error("Decoding tuple failed!");

        char key[MAXATOMLEN];
        if (ei_decode_atom(iobuf, &index, key) != 0)
            return error("Decoding key atom failed!");

        if (strcmp(key, "read") == 0) {
            if (arity != 2)
                return error("read message with invalid arity!");
            unsigned long timeout;
            if (ei_decode_ulong(iobuf, &index, &timeout) != 0)
                return error("Decoding timeout failed!");

            fprintf(stderr, "Received {read, %lu}.\n", timeout);

            index = LENGTH_BYTES;

            if (ei_encode_version(iobuf, &index) != 0)
                return error("Encoding version failed!");

            dht22_reading_t reading;
            if (read_dht22(dht22_pin, timeout*1000, &reading) == 0) {
                /* Success */
                fprintf(stderr, "Successful read: %u RH, %d deg C\n",
                        reading.humidity, reading.temp);

                if (ei_encode_tuple_header(iobuf, &index, 3) != 0)
                    return error("Encoding tuple header failed!");

                if (ei_encode_atom(iobuf, &index, "ok") != 0)
                    return error("Encoding 'ok' failed!");

                if (ei_encode_ulong(iobuf, &index, reading.humidity) != 0)
                    return error("Encoding humidity failed!");

                if (ei_encode_long(iobuf, &index, reading.temp) != 0)
                    return error("Encoding temp failed!");
            } else {
                fprintf(stderr, "Sensor read timed out!");

                if (ei_encode_tuple_header(iobuf, &index, 1) != 0)
                    return error("Encoding tuple header failed!");

                if (ei_encode_atom(iobuf, &index, "timeout") != 0)
                    return error("Encoding 'timeout' failed!");
            }

            assert(index > LENGTH_BYTES);
            assert(index < BUFSIZE);
            uint32_t term_length = index - LENGTH_BYTES;
            iobuf[0] = (term_length >> 8) & 0xFF;
            iobuf[1] = term_length & 0xFF;

            if (write_exact(iobuf, index) != index)
                return error("Write error!");

        } else {
            return error("Unhandled message!");
        }
    }

    return 0;
}

static int read_msg(int *index)
{
    *index = 0;
    if (read_exact(iobuf, LENGTH_BYTES) != LENGTH_BYTES)
        return 0;

    uint32_t term_length = iobuf[0] << 8 | iobuf[1];
    if (read_exact(iobuf, term_length) != term_length)
        return 0;

    return term_length;
}

static int read_exact(char *buf, int len)
{
    int i, got=0;

    do {
        if ((i = read(0, buf+got, len-got)) <= 0)
            return i;
        got += i;
    } while (got<len);

    return len;
}

static int write_exact(char *buf, int len)
{
    int i, wrote = 0;

    do {
        if ((i = write(1, buf+wrote, len-wrote)) <= 0)
            return i;
        wrote += i;
    } while (wrote<len);

    return len;
}

static int error(const char *msg)
{
    fprintf(stderr, "%s\n", msg);
    return -1;
}

int main(int argc, char *argv[])
{
    fprintf(stderr, "Initializing dht_interface.\n");

    if (init() != 0) {
        fprintf(stderr, "Initialization error.\n");
        exit(1);
    }
    fprintf(stderr, "Initialized.\n");

    int rc = run();
    close_lockfile(lock_fd);

    if (rc == 0) {
        exit(0);
    } else {
        fprintf(stderr, "Abnormal exit.\n");
        exit(exit_code || 1);
    }
}
