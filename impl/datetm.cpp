/*
 * Copyright (c) 2022 Light Labs Inc.
 * All Rights Reserved
 * Licensed under the MIT license.
 */

#include "datetm.h"

void SetTime( const struct timespec & ts, SetTm setTm )
{
    struct tm t;
    localtime_r( &ts.tv_sec, &t );
    setTm( t, ts.tv_nsec / 10 / 1000 / 1000 );
}

void SetNow( SetTm setTm )
{
    struct timespec ts;
    clock_gettime( CLOCK_REALTIME, &ts );
    SetTime( ts, setTm );
}
