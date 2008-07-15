/*
 * purple - Jabber Protocol Plugin
 *
 * Copyright (C) 2007, Andreas Monitzer <andy@monitzer.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	 See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA	 02111-1307	 USA
 *
 */

#ifndef _PURPLE_JABBER_CAPS_H_
#define _PURPLE_JABBER_CAPS_H_

typedef struct _JabberCapsClientInfo JabberCapsClientInfo;

#include "jabber.h"

/* Implementation of XEP-0115 */
extern GHashTable *capstable;

typedef struct _JabberIdentity JabberCapsIdentity;

struct _JabberCapsClientInfo {
	GList *identities; /* JabberCapsIdentity */
	GList *features; /* char * */
	GList *forms; /* xmlnode * */
};

typedef struct _JabberCapsClientInfo JabberCapsValueExt;

typedef struct _JabberDataFormField {
	gchar *var;
	GList *values;
} JabberDataFormField;

typedef struct _JabberCapsKey {
	char *node;
	char *ver;
	char *hash;
} JabberCapsKey;

typedef void (*jabber_caps_get_info_cb)(JabberCapsClientInfo *info, gpointer user_data);

void jabber_caps_init(void);

void jabber_caps_destroy_key(gpointer value);

/**
 *	Main entity capabilites function to get the capabilities of a contact.
 */
void jabber_caps_get_info(JabberStream *js, const char *who, const char *node, const char *ver, const char *hash, jabber_caps_get_info_cb cb, gpointer user_data);
void jabber_caps_free_clientinfo(JabberCapsClientInfo *clientinfo);

/**
 *	Processes a query-node and returns a JabberCapsClientInfo object with all relevant info.
 *	
 *	@param 	query 	A query object.
 *	@return 		A JabberCapsClientInfo object.
 */
JabberCapsClientInfo *jabber_caps_parse_client_info(xmlnode *query);

/**
 *	Takes a JabberCapsClientInfo pointer and returns the caps hash according to
 *	XEP-0115 Version 1.5.
 *
 *	@param info A JabberCapsClientInfo pointer.
 *	@param hash Hash cipher to be used. Either sha-1 or md5.
 *	@return		The base64 encoded SHA-1 hash; needs to be freed if not needed 
 *				any furthermore. 
 */
gchar *jabber_caps_calcualte_hash(JabberCapsClientInfo *info, const char *hash);

void jabber_caps_calculate_own_hash();

/** Get the current caps hash.
 * 	@ret hash
**/
const gchar* jabber_caps_get_own_hash();

/**
 *
 */
void jabber_caps_broadcast_change();

#endif /* _PURPLE_JABBER_CAPS_H_ */
