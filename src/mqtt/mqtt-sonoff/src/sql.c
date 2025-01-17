/*
 * This file is part of libipc (https://github.com/TheCrypt0/libipc).
 * Copyright (c) 2019 Davide Maggioni.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, version 3.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include "sql.h"

//-----------------------------------------------------------------------------
// GENERAL STATIC VARS AND FUNCTIONS
//-----------------------------------------------------------------------------

char *sql_cmd_params[][2] = {
    { "sensitivity",         "off"     },    //SQL_CMD_SENSITIVITY_0,
    { "sensitivity",         "low"     },    //SQL_CMD_SENSITIVITY_25,
    { "sensitivity",         "medium"  },    //SQL_CMD_SENSITIVITY_50,
    { "sensitivity",         "high"    },    //SQL_CMD_SENSITIVITY_75,
    { "motion_detection",    "yes"     },    //SQL_CMD_MOTION_DETECTION_ENABLED,
    { "motion_detection",    "no"      },    //SQL_CMD_MOTION_DETECTION_DISABLED,
    { "local_record",        "yes"     },    //SQL_CMD_LOCAL_RECORD_ENABLED,
    { "local_record",        "no"      },    //SQL_CMD_LOCAL_RECORD_DISABLED,
    { "ir",                  "auto"    },    //SQL_IR_AUTO
    { "ir",                  "on"      },    //SQL_IR_ON
    { "ir",                  "off"     },    //SQL_IR_OFF
    { "rotate",              "yes"     },    //SQL_CMD_ROTATE_ENABLED,
    { "rotate",              "no"      },    //SQL_CMD_ROTATE_DISABLED,
    { "switch_on",           "yes"     },    //PRIVACY_OFF
    { "switch_on",           "no"      },    //PRIVACY_ON
    { "",                    ""        }     //SQL_CMD_LAST
};

static pthread_t *tr_sql;
int tr_sql_routine;
int ipcsys_db = 1;
sqlite3 *dbc = NULL, *dbc_sys = NULL, *dbc_mmc = NULL;
char last_msg[32];

int sensitivity = -1;
int motion_detection = -1;
int local_record = -1;
int rotate = -1;
int ir = -1;

static int start_sql_thread();
static void *sql_thread(void *args);
static int parse_message(char *msg);

static void call_callback(SQL_MESSAGE_TYPE type);
static void call_callback_cmd(SQL_COMMAND_TYPE type);
static void sql_debug(const char* fmt, ...);

//-----------------------------------------------------------------------------
// MESSAGES HANDLERS
//-----------------------------------------------------------------------------

static void handle_sql_motion_start();
static void handle_sql_command(int cmd);

//-----------------------------------------------------------------------------
// FUNCTION POINTERS TO CALLBACKS
//-----------------------------------------------------------------------------

typedef void(*func_ptr_t)(void* arg);

static func_ptr_t *sql_callbacks;

//=============================================================================

//-----------------------------------------------------------------------------
// INIT
//-----------------------------------------------------------------------------

int sql_init(int sysdb)
{
    int ret = 0;
    ipcsys_db = sysdb;

    memset(last_msg, '\0', sizeof(last_msg));
    if (ipcsys_db) {
        ret = sqlite3_open_v2(IPCSYS_DB, &dbc_sys, SQLITE_OPEN_READONLY, NULL);
        dbc = dbc_sys;
    } else {
        ret = sqlite3_open_v2(IPCMMC_DB, &dbc_mmc, SQLITE_OPEN_READONLY, NULL);
        dbc = dbc_mmc;
        ret = sqlite3_open_v2(IPCSYS_DB, &dbc_sys, SQLITE_OPEN_READONLY, NULL);
    }

    if (ret != SQLITE_OK) {
        fprintf(stderr, "Error opening db\n");
        return -1;
    }

    ret = start_sql_thread();
    if(ret != 0)
        return -2;

    sql_callbacks = malloc((sizeof(func_ptr_t)) * SQL_MSG_LAST);

    return 0;
}

void sql_stop()
{
    if(tr_sql != NULL)
    {
        tr_sql_routine = 0;
        pthread_join(*tr_sql, NULL);
        free(tr_sql);
    }

    if(sql_callbacks != NULL)
        free(sql_callbacks);

    if (dbc_sys) {
        sqlite3_close_v2(dbc_sys);
    }
    if (dbc_mmc) {
        sqlite3_close_v2(dbc_mmc);
    }

}

static int start_sql_thread()
{
    int ret;

    tr_sql = malloc(sizeof(pthread_t));
    tr_sql_routine = 1;
    ret = pthread_create(tr_sql, NULL, &sql_thread, NULL);
    if(ret != 0)
    {
        fprintf(stderr, "Can't create sql thread. Error: %d\n", ret);
        return -1;
    }

    return 0;
}

static void *sql_thread(void *args)
{
    sqlite3_stmt *stmt1 = NULL;
    sqlite3_stmt *stmt2 = NULL;
    sqlite3_stmt *stmt3 = NULL;
    sqlite3_stmt *stmt4 = NULL;
    sqlite3_stmt *stmt5 = NULL;
    int ret = 0;
    int itmp = 0;
    char buffer[1024];

    if (ipcsys_db) {
        sprintf (buffer, "select max(c_alarm_time), c_alarm_context from t_alarm_log;");
    } else {
        sprintf (buffer, "select max(c_alarm_time), c_alarm_code from T_RecordFile;");
    }
    ret = sqlite3_prepare_v2(dbc, buffer, -1, &stmt1, NULL);
    if (ret != SQLITE_OK) {
        fprintf(stderr, "Failed to fetch data: %s\n", sqlite3_errmsg(dbc));
    }

    sprintf (buffer, "select c_sensitivity,c_enable from t_mdarea where c_index=0;");
    ret = sqlite3_prepare_v2(dbc_sys, buffer, -1, &stmt2, NULL);
    if (ret != SQLITE_OK) {
        fprintf(stderr, "Failed to fetch data: %s\n", sqlite3_errmsg(dbc_sys));
    }

    sprintf (buffer, "select c_enabled from t_record_plan where c_recplan_no=1;");
    ret = sqlite3_prepare_v2(dbc_sys, buffer, -1, &stmt3, NULL);
    if (ret != SQLITE_OK) {
        fprintf(stderr, "Failed to fetch data: %s\n", sqlite3_errmsg(dbc_sys));
    }

    sprintf (buffer, "select c_param_value from t_sys_param where c_param_name=\"InfraredLamp\";");
    ret = sqlite3_prepare_v2(dbc_sys, buffer, -1, &stmt4, NULL);
    if (ret != SQLITE_OK) {
        fprintf(stderr, "Failed to fetch data: %s\n", sqlite3_errmsg(dbc_sys));
    }

    sprintf (buffer, "select c_param_value from t_sys_param where c_param_name=\"mirror\";");
    ret = sqlite3_prepare_v2(dbc_sys, buffer, -1, &stmt5, NULL);
    if (ret != SQLITE_OK) {
        fprintf(stderr, "Failed to fetch data: %s\n", sqlite3_errmsg(dbc_sys));
    }

    while(tr_sql_routine)
    {
        ret = sqlite3_step(stmt1);
        if (ret == SQLITE_ROW) {
            if (sqlite3_column_text(stmt1, 0) != NULL) {
                parse_message((char *) sqlite3_column_text(stmt1, 0));
            }
        }
        ret = sqlite3_reset(stmt1);

        ret = sqlite3_step(stmt2);
        if (ret == SQLITE_ROW) {
            itmp = sqlite3_column_int(stmt2, 0);
            if (sensitivity != itmp) {
                sensitivity = itmp;
                if (sensitivity == 0)
                    handle_sql_command(SQL_CMD_SENSITIVITY_0);
                else if (sensitivity == 25)
                    handle_sql_command(SQL_CMD_SENSITIVITY_25);
                else if (sensitivity == 50)
                    handle_sql_command(SQL_CMD_SENSITIVITY_50);
                else if (sensitivity == 75)
                    handle_sql_command(SQL_CMD_SENSITIVITY_75);
            }
            itmp = sqlite3_column_int(stmt2, 1);
            if (motion_detection != itmp) {
                motion_detection = itmp;
                if (motion_detection == 1)
                    handle_sql_command(SQL_CMD_MOTION_DETECTION_ENABLED);
                else
                    handle_sql_command(SQL_CMD_MOTION_DETECTION_DISABLED);
            }
        }
        ret = sqlite3_reset(stmt2);

        ret = sqlite3_step(stmt3);
        if (ret == SQLITE_ROW) {
            itmp = sqlite3_column_int(stmt3, 0);
            if (local_record != itmp) {
                local_record = itmp;
                if (local_record == 1)
                    handle_sql_command(SQL_CMD_LOCAL_RECORD_ENABLED);
                else
                    handle_sql_command(SQL_CMD_LOCAL_RECORD_DISABLED);
            }
        }
        ret = sqlite3_reset(stmt3);

        ret = sqlite3_step(stmt4);
        if (ret == SQLITE_ROW) {
            itmp = sqlite3_column_int(stmt4, 0);
            if (ir != itmp) {
                ir = itmp;
                if (ir == 2)
                    handle_sql_command(SQL_CMD_IR_AUTO);
                else if (ir == 0)
                    handle_sql_command(SQL_CMD_IR_ON);
                else if (ir == 1)
                    handle_sql_command(SQL_CMD_IR_OFF);
            }
        }
        ret = sqlite3_reset(stmt4);

        ret = sqlite3_step(stmt5);
        if (ret == SQLITE_ROW) {
            itmp = sqlite3_column_int(stmt5, 0);
            if (rotate != itmp) {
                rotate = itmp;
                if (rotate == 1)
                    handle_sql_command(SQL_CMD_ROTATE_ENABLED);
                else
                    handle_sql_command(SQL_CMD_ROTATE_DISABLED);
            }
        }
        ret = sqlite3_reset(stmt5);

        usleep(1000*1000);
    }

    sqlite3_finalize(stmt1);
    sqlite3_finalize(stmt2);
    sqlite3_finalize(stmt3);
    sqlite3_finalize(stmt4);

    return 0;
}

//-----------------------------------------------------------------------------
// MSG PARSER
//-----------------------------------------------------------------------------

static int parse_message(char *msg)
{
    sql_debug("Parsing message.\n");

    sql_debug(msg);
    sql_debug("\n");

    if (strcmp(last_msg, msg) != 0) {
        if (last_msg[0] != '\0') {
            handle_sql_motion_start();
        }
        strcpy(last_msg, msg);
        return 0;
    }

    return -1;
}

//-----------------------------------------------------------------------------
// HANDLERS
//-----------------------------------------------------------------------------

static void handle_sql_motion_start()
{
    sql_debug("GOT MOTION START\n");
    call_callback(SQL_MSG_MOTION_START);
}

static void handle_sql_command(int cmd)
{
    sql_debug("GOT MOTION START\n");
    call_callback_cmd(cmd);
}

//-----------------------------------------------------------------------------
// GETTERS AND SETTERS
//-----------------------------------------------------------------------------

int sql_set_callback(SQL_MESSAGE_TYPE type, void (*f)())
{
    if(type >= SQL_MSG_LAST)
        return -1;

    sql_callbacks[(int)type]=f;

    return 0;
}

//-----------------------------------------------------------------------------
// UTILS
//-----------------------------------------------------------------------------

static void call_callback(SQL_MESSAGE_TYPE type)
{
    func_ptr_t f;
    // Not handling callbacks with parameters (yet)
    f = sql_callbacks[(int)type];
    if(f != NULL)
        (*f)(NULL);
}

static void call_callback_cmd(SQL_COMMAND_TYPE type)
{
    func_ptr_t f;
    f=sql_callbacks[SQL_MSG_COMMAND];
    if(f!=NULL)
        (*f)(&type);
}

static void sql_debug(const char* fmt, ...)
{
#if SQL_DEBUG
    va_list args;
    va_start(args, fmt);
    vprintf(fmt, args);
    va_end(args);
#endif
}
