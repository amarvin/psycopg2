/* typecast_datetime.c - date and time typecasting functions to python types
 *
 * Copyright (C) 2001-2003 Federico Di Gregorio <fog@debian.org>
 *
 * This file is part of the psycopg module.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2,
 * or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#include <math.h>
#include "datetime.h"


/* the pointer to the datetime module API is initialized by the module init
   code, we just need to grab it */
extern PyObject* pyDateTimeModuleP;
extern PyObject *pyDateTypeP;
extern PyObject *pyTimeTypeP;
extern PyObject *pyDateTimeTypeP;
extern PyObject *pyDeltaTypeP;

/** DATE - cast a date into a date python object **/

static PyObject *
typecast_PYDATE_cast(unsigned char *str, int len, PyObject *curs)
{
    PyObject* obj = NULL;
    int n, y=0, m=0, d=0;
     
    if (str == NULL) {Py_INCREF(Py_None); return Py_None;}
    
    /* check for infinity */
    if (!strcmp(str, "infinity") || !strcmp(str, "-infinity")) {
        if (str[0] == '-') {
            obj = PyObject_GetAttrString(pyDateTypeP, "min");
        }
        else {
            obj = PyObject_GetAttrString(pyDateTypeP, "max");
        }
    }

    else {
        n = sscanf(str, "%d-%d-%d", &y, &m, &d);
    
        if (n != 3) {
            PyErr_SetString(DataError, "unable to parse date");
        }
        else {
            obj = PyObject_CallFunction(pyDateTypeP, "iii", y, m, d);
        }
    }
    return obj;
}

/** DATETIME - cast a timestamp into a datetime python object **/

static PyObject *
typecast_PYDATETIME_cast(unsigned char *str, int len, PyObject *curs)
{
    PyObject* obj = NULL;
    int n, y=0, m=0, d=0;
    int hh=0, mm=0;
    int tzh=0, tzm=0;
    double ss=0.0;
    char tzs=0;
    
    if (str == NULL) {Py_INCREF(Py_None); return Py_None;}
    
    /* check for infinity */
    if (!strcmp(str, "infinity") || !strcmp(str, "-infinity")) {
        if (str[0] == '-') {
            obj = PyObject_GetAttrString(pyDateTimeModuleP, "min");
        }
        else {
            obj = PyObject_GetAttrString(pyDateTimeModuleP, "max");
        }
    }

    else {
         Dprintf("typecast_PYDATETIME_cast: s = %s", str);
        n = sscanf(str, "%d-%d-%d %d:%d:%lf%c%d:%d",
                   &y, &m, &d, &hh, &mm, &ss, &tzs, &tzh, &tzm);
        Dprintf("typecast_PYDATETIME_cast: date parsed, %d components", n);
        
        if (n != 3 && n != 6 && n <= 7) {
            PyErr_SetString(DataError, "unable to parse date");
        }
        else {
            double micro = (ss - floor(ss)) * 1000000.0;
            int sec = (int)floor(ss);
            if (sec > 59) {
                mm += 1;
                sec -= 60;
            }
            if (tzs && ((cursorObject*)curs)->tzinfo_factory != Py_None) {
                /* we have a time zone, calculate minutes and create
                   appropriate tzinfo object calling the factory */
                PyObject *tzinfo;
                tzm += tzh*60;
                if (tzs == '-') tzm = -tzm;
                Dprintf("typecast_PYDATETIME_cast: UTC offset = %dm", tzm);
                tzinfo = PyObject_CallFunction(
                    ((cursorObject*)curs)->tzinfo_factory, "i", tzm);
                obj = PyObject_CallFunction(pyDateTimeTypeP, "iiiiiiiO",
                     y, m, d, hh, mm, sec, (int)round(micro), tzinfo);
                Dprintf("typecast_PYDATETIME_cast: tzinfo: %p, refcnt = %d",
                        tzinfo, tzinfo->ob_refcnt);
                Py_XDECREF(tzinfo);
            }
            else {
                obj = PyObject_CallFunction(pyDateTimeTypeP, "iiiiiii",
                     y, m, d, hh, mm, sec, (int)round(micro));
            }
        }
    }
    return obj;
}

/** TIME - parse time into a time object **/

static PyObject *
typecast_PYTIME_cast(unsigned char *str, int len, PyObject *curs)
{
    PyObject* obj = NULL;
    int n, hh=0, mm=0;
    double ss=0.0;
    
    if (str == NULL) {Py_INCREF(Py_None); return Py_None;}
        
    n = sscanf(str, "%d:%d:%lf", &hh, &mm, &ss);
    
    if (n != 3) {
        PyErr_SetString(DataError, "unable to parse time");
    }
    else {
        double micro = (ss - floor(ss)) * 1000000.0;
        int sec = (int)floor(ss);
        if (sec > 59) {
            mm += 1;
            sec -= 60;
        }
        obj = PyObject_CallFunction(pyTimeTypeP, "iiii",
            hh, mm, sec, (int)round(micro));
    }
    return obj;          
}

/** INTERVAL - parse an interval into a timedelta object **/

static PyObject *
typecast_PYINTERVAL_cast(unsigned char *str, int len, PyObject *curs)
{
    long years = 0, months = 0, days = 0, denominator = 1;
    double hours = 0.0, minutes = 0.0, seconds = 0.0, hundredths = 0.0;
    double v = 0.0, sign = 1.0;
    int part = 0, sec;
    double micro;

    if (str == NULL) {Py_INCREF(Py_None); return Py_None;}

    Dprintf("typecast_PYINTERVAL_cast: s = %s", str);
    
    while (*str) {
        switch (*str) {

        case '-':
            sign = -1.0;
            break;

        case '0': case '1': case '2': case '3': case '4':
        case '5': case '6': case '7': case '8': case '9':
            v = v*10 + (double)*str - (double)'0';
            if (part == 6){
                denominator *= 10;
            }
            break;

        case 'y':
            if (part == 0) {
                years = (long)(v*sign);
                str = skip_until_space(str);
                v = 0.0; sign = 1.0; part = 1;
            }
            break;

        case 'm':
            if (part <= 1) {
                months = (long)(v*sign);
                str = skip_until_space(str);
                v = 0.0; sign = 1.0; part = 2;
            }
            break;

        case 'd':
            if (part <= 2) {
                days = (long)(v*sign);
                str = skip_until_space(str);
                v = 0.0; sign = 1.0; part = 3;
            }
            break;

        case ':':
            if (part <= 3) {
                hours = v;
                v = 0.0; part = 4;
            }
            else if (part == 4) {
                minutes = v;
                v = 0.0; part = 5;
            }
            break;

        case '.':
            if (part == 5) {
                seconds = v;
                v = 0.0; part = 6;
            }
            break;   

        default:
            break;
        }
        
        str++;
    }

    /* manage last value, be it minutes or seconds or hundredths of a second */
    if (part == 4) {
        minutes = v;
    }
    else if (part == 5) {
        seconds = v;
    }
    else if (part == 6) {
        hundredths = v;
        hundredths = hundredths/denominator;
    }
    
    /* calculates seconds */
    if (sign < 0.0) {
        seconds = - (hundredths + seconds + minutes*60 + hours*3600);
    }
    else {
        seconds += hundredths + minutes*60 + hours*3600;
    }

    /* calculates days */
    days += years*365 + months*30;

    micro = (seconds - floor(seconds)) * 1000000.0;
    sec = (int)floor(seconds);
    return PyObject_CallFunction(pyDeltaTypeP, "iii",
                                 days, sec, (int)round(micro));
}

/* psycopg defaults to using python datetime types */

#ifdef PSYCOPG_DEFAULT_PYDATETIME 
#define typecast_DATE_cast typecast_PYDATE_cast
#define typecast_TIME_cast typecast_PYTIME_cast
#define typecast_INTERVAL_cast typecast_PYINTERVAL_cast
#define typecast_DATETIME_cast typecast_PYDATETIME_cast
#endif
