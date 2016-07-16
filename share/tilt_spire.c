/*
 * Copyright (C) 2003 Robert Kooima
 *
 * NEVERBALL is  free software; you can redistribute  it and/or modify
 * it under the  terms of the GNU General  Public License as published
 * by the Free  Software Foundation; either version 2  of the License,
 * or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT  ANY  WARRANTY;  without   even  the  implied  warranty  of
 * MERCHANTABILITY or  FITNESS FOR A PARTICULAR PURPOSE.   See the GNU
 * General Public License for more details.
 */

#include <SDL.h>
#include <SDL_thread.h>
#include <math.h>
#include <stdio.h>

#include "config.h"

/*---------------------------------------------------------------------------*/

#define _ENABLE_TILT
#include <hidapi/hidapi.h>

/*
 * This data structure tracks button changes, counting transitions so that
 * none are missed if the event handling thread falls significantly behind
 * the device IO thread.
 */

#define BUTTON_NC 0
#define BUTTON_DN 1
#define BUTTON_UP 2

struct button_state
{
    unsigned char curr;
    unsigned char last;
    unsigned char upc;
    unsigned char dnc;
};

static void set_button(struct button_state *B, int s)
{
    if ((B->curr == 0) != (s == 0))
    {
        if (B->curr)
        {
            B->upc++;
            B->curr = 0;
        }
        else
        {
            B->dnc++;
            B->curr = 1;
        }
    }
}

static int get_button(struct button_state *B)
{
    int ch = BUTTON_NC;

    if      (B->last == 1 && B->upc > 0)
    {
        B->upc--;
        B->last = 0;
        ch = BUTTON_UP;
    }
    else if (B->last == 0 && B->dnc > 0)
    {
        B->dnc--;
        B->last = 1;
        ch = BUTTON_DN;
    }

    return ch;
}

/*---------------------------------------------------------------------------*/

struct tilt_state
{
    int   status;
    float x;
    float z;
    struct button_state pause;
};

typedef struct __attribute__((packed)) {
	int16_t x;
	int16_t y;
	int16_t z;
	int16_t spu;
} report_t;

static struct tilt_state state;
static SDL_mutex        *mutex  = NULL;
static SDL_Thread       *thread = NULL;

#define FILTER 8.f

static int tilt_func(void *data)
{
    int running = 1;
    hid_device *dev;
    report_t report;

	// Open the device using the VID, PID,
	// and optionally the Serial number.
	dev = hid_open_path("/dev/hidraw3");
	if (dev == NULL) {
		printf("can't open device :(\n");
		return 0;
	}

    SDL_mutexP(mutex);
    state.status = running;
    SDL_mutexV(mutex);

    while (mutex && running)
    {
        if (hid_read(dev, (unsigned char*)&report, sizeof(report)) < 0)
            break;

        SDL_mutexP(mutex);
        {
            running = state.status;

            set_button(&state.pause, report.spu < 2000);

            float y = report.y / 100.f;
            float x = -report.x / 100.f;
            printf("%.02g %.02g\n", y, x);
            state.x = x; //(state.x * (FILTER - 1) +
                      // report.x) / FILTER;
            state.z = y;//(state.z * (FILTER - 1) +
                      // report.y) / FILTER;
        }
        SDL_mutexV(mutex);
    }

    return 0;
}

void tilt_init(void)
{
    memset(&state, 0, sizeof (struct tilt_state));

    mutex  = SDL_CreateMutex();
    thread = SDL_CreateThread(tilt_func, "tilt_spire", NULL);
}

void tilt_free(void)
{
    if (mutex)
    {
        /* Get/set the status of the tilt sensor thread. */

        SDL_mutexP(mutex);
        state.status = 0;
        SDL_mutexV(mutex);

        /* Kill the thread and destroy the mutex. */

        //SDL_KillThread(thread);
        SDL_DestroyMutex(mutex);

        mutex  = NULL;
        thread = NULL;
    }
}

int tilt_get_button(int *b, int *s)
{
    int ch = BUTTON_NC;

    if (mutex)
    {
        SDL_mutexP(mutex);
        {
            if      ((ch = get_button(&state.pause)))
            {
                *b = config_get_d(CONFIG_JOYSTICK_BUTTON_START);
                *s = (ch == BUTTON_DN);
            }
        }
        SDL_mutexV(mutex);
    }
    return ch;
}

float tilt_get_x(void)
{
    float x = 0.0f;

    if (mutex)
    {
        SDL_mutexP(mutex);
        x = state.x;
        SDL_mutexV(mutex);
    }

    return x;
}

float tilt_get_z(void)
{
    float z = 0.0f;

    if (mutex)
    {
        SDL_mutexP(mutex);
        z = state.z;
        SDL_mutexV(mutex);
    }

    return z;
}

int tilt_stat(void)
{
    int b = 0;

    if (mutex)
    {
        SDL_mutexP(mutex);
        b = state.status;
        SDL_mutexV(mutex);
    }
    return b;
}

/*---------------------------------------------------------------------------*/
