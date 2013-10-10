/**
 * @file account-manager.c Account Manager API
 * @ingroup core
 */

/* purple
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
#include "internal.h"
#include "account-manager.h"
#include "core.h"
#include "dbus-maybe.h"
#include "debug.h"
#include "enums.h"
#include "network.h"
#include "pounce.h"

#define PURPLE_ACCOUNT_MANAGER_GET_PRIVATE(obj) \
	(G_TYPE_INSTANCE_GET_PRIVATE((obj), PURPLE_TYPE_ACCOUNT_MANAGER, PurpleAccountManagerPrivate))

/** @copydoc _PurpleAccountManagerPrivate */
typedef struct _PurpleAccountManagerPrivate  PurpleAccountManagerPrivate;

/** Private data of an account manager */
struct _PurpleAccountManagerPrivate {
	PurpleAccountUiOps *account_ui_ops;

	GList   *accounts;
	guint    save_timer;
	gboolean accounts_loaded;
};

static PurpleAccountManager        *account_manager = NULL;
static PurpleAccountManagerPrivate *priv            = NULL;

static GObjectClass *parent_class = NULL;

/* Account Manager property enums */
enum
{
	PROP_0,
	PROP_UI_OPS,
	PROP_LAST
};

/* Account Manager signal enums */
enum
{
	SIG_ACC_CONNECTING,
	SIG_ACC_DISABLED,
	SIG_ACC_ENABLED,
	SIG_ACC_SETTING_INFO,
	SIG_ACC_SET_INFO,
	SIG_ACC_CREATED,
	SIG_ACC_DESTROYING,
	SIG_ACC_ADDED,
	SIG_ACC_REMOVED,
	SIG_ACC_STATUS_CHANGED,
	SIG_ACC_ACTIONS_CHANGED,
	SIG_ACC_ALIAS_CHANGED,
	SIG_ACC_AUTH_REQUESTED,
	SIG_ACC_AUTH_DENIED,
	SIG_ACC_AUTH_GRANTED,
	SIG_ACC_ERROR_CHANGED,
	SIG_ACC_SIGNED_ON,
	SIG_ACC_SIGNED_OFF,
	SIG_ACC_CONNECTION_ERROR,
	SIG_LAST
};
static guint signals[SIG_LAST] = { 0 };

void _purple_account_set_current_error(PurpleAccount *account,
		PurpleConnectionErrorInfo *new_err);

/*********************************************************************
 * Writing to disk                                                   *
 *********************************************************************/
static PurpleXmlNode *
accounts_to_xmlnode(void)
{
	PurpleXmlNode *node, *child;
	GList *cur;

	node = purple_xmlnode_new("account");
	purple_xmlnode_set_attrib(node, "version", "1.0");

	for (cur = purple_accounts_get_all(); cur != NULL; cur = cur->next)
	{
		child = purple_account_to_xmlnode(cur->data);
		purple_xmlnode_insert_child(node, child);
	}

	return node;
}

static void
sync_accounts(void)
{
	PurpleXmlNode *node;
	char *data;

	g_return_if_fail(priv != NULL);

	if (!priv->accounts_loaded)
	{
		purple_debug_error("account", "Attempted to save accounts before "
						 "they were read!\n");
		return;
	}

	node = accounts_to_xmlnode();
	data = purple_xmlnode_to_formatted_str(node, NULL);
	purple_util_write_data_to_file("accounts.xml", data, -1);
	g_free(data);
	purple_xmlnode_free(node);
}

static gboolean
save_cb(gpointer data)
{
	g_return_val_if_fail(priv != NULL, FALSE);

	sync_accounts();
	priv->save_timer = 0;
	return FALSE;
}

void
purple_accounts_schedule_save(void)
{
	g_return_if_fail(priv != NULL);

	if (priv->save_timer == 0)
		priv->save_timer = purple_timeout_add_seconds(5, save_cb, NULL);
}

/*********************************************************************
 * Reading from disk                                                 *
 *********************************************************************/
static void
migrate_yahoo_japan(PurpleAccount *account)
{
	/* detect a Yahoo! JAPAN account that existed prior to 2.6.0 and convert it
	 * to use the new yahoojp protocol.  Also remove the account-specific settings
	 * we no longer need */

	if(purple_strequal(purple_account_get_protocol_id(account), "yahoo")) {
		if(purple_account_get_bool(account, "yahoojp", FALSE)) {
			const char *serverjp = purple_account_get_string(account, "serverjp", NULL);
			const char *xferjp_host = purple_account_get_string(account, "xferjp_host", NULL);

			g_return_if_fail(serverjp != NULL);
			g_return_if_fail(xferjp_host != NULL);

			purple_account_set_string(account, "server", serverjp);
			purple_account_set_string(account, "xfer_host", xferjp_host);

			purple_account_set_protocol_id(account, "yahoojp");
		}

		/* these should always be nuked */
		purple_account_remove_setting(account, "yahoojp");
		purple_account_remove_setting(account, "serverjp");
		purple_account_remove_setting(account, "xferjp_host");

	}
}

static void
migrate_icq_server(PurpleAccount *account)
{
	/* Migrate the login server setting for ICQ accounts.  See
	 * 'mtn log --last 1 --no-graph --from b6d7712e90b68610df3bd2d8cbaf46d94c8b3794'
	 * for details on the change. */

	if(purple_strequal(purple_account_get_protocol_id(account), "icq")) {
		const char *tmp = purple_account_get_string(account, "server", NULL);

		/* Non-secure server */
		if(purple_strequal(tmp,	"login.messaging.aol.com") ||
				purple_strequal(tmp, "login.oscar.aol.com"))
			purple_account_set_string(account, "server", "login.icq.com");

		/* Secure server */
		if(purple_strequal(tmp, "slogin.oscar.aol.com"))
			purple_account_set_string(account, "server", "slogin.icq.com");
	}
}

static void
migrate_xmpp_encryption(PurpleAccount *account)
{
	/* When this is removed, nuke the "old_ssl" and "require_tls" settings */
	if (g_str_equal(purple_account_get_protocol_id(account), "jabber")) {
		const char *sec = purple_account_get_string(account, "connection_security", "");

		if (g_str_equal("", sec)) {
			const char *val = "require_tls";
			if (purple_account_get_bool(account, "old_ssl", FALSE))
				val = "old_ssl";
			else if (!purple_account_get_bool(account, "require_tls", TRUE))
				val = "opportunistic_tls";

			purple_account_set_string(account, "connection_security", val);
		}
	}
}

static void
parse_settings(PurpleXmlNode *node, PurpleAccount *account)
{
	const char *ui;
	PurpleXmlNode *child;

	/* Get the UI string, if these are UI settings */
	ui = purple_xmlnode_get_attrib(node, "ui");

	/* Read settings, one by one */
	for (child = purple_xmlnode_get_child(node, "setting"); child != NULL;
			child = purple_xmlnode_get_next_twin(child))
	{
		const char *name, *str_type;
		PurplePrefType type;
		char *data;

		name = purple_xmlnode_get_attrib(child, "name");
		if (name == NULL)
			/* Ignore this setting */
			continue;

		str_type = purple_xmlnode_get_attrib(child, "type");
		if (str_type == NULL)
			/* Ignore this setting */
			continue;

		if (purple_strequal(str_type, "string"))
			type = PURPLE_PREF_STRING;
		else if (purple_strequal(str_type, "int"))
			type = PURPLE_PREF_INT;
		else if (purple_strequal(str_type, "bool"))
			type = PURPLE_PREF_BOOLEAN;
		else
			/* Ignore this setting */
			continue;

		data = purple_xmlnode_get_data(child);
		if (data == NULL)
			/* Ignore this setting */
			continue;

		if (ui == NULL)
		{
			if (type == PURPLE_PREF_STRING)
				purple_account_set_string(account, name, data);
			else if (type == PURPLE_PREF_INT)
				purple_account_set_int(account, name, atoi(data));
			else if (type == PURPLE_PREF_BOOLEAN)
				purple_account_set_bool(account, name,
									  (*data == '0' ? FALSE : TRUE));
		} else {
			if (type == PURPLE_PREF_STRING)
				purple_account_set_ui_string(account, ui, name, data);
			else if (type == PURPLE_PREF_INT)
				purple_account_set_ui_int(account, ui, name, atoi(data));
			else if (type == PURPLE_PREF_BOOLEAN)
				purple_account_set_ui_bool(account, ui, name,
										 (*data == '0' ? FALSE : TRUE));
		}

		g_free(data);
	}

	/* we do this here because we need access to account settings to determine
	 * if we can/should migrate an old Yahoo! JAPAN account */
	migrate_yahoo_japan(account);
	/* we do this here because we need access to account settings to determine
	 * if we can/should migrate an ICQ account's server setting */
	migrate_icq_server(account);
	/* we do this here because we need to do it before the user views the
	 * Edit Account dialog. */
	migrate_xmpp_encryption(account);
}

static GList *
parse_status_attrs(PurpleXmlNode *node, PurpleStatus *status)
{
	GList *list = NULL;
	PurpleXmlNode *child;
	GValue *attr_value;

	for (child = purple_xmlnode_get_child(node, "attribute"); child != NULL;
			child = purple_xmlnode_get_next_twin(child))
	{
		const char *id = purple_xmlnode_get_attrib(child, "id");
		const char *value = purple_xmlnode_get_attrib(child, "value");

		if (!id || !*id || !value || !*value)
			continue;

		attr_value = purple_status_get_attr_value(status, id);
		if (!attr_value)
			continue;

		list = g_list_append(list, (char *)id);

		switch (G_VALUE_TYPE(attr_value))
		{
			case G_TYPE_STRING:
				list = g_list_append(list, (char *)value);
				break;
			case G_TYPE_INT:
			case G_TYPE_BOOLEAN:
			{
				int v;
				if (sscanf(value, "%d", &v) == 1)
					list = g_list_append(list, GINT_TO_POINTER(v));
				else
					list = g_list_remove(list, id);
				break;
			}
			default:
				break;
		}
	}

	return list;
}

static void
parse_status(PurpleXmlNode *node, PurpleAccount *account)
{
	gboolean active = FALSE;
	const char *data;
	const char *type;
	PurpleXmlNode *child;
	GList *attrs = NULL;

	/* Get the active/inactive state */
	data = purple_xmlnode_get_attrib(node, "active");
	if (data == NULL)
		return;
	if (g_ascii_strcasecmp(data, "true") == 0)
		active = TRUE;
	else if (g_ascii_strcasecmp(data, "false") == 0)
		active = FALSE;
	else
		return;

	/* Get the type of the status */
	type = purple_xmlnode_get_attrib(node, "type");
	if (type == NULL)
		return;

	/* Read attributes into a GList */
	child = purple_xmlnode_get_child(node, "attributes");
	if (child != NULL)
	{
		attrs = parse_status_attrs(child,
						purple_account_get_status(account, type));
	}

	purple_account_set_status_list(account, type, active, attrs);

	g_list_free(attrs);
}

static void
parse_statuses(PurpleXmlNode *node, PurpleAccount *account)
{
	PurpleXmlNode *child;

	for (child = purple_xmlnode_get_child(node, "status"); child != NULL;
			child = purple_xmlnode_get_next_twin(child))
	{
		parse_status(child, account);
	}
}

static void
parse_proxy_info(PurpleXmlNode *node, PurpleAccount *account)
{
	PurpleProxyInfo *proxy_info;
	PurpleXmlNode *child;
	char *data;

	proxy_info = purple_proxy_info_new();

	/* Use the global proxy settings, by default */
	purple_proxy_info_set_type(proxy_info, PURPLE_PROXY_USE_GLOBAL);

	/* Read proxy type */
	child = purple_xmlnode_get_child(node, "type");
	if ((child != NULL) && ((data = purple_xmlnode_get_data(child)) != NULL))
	{
		if (purple_strequal(data, "global"))
			purple_proxy_info_set_type(proxy_info, PURPLE_PROXY_USE_GLOBAL);
		else if (purple_strequal(data, "none"))
			purple_proxy_info_set_type(proxy_info, PURPLE_PROXY_NONE);
		else if (purple_strequal(data, "http"))
			purple_proxy_info_set_type(proxy_info, PURPLE_PROXY_HTTP);
		else if (purple_strequal(data, "socks4"))
			purple_proxy_info_set_type(proxy_info, PURPLE_PROXY_SOCKS4);
		else if (purple_strequal(data, "socks5"))
			purple_proxy_info_set_type(proxy_info, PURPLE_PROXY_SOCKS5);
		else if (purple_strequal(data, "tor"))
			purple_proxy_info_set_type(proxy_info, PURPLE_PROXY_TOR);
		else if (purple_strequal(data, "envvar"))
			purple_proxy_info_set_type(proxy_info, PURPLE_PROXY_USE_ENVVAR);
		else
		{
			purple_debug_error("account", "Invalid proxy type found when "
							 "loading account information for %s\n",
							 purple_account_get_username(account));
		}
		g_free(data);
	}

	/* Read proxy host */
	child = purple_xmlnode_get_child(node, "host");
	if ((child != NULL) && ((data = purple_xmlnode_get_data(child)) != NULL))
	{
		purple_proxy_info_set_host(proxy_info, data);
		g_free(data);
	}

	/* Read proxy port */
	child = purple_xmlnode_get_child(node, "port");
	if ((child != NULL) && ((data = purple_xmlnode_get_data(child)) != NULL))
	{
		purple_proxy_info_set_port(proxy_info, atoi(data));
		g_free(data);
	}

	/* Read proxy username */
	child = purple_xmlnode_get_child(node, "username");
	if ((child != NULL) && ((data = purple_xmlnode_get_data(child)) != NULL))
	{
		purple_proxy_info_set_username(proxy_info, data);
		g_free(data);
	}

	/* Read proxy password */
	child = purple_xmlnode_get_child(node, "password");
	if ((child != NULL) && ((data = purple_xmlnode_get_data(child)) != NULL))
	{
		purple_proxy_info_set_password(proxy_info, data);
		g_free(data);
	}

	/* If there are no values set then proxy_info NULL */
	if ((purple_proxy_info_get_type(proxy_info) == PURPLE_PROXY_USE_GLOBAL) &&
		(purple_proxy_info_get_host(proxy_info) == NULL) &&
		(purple_proxy_info_get_port(proxy_info) == 0) &&
		(purple_proxy_info_get_username(proxy_info) == NULL) &&
		(purple_proxy_info_get_password(proxy_info) == NULL))
	{
		purple_proxy_info_destroy(proxy_info);
		return;
	}

	purple_account_set_proxy_info(account, proxy_info);
}

static void
parse_current_error(PurpleXmlNode *node, PurpleAccount *account)
{
	guint type;
	char *type_str = NULL, *description = NULL;
	PurpleXmlNode *child;
	PurpleConnectionErrorInfo *current_error = NULL;

	child = purple_xmlnode_get_child(node, "type");
	if (child == NULL || (type_str = purple_xmlnode_get_data(child)) == NULL)
		return;
	type = atoi(type_str);
	g_free(type_str);

	if (type > PURPLE_CONNECTION_ERROR_OTHER_ERROR)
	{
		purple_debug_error("account",
			"Invalid PurpleConnectionError value %d found when "
			"loading account information for %s\n",
			type, purple_account_get_username(account));
		type = PURPLE_CONNECTION_ERROR_OTHER_ERROR;
	}

	child = purple_xmlnode_get_child(node, "description");
	if (child)
		description = purple_xmlnode_get_data(child);
	if (description == NULL)
		description = g_strdup("");

	current_error = g_new0(PurpleConnectionErrorInfo, 1);
	PURPLE_DBUS_REGISTER_POINTER(current_error, PurpleConnectionErrorInfo);
	current_error->type = type;
	current_error->description = description;

	_purple_account_set_current_error(account, current_error);
}

static PurpleAccount *
parse_account(PurpleXmlNode *node)
{
	PurpleAccount *ret;
	PurpleXmlNode *child;
	char *protocol_id = NULL;
	char *name = NULL;
	char *data;

	child = purple_xmlnode_get_child(node, "protocol");
	if (child != NULL)
		protocol_id = purple_xmlnode_get_data(child);

	child = purple_xmlnode_get_child(node, "name");
	if (child != NULL)
		name = purple_xmlnode_get_data(child);
	if (name == NULL)
	{
		/* Do we really need to do this? */
		child = purple_xmlnode_get_child(node, "username");
		if (child != NULL)
			name = purple_xmlnode_get_data(child);
	}

	if ((protocol_id == NULL) || (name == NULL))
	{
		g_free(protocol_id);
		g_free(name);
		return NULL;
	}

	ret = purple_account_new(name, protocol_id);
	g_free(name);
	g_free(protocol_id);

	/* Read the alias */
	child = purple_xmlnode_get_child(node, "alias");
	if ((child != NULL) && ((data = purple_xmlnode_get_data(child)) != NULL))
	{
		if (*data != '\0')
			purple_account_set_private_alias(ret, data);
		g_free(data);
	}

	/* Read the statuses */
	child = purple_xmlnode_get_child(node, "statuses");
	if (child != NULL)
	{
		parse_statuses(child, ret);
	}

	/* Read the userinfo */
	child = purple_xmlnode_get_child(node, "userinfo");
	if ((child != NULL) && ((data = purple_xmlnode_get_data(child)) != NULL))
	{
		purple_account_set_user_info(ret, data);
		g_free(data);
	}

	/* Read an old buddyicon */
	child = purple_xmlnode_get_child(node, "buddyicon");
	if ((child != NULL) && ((data = purple_xmlnode_get_data(child)) != NULL))
	{
		const char *dirname = purple_buddy_icons_get_cache_dir();
		char *filename = g_build_filename(dirname, data, NULL);
		gchar *contents;
		gsize len;

		if (g_file_get_contents(filename, &contents, &len, NULL))
		{
			purple_buddy_icons_set_account_icon(ret, (guchar *)contents, len);
		}

		g_free(filename);
		g_free(data);
	}

	/* Read settings (both core and UI) */
	for (child = purple_xmlnode_get_child(node, "settings"); child != NULL;
			child = purple_xmlnode_get_next_twin(child))
	{
		parse_settings(child, ret);
	}

	/* Read proxy */
	child = purple_xmlnode_get_child(node, "proxy");
	if (child != NULL)
	{
		parse_proxy_info(child, ret);
	}

	/* Read current error */
	child = purple_xmlnode_get_child(node, "current_error");
	if (child != NULL)
	{
		parse_current_error(child, ret);
	}

	/* Read the password */
	child = purple_xmlnode_get_child(node, "password");
	if (child != NULL)
	{
		const char *keyring_id = purple_xmlnode_get_attrib(child, "keyring_id");
		const char *mode = purple_xmlnode_get_attrib(child, "mode");
		gboolean result;

		data = purple_xmlnode_get_data(child);
		result = purple_keyring_import_password(ret, keyring_id, mode, data, NULL);

		if (result == TRUE || purple_keyring_get_inuse() == NULL) {
			purple_account_set_remember_password(ret, TRUE);
		} else {
			purple_debug_error("account", "Failed to import password.\n");
		} 
		purple_str_wipe(data);
	}

	return ret;
}

static void
load_accounts(void)
{
	PurpleXmlNode *node, *child;

	g_return_if_fail(priv != NULL);

	priv->accounts_loaded = TRUE;

	node = purple_util_read_xml_from_file("accounts.xml", _("accounts"));

	if (node == NULL)
		return;

	for (child = purple_xmlnode_get_child(node, "account"); child != NULL;
			child = purple_xmlnode_get_next_twin(child))
	{
		PurpleAccount *new_acct;
		new_acct = parse_account(child);
		purple_accounts_add(new_acct);
	}

	purple_xmlnode_free(node);

	_purple_buddy_icons_account_loaded_cb();
}

void
purple_accounts_add(PurpleAccount *account)
{
	g_return_if_fail(priv != NULL);
	g_return_if_fail(account != NULL);

	if (g_list_find(priv->accounts, account) != NULL)
		return;

	priv->accounts = g_list_append(priv->accounts, account);

	purple_accounts_schedule_save();

	g_signal_emit(account_manager, signals[SIG_ACC_ADDED], 0, account);
}

void
purple_accounts_remove(PurpleAccount *account)
{
	g_return_if_fail(priv != NULL);
	g_return_if_fail(account != NULL);

	priv->accounts = g_list_remove(priv->accounts, account);

	purple_accounts_schedule_save();

	/* Clearing the error ensures that account-error-changed is emitted,
	 * which is the end of the guarantee that the the error's pointer is
	 * valid.
	 */
	purple_account_clear_current_error(account);
	g_signal_emit(account_manager, signals[SIG_ACC_REMOVED], 0, account);
}

static void
purple_accounts_delete_set(PurpleAccount *account, GError *error, gpointer data)
{
	g_object_unref(G_OBJECT(account));
}

void
purple_accounts_delete(PurpleAccount *account)
{
	PurpleBlistNode *gnode, *cnode, *bnode;
	GList *iter;

	g_return_if_fail(account != NULL);

	/*
	 * Disable the account before blowing it out of the water.
	 * Conceptually it probably makes more sense to disable the
	 * account for all UIs rather than the just the current UI,
	 * but it doesn't really matter.
	 */
	purple_account_set_enabled(account, purple_core_get_ui(), FALSE);

	purple_notify_close_with_handle(account);
	purple_request_close_with_handle(account);

	purple_accounts_remove(account);

	/* Remove this account's buddies */
	for (gnode = purple_blist_get_root();
	     gnode != NULL;
		 gnode = purple_blist_node_get_sibling_next(gnode))
	{
		if (!PURPLE_IS_GROUP(gnode))
			continue;

		cnode = purple_blist_node_get_first_child(gnode);
		while (cnode) {
			PurpleBlistNode *cnode_next = purple_blist_node_get_sibling_next(cnode);

			if(PURPLE_IS_CONTACT(cnode)) {
				bnode = purple_blist_node_get_first_child(cnode);
				while (bnode) {
					PurpleBlistNode *bnode_next = purple_blist_node_get_sibling_next(bnode);

					if (PURPLE_IS_BUDDY(bnode)) {
						PurpleBuddy *b = (PurpleBuddy *)bnode;

						if (purple_buddy_get_account(b) == account)
							purple_blist_remove_buddy(b);
					}
					bnode = bnode_next;
				}
			} else if (PURPLE_IS_CHAT(cnode)) {
				PurpleChat *c = (PurpleChat *)cnode;

				if (purple_chat_get_account(c) == account)
					purple_blist_remove_chat(c);
			}
			cnode = cnode_next;
		}
	}

	/* Remove any open conversation for this account */
	for (iter = purple_conversations_get_all(); iter; ) {
		PurpleConversation *conv = iter->data;
		iter = iter->next;
		if (purple_conversation_get_account(conv) == account)
			g_object_unref(conv);
	}

	/* Remove this account's pounces */
	purple_pounce_destroy_all_by_account(account);

	/* This will cause the deletion of an old buddy icon. */
	purple_buddy_icons_set_account_icon(account, NULL, 0);

	/* This is async because we do not want the
	 * account being overwritten before we are done.
	 */
	purple_keyring_set_password(account, NULL,
		purple_accounts_delete_set, NULL);
}

void
purple_accounts_reorder(PurpleAccount *account, guint new_index)
{
	gint index;
	GList *l;

	g_return_if_fail(priv != NULL);
	g_return_if_fail(account != NULL);
	g_return_if_fail(new_index <= g_list_length(priv->accounts));

	index = g_list_index(priv->accounts, account);

	if (index < 0) {
		purple_debug_error("account",
				   "Unregistered account (%s) discovered during reorder!\n",
				   purple_account_get_username(account));
		return;
	}

	l = g_list_nth(priv->accounts, index);

	if (new_index > (guint)index)
		new_index--;

	/* Remove the old one. */
	priv->accounts = g_list_delete_link(priv->accounts, l);

	/* Insert it where it should go. */
	priv->accounts = g_list_insert(priv->accounts, account, new_index);

	purple_accounts_schedule_save();
}

GList *
purple_accounts_get_all(void)
{
	g_return_val_if_fail(priv != NULL, NULL);

	return priv->accounts;
}

GList *
purple_accounts_get_all_active(void)
{
	GList *list = NULL;
	GList *all = purple_accounts_get_all();

	while (all != NULL) {
		PurpleAccount *account = all->data;

		if (purple_account_get_enabled(account, purple_core_get_ui()))
			list = g_list_append(list, account);

		all = all->next;
	}

	return list;
}

PurpleAccount *
purple_accounts_find(const char *name, const char *protocol_id)
{
	PurpleAccount *account = NULL;
	GList *l;
	char *who;

	g_return_val_if_fail(name != NULL, NULL);
	g_return_val_if_fail(protocol_id != NULL, NULL);

	for (l = purple_accounts_get_all(); l != NULL; l = l->next) {
		account = (PurpleAccount *)l->data;

		if (!purple_strequal(purple_account_get_protocol_id(account), protocol_id))
			continue;

		who = g_strdup(purple_normalize(account, name));
		if (purple_strequal(purple_normalize(account, purple_account_get_username(account)), who)) {
			g_free(who);
			return account;
		}
		g_free(who);
	}

	return NULL;
}

void
purple_accounts_restore_current_statuses()
{
	GList *l;
	PurpleAccount *account;

	/* If we're not connected to the Internet right now, we bail on this */
	if (!purple_network_is_available())
	{
		purple_debug_warning("account", "Network not connected; skipping reconnect\n");
		return;
	}

	for (l = purple_accounts_get_all(); l != NULL; l = l->next)
	{
		account = (PurpleAccount *)l->data;

		if (purple_account_get_enabled(account, purple_core_get_ui()) &&
			(purple_presence_is_online(purple_account_get_presence(account))))
		{
			purple_account_connect(account);
		}
	}
}

void
purple_accounts_set_ui_ops(PurpleAccountUiOps *ops)
{
	g_return_if_fail(priv != NULL);

	priv->account_ui_ops = ops;

	g_object_notify(G_OBJECT(account_manager), "ui-ops");
}

PurpleAccountUiOps *
purple_accounts_get_ui_ops(void)
{
	g_return_val_if_fail(priv != NULL, FALSE);

	return priv->account_ui_ops;
}

static void
signed_on_cb(PurpleConnection *gc,
             gpointer unused)
{
	PurpleAccount *account;

	g_return_if_fail(account_manager != NULL);

	account = purple_connection_get_account(gc);
	purple_account_clear_current_error(account);

	g_signal_emit(account_manager, signals[SIG_ACC_SIGNED_ON], 0, account);
}

static void
signed_off_cb(PurpleConnection *gc,
              gpointer unused)
{
	PurpleAccount *account;

	g_return_if_fail(account_manager != NULL);

	account = purple_connection_get_account(gc);

	g_signal_emit(account_manager, signals[SIG_ACC_SIGNED_OFF], 0, account);
}

static void
connection_error_cb(PurpleConnection *gc,
                    PurpleConnectionError type,
                    const gchar *description,
                    gpointer unused)
{
	PurpleAccount *account;
	PurpleConnectionErrorInfo *err;

	g_return_if_fail(account_manager != NULL);

	account = purple_connection_get_account(gc);

	g_return_if_fail(account != NULL);

	err = g_new0(PurpleConnectionErrorInfo, 1);
	PURPLE_DBUS_REGISTER_POINTER(err, PurpleConnectionErrorInfo);

	err->type = type;
	err->description = g_strdup(description);

	_purple_account_set_current_error(account, err);

	g_signal_emit(account_manager, signals[SIG_ACC_CONNECTION_ERROR], 0,
	                   account, type, description);
}

static void
password_migration_cb(PurpleAccount *account)
{
	/* account may be NULL (means: all) */

	purple_accounts_schedule_save();
}

PurpleAccountManager *
purple_account_manager_get_instance(void)
{
	g_return_val_if_fail(account_manager != NULL, NULL);

	return account_manager;
}

/**************************************************************************
 * GObject code
 **************************************************************************/

/* Set method for GObject properties */
static void
purple_account_manager_set_property(GObject *obj, guint param_id,
		const GValue *value, GParamSpec *pspec)
{
	switch (param_id) {
		case PROP_UI_OPS:
			purple_accounts_set_ui_ops(g_value_get_pointer(value));
			break;
		default:
			G_OBJECT_WARN_INVALID_PROPERTY_ID(obj, param_id, pspec);
			break;
	}
}

/* Get method for GObject properties */
static void
purple_account_manager_get_property(GObject *obj, guint param_id, GValue *value,
		GParamSpec *pspec)
{
	switch (param_id) {
		case PROP_UI_OPS:
			g_value_set_pointer(value, purple_accounts_get_ui_ops());
			break;
		default:
			G_OBJECT_WARN_INVALID_PROPERTY_ID(obj, param_id, pspec);
			break;
	}
}

/* GObject dispose function */
static void
purple_account_manager_dispose(GObject *object)
{
	if (priv->save_timer != 0)
	{
		purple_timeout_remove(priv->save_timer);
		priv->save_timer = 0;
		sync_accounts();
	}

	for (; priv->accounts; priv->accounts = g_list_delete_link(priv->accounts, priv->accounts))
		g_object_unref(G_OBJECT(priv->accounts->data));

	G_OBJECT_CLASS(parent_class)->dispose(object);
}

static gboolean
purple_account_request_response_accumulator(GSignalInvocationHint *ihint,
		GValue *return_accu, const GValue *handler_return, gpointer data)
{
	PurpleAccountRequestResponse response;

	response = g_value_get_enum(handler_return);
	g_value_set_enum(return_accu, response);

	if (response == PURPLE_ACCOUNT_RESPONSE_PASS)
		return TRUE;

	return FALSE;
}

/* Class initializer function */
static void purple_account_manager_class_init(PurpleAccountManagerClass *klass)
{
	GObjectClass *obj_class = G_OBJECT_CLASS(klass);

	parent_class = g_type_class_peek_parent(klass);

	obj_class->dispose = purple_account_manager_dispose;

	/* Setup properties */
	obj_class->get_property = purple_account_manager_get_property;
	obj_class->set_property = purple_account_manager_set_property;

	g_object_class_install_property(obj_class, PROP_UI_OPS,
			g_param_spec_pointer("ui-ops", _("UI Ops"),
				_("UI Operations structure for accounts."),
				G_PARAM_READWRITE)
			);

	signals[SIG_ACC_CONNECTING] =
		g_signal_new("account-connecting", G_OBJECT_CLASS_TYPE(klass),
		             G_SIGNAL_ACTION, 0, NULL, NULL,
		             purple_smarshal_VOID__OBJECT, G_TYPE_NONE, 1,
		             PURPLE_TYPE_ACCOUNT);

	signals[SIG_ACC_DISABLED] =
		g_signal_new("account-disabled", G_OBJECT_CLASS_TYPE(klass),
		             G_SIGNAL_ACTION, 0, NULL, NULL,
		             purple_smarshal_VOID__OBJECT, G_TYPE_NONE, 1,
		             PURPLE_TYPE_ACCOUNT);

	signals[SIG_ACC_ENABLED]
		g_signal_new("account-enabled", G_OBJECT_CLASS_TYPE(klass),
		             G_SIGNAL_ACTION, 0, NULL, NULL,
		             purple_smarshal_VOID__OBJECT, G_TYPE_NONE, 1,
		             PURPLE_TYPE_ACCOUNT);

	signals[SIG_ACC_SETTING_INFO]
		g_signal_new("account-setting-info", G_OBJECT_CLASS_TYPE(klass),
		             G_SIGNAL_ACTION, 0, NULL, NULL,
		             purple_smarshal_VOID__OBJECT_STRING, G_TYPE_NONE, 2,
		             PURPLE_TYPE_ACCOUNT, G_TYPE_STRING);

	signals[SIG_ACC_SET_INFO]
		g_signal_new("account-set-info", G_OBJECT_CLASS_TYPE(klass),
		             G_SIGNAL_ACTION, 0, NULL, NULL,
		             purple_smarshal_VOID__OBJECT_STRING, G_TYPE_NONE, 2,
		             PURPLE_TYPE_ACCOUNT, G_TYPE_STRING);

	signals[SIG_ACC_CREATED]
		g_signal_new("account-created", G_OBJECT_CLASS_TYPE(klass),
		             G_SIGNAL_ACTION, 0, NULL, NULL,
		             purple_smarshal_VOID__OBJECT, G_TYPE_NONE, 1,
		             PURPLE_TYPE_ACCOUNT);

	signals[SIG_ACC_DESTROYING]
		g_signal_new("account-destroying", G_OBJECT_CLASS_TYPE(klass),
		             G_SIGNAL_ACTION, 0, NULL, NULL,
		             purple_smarshal_VOID__OBJECT, G_TYPE_NONE, 1,
		             PURPLE_TYPE_ACCOUNT);

	signals[SIG_ACC_ADDED]
		g_signal_new("account-added", G_OBJECT_CLASS_TYPE(klass),
		             G_SIGNAL_ACTION, 0, NULL, NULL,
		             purple_smarshal_VOID__OBJECT, G_TYPE_NONE, 1,
		             PURPLE_TYPE_ACCOUNT);

	signals[SIG_ACC_REMOVED]
		g_signal_new("account-removed", G_OBJECT_CLASS_TYPE(klass),
		             G_SIGNAL_ACTION, 0, NULL, NULL,
		             purple_smarshal_VOID__OBJECT, G_TYPE_NONE, 1,
		             PURPLE_TYPE_ACCOUNT);

	signals[SIG_ACC_STATUS_CHANGED]
		g_signal_new("account-status-changed", G_OBJECT_CLASS_TYPE(klass),
		             G_SIGNAL_ACTION, 0, NULL, NULL,
		             purple_smarshal_VOID__OBJECT_OBJECT_OBJECT, G_TYPE_NONE, 3,
		             PURPLE_TYPE_ACCOUNT, PURPLE_TYPE_STATUS,
		             PURPLE_TYPE_STATUS);

	signals[SIG_ACC_ACTIONS_CHANGED]
		g_signal_new("account-actions-changed", G_OBJECT_CLASS_TYPE(klass),
		             G_SIGNAL_ACTION, 0, NULL, NULL,
		             purple_smarshal_VOID__OBJECT, G_TYPE_NONE, 1,
		             PURPLE_TYPE_ACCOUNT);

	signals[SIG_ACC_ALIAS_CHANGED]
		g_signal_new("account-alias-changed", G_OBJECT_CLASS_TYPE(klass),
		             G_SIGNAL_ACTION, 0, NULL, NULL,
		             purple_smarshal_VOID__OBJECT_STRING, G_TYPE_NONE, 2,
		             PURPLE_TYPE_ACCOUNT, G_TYPE_STRING);

	signals[SIG_ACC_AUTH_REQUESTED]
		g_signal_new("account-authorization-requested",
		             G_OBJECT_CLASS_TYPE(klass), G_SIGNAL_ACTION, 0,
		             purple_account_request_response_accumulator, NULL,
		             purple_smarshal_ENUM__OBJECT_STRING_STRING_STRING,
		             PURPLE_TYPE_ACCOUNT_REQUEST_RESPONSE, 4,
		             PURPLE_TYPE_ACCOUNT, G_TYPE_STRING, G_TYPE_STRING,
		             G_TYPE_STRING);

	signals[SIG_ACC_AUTH_DENIED]
		g_signal_new("account-authorization-denied", G_OBJECT_CLASS_TYPE(klass),
		             G_SIGNAL_ACTION, 0, NULL, NULL,
		             purple_smarshal_VOID__OBJECT_STRING_STRING, G_TYPE_NONE, 3,
		             PURPLE_TYPE_ACCOUNT, G_TYPE_STRING, G_TYPE_STRING);

	signals[SIG_ACC_AUTH_GRANTED]
		g_signal_new("account-authorization-granted", G_OBJECT_CLASS_TYPE(klass),
		             G_SIGNAL_ACTION, 0, NULL, NULL,
		             purple_smarshal_VOID__OBJECT_STRING_STRING, G_TYPE_NONE, 3,
		             PURPLE_TYPE_ACCOUNT, G_TYPE_STRING, G_TYPE_STRING);

	signals[SIG_ACC_ERROR_CHANGED]
		g_signal_new("account-error-changed", G_OBJECT_CLASS_TYPE(klass),
		             G_SIGNAL_ACTION, 0, NULL, NULL,
		             purple_smarshal_VOID__OBJECT_ENUM_ENUM, G_TYPE_NONE, 3,
		             PURPLE_TYPE_ACCOUNT, PURPLE_TYPE_CONNECTION_ERROR_INFO,
		             PURPLE_TYPE_CONNECTION_ERROR_INFO);

	signals[SIG_ACC_SIGNED_ON]
		g_signal_new("account-signed-on", G_OBJECT_CLASS_TYPE(klass),
		             G_SIGNAL_ACTION, 0, NULL, NULL,
		             purple_smarshal_VOID__OBJECT, G_TYPE_NONE, 1,
		             PURPLE_TYPE_ACCOUNT);

	signals[SIG_ACC_SIGNED_OFF]
		g_signal_new("account-signed-off", G_OBJECT_CLASS_TYPE(klass),
		             G_SIGNAL_ACTION, 0, NULL, NULL,
		             purple_smarshal_VOID__OBJECT, G_TYPE_NONE, 1,
		             PURPLE_TYPE_ACCOUNT);

	signals[SIG_ACC_CONNECTION_ERROR]
		g_signal_new("account-connection-error", G_OBJECT_CLASS_TYPE(klass),
		             G_SIGNAL_ACTION, 0, NULL, NULL,
		             purple_smarshal_VOID__OBJECT_ENUM_STRING, G_TYPE_NONE, 3,
		             PURPLE_TYPE_ACCOUNT, PURPLE_TYPE_CONNECTION_ERROR,
		             G_TYPE_STRING);

	g_type_class_add_private(klass, sizeof(PurpleAccountManagerPrivate));
}

GType
purple_account_manager_get_type(void)
{
	static GType type = 0;

	if(type == 0) {
		static const GTypeInfo info = {
			sizeof(PurpleAccountManagerClass),
			NULL,
			NULL,
			(GClassInitFunc)purple_account_manager_class_init,
			NULL,
			NULL,
			sizeof(PurpleAccountManager),
			0,
			NULL,
			NULL,
		};

		type = g_type_register_static(G_TYPE_OBJECT,
				"PurpleAccountManager",
				&info, 0);
	}

	return type;
}

static void
manager_destroyed_cb(gpointer data, GObject *obj_was)
{
	priv            = NULL;
	account_manager = NULL;
}

void purple_accounts_init(void)
{
	if (!account_manager) {
		void *conn_handle = purple_connections_get_handle();

		account_manager = g_object_new(PURPLE_TYPE_ACCOUNT_MANAGER, NULL);

		g_return_if_fail(account_manager != NULL);

		g_object_weak_ref(G_OBJECT(account_manager), manager_destroyed_cb, NULL);

		purple_signal_connect(conn_handle, "signed-on", handle,
			                  PURPLE_CALLBACK(signed_on_cb), NULL);
		purple_signal_connect(conn_handle, "signed-off", handle,
			                  PURPLE_CALLBACK(signed_off_cb), NULL);
		purple_signal_connect(conn_handle, "connection-error", handle,
			                  PURPLE_CALLBACK(connection_error_cb), NULL);
		purple_signal_connect(purple_keyring_get_handle(), "password-migration", handle,
			                  PURPLE_CALLBACK(password_migration_cb), NULL);

		load_accounts();
	}
}

void purple_accounts_uninit(void)
{
	g_object_unref(account_manager);
}
