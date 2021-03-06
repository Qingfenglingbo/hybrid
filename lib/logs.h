/***************************************************************************
 *   Copyright (C) 2011 by levin                                           *
 *   levin108@gmail.com                                                    *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program; if not, write to the                         *
 *   Free Software Foundation, Inc.,                                       *
 *   51 Franklin Street, Suite 500, Boston, MA 02110-1335, USA.            *
 ***************************************************************************/

#ifndef HYBRID_LOGS_H
#define HYBRID_LOGS_H
#include <glib.h>
#include "util.h"
#include "account.h"
#include "xmlnode.h"

typedef struct _HybridLogs HybridLogs;
typedef struct _HybridLogEntry HybridLogEntry;

struct _HybridLogs {
	gchar *log_path;
	gchar *id;
	time_t time;
	xmlnode *root;
};

struct _HybridLogEntry {
    gchar *name;
    gchar *time;
    gchar *content;
    gint is_send;
};

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Initialize the logs context.
 *
 * HYBRID_OK or HYBRID_ERROR in case of an error.
 */
gint hybrid_logs_init(void);

/**
 * Check whether there's log for the specified account and buddy
 *
 * @param account The log for which account.
 * @param id      The id of the chat window.
 */
gboolean hybrid_logs_exist(HybridAccount *account, const gchar *id);

/**
 * Create a log context.
 *
 * @param account The log for which account.
 * @param id      The id of the chat window.
 *
 * @return The log context created.
 */
HybridLogs *hybrid_logs_create(HybridAccount *account,
		const gchar *id);

/**
 * Write a log.
 *
 * @param log     The log context.
 * @param name    The name of the message sender.
 * @param msg     The content of the message.
 * @param sendout Whether the message is sent out or received.
 */
gint hybrid_logs_write(HybridLogs *log, const gchar *name, const gchar *msg,
					gboolean sendout);

/**
 * Read a log entry.
 *
 * @param account  The account whose log to read.
 * @param id       The Id of buddy to whome of the log.
 * @param logname  The log filename.
 *
 * @return         The list of log entries.
 */
GSList *hybrid_logs_read(HybridAccount *account, const gchar *id,
                    const gchar *logname);
/**
 * Get the log directory path.
 *
 * @param account The account for the log.
 * @param id      The id of the buddy for the log.
 *
 * @return        Path string, needs to be freed after use.
 */
gchar *hybrid_logs_get_path(HybridAccount *account, const gchar *id);

/**
 * Destroy a log context.
 *
 * @param log The log context to destroy.
 */
void hybrid_logs_destroy(HybridLogs *log);

#ifdef __cplusplus
}
#endif

#endif /* HYBRID_LOGS_H */
