/**
 * @file ping.h ping functions
 *
 * purple
 *
 * Purple is the legal property of its developers, whose names are too numerous
 * to list here.  Please refer to the COPYRIGHT file distributed with this
 * source distribution.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02111-1301  USA
 */

#ifndef PURPLE_JABBER_PING_H
#define PURPLE_JABBER_PING_H

#include "jabber.h"
#include "iq.h"
#include "xmlnode.h"

void jabber_ping_parse(JabberStream *js, const char *from,
                       JabberIqType, const char *id, PurpleXmlNode *child);
gboolean jabber_ping_jid(JabberStream *js, const char *jid);
void jabber_keepalive_ping(JabberStream *js);

#endif /* PURPLE_JABBER_PING_H */
