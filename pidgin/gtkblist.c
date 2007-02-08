/*
 * @file gtkblist.c GTK+ BuddyList API
 * @ingroup gtkui
 *
 * gaim
 *
 * Gaim is the legal property of its developers, whose names are too numerous
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
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */
#include "internal.h"
#include "pidgin.h"

#include "account.h"
#include "connection.h"
#include "core.h"
#include "debug.h"
#include "notify.h"
#include "prpl.h"
#include "prefs.h"
#include "plugin.h"
#include "request.h"
#include "signals.h"
#include "gaimstock.h"
#include "util.h"

#include "gtkaccount.h"
#include "gtkblist.h"
#include "gtkcellrendererexpander.h"
#include "gtkconv.h"
#include "gtkdebug.h"
#include "gtkdialogs.h"
#include "gtkft.h"
#include "gtklog.h"
#include "gtkmenutray.h"
#include "gtkpounce.h"
#include "gtkplugin.h"
#include "gtkprefs.h"
#include "gtkprivacy.h"
#include "gtkroomlist.h"
#include "gtkstatusbox.h"
#include "gtkscrollbook.h"
#include "gtkutils.h"

#include <gdk/gdkkeysyms.h>
#include <gtk/gtk.h>
#include <gdk/gdk.h>

#define HEADLINE_CLOSE_SIZE 12

typedef struct
{
	GaimAccount *account;

	GtkWidget *window;
	GtkWidget *combo;
	GtkWidget *entry;
	GtkWidget *entry_for_alias;
	GtkWidget *account_box;

} PidginAddBuddyData;

typedef struct
{
	GaimAccount *account;
	gchar *default_chat_name;

	GtkWidget *window;
	GtkWidget *account_menu;
	GtkWidget *alias_entry;
	GtkWidget *group_combo;
	GtkWidget *entries_box;
	GtkSizeGroup *sg;

	GList *entries;

} PidginAddChatData;

typedef struct
{
	GaimAccount *account;

	GtkWidget *window;
	GtkWidget *account_menu;
	GtkWidget *entries_box;
	GtkSizeGroup *sg;

	GList *entries;
} PidginJoinChatData;


static GtkWidget *accountmenu = NULL;

static guint visibility_manager_count = 0;
static gboolean gtk_blist_obscured = FALSE;

static GList *pidgin_blist_sort_methods = NULL;
static struct pidgin_blist_sort_method *current_sort_method = NULL;
static void sort_method_none(GaimBlistNode *node, GaimBuddyList *blist, GtkTreeIter groupiter, GtkTreeIter *cur, GtkTreeIter *iter);

/* The functions we use for sorting aren't available in gtk 2.0.x, and
 * segfault in 2.2.0.  2.2.1 is known to work, so I'll require that */
#if GTK_CHECK_VERSION(2,2,1)
static void sort_method_alphabetical(GaimBlistNode *node, GaimBuddyList *blist, GtkTreeIter groupiter, GtkTreeIter *cur, GtkTreeIter *iter);
static void sort_method_status(GaimBlistNode *node, GaimBuddyList *blist, GtkTreeIter groupiter, GtkTreeIter *cur, GtkTreeIter *iter);
static void sort_method_log(GaimBlistNode *node, GaimBuddyList *blist, GtkTreeIter groupiter, GtkTreeIter *cur, GtkTreeIter *iter);
#endif
static PidginBuddyList *gtkblist = NULL;

static gboolean pidgin_blist_refresh_timer(GaimBuddyList *list);
static void pidgin_blist_update_buddy(GaimBuddyList *list, GaimBlistNode *node, gboolean statusChange);
static void pidgin_blist_selection_changed(GtkTreeSelection *selection, gpointer data);
static void pidgin_blist_update(GaimBuddyList *list, GaimBlistNode *node);
static void pidgin_blist_update_group(GaimBuddyList *list, GaimBlistNode *node);
static void pidgin_blist_update_contact(GaimBuddyList *list, GaimBlistNode *node);
static char *gaim_get_tooltip_text(GaimBlistNode *node, gboolean full);
static const char *item_factory_translate_func (const char *path, gpointer func_data);
static gboolean get_iter_from_node(GaimBlistNode *node, GtkTreeIter *iter);
static void redo_buddy_list(GaimBuddyList *list, gboolean remove, gboolean rerender);
static void pidgin_blist_collapse_contact_cb(GtkWidget *w, GaimBlistNode *node);
static char *gaim_get_group_title(GaimBlistNode *gnode, gboolean expanded);

static void pidgin_blist_tooltip_destroy(void);

struct _pidgin_blist_node {
	GtkTreeRowReference *row;
	gboolean contact_expanded;
	gboolean recent_signonoff;
	gint recent_signonoff_timer;
};

static char dim_grey_string[8] = "";
static char *dim_grey()
{
	if (!gtkblist)
		return "dim grey";
	if (!dim_grey_string[0]) {
		GtkStyle *style = gtk_widget_get_style(gtkblist->treeview);
		snprintf(dim_grey_string, sizeof(dim_grey_string), "#%02x%02x%02x",
			 style->text_aa[GTK_STATE_NORMAL].red >> 8,
			 style->text_aa[GTK_STATE_NORMAL].green >> 8,
			 style->text_aa[GTK_STATE_NORMAL].blue >> 8);
	}
	return dim_grey_string;
}

/***************************************************
 *              Callbacks                          *
 ***************************************************/
static gboolean gtk_blist_visibility_cb(GtkWidget *w, GdkEventVisibility *event, gpointer data)
{
	if (event->state == GDK_VISIBILITY_FULLY_OBSCURED)
		gtk_blist_obscured = TRUE;
	else if (gtk_blist_obscured) {
			gtk_blist_obscured = FALSE;
			pidgin_blist_refresh_timer(gaim_get_blist());
	}

	/* continue to handle event normally */
	return FALSE;
}

static gboolean gtk_blist_window_state_cb(GtkWidget *w, GdkEventWindowState *event, gpointer data)
{
	if(event->changed_mask & GDK_WINDOW_STATE_WITHDRAWN) {
		if(event->new_window_state & GDK_WINDOW_STATE_WITHDRAWN)
			gaim_prefs_set_bool("/gaim/gtk/blist/list_visible", FALSE);
		else {
			gaim_prefs_set_bool("/gaim/gtk/blist/list_visible", TRUE);
			pidgin_blist_refresh_timer(gaim_get_blist());
		}
	}

	if(event->changed_mask & GDK_WINDOW_STATE_MAXIMIZED) {
		if(event->new_window_state & GDK_WINDOW_STATE_MAXIMIZED)
			gaim_prefs_set_bool("/gaim/gtk/blist/list_maximized", TRUE);
		else
			gaim_prefs_set_bool("/gaim/gtk/blist/list_maximized", FALSE);
	}

	/* Refresh gtkblist if un-iconifying */
	if (event->changed_mask & GDK_WINDOW_STATE_ICONIFIED){
		if (!(event->new_window_state & GDK_WINDOW_STATE_ICONIFIED))
			pidgin_blist_refresh_timer(gaim_get_blist());
	}

	return FALSE;
}

static gboolean gtk_blist_delete_cb(GtkWidget *w, GdkEventAny *event, gpointer data)
{
	if(visibility_manager_count)
		gaim_blist_set_visible(FALSE);
	else
		gaim_core_quit();

	/* we handle everything, event should not propogate further */
	return TRUE;
}

static gboolean gtk_blist_configure_cb(GtkWidget *w, GdkEventConfigure *event, gpointer data)
{
	/* unfortunately GdkEventConfigure ignores the window gravity, but  *
	 * the only way we have of setting the position doesn't. we have to *
	 * call get_position because it does pay attention to the gravity.  *
	 * this is inefficient and I agree it sucks, but it's more likely   *
	 * to work correctly.                                    - Robot101 */
	gint x, y;

	/* check for visibility because when we aren't visible, this will   *
	 * give us bogus (0,0) coordinates.                      - xOr      */
	if (GTK_WIDGET_VISIBLE(w))
		gtk_window_get_position(GTK_WINDOW(w), &x, &y);
	else
		return FALSE; /* carry on normally */

#ifdef _WIN32
	/* Workaround for GTK+ bug # 169811 - "configure_event" is fired
	 * when the window is being maximized */
	if (gdk_window_get_state(w->window)
			& GDK_WINDOW_STATE_MAXIMIZED) {
		return FALSE;
	}
#endif

	/* don't save if nothing changed */
	if (x == gaim_prefs_get_int("/gaim/gtk/blist/x") &&
		y == gaim_prefs_get_int("/gaim/gtk/blist/y") &&
		event->width  == gaim_prefs_get_int("/gaim/gtk/blist/width") &&
		event->height == gaim_prefs_get_int("/gaim/gtk/blist/height")) {

		return FALSE; /* carry on normally */
	}

	/* don't save off-screen positioning */
	if (x + event->width < 0 ||
		y + event->height < 0 ||
		x > gdk_screen_width() ||
		y > gdk_screen_height()) {

		return FALSE; /* carry on normally */
	}

	/* ignore changes when maximized */
	if(gaim_prefs_get_bool("/gaim/gtk/blist/list_maximized"))
		return FALSE;

	/* store the position */
	gaim_prefs_set_int("/gaim/gtk/blist/x",      x);
	gaim_prefs_set_int("/gaim/gtk/blist/y",      y);
	gaim_prefs_set_int("/gaim/gtk/blist/width",  event->width);
	gaim_prefs_set_int("/gaim/gtk/blist/height", event->height);

	gtk_widget_set_size_request(gtkblist->headline_label,
				    gaim_prefs_get_int("/gaim/gtk/blist/width")-25,-1);
	/* continue to handle event normally */
	return FALSE;
}

static void gtk_blist_menu_info_cb(GtkWidget *w, GaimBuddy *b)
{
	serv_get_info(b->account->gc, b->name);
}

static void gtk_blist_menu_im_cb(GtkWidget *w, GaimBuddy *b)
{
	pidgindialogs_im_with_user(b->account, b->name);
}

static void gtk_blist_menu_send_file_cb(GtkWidget *w, GaimBuddy *b)
{
	serv_send_file(b->account->gc, b->name, NULL);
}

static void gtk_blist_menu_autojoin_cb(GtkWidget *w, GaimChat *chat)
{
	gaim_blist_node_set_bool((GaimBlistNode*)chat, "gtk-autojoin",
			gtk_check_menu_item_get_active(GTK_CHECK_MENU_ITEM(w)));
}

static void gtk_blist_join_chat(GaimChat *chat)
{
	GaimConversation *conv;

	conv = gaim_find_conversation_with_account(GAIM_CONV_TYPE_CHAT,
											   gaim_chat_get_name(chat),
											   chat->account);

	if (conv != NULL)
		pidgin_conv_present_conversation(conv);

	serv_join_chat(chat->account->gc, chat->components);
}

static void gtk_blist_menu_join_cb(GtkWidget *w, GaimChat *chat)
{
	gtk_blist_join_chat(chat);
}

static void gtk_blist_renderer_edited_cb(GtkCellRendererText *text_rend, char *arg1,
					 char *arg2, gpointer nada)
{
	GtkTreeIter iter;
	GtkTreePath *path;
	GValue val;
	GaimBlistNode *node;
	GaimGroup *dest;

	path = gtk_tree_path_new_from_string (arg1);
	gtk_tree_model_get_iter (GTK_TREE_MODEL(gtkblist->treemodel), &iter, path);
	gtk_tree_path_free (path);
	val.g_type = 0;
	gtk_tree_model_get_value (GTK_TREE_MODEL(gtkblist->treemodel), &iter, NODE_COLUMN, &val);
	node = g_value_get_pointer(&val);
	gtk_tree_view_set_enable_search (GTK_TREE_VIEW(gtkblist->treeview), TRUE);
	g_object_set(G_OBJECT(gtkblist->text_rend), "editable", FALSE, NULL);

	switch (node->type)
	{
		case GAIM_BLIST_CONTACT_NODE:
			{
				GaimContact *contact = (GaimContact *)node;
				struct _pidgin_blist_node *gtknode = (struct _pidgin_blist_node *)node->ui_data;

				if (contact->alias || gtknode->contact_expanded)
					gaim_blist_alias_contact(contact, arg2);
				else
				{
					GaimBuddy *buddy = gaim_contact_get_priority_buddy(contact);
					gaim_blist_alias_buddy(buddy, arg2);
					serv_alias_buddy(buddy);
				}
			}
			break;

		case GAIM_BLIST_BUDDY_NODE:
			gaim_blist_alias_buddy((GaimBuddy*)node, arg2);
			serv_alias_buddy((GaimBuddy *)node);
			break;
		case GAIM_BLIST_GROUP_NODE:
			dest = gaim_find_group(arg2);
			if (dest != NULL && strcmp(arg2, ((GaimGroup*) node)->name)) {
				pidgindialogs_merge_groups((GaimGroup*) node, arg2);
			} else
				gaim_blist_rename_group((GaimGroup*)node, arg2);
			break;
		case GAIM_BLIST_CHAT_NODE:
			gaim_blist_alias_chat((GaimChat*)node, arg2);
			break;
		default:
			break;
	}
}

static void gtk_blist_menu_alias_cb(GtkWidget *w, GaimBlistNode *node)
{
	GtkTreeIter iter;
	GtkTreePath *path;
	const char *text = NULL;
	char *esc;

	if (!(get_iter_from_node(node, &iter))) {
		/* This is either a bug, or the buddy is in a collapsed contact */
		node = node->parent;
		if (!get_iter_from_node(node, &iter))
			/* Now it's definitely a bug */
			return;
	}

	switch (node->type) {
	case GAIM_BLIST_BUDDY_NODE:
		text = gaim_buddy_get_alias((GaimBuddy *)node);
		break;
	case GAIM_BLIST_CONTACT_NODE:
		text = gaim_contact_get_alias((GaimContact *)node);
		break;
	case GAIM_BLIST_GROUP_NODE:
		text = ((GaimGroup *)node)->name;
		break;
	case GAIM_BLIST_CHAT_NODE:
		text = gaim_chat_get_name((GaimChat *)node);
		break;
	default:
		g_return_if_reached();
	}

	esc = g_markup_escape_text(text, -1);
	gtk_tree_store_set(gtkblist->treemodel, &iter, NAME_COLUMN, esc, -1);
	g_free(esc);

	path = gtk_tree_model_get_path(GTK_TREE_MODEL(gtkblist->treemodel), &iter);
	g_object_set(G_OBJECT(gtkblist->text_rend), "editable", TRUE, NULL);
	gtk_tree_view_set_enable_search (GTK_TREE_VIEW(gtkblist->treeview), FALSE);
	gtk_widget_grab_focus(gtkblist->treeview);
#if GTK_CHECK_VERSION(2,2,0)
	gtk_tree_view_set_cursor_on_cell(GTK_TREE_VIEW(gtkblist->treeview), path,
			gtkblist->text_column, gtkblist->text_rend, TRUE);
#else
	gtk_tree_view_set_cursor(GTK_TREE_VIEW(gtkblist->treeview), path, gtkblist->text_column, TRUE);
#endif
	gtk_tree_path_free(path);
}

static void gtk_blist_menu_bp_cb(GtkWidget *w, GaimBuddy *b)
{
	pidgin_pounce_editor_show(b->account, b->name, NULL);
}

static void gtk_blist_menu_showlog_cb(GtkWidget *w, GaimBlistNode *node)
{
	GaimLogType type;
	GaimAccount *account;
	char *name = NULL;

	pidgin_set_cursor(gtkblist->window, GDK_WATCH);

	if (GAIM_BLIST_NODE_IS_BUDDY(node)) {
		GaimBuddy *b = (GaimBuddy*) node;
		type = GAIM_LOG_IM;
		name = g_strdup(b->name);
		account = b->account;
	} else if (GAIM_BLIST_NODE_IS_CHAT(node)) {
		GaimChat *c = (GaimChat*) node;
		GaimPluginProtocolInfo *prpl_info = NULL;
		type = GAIM_LOG_CHAT;
		account = c->account;
		prpl_info = GAIM_PLUGIN_PROTOCOL_INFO(gaim_find_prpl(gaim_account_get_protocol_id(account)));
		if (prpl_info && prpl_info->get_chat_name) {
			name = prpl_info->get_chat_name(c->components);
		}
	} else if (GAIM_BLIST_NODE_IS_CONTACT(node)) {
		pidgin_log_show_contact((GaimContact *)node);
		pidgin_clear_cursor(gtkblist->window);
		return;
	} else {
		pidgin_clear_cursor(gtkblist->window);

		/* This callback should not have been registered for a node
		 * that doesn't match the type of one of the blocks above. */
		g_return_if_reached();
	}

	if (name && account) {
		pidgin_log_show(type, name, account);
		g_free(name);

		pidgin_clear_cursor(gtkblist->window);
	}
}

static void gtk_blist_show_systemlog_cb()
{
	pidgin_syslog_show();
}

static void gtk_blist_show_onlinehelp_cb()
{
	gaim_notify_uri(NULL, GAIM_WEBSITE "documentation.php");
}

static void
do_join_chat(PidginJoinChatData *data)
{
	if (data)
	{
		GHashTable *components =
			g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
		GList *tmp;
		GaimChat *chat;

		for (tmp = data->entries; tmp != NULL; tmp = tmp->next)
		{
			if (g_object_get_data(tmp->data, "is_spin"))
			{
				g_hash_table_replace(components,
					g_strdup(g_object_get_data(tmp->data, "identifier")),
					g_strdup_printf("%d",
							gtk_spin_button_get_value_as_int(tmp->data)));
			}
			else
			{
				g_hash_table_replace(components,
					g_strdup(g_object_get_data(tmp->data, "identifier")),
					g_strdup(gtk_entry_get_text(tmp->data)));
			}
		}

		chat = gaim_chat_new(data->account, NULL, components);
		gtk_blist_join_chat(chat);
		gaim_blist_remove_chat(chat);
	}
}

static void
do_joinchat(GtkWidget *dialog, int id, PidginJoinChatData *info)
{
	switch(id)
	{
		case GTK_RESPONSE_OK:
			do_join_chat(info);

		break;
	}

	gtk_widget_destroy(GTK_WIDGET(dialog));
	g_list_free(info->entries);
	g_free(info);
}

/*
 * Check the values of all the text entry boxes.  If any required input
 * strings are empty then don't allow the user to click on "OK."
 */
static void
joinchat_set_sensitive_if_input_cb(GtkWidget *entry, gpointer user_data)
{
	PidginJoinChatData *data;
	GList *tmp;
	const char *text;
	gboolean required;
	gboolean sensitive = TRUE;

	data = user_data;

	for (tmp = data->entries; tmp != NULL; tmp = tmp->next)
	{
		if (!g_object_get_data(tmp->data, "is_spin"))
		{
			required = GPOINTER_TO_INT(g_object_get_data(tmp->data, "required"));
			text = gtk_entry_get_text(tmp->data);
			if (required && (*text == '\0'))
				sensitive = FALSE;
		}
	}

	gtk_dialog_set_response_sensitive(GTK_DIALOG(data->window), GTK_RESPONSE_OK, sensitive);
}

static void
pidgin_blist_update_privacy_cb(GaimBuddy *buddy)
{
	pidgin_blist_update_buddy(gaim_get_blist(), (GaimBlistNode*)(buddy), TRUE);
}

static void
rebuild_joinchat_entries(PidginJoinChatData *data)
{
	GaimConnection *gc;
	GList *list = NULL, *tmp;
	GHashTable *defaults = NULL;
	struct proto_chat_entry *pce;
	gboolean focus = TRUE;

	g_return_if_fail(data->account != NULL);

	gc = gaim_account_get_connection(data->account);

	while ((tmp = gtk_container_get_children(GTK_CONTAINER(data->entries_box))))
		gtk_widget_destroy(tmp->data);

	g_list_free(data->entries);
	data->entries = NULL;

	if (GAIM_PLUGIN_PROTOCOL_INFO(gc->prpl)->chat_info != NULL)
		list = GAIM_PLUGIN_PROTOCOL_INFO(gc->prpl)->chat_info(gc);

	if (GAIM_PLUGIN_PROTOCOL_INFO(gc->prpl)->chat_info_defaults != NULL)
		defaults = GAIM_PLUGIN_PROTOCOL_INFO(gc->prpl)->chat_info_defaults(gc, NULL);

	for (tmp = list; tmp; tmp = tmp->next)
	{
		GtkWidget *label;
		GtkWidget *rowbox;
		GtkWidget *input;

		pce = tmp->data;

		rowbox = gtk_hbox_new(FALSE, GAIM_HIG_BORDER);
		gtk_box_pack_start(GTK_BOX(data->entries_box), rowbox, FALSE, FALSE, 0);

		label = gtk_label_new_with_mnemonic(pce->label);
		gtk_misc_set_alignment(GTK_MISC(label), 0, 0.5);
		gtk_size_group_add_widget(data->sg, label);
		gtk_box_pack_start(GTK_BOX(rowbox), label, FALSE, FALSE, 0);

		if (pce->is_int)
		{
			GtkObject *adjust;
			adjust = gtk_adjustment_new(pce->min, pce->min, pce->max,
										1, 10, 10);
			input = gtk_spin_button_new(GTK_ADJUSTMENT(adjust), 1, 0);
			gtk_widget_set_size_request(input, 50, -1);
			gtk_box_pack_end(GTK_BOX(rowbox), input, FALSE, FALSE, 0);
		}
		else
		{
			char *value;
			input = gtk_entry_new();
			gtk_entry_set_activates_default(GTK_ENTRY(input), TRUE);
			value = g_hash_table_lookup(defaults, pce->identifier);
			if (value != NULL)
				gtk_entry_set_text(GTK_ENTRY(input), value);
			if (pce->secret)
			{
				gtk_entry_set_visibility(GTK_ENTRY(input), FALSE);
				if (gtk_entry_get_invisible_char(GTK_ENTRY(input)) == '*')
					gtk_entry_set_invisible_char(GTK_ENTRY(input), GAIM_INVISIBLE_CHAR);
			}
			gtk_box_pack_end(GTK_BOX(rowbox), input, TRUE, TRUE, 0);
			g_signal_connect(G_OBJECT(input), "changed",
							 G_CALLBACK(joinchat_set_sensitive_if_input_cb), data);
		}

		/* Do the following for any type of input widget */
		if (focus)
		{
			gtk_widget_grab_focus(input);
			focus = FALSE;
		}
		gtk_label_set_mnemonic_widget(GTK_LABEL(label), input);
		pidgin_set_accessible_label(input, label);
		g_object_set_data(G_OBJECT(input), "identifier", (gpointer)pce->identifier);
		g_object_set_data(G_OBJECT(input), "is_spin", GINT_TO_POINTER(pce->is_int));
		g_object_set_data(G_OBJECT(input), "required", GINT_TO_POINTER(pce->required));
		data->entries = g_list_append(data->entries, input);

		g_free(pce);
	}

	g_list_free(list);
	g_hash_table_destroy(defaults);

	/* Set whether the "OK" button should be clickable initially */
	joinchat_set_sensitive_if_input_cb(NULL, data);

	gtk_widget_show_all(data->entries_box);
}

static void
joinchat_select_account_cb(GObject *w, GaimAccount *account,
                           PidginJoinChatData *data)
{
    data->account = account;
    rebuild_joinchat_entries(data);
}

static gboolean
chat_account_filter_func(GaimAccount *account)
{
	GaimConnection *gc = gaim_account_get_connection(account);
	GaimPluginProtocolInfo *prpl_info = NULL;

	prpl_info = GAIM_PLUGIN_PROTOCOL_INFO(gc->prpl);

	return (prpl_info->chat_info != NULL);
}

gboolean
pidgin_blist_joinchat_is_showable()
{
	GList *c;
	GaimConnection *gc;

	for (c = gaim_connections_get_all(); c != NULL; c = c->next) {
		gc = c->data;

		if (chat_account_filter_func(gaim_connection_get_account(gc)))
			return TRUE;
	}

	return FALSE;
}

void
pidgin_blist_joinchat_show(void)
{
	GtkWidget *hbox, *vbox;
	GtkWidget *rowbox;
	GtkWidget *label;
	PidginBuddyList *gtkblist;
	GtkWidget *img = NULL;
	PidginJoinChatData *data = NULL;

	gtkblist = PIDGIN_BLIST(gaim_get_blist());
	img = gtk_image_new_from_stock(PIDGIN_STOCK_DIALOG_QUESTION,
					gtk_icon_size_from_name(PIDGIN_ICON_SIZE_TANGO_HUGE));
	data = g_new0(PidginJoinChatData, 1);

	data->window = gtk_dialog_new_with_buttons(_("Join a Chat"),
		NULL, GTK_DIALOG_NO_SEPARATOR,
		GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
		PIDGIN_STOCK_CHAT, GTK_RESPONSE_OK, NULL);
	gtk_dialog_set_default_response(GTK_DIALOG(data->window), GTK_RESPONSE_OK);
	gtk_container_set_border_width(GTK_CONTAINER(data->window), GAIM_HIG_BOX_SPACE);
	gtk_window_set_resizable(GTK_WINDOW(data->window), FALSE);
	gtk_box_set_spacing(GTK_BOX(GTK_DIALOG(data->window)->vbox), GAIM_HIG_BORDER);
	gtk_container_set_border_width(
		GTK_CONTAINER(GTK_DIALOG(data->window)->vbox), GAIM_HIG_BOX_SPACE);
	gtk_window_set_role(GTK_WINDOW(data->window), "join_chat");

	hbox = gtk_hbox_new(FALSE, GAIM_HIG_BORDER);
	gtk_container_add(GTK_CONTAINER(GTK_DIALOG(data->window)->vbox), hbox);
	gtk_box_pack_start(GTK_BOX(hbox), img, FALSE, FALSE, 0);
	gtk_misc_set_alignment(GTK_MISC(img), 0, 0);

	vbox = gtk_vbox_new(FALSE, 5);
	gtk_container_set_border_width(GTK_CONTAINER(vbox), 0);
	gtk_container_add(GTK_CONTAINER(hbox), vbox);

	label = gtk_label_new(_("Please enter the appropriate information "
							"about the chat you would like to join.\n"));
	gtk_label_set_line_wrap(GTK_LABEL(label), TRUE);
	gtk_misc_set_alignment(GTK_MISC(label), 0, 0);
	gtk_box_pack_start(GTK_BOX(vbox), label, FALSE, FALSE, 0);

	rowbox = gtk_hbox_new(FALSE, GAIM_HIG_BORDER);
	gtk_box_pack_start(GTK_BOX(vbox), rowbox, TRUE, TRUE, 0);

	data->sg = gtk_size_group_new(GTK_SIZE_GROUP_HORIZONTAL);

	label = gtk_label_new_with_mnemonic(_("_Account:"));
	gtk_misc_set_alignment(GTK_MISC(label), 0, 0.5);
	gtk_box_pack_start(GTK_BOX(rowbox), label, FALSE, FALSE, 0);
	gtk_size_group_add_widget(data->sg, label);

	data->account_menu = pidgin_account_option_menu_new(NULL, FALSE,
			G_CALLBACK(joinchat_select_account_cb),
			chat_account_filter_func, data);
	gtk_box_pack_start(GTK_BOX(rowbox), data->account_menu, TRUE, TRUE, 0);
	gtk_label_set_mnemonic_widget(GTK_LABEL(label),
								  GTK_WIDGET(data->account_menu));
	pidgin_set_accessible_label (data->account_menu, label);

	data->entries_box = gtk_vbox_new(FALSE, 5);
	gtk_container_add(GTK_CONTAINER(vbox), data->entries_box);
	gtk_container_set_border_width(GTK_CONTAINER(data->entries_box), 0);

	data->account =	pidgin_account_option_menu_get_selected(data->account_menu);

	rebuild_joinchat_entries(data);

	g_signal_connect(G_OBJECT(data->window), "response",
					 G_CALLBACK(do_joinchat), data);

	g_object_unref(data->sg);

	gtk_widget_show_all(data->window);
}

static void gtk_blist_row_expanded_cb(GtkTreeView *tv, GtkTreeIter *iter, GtkTreePath *path, gpointer user_data) {
	GaimBlistNode *node;
	GValue val;

	val.g_type = 0;
	gtk_tree_model_get_value(GTK_TREE_MODEL(gtkblist->treemodel), iter, NODE_COLUMN, &val);

	node = g_value_get_pointer(&val);

	if (GAIM_BLIST_NODE_IS_GROUP(node)) {
		char *title;

		title = gaim_get_group_title(node, TRUE);

		gtk_tree_store_set(gtkblist->treemodel, iter,
		   NAME_COLUMN, title,
		   -1);

		g_free(title);

		gaim_blist_node_set_bool(node, "collapsed", FALSE);
	}
}

static void gtk_blist_row_collapsed_cb(GtkTreeView *tv, GtkTreeIter *iter, GtkTreePath *path, gpointer user_data) {
	GaimBlistNode *node;
	GValue val;

	val.g_type = 0;
	gtk_tree_model_get_value(GTK_TREE_MODEL(gtkblist->treemodel), iter, NODE_COLUMN, &val);

	node = g_value_get_pointer(&val);

	if (GAIM_BLIST_NODE_IS_GROUP(node)) {
		char *title;

		title = gaim_get_group_title(node, FALSE);

		gtk_tree_store_set(gtkblist->treemodel, iter,
		   NAME_COLUMN, title,
		   -1);

		g_free(title);

		gaim_blist_node_set_bool(node, "collapsed", TRUE);
	} else if(GAIM_BLIST_NODE_IS_CONTACT(node)) {
		pidgin_blist_collapse_contact_cb(NULL, node);
	}
}

static void gtk_blist_row_activated_cb(GtkTreeView *tv, GtkTreePath *path, GtkTreeViewColumn *col, gpointer data) {
	GaimBlistNode *node;
	GtkTreeIter iter;
	GValue val;

	gtk_tree_model_get_iter(GTK_TREE_MODEL(gtkblist->treemodel), &iter, path);

	val.g_type = 0;
	gtk_tree_model_get_value (GTK_TREE_MODEL(gtkblist->treemodel), &iter, NODE_COLUMN, &val);
	node = g_value_get_pointer(&val);

	if(GAIM_BLIST_NODE_IS_CONTACT(node) || GAIM_BLIST_NODE_IS_BUDDY(node)) {
		GaimBuddy *buddy;

		if(GAIM_BLIST_NODE_IS_CONTACT(node))
			buddy = gaim_contact_get_priority_buddy((GaimContact*)node);
		else
			buddy = (GaimBuddy*)node;

		pidgindialogs_im_with_user(buddy->account, buddy->name);
	} else if (GAIM_BLIST_NODE_IS_CHAT(node)) {
		gtk_blist_join_chat((GaimChat *)node);
	} else if (GAIM_BLIST_NODE_IS_GROUP(node)) {
/*		if (gtk_tree_view_row_expanded(tv, path))
			gtk_tree_view_collapse_row(tv, path);
		else
			gtk_tree_view_expand_row(tv,path,FALSE);*/
	}
}

static void pidgin_blist_add_chat_cb()
{
	GtkTreeSelection *sel = gtk_tree_view_get_selection(GTK_TREE_VIEW(gtkblist->treeview));
	GtkTreeIter iter;
	GaimBlistNode *node;

	if(gtk_tree_selection_get_selected(sel, NULL, &iter)){
		gtk_tree_model_get(GTK_TREE_MODEL(gtkblist->treemodel), &iter, NODE_COLUMN, &node, -1);
		if (GAIM_BLIST_NODE_IS_BUDDY(node))
			gaim_blist_request_add_chat(NULL, (GaimGroup*)node->parent->parent, NULL, NULL);
		if (GAIM_BLIST_NODE_IS_CONTACT(node) || GAIM_BLIST_NODE_IS_CHAT(node))
			gaim_blist_request_add_chat(NULL, (GaimGroup*)node->parent, NULL, NULL);
		else if (GAIM_BLIST_NODE_IS_GROUP(node))
			gaim_blist_request_add_chat(NULL, (GaimGroup*)node, NULL, NULL);
	}
	else {
		gaim_blist_request_add_chat(NULL, NULL, NULL, NULL);
	}
}

static void pidgin_blist_add_buddy_cb()
{
	GtkTreeSelection *sel = gtk_tree_view_get_selection(GTK_TREE_VIEW(gtkblist->treeview));
	GtkTreeIter iter;
	GaimBlistNode *node;

	if(gtk_tree_selection_get_selected(sel, NULL, &iter)){
		gtk_tree_model_get(GTK_TREE_MODEL(gtkblist->treemodel), &iter, NODE_COLUMN, &node, -1);
		if (GAIM_BLIST_NODE_IS_BUDDY(node)) {
			gaim_blist_request_add_buddy(NULL, NULL, ((GaimGroup*)node->parent->parent)->name,
					NULL);
		} else if (GAIM_BLIST_NODE_IS_CONTACT(node)
				|| GAIM_BLIST_NODE_IS_CHAT(node)) {
			gaim_blist_request_add_buddy(NULL, NULL, ((GaimGroup*)node->parent)->name, NULL);
		} else if (GAIM_BLIST_NODE_IS_GROUP(node)) {
			gaim_blist_request_add_buddy(NULL, NULL, ((GaimGroup*)node)->name, NULL);
		}
	}
	else {
		gaim_blist_request_add_buddy(NULL, NULL, NULL, NULL);
	}
}

static void
pidgin_blist_remove_cb (GtkWidget *w, GaimBlistNode *node)
{
	if (GAIM_BLIST_NODE_IS_BUDDY(node)) {
		pidgindialogs_remove_buddy((GaimBuddy*)node);
	} else if (GAIM_BLIST_NODE_IS_CHAT(node)) {
		pidgindialogs_remove_chat((GaimChat*)node);
	} else if (GAIM_BLIST_NODE_IS_GROUP(node)) {
		pidgindialogs_remove_group((GaimGroup*)node);
	} else if (GAIM_BLIST_NODE_IS_CONTACT(node)) {
		pidgindialogs_remove_contact((GaimContact*)node);
	}
}

struct _expand {
	GtkTreeView *treeview;
	GtkTreePath *path;
	GaimBlistNode *node;
};

static gboolean
scroll_to_expanded_cell(gpointer data)
{
	struct _expand *ex = data;
	gtk_tree_view_scroll_to_cell(ex->treeview, ex->path, NULL, FALSE, 0, 0);
	pidgin_blist_update_contact(NULL, ex->node);

	gtk_tree_path_free(ex->path);
	g_free(ex);

	return FALSE;
}

static void
pidgin_blist_expand_contact_cb(GtkWidget *w, GaimBlistNode *node)
{
	struct _pidgin_blist_node *gtknode;
	GtkTreeIter iter, parent;
	GaimBlistNode *bnode;
	GtkTreePath *path;

	if(!GAIM_BLIST_NODE_IS_CONTACT(node))
		return;

	gtknode = (struct _pidgin_blist_node *)node->ui_data;

	gtknode->contact_expanded = TRUE;

	for(bnode = node->child; bnode; bnode = bnode->next) {
		pidgin_blist_update(NULL, bnode);
	}

	/* This ensures that the bottom buddy is visible, i.e. not scrolled off the alignment */
	if (get_iter_from_node(node, &parent)) {
		struct _expand *ex = g_new0(struct _expand, 1);

		gtk_tree_model_iter_nth_child(GTK_TREE_MODEL(gtkblist->treemodel), &iter, &parent,
				  gtk_tree_model_iter_n_children(GTK_TREE_MODEL(gtkblist->treemodel), &parent) -1);
		path = gtk_tree_model_get_path(GTK_TREE_MODEL(gtkblist->treemodel), &iter);

		/* Let the treeview draw so it knows where to scroll */
		ex->treeview = GTK_TREE_VIEW(gtkblist->treeview);
		ex->path = path;
		ex->node = node->child;
		g_idle_add(scroll_to_expanded_cell, ex);
	}
}

static void
pidgin_blist_collapse_contact_cb(GtkWidget *w, GaimBlistNode *node)
{
	GaimBlistNode *bnode;
	struct _pidgin_blist_node *gtknode;

	if(!GAIM_BLIST_NODE_IS_CONTACT(node))
		return;

	gtknode = (struct _pidgin_blist_node *)node->ui_data;

	gtknode->contact_expanded = FALSE;

	for(bnode = node->child; bnode; bnode = bnode->next) {
		pidgin_blist_update(NULL, bnode);
	}
}

static void
toggle_privacy(GtkWidget *widget, GaimBlistNode *node)
{
	GaimBuddy *buddy;
	GaimAccount *account;
	gboolean permitted;
	const char *name;

	if (!GAIM_BLIST_NODE_IS_BUDDY(node))
		return;

	buddy = (GaimBuddy *)node;
	account = gaim_buddy_get_account(buddy);
	name = gaim_buddy_get_name(buddy);

	permitted = gaim_privacy_check(account, name);

	/* XXX: Perhaps ask whether to restore the previous lists where appropirate? */

	if (permitted)
		gaim_privacy_deny(account, name, FALSE, FALSE);
	else
		gaim_privacy_allow(account, name, FALSE, FALSE);

	pidgin_blist_update(gaim_get_blist(), node);
}

void pidgin_append_blist_node_privacy_menu(GtkWidget *menu, GaimBlistNode *node)
{
	GaimBuddy *buddy = (GaimBuddy *)node;
	GaimAccount *account;
	gboolean permitted;

	account = gaim_buddy_get_account(buddy);
	permitted = gaim_privacy_check(account, gaim_buddy_get_name(buddy));

	pidgin_new_item_from_stock(menu, permitted ? _("_Block") : _("Un_block"),
						PIDGIN_STOCK_BLOCK, G_CALLBACK(toggle_privacy),
						node, 0 ,0, NULL);
}

void
pidgin_append_blist_node_proto_menu(GtkWidget *menu, GaimConnection *gc,
                                      GaimBlistNode *node)
{
	GList *l, *ll;
	GaimPluginProtocolInfo *prpl_info = GAIM_PLUGIN_PROTOCOL_INFO(gc->prpl);

	if(!prpl_info || !prpl_info->blist_node_menu)
		return;

	for(l = ll = prpl_info->blist_node_menu(node); l; l = l->next) {
		GaimMenuAction *act = (GaimMenuAction *) l->data;
		pidgin_append_menu_action(menu, act, node);
	}
	g_list_free(ll);
}

void
pidgin_append_blist_node_extended_menu(GtkWidget *menu, GaimBlistNode *node)
{
	GList *l, *ll;

	for(l = ll = gaim_blist_node_get_extended_menu(node); l; l = l->next) {
		GaimMenuAction *act = (GaimMenuAction *) l->data;
		pidgin_append_menu_action(menu, act, node);
	}
	g_list_free(ll);
}

void
pidgin_blist_make_buddy_menu(GtkWidget *menu, GaimBuddy *buddy, gboolean sub) {
	GaimPluginProtocolInfo *prpl_info;
	GaimContact *contact;
	gboolean contact_expanded = FALSE;

	g_return_if_fail(menu);
	g_return_if_fail(buddy);

	prpl_info = GAIM_PLUGIN_PROTOCOL_INFO(buddy->account->gc->prpl);

	contact = gaim_buddy_get_contact(buddy);
	if (contact) {
		contact_expanded = ((struct _pidgin_blist_node *)(((GaimBlistNode*)contact)->ui_data))->contact_expanded;
	}

	if (prpl_info && prpl_info->get_info) {
		pidgin_new_item_from_stock(menu, _("Get _Info"), PIDGIN_STOCK_TOOLBAR_USER_INFO,
				G_CALLBACK(gtk_blist_menu_info_cb), buddy, 0, 0, NULL);
	}
	pidgin_new_item_from_stock(menu, _("I_M"), PIDGIN_STOCK_TOOLBAR_MESSAGE_NEW,
			G_CALLBACK(gtk_blist_menu_im_cb), buddy, 0, 0, NULL);
	if (prpl_info && prpl_info->send_file) {
		if (!prpl_info->can_receive_file ||
			prpl_info->can_receive_file(buddy->account->gc, buddy->name))
		{
			pidgin_new_item_from_stock(menu, _("_Send File"),
									 PIDGIN_STOCK_FILE_TRANSFER,
									 G_CALLBACK(gtk_blist_menu_send_file_cb),
									 buddy, 0, 0, NULL);
		}
	}

	pidgin_new_item_from_stock(menu, _("Add Buddy _Pounce"), NULL,
			G_CALLBACK(gtk_blist_menu_bp_cb), buddy, 0, 0, NULL);

	if(((GaimBlistNode*)buddy)->parent->child->next && !sub && !contact_expanded) {
		pidgin_new_item_from_stock(menu, _("View _Log"), NULL,
				G_CALLBACK(gtk_blist_menu_showlog_cb),
				contact, 0, 0, NULL);
	} else if (!sub) {
		pidgin_new_item_from_stock(menu, _("View _Log"), NULL,
				G_CALLBACK(gtk_blist_menu_showlog_cb), buddy, 0, 0, NULL);
	}


	pidgin_append_blist_node_proto_menu(menu, buddy->account->gc,
										  (GaimBlistNode *)buddy);
	pidgin_append_blist_node_extended_menu(menu, (GaimBlistNode *)buddy);

	if (((GaimBlistNode*)buddy)->parent->child->next && !sub && !contact_expanded) {
		pidgin_separator(menu);
		pidgin_append_blist_node_privacy_menu(menu, (GaimBlistNode *)buddy);
		pidgin_new_item_from_stock(menu, _("Alias..."), PIDGIN_STOCK_ALIAS,
				G_CALLBACK(gtk_blist_menu_alias_cb),
				contact, 0, 0, NULL);
		pidgin_new_item_from_stock(menu, _("Remove"), GTK_STOCK_REMOVE,
				G_CALLBACK(pidgin_blist_remove_cb),
				contact, 0, 0, NULL);
	} else if (!sub || contact_expanded) {
		pidgin_separator(menu);
		pidgin_append_blist_node_privacy_menu(menu, (GaimBlistNode *)buddy);
		pidgin_new_item_from_stock(menu, _("_Alias..."), PIDGIN_STOCK_ALIAS,
				G_CALLBACK(gtk_blist_menu_alias_cb), buddy, 0, 0, NULL);
		pidgin_new_item_from_stock(menu, _("_Remove"), GTK_STOCK_REMOVE,
				G_CALLBACK(pidgin_blist_remove_cb), buddy,
				0, 0, NULL);
	}
}

static gboolean
gtk_blist_key_press_cb(GtkWidget *tv, GdkEventKey *event, gpointer data) {
	GaimBlistNode *node;
	GValue val;
	GtkTreeIter iter;
	GtkTreeSelection *sel;

	sel = gtk_tree_view_get_selection(GTK_TREE_VIEW(tv));
	if(!gtk_tree_selection_get_selected(sel, NULL, &iter))
		return FALSE;

	val.g_type = 0;
	gtk_tree_model_get_value(GTK_TREE_MODEL(gtkblist->treemodel), &iter,
			NODE_COLUMN, &val);
	node = g_value_get_pointer(&val);

	if(event->state & GDK_CONTROL_MASK &&
			(event->keyval == 'o' || event->keyval == 'O')) {
		GaimBuddy *buddy;

		if(GAIM_BLIST_NODE_IS_CONTACT(node)) {
			buddy = gaim_contact_get_priority_buddy((GaimContact*)node);
		} else if(GAIM_BLIST_NODE_IS_BUDDY(node)) {
			buddy = (GaimBuddy*)node;
		} else {
			return FALSE;
		}
		if(buddy)
			serv_get_info(buddy->account->gc, buddy->name);
	}

	return FALSE;
}

static GtkWidget *
create_group_menu (GaimBlistNode *node, GaimGroup *g)
{
	GtkWidget *menu;
	GtkWidget *item;

	menu = gtk_menu_new();
	pidgin_new_item_from_stock(menu, _("Add a _Buddy"), GTK_STOCK_ADD,
				 G_CALLBACK(pidgin_blist_add_buddy_cb), node, 0, 0, NULL);
	item = pidgin_new_item_from_stock(menu, _("Add a C_hat"), GTK_STOCK_ADD,
				 G_CALLBACK(pidgin_blist_add_chat_cb), node, 0, 0, NULL);
	gtk_widget_set_sensitive(item, pidgin_blist_joinchat_is_showable());
	pidgin_new_item_from_stock(menu, _("_Delete Group"), GTK_STOCK_REMOVE,
				 G_CALLBACK(pidgin_blist_remove_cb), node, 0, 0, NULL);
	pidgin_new_item_from_stock(menu, _("_Rename"), NULL,
				 G_CALLBACK(gtk_blist_menu_alias_cb), node, 0, 0, NULL);

	pidgin_append_blist_node_extended_menu(menu, node);

	return menu;
}


static GtkWidget *
create_chat_menu(GaimBlistNode *node, GaimChat *c) {
	GtkWidget *menu;
	gboolean autojoin;

	menu = gtk_menu_new();
	autojoin = (gaim_blist_node_get_bool(node, "gtk-autojoin") ||
			(gaim_blist_node_get_string(node, "gtk-autojoin") != NULL));

	pidgin_new_item_from_stock(menu, _("_Join"), PIDGIN_STOCK_CHAT,
			G_CALLBACK(gtk_blist_menu_join_cb), node, 0, 0, NULL);
	pidgin_new_check_item(menu, _("Auto-Join"),
			G_CALLBACK(gtk_blist_menu_autojoin_cb), node, autojoin);
	pidgin_new_item_from_stock(menu, _("View _Log"), NULL,
			G_CALLBACK(gtk_blist_menu_showlog_cb), node, 0, 0, NULL);

	pidgin_append_blist_node_proto_menu(menu, c->account->gc, node);
	pidgin_append_blist_node_extended_menu(menu, node);

	pidgin_separator(menu);

	pidgin_new_item_from_stock(menu, _("_Alias..."), PIDGIN_STOCK_ALIAS,
				 G_CALLBACK(gtk_blist_menu_alias_cb), node, 0, 0, NULL);
	pidgin_new_item_from_stock(menu, _("_Remove"), GTK_STOCK_REMOVE,
				 G_CALLBACK(pidgin_blist_remove_cb), node, 0, 0, NULL);

	return menu;
}

static GtkWidget *
create_contact_menu (GaimBlistNode *node)
{
	GtkWidget *menu;

	menu = gtk_menu_new();

	pidgin_new_item_from_stock(menu, _("View _Log"), NULL,
				 G_CALLBACK(gtk_blist_menu_showlog_cb),
				 node, 0, 0, NULL);

	pidgin_separator(menu);

	pidgin_new_item_from_stock(menu, _("_Alias..."), PIDGIN_STOCK_ALIAS,
				 G_CALLBACK(gtk_blist_menu_alias_cb), node, 0, 0, NULL);
	pidgin_new_item_from_stock(menu, _("_Remove"), GTK_STOCK_REMOVE,
				 G_CALLBACK(pidgin_blist_remove_cb), node, 0, 0, NULL);

	pidgin_separator(menu);

	pidgin_new_item_from_stock(menu, _("_Collapse"), GTK_STOCK_ZOOM_OUT,
				 G_CALLBACK(pidgin_blist_collapse_contact_cb),
				 node, 0, 0, NULL);

	pidgin_append_blist_node_extended_menu(menu, node);

	return menu;
}

static GtkWidget *
create_buddy_menu(GaimBlistNode *node, GaimBuddy *b) {
	struct _pidgin_blist_node *gtknode = (struct _pidgin_blist_node *)node->ui_data;
	GtkWidget *menu;
	GtkWidget *menuitem;
	gboolean show_offline = gaim_prefs_get_bool("/gaim/gtk/blist/show_offline_buddies");

	menu = gtk_menu_new();
	pidgin_blist_make_buddy_menu(menu, b, FALSE);

	if(GAIM_BLIST_NODE_IS_CONTACT(node)) {
		pidgin_separator(menu);

		if(gtknode->contact_expanded) {
			pidgin_new_item_from_stock(menu, _("_Collapse"),
						 GTK_STOCK_ZOOM_OUT,
						 G_CALLBACK(pidgin_blist_collapse_contact_cb),
						 node, 0, 0, NULL);
		} else {
			pidgin_new_item_from_stock(menu, _("_Expand"),
						 GTK_STOCK_ZOOM_IN,
						 G_CALLBACK(pidgin_blist_expand_contact_cb), node,
						 0, 0, NULL);
		}
		if(node->child->next) {
			GaimBlistNode *bnode;

			for(bnode = node->child; bnode; bnode = bnode->next) {
				GaimBuddy *buddy = (GaimBuddy*)bnode;
				GdkPixbuf *buf;
				GtkWidget *submenu;
				GtkWidget *image;

				if(buddy == b)
					continue;
				if(!buddy->account->gc)
					continue;
				if(!show_offline && !GAIM_BUDDY_IS_ONLINE(buddy))
					continue;

				menuitem = gtk_image_menu_item_new_with_label(buddy->name);
				buf = pidgin_create_prpl_icon(buddy->account,PIDGIN_PRPL_ICON_SMALL);
				image = gtk_image_new_from_pixbuf(buf);
				g_object_unref(G_OBJECT(buf));
				gtk_image_menu_item_set_image(GTK_IMAGE_MENU_ITEM(menuitem),
											  image);
				gtk_widget_show(image);
				gtk_menu_shell_append(GTK_MENU_SHELL(menu), menuitem);
				gtk_widget_show(menuitem);

				submenu = gtk_menu_new();
				gtk_menu_item_set_submenu(GTK_MENU_ITEM(menuitem), submenu);
				gtk_widget_show(submenu);

				pidgin_blist_make_buddy_menu(submenu, buddy, TRUE);
			}
		}
	}
	return menu;
}

static gboolean
pidgin_blist_show_context_menu(GaimBlistNode *node,
								 GtkMenuPositionFunc func,
								 GtkWidget *tv,
								 guint button,
								 guint32 time)
{
	struct _pidgin_blist_node *gtknode;
	GtkWidget *menu = NULL;
	gboolean handled = FALSE;

	gtknode = (struct _pidgin_blist_node *)node->ui_data;

	/* Create a menu based on the thing we right-clicked on */
	if (GAIM_BLIST_NODE_IS_GROUP(node)) {
		GaimGroup *g = (GaimGroup *)node;

		menu = create_group_menu(node, g);
	} else if (GAIM_BLIST_NODE_IS_CHAT(node)) {
		GaimChat *c = (GaimChat *)node;

		menu = create_chat_menu(node, c);
	} else if ((GAIM_BLIST_NODE_IS_CONTACT(node)) && (gtknode->contact_expanded)) {
		menu = create_contact_menu(node);
	} else if (GAIM_BLIST_NODE_IS_CONTACT(node) || GAIM_BLIST_NODE_IS_BUDDY(node)) {
		GaimBuddy *b;

		if (GAIM_BLIST_NODE_IS_CONTACT(node))
			b = gaim_contact_get_priority_buddy((GaimContact*)node);
		else
			b = (GaimBuddy *)node;

		menu = create_buddy_menu(node, b);
	}

#ifdef _WIN32
	/* Unhook the tooltip-timeout since we don't want a tooltip
	 * to appear and obscure the context menu we are about to show
	   This is a workaround for GTK+ bug 107320. */
	if (gtkblist->timeout) {
		g_source_remove(gtkblist->timeout);
		gtkblist->timeout = 0;
	}
#endif

	/* Now display the menu */
	if (menu != NULL) {
		gtk_widget_show_all(menu);
		gtk_menu_popup(GTK_MENU(menu), NULL, NULL, func, tv, button, time);
		handled = TRUE;
	}

	return handled;
}

static gboolean gtk_blist_button_press_cb(GtkWidget *tv, GdkEventButton *event, gpointer user_data)
{
	GtkTreePath *path;
	GaimBlistNode *node;
	GValue val;
	GtkTreeIter iter;
	GtkTreeSelection *sel;
	GaimPlugin *prpl = NULL;
	GaimPluginProtocolInfo *prpl_info = NULL;
	struct _pidgin_blist_node *gtknode;
	gboolean handled = FALSE;

	/* Here we figure out which node was clicked */
	if (!gtk_tree_view_get_path_at_pos(GTK_TREE_VIEW(tv), event->x, event->y, &path, NULL, NULL, NULL))
		return FALSE;
	gtk_tree_model_get_iter(GTK_TREE_MODEL(gtkblist->treemodel), &iter, path);
	val.g_type = 0;
	gtk_tree_model_get_value(GTK_TREE_MODEL(gtkblist->treemodel), &iter, NODE_COLUMN, &val);
	node = g_value_get_pointer(&val);
	gtknode = (struct _pidgin_blist_node *)node->ui_data;

	/* Right click draws a context menu */
	if ((event->button == 3) && (event->type == GDK_BUTTON_PRESS)) {
		handled = pidgin_blist_show_context_menu(node, NULL, tv, 3, event->time);

	/* CTRL+middle click expands or collapse a contact */
	} else if ((event->button == 2) && (event->type == GDK_BUTTON_PRESS) &&
			   (event->state & GDK_CONTROL_MASK) && (GAIM_BLIST_NODE_IS_CONTACT(node))) {
		if (gtknode->contact_expanded)
			pidgin_blist_collapse_contact_cb(NULL, node);
		else
			pidgin_blist_expand_contact_cb(NULL, node);
		handled = TRUE;

	/* Double middle click gets info */
	} else if ((event->button == 2) && (event->type == GDK_2BUTTON_PRESS) &&
			   ((GAIM_BLIST_NODE_IS_CONTACT(node)) || (GAIM_BLIST_NODE_IS_BUDDY(node)))) {
		GaimBuddy *b;
		if(GAIM_BLIST_NODE_IS_CONTACT(node))
			b = gaim_contact_get_priority_buddy((GaimContact*)node);
		else
			b = (GaimBuddy *)node;

		prpl = gaim_find_prpl(gaim_account_get_protocol_id(b->account));
		if (prpl != NULL)
			prpl_info = GAIM_PLUGIN_PROTOCOL_INFO(prpl);

		if (prpl && prpl_info->get_info)
			serv_get_info(b->account->gc, b->name);
		handled = TRUE;
	}

#if (1)
	/*
	 * This code only exists because GTK+ doesn't work.  If we return
	 * FALSE here, as would be normal the event propoagates down and
	 * somehow gets interpreted as the start of a drag event.
	 *
	 * Um, isn't it _normal_ to return TRUE here?  Since the event
	 * was handled?  --Mark
	 */
	if(handled) {
		sel = gtk_tree_view_get_selection(GTK_TREE_VIEW(tv));
		gtk_tree_selection_select_path(sel, path);
		gtk_tree_path_free(path);
		return TRUE;
	}
#endif
	gtk_tree_path_free(path);

	return FALSE;
}

static gboolean
pidgin_blist_popup_menu_cb(GtkWidget *tv, void *user_data)
{
	GaimBlistNode *node;
	GValue val;
	GtkTreeIter iter;
	GtkTreeSelection *sel;
	gboolean handled = FALSE;

	sel = gtk_tree_view_get_selection(GTK_TREE_VIEW(tv));
	if (!gtk_tree_selection_get_selected(sel, NULL, &iter))
		return FALSE;

	val.g_type = 0;
	gtk_tree_model_get_value(GTK_TREE_MODEL(gtkblist->treemodel),
							 &iter, NODE_COLUMN, &val);
	node = g_value_get_pointer(&val);

	/* Shift+F10 draws a context menu */
	handled = pidgin_blist_show_context_menu(node, pidgin_treeview_popup_menu_position_func, tv, 0, GDK_CURRENT_TIME);

	return handled;
}

static void pidgin_blist_buddy_details_cb(gpointer data, guint action, GtkWidget *item)
{
	pidgin_set_cursor(gtkblist->window, GDK_WATCH);

	gaim_prefs_set_bool("/gaim/gtk/blist/show_buddy_icons",
			    gtk_check_menu_item_get_active(GTK_CHECK_MENU_ITEM(item)));

	pidgin_clear_cursor(gtkblist->window);
}

static void pidgin_blist_show_idle_time_cb(gpointer data, guint action, GtkWidget *item)
{
	pidgin_set_cursor(gtkblist->window, GDK_WATCH);

	gaim_prefs_set_bool("/gaim/gtk/blist/show_idle_time",
			    gtk_check_menu_item_get_active(GTK_CHECK_MENU_ITEM(item)));

	pidgin_clear_cursor(gtkblist->window);
}

static void pidgin_blist_show_empty_groups_cb(gpointer data, guint action, GtkWidget *item)
{
	pidgin_set_cursor(gtkblist->window, GDK_WATCH);

	gaim_prefs_set_bool("/gaim/gtk/blist/show_empty_groups",
			gtk_check_menu_item_get_active(GTK_CHECK_MENU_ITEM(item)));

	pidgin_clear_cursor(gtkblist->window);
}

static void pidgin_blist_edit_mode_cb(gpointer callback_data, guint callback_action,
		GtkWidget *checkitem)
{
	pidgin_set_cursor(gtkblist->window, GDK_WATCH);

	gaim_prefs_set_bool("/gaim/gtk/blist/show_offline_buddies",
			gtk_check_menu_item_get_active(GTK_CHECK_MENU_ITEM(checkitem)));

	pidgin_clear_cursor(gtkblist->window);
}

static void pidgin_blist_mute_sounds_cb(gpointer data, guint action, GtkWidget *item)
{
	gaim_prefs_set_bool("/gaim/gtk/sound/mute", GTK_CHECK_MENU_ITEM(item)->active);
}

static void
pidgin_blist_mute_pref_cb(const char *name, GaimPrefType type,
							gconstpointer value, gpointer data)
{
	gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(gtk_item_factory_get_item(gtkblist->ift,
						N_("/Tools/Mute Sounds"))),	(gboolean)GPOINTER_TO_INT(value));
}

static void
pidgin_blist_sound_method_pref_cb(const char *name, GaimPrefType type,
									gconstpointer value, gpointer data)
{
	gboolean sensitive = TRUE;

	if(!strcmp(value, "none"))
		sensitive = FALSE;

	gtk_widget_set_sensitive(gtk_item_factory_get_widget(gtkblist->ift, N_("/Tools/Mute Sounds")), sensitive);
}

static void
add_buddies_from_vcard(const char *prpl_id, GaimGroup *group, GList *list,
					   const char *alias)
{
	GList *l;
	GaimAccount *account = NULL;
	GaimConnection *gc;

	if (list == NULL)
		return;

	for (l = gaim_connections_get_all(); l != NULL; l = l->next)
	{
		gc = (GaimConnection *)l->data;
		account = gaim_connection_get_account(gc);

		if (!strcmp(gaim_account_get_protocol_id(account), prpl_id))
			break;

		account = NULL;
	}

	if (account != NULL)
	{
		for (l = list; l != NULL; l = l->next)
		{
			gaim_blist_request_add_buddy(account, l->data,
										 (group ? group->name : NULL),
										 alias);
		}
	}

	g_list_foreach(list, (GFunc)g_free, NULL);
	g_list_free(list);
}

static gboolean
parse_vcard(const char *vcard, GaimGroup *group)
{
	char *temp_vcard;
	char *s, *c;
	char *alias    = NULL;
	GList *aims    = NULL;
	GList *icqs    = NULL;
	GList *yahoos  = NULL;
	GList *msns    = NULL;
	GList *jabbers = NULL;

	s = temp_vcard = g_strdup(vcard);

	while (*s != '\0' && strncmp(s, "END:vCard", strlen("END:vCard")))
	{
		char *field, *value;

		field = s;

		/* Grab the field */
		while (*s != '\r' && *s != '\n' && *s != '\0' && *s != ':')
			s++;

		if (*s == '\r') s++;
		if (*s == '\n')
		{
			s++;
			continue;
		}

		if (*s != '\0') *s++ = '\0';

		if ((c = strchr(field, ';')) != NULL)
			*c = '\0';

		/* Proceed to the end of the line */
		value = s;

		while (*s != '\r' && *s != '\n' && *s != '\0')
			s++;

		if (*s == '\r') *s++ = '\0';
		if (*s == '\n') *s++ = '\0';

		/* We only want to worry about a few fields here. */
		if (!strcmp(field, "FN"))
			alias = g_strdup(value);
		else if (!strcmp(field, "X-AIM") || !strcmp(field, "X-ICQ") ||
				 !strcmp(field, "X-YAHOO") || !strcmp(field, "X-MSN") ||
				 !strcmp(field, "X-JABBER"))
		{
			char **values = g_strsplit(value, ":", 0);
			char **im;

			for (im = values; *im != NULL; im++)
			{
				if (!strcmp(field, "X-AIM"))
					aims = g_list_append(aims, g_strdup(*im));
				else if (!strcmp(field, "X-ICQ"))
					icqs = g_list_append(icqs, g_strdup(*im));
				else if (!strcmp(field, "X-YAHOO"))
					yahoos = g_list_append(yahoos, g_strdup(*im));
				else if (!strcmp(field, "X-MSN"))
					msns = g_list_append(msns, g_strdup(*im));
				else if (!strcmp(field, "X-JABBER"))
					jabbers = g_list_append(jabbers, g_strdup(*im));
			}

			g_strfreev(values);
		}
	}

	g_free(temp_vcard);

	if (aims == NULL && icqs == NULL && yahoos == NULL &&
		msns == NULL && jabbers == NULL)
	{
		g_free(alias);

		return FALSE;
	}

	add_buddies_from_vcard("prpl-oscar",  group, aims,    alias);
	add_buddies_from_vcard("prpl-oscar",  group, icqs,    alias);
	add_buddies_from_vcard("prpl-yahoo",  group, yahoos,  alias);
	add_buddies_from_vcard("prpl-msn",    group, msns,    alias);
	add_buddies_from_vcard("prpl-jabber", group, jabbers, alias);

	g_free(alias);

	return TRUE;
}

#ifdef _WIN32
static void pidgin_blist_drag_begin(GtkWidget *widget,
		GdkDragContext *drag_context, gpointer user_data)
{
	pidgin_blist_tooltip_destroy();


	/* Unhook the tooltip-timeout since we don't want a tooltip
	 * to appear and obscure the dragging operation.
	 * This is a workaround for GTK+ bug 107320. */
	if (gtkblist->timeout) {
		g_source_remove(gtkblist->timeout);
		gtkblist->timeout = 0;
	}
}
#endif

static void pidgin_blist_drag_data_get_cb(GtkWidget *widget,
											GdkDragContext *dc,
											GtkSelectionData *data,
											guint info,
											guint time,
											gpointer null)
{

	if (data->target == gdk_atom_intern("GAIM_BLIST_NODE", FALSE))
	{
		GtkTreeRowReference *ref = g_object_get_data(G_OBJECT(dc), "gtk-tree-view-source-row");
		GtkTreePath *sourcerow = gtk_tree_row_reference_get_path(ref);
		GtkTreeIter iter;
		GaimBlistNode *node = NULL;
		GValue val;
		if(!sourcerow)
			return;
		gtk_tree_model_get_iter(GTK_TREE_MODEL(gtkblist->treemodel), &iter, sourcerow);
		val.g_type = 0;
		gtk_tree_model_get_value (GTK_TREE_MODEL(gtkblist->treemodel), &iter, NODE_COLUMN, &val);
		node = g_value_get_pointer(&val);
		gtk_selection_data_set (data,
					gdk_atom_intern ("GAIM_BLIST_NODE", FALSE),
					8, /* bits */
					(void*)&node,
					sizeof (node));

		gtk_tree_path_free(sourcerow);
	}
	else if (data->target == gdk_atom_intern("application/x-im-contact", FALSE))
	{
		GtkTreeRowReference *ref;
		GtkTreePath *sourcerow;
		GtkTreeIter iter;
		GaimBlistNode *node = NULL;
		GaimBuddy *buddy;
		GaimConnection *gc;
		GValue val;
		GString *str;
		const char *protocol;

		ref = g_object_get_data(G_OBJECT(dc), "gtk-tree-view-source-row");
		sourcerow = gtk_tree_row_reference_get_path(ref);

		if (!sourcerow)
			return;

		gtk_tree_model_get_iter(GTK_TREE_MODEL(gtkblist->treemodel), &iter,
								sourcerow);
		val.g_type = 0;
		gtk_tree_model_get_value(GTK_TREE_MODEL(gtkblist->treemodel), &iter,
								 NODE_COLUMN, &val);

		node = g_value_get_pointer(&val);

		if (GAIM_BLIST_NODE_IS_CONTACT(node))
		{
			buddy = gaim_contact_get_priority_buddy((GaimContact *)node);
		}
		else if (!GAIM_BLIST_NODE_IS_BUDDY(node))
		{
			gtk_tree_path_free(sourcerow);
			return;
		}
		else
		{
			buddy = (GaimBuddy *)node;
		}

		gc = gaim_account_get_connection(buddy->account);

		if (gc == NULL)
		{
			gtk_tree_path_free(sourcerow);
			return;
		}

		protocol =
			GAIM_PLUGIN_PROTOCOL_INFO(gc->prpl)->list_icon(buddy->account,
														   buddy);

		str = g_string_new(NULL);
		g_string_printf(str,
			"MIME-Version: 1.0\r\n"
			"Content-Type: application/x-im-contact\r\n"
			"X-IM-Protocol: %s\r\n"
			"X-IM-Username: %s\r\n",
			protocol,
			buddy->name);

		if (buddy->alias != NULL)
		{
			g_string_append_printf(str,
				"X-IM-Alias: %s\r\n",
				buddy->alias);
		}

		g_string_append(str, "\r\n");

		gtk_selection_data_set(data,
					gdk_atom_intern("application/x-im-contact", FALSE),
					8, /* bits */
					(const guchar *)str->str,
					strlen(str->str) + 1);

		g_string_free(str, TRUE);
		gtk_tree_path_free(sourcerow);
	}
}

static void pidgin_blist_drag_data_rcv_cb(GtkWidget *widget, GdkDragContext *dc, guint x, guint y,
			  GtkSelectionData *sd, guint info, guint t)
{
	if (gtkblist->drag_timeout) {
		g_source_remove(gtkblist->drag_timeout);
		gtkblist->drag_timeout = 0;
	}

	if (sd->target == gdk_atom_intern("GAIM_BLIST_NODE", FALSE) && sd->data) {
		GaimBlistNode *n = NULL;
		GtkTreePath *path = NULL;
		GtkTreeViewDropPosition position;
		memcpy(&n, sd->data, sizeof(n));
		if(gtk_tree_view_get_dest_row_at_pos(GTK_TREE_VIEW(widget), x, y, &path, &position)) {
			/* if we're here, I think it means the drop is ok */
			GtkTreeIter iter;
			GaimBlistNode *node;
			GValue val;
			struct _pidgin_blist_node *gtknode;

			gtk_tree_model_get_iter(GTK_TREE_MODEL(gtkblist->treemodel),
					&iter, path);
			val.g_type = 0;
			gtk_tree_model_get_value (GTK_TREE_MODEL(gtkblist->treemodel),
					&iter, NODE_COLUMN, &val);
			node = g_value_get_pointer(&val);
			gtknode = node->ui_data;

			if (GAIM_BLIST_NODE_IS_CONTACT(n)) {
				GaimContact *c = (GaimContact*)n;
				if (GAIM_BLIST_NODE_IS_CONTACT(node) && gtknode->contact_expanded) {
					gaim_blist_merge_contact(c, node);
				} else if (GAIM_BLIST_NODE_IS_CONTACT(node) ||
						GAIM_BLIST_NODE_IS_CHAT(node)) {
					switch(position) {
						case GTK_TREE_VIEW_DROP_AFTER:
						case GTK_TREE_VIEW_DROP_INTO_OR_AFTER:
							gaim_blist_add_contact(c, (GaimGroup*)node->parent,
									node);
							break;
						case GTK_TREE_VIEW_DROP_BEFORE:
						case GTK_TREE_VIEW_DROP_INTO_OR_BEFORE:
							gaim_blist_add_contact(c, (GaimGroup*)node->parent,
									node->prev);
							break;
					}
				} else if(GAIM_BLIST_NODE_IS_GROUP(node)) {
					gaim_blist_add_contact(c, (GaimGroup*)node, NULL);
				} else if(GAIM_BLIST_NODE_IS_BUDDY(node)) {
					gaim_blist_merge_contact(c, node);
				}
			} else if (GAIM_BLIST_NODE_IS_BUDDY(n)) {
				GaimBuddy *b = (GaimBuddy*)n;
				if (GAIM_BLIST_NODE_IS_BUDDY(node)) {
					switch(position) {
						case GTK_TREE_VIEW_DROP_AFTER:
						case GTK_TREE_VIEW_DROP_INTO_OR_AFTER:
							gaim_blist_add_buddy(b, (GaimContact*)node->parent,
									(GaimGroup*)node->parent->parent, node);
							break;
						case GTK_TREE_VIEW_DROP_BEFORE:
						case GTK_TREE_VIEW_DROP_INTO_OR_BEFORE:
							gaim_blist_add_buddy(b, (GaimContact*)node->parent,
									(GaimGroup*)node->parent->parent,
									node->prev);
							break;
					}
				} else if(GAIM_BLIST_NODE_IS_CHAT(node)) {
					gaim_blist_add_buddy(b, NULL, (GaimGroup*)node->parent,
							NULL);
				} else if (GAIM_BLIST_NODE_IS_GROUP(node)) {
					gaim_blist_add_buddy(b, NULL, (GaimGroup*)node, NULL);
				} else if (GAIM_BLIST_NODE_IS_CONTACT(node)) {
					if(gtknode->contact_expanded) {
						switch(position) {
							case GTK_TREE_VIEW_DROP_INTO_OR_AFTER:
							case GTK_TREE_VIEW_DROP_AFTER:
							case GTK_TREE_VIEW_DROP_INTO_OR_BEFORE:
								gaim_blist_add_buddy(b, (GaimContact*)node,
										(GaimGroup*)node->parent, NULL);
								break;
							case GTK_TREE_VIEW_DROP_BEFORE:
								gaim_blist_add_buddy(b, NULL,
										(GaimGroup*)node->parent, node->prev);
								break;
						}
					} else {
						switch(position) {
							case GTK_TREE_VIEW_DROP_INTO_OR_AFTER:
							case GTK_TREE_VIEW_DROP_AFTER:
								gaim_blist_add_buddy(b, NULL,
										(GaimGroup*)node->parent, NULL);
								break;
							case GTK_TREE_VIEW_DROP_INTO_OR_BEFORE:
							case GTK_TREE_VIEW_DROP_BEFORE:
								gaim_blist_add_buddy(b, NULL,
										(GaimGroup*)node->parent, node->prev);
								break;
						}
					}
				}
			} else if (GAIM_BLIST_NODE_IS_CHAT(n)) {
				GaimChat *chat = (GaimChat *)n;
				if (GAIM_BLIST_NODE_IS_BUDDY(node)) {
					switch(position) {
						case GTK_TREE_VIEW_DROP_AFTER:
						case GTK_TREE_VIEW_DROP_INTO_OR_AFTER:
						case GTK_TREE_VIEW_DROP_BEFORE:
						case GTK_TREE_VIEW_DROP_INTO_OR_BEFORE:
							gaim_blist_add_chat(chat,
									(GaimGroup*)node->parent->parent,
									node->parent);
							break;
					}
				} else if(GAIM_BLIST_NODE_IS_CONTACT(node) ||
						GAIM_BLIST_NODE_IS_CHAT(node)) {
					switch(position) {
						case GTK_TREE_VIEW_DROP_AFTER:
						case GTK_TREE_VIEW_DROP_INTO_OR_AFTER:
							gaim_blist_add_chat(chat, (GaimGroup*)node->parent, node);
							break;
						case GTK_TREE_VIEW_DROP_BEFORE:
						case GTK_TREE_VIEW_DROP_INTO_OR_BEFORE:
							gaim_blist_add_chat(chat, (GaimGroup*)node->parent, node->prev);
							break;
					}
				} else if (GAIM_BLIST_NODE_IS_GROUP(node)) {
					gaim_blist_add_chat(chat, (GaimGroup*)node, NULL);
				}
			} else if (GAIM_BLIST_NODE_IS_GROUP(n)) {
				GaimGroup *g = (GaimGroup*)n;
				if (GAIM_BLIST_NODE_IS_GROUP(node)) {
					switch (position) {
					case GTK_TREE_VIEW_DROP_INTO_OR_AFTER:
					case GTK_TREE_VIEW_DROP_AFTER:
						gaim_blist_add_group(g, node);
						break;
					case GTK_TREE_VIEW_DROP_INTO_OR_BEFORE:
					case GTK_TREE_VIEW_DROP_BEFORE:
						gaim_blist_add_group(g, node->prev);
						break;
					}
				} else if(GAIM_BLIST_NODE_IS_BUDDY(node)) {
					gaim_blist_add_group(g, node->parent->parent);
				} else if(GAIM_BLIST_NODE_IS_CONTACT(node) ||
						GAIM_BLIST_NODE_IS_CHAT(node)) {
					gaim_blist_add_group(g, node->parent);
				}
			}

			gtk_tree_path_free(path);
			gtk_drag_finish(dc, TRUE, (dc->action == GDK_ACTION_MOVE), t);
		}
	}
	else if (sd->target == gdk_atom_intern("application/x-im-contact",
										   FALSE) && sd->data)
	{
		GaimGroup *group = NULL;
		GtkTreePath *path = NULL;
		GtkTreeViewDropPosition position;
		GaimAccount *account;
		char *protocol = NULL;
		char *username = NULL;
		char *alias = NULL;

		if (gtk_tree_view_get_dest_row_at_pos(GTK_TREE_VIEW(widget),
											  x, y, &path, &position))
		{
			GtkTreeIter iter;
			GaimBlistNode *node;
			GValue val;

			gtk_tree_model_get_iter(GTK_TREE_MODEL(gtkblist->treemodel),
									&iter, path);
			val.g_type = 0;
			gtk_tree_model_get_value (GTK_TREE_MODEL(gtkblist->treemodel),
									  &iter, NODE_COLUMN, &val);
			node = g_value_get_pointer(&val);

			if (GAIM_BLIST_NODE_IS_BUDDY(node))
			{
				group = (GaimGroup *)node->parent->parent;
			}
			else if (GAIM_BLIST_NODE_IS_CHAT(node) ||
					 GAIM_BLIST_NODE_IS_CONTACT(node))
			{
				group = (GaimGroup *)node->parent;
			}
			else if (GAIM_BLIST_NODE_IS_GROUP(node))
			{
				group = (GaimGroup *)node;
			}
		}

		if (pidgin_parse_x_im_contact((const char *)sd->data, FALSE, &account,
										&protocol, &username, &alias))
		{
			if (account == NULL)
			{
				gaim_notify_error(NULL, NULL,
					_("You are not currently signed on with an account that "
					  "can add that buddy."), NULL);
			}
			else
			{
				gaim_blist_request_add_buddy(account, username,
											 (group ? group->name : NULL),
											 alias);
			}
		}

		g_free(username);
		g_free(protocol);
		g_free(alias);

		if (path != NULL)
			gtk_tree_path_free(path);

		gtk_drag_finish(dc, TRUE, (dc->action == GDK_ACTION_MOVE), t);
	}
	else if (sd->target == gdk_atom_intern("text/x-vcard", FALSE) && sd->data)
	{
		gboolean result;
		GaimGroup *group = NULL;
		GtkTreePath *path = NULL;
		GtkTreeViewDropPosition position;

		if (gtk_tree_view_get_dest_row_at_pos(GTK_TREE_VIEW(widget),
											  x, y, &path, &position))
		{
			GtkTreeIter iter;
			GaimBlistNode *node;
			GValue val;

			gtk_tree_model_get_iter(GTK_TREE_MODEL(gtkblist->treemodel),
									&iter, path);
			val.g_type = 0;
			gtk_tree_model_get_value (GTK_TREE_MODEL(gtkblist->treemodel),
									  &iter, NODE_COLUMN, &val);
			node = g_value_get_pointer(&val);

			if (GAIM_BLIST_NODE_IS_BUDDY(node))
			{
				group = (GaimGroup *)node->parent->parent;
			}
			else if (GAIM_BLIST_NODE_IS_CHAT(node) ||
					 GAIM_BLIST_NODE_IS_CONTACT(node))
			{
				group = (GaimGroup *)node->parent;
			}
			else if (GAIM_BLIST_NODE_IS_GROUP(node))
			{
				group = (GaimGroup *)node;
			}
		}

		result = parse_vcard((const gchar *)sd->data, group);

		gtk_drag_finish(dc, result, (dc->action == GDK_ACTION_MOVE), t);
	} else if (sd->target == gdk_atom_intern("text/uri-list", FALSE) && sd->data) {
		GtkTreePath *path = NULL;
		GtkTreeViewDropPosition position;

		if (gtk_tree_view_get_dest_row_at_pos(GTK_TREE_VIEW(widget),
											  x, y, &path, &position))
			{
				GtkTreeIter iter;
				GaimBlistNode *node;
				GValue val;

				gtk_tree_model_get_iter(GTK_TREE_MODEL(gtkblist->treemodel),
							&iter, path);
				val.g_type = 0;
				gtk_tree_model_get_value (GTK_TREE_MODEL(gtkblist->treemodel),
							  &iter, NODE_COLUMN, &val);
				node = g_value_get_pointer(&val);

				if (GAIM_BLIST_NODE_IS_BUDDY(node) || GAIM_BLIST_NODE_IS_CONTACT(node)) {
					GaimBuddy *b = GAIM_BLIST_NODE_IS_BUDDY(node) ? (GaimBuddy*)node : gaim_contact_get_priority_buddy((GaimContact*)node);
					pidgin_dnd_file_manage(sd, b->account, b->name);
					gtk_drag_finish(dc, TRUE, (dc->action == GDK_ACTION_MOVE), t);
				} else {
					gtk_drag_finish(dc, FALSE, FALSE, t);
				}
			}
	}
}

/* Altered from do_colorshift in gnome-panel */
static void
do_alphashift (GdkPixbuf *dest, GdkPixbuf *src, int shift)
{
	gint i, j;
	gint width, height, has_alpha, srcrowstride, destrowstride;
	guchar *target_pixels;
	guchar *original_pixels;
	guchar *pixsrc;
	guchar *pixdest;
	int val;
	guchar a;

	has_alpha = gdk_pixbuf_get_has_alpha (src);
	if (!has_alpha)
	  return;

	width = gdk_pixbuf_get_width (src);
	height = gdk_pixbuf_get_height (src);
	srcrowstride = gdk_pixbuf_get_rowstride (src);
	destrowstride = gdk_pixbuf_get_rowstride (dest);
	target_pixels = gdk_pixbuf_get_pixels (dest);
	original_pixels = gdk_pixbuf_get_pixels (src);

	for (i = 0; i < height; i++) {
		pixdest = target_pixels + i*destrowstride;
		pixsrc = original_pixels + i*srcrowstride;
		for (j = 0; j < width; j++) {
			*(pixdest++) = *(pixsrc++);
			*(pixdest++) = *(pixsrc++);
			*(pixdest++) = *(pixsrc++);
			a = *(pixsrc++);
			val = a - shift;
			*(pixdest++) = CLAMP(val, 0, 255);
		}
	}
}


static GdkPixbuf *pidgin_blist_get_buddy_icon(GaimBlistNode *node,
		gboolean scaled, gboolean greyed, gboolean custom)
{
	GdkPixbuf *buf, *ret = NULL;
	GdkPixbufLoader *loader;
	GaimBuddyIcon *icon;
	const guchar *data = NULL;
	gsize len;
	GaimBuddy *buddy = NULL;
	GaimChat *chat = NULL;
	GaimAccount *account = NULL;
	GaimPluginProtocolInfo *prpl_info = NULL;

	if(GAIM_BLIST_NODE_IS_CONTACT(node)) {
		buddy = gaim_contact_get_priority_buddy((GaimContact*)node);
	} else if(GAIM_BLIST_NODE_IS_BUDDY(node)) {
		buddy = (GaimBuddy*)node;
	} else if(GAIM_BLIST_NODE_IS_CHAT(node)) {
		chat = (GaimChat*)node;
	} else {
		return NULL;
	}

	if(buddy != NULL)
		account = gaim_buddy_get_account(buddy);
	else if(chat != NULL)
		account = chat->account;

	if(account && account->gc)
		prpl_info = GAIM_PLUGIN_PROTOCOL_INFO(account->gc->prpl);

#if 0
	if (!gaim_prefs_get_bool("/gaim/gtk/blist/show_buddy_icons"))
		return NULL;
#endif

	if (custom) {
		const char *file = gaim_blist_node_get_string((GaimBlistNode*)gaim_buddy_get_contact(buddy),
							"custom_buddy_icon");
		if (file && *file) {
			char *contents;
			GError *err  = NULL;
			if (!g_file_get_contents(file, &contents, &len, &err)) {
				gaim_debug_info("custom -icon", "Could not open custom-icon %s for %s\n",
							file, gaim_buddy_get_name(buddy), err->message);
				g_error_free(err);
			} else
				data = (const guchar*)contents;
		}
	}

	if (data == NULL) {
		if(buddy != NULL) {
			if (!(icon = gaim_buddy_get_icon(buddy)))
				if (!(icon = gaim_buddy_icons_find(buddy->account, buddy->name))) /* Not sure I like this...*/
					return NULL;
			data = gaim_buddy_icon_get_data(icon, &len);
		}
		custom = FALSE;  /* We are not using the custom icon */
	}

	if(data == NULL)
		return NULL;

	loader = gdk_pixbuf_loader_new();
	gdk_pixbuf_loader_write(loader, data, len, NULL);
	gdk_pixbuf_loader_close(loader, NULL);
	buf = gdk_pixbuf_loader_get_pixbuf(loader);
	if (buf)
		g_object_ref(G_OBJECT(buf));
	g_object_unref(G_OBJECT(loader));

	if (custom)
		g_free((void*)data);
	if (buf) {
		int orig_width, orig_height;
		int scale_width, scale_height;

		if (greyed) {
			GaimPresence *presence = gaim_buddy_get_presence(buddy);
			if (!GAIM_BUDDY_IS_ONLINE(buddy))
				gdk_pixbuf_saturate_and_pixelate(buf, buf, 0.0, FALSE);
			if (gaim_presence_is_idle(presence))
				gdk_pixbuf_saturate_and_pixelate(buf, buf, 0.25, FALSE);
		}

		/* i'd use the pidgin_buddy_icon_get_scale_size() thing,
		 * but it won't tell me the original size, which I need for scaling
		 * purposes */
		scale_width = orig_width = gdk_pixbuf_get_width(buf);
		scale_height = orig_height = gdk_pixbuf_get_height(buf);

		if (prpl_info && prpl_info->icon_spec.scale_rules & GAIM_ICON_SCALE_DISPLAY)
			gaim_buddy_icon_get_scale_size(&prpl_info->icon_spec, &scale_width, &scale_height);

		if (scaled) {
			if(scale_height > scale_width) {
				scale_width = 32.0 * (double)scale_width / (double)scale_height;
				scale_height = 32;
			} else {
				scale_height = 32.0 * (double)scale_height / (double)scale_width;
				scale_width = 32;
			}

			ret = gdk_pixbuf_new(GDK_COLORSPACE_RGB, TRUE, 8, 32, 32);
			gdk_pixbuf_fill(ret, 0x00000000);
			gdk_pixbuf_scale(buf, ret, (32-scale_width)/2, (32-scale_height)/2, scale_width, scale_height, (32-scale_width)/2, (32-scale_height)/2, (double)scale_width/(double)orig_width, (double)scale_height/(double)orig_height, GDK_INTERP_BILINEAR);
		} else {
			ret = gdk_pixbuf_scale_simple(buf,scale_width,scale_height, GDK_INTERP_BILINEAR);
		}
		g_object_unref(G_OBJECT(buf));
	}

	return ret;
}
/* # - Status Icon
 * P - Protocol Icon
 * A - Buddy Icon
 * [ - SMALL_SPACE
 * = - LARGE_SPACE
 *                   +--- STATUS_SIZE                +--- td->avatar_width
 *                   |         +-- td->name_width    |
 *                +----+   +-------+            +---------+
 *                |    |   |       |            |         |
 *                +-------------------------------------------+
 *                |       [          =        [               |--- TOOLTIP_BORDER
 *name_height --+-| ######[BuddyName = PP     [   AAAAAAAAAAA |--+
 *              | | ######[          = PP     [   AAAAAAAAAAA |  |
 * STATUS SIZE -| | ######[[[[[[[[[[[[[[[[[[[[[   AAAAAAAAAAA |  |
 *           +--+-| ######[Account: So-and-so [   AAAAAAAAAAA |  |-- td->avatar_height
 *           |    |       [Idle: 4h 15m       [   AAAAAAAAAAA |  |
 *  height --+    |       [Foo: Bar, Baz      [   AAAAAAAAAAA |  |
 *           |    |       [Status: Awesome    [   AAAAAAAAAAA |--+
 *           +----|       [Stop: Hammer Time  [               |
 *                |       [                   [               |--- TOOLTIP_BORDER
 *                +-------------------------------------------+
 *                 |       |                |                |
 *                 |       +----------------+                |
 *                 |               |                         |
 *                 |               +-- td->width             |
 *                 |                                         |
 *                 +---- TOOLTIP_BORDER                      +---- TOOLTIP_BORDER
 *
 * 
 */
#define STATUS_SIZE 22
#define TOOLTIP_BORDER 12
#define SMALL_SPACE 6
#define LARGE_SPACE 12
#define PRPL_SIZE 16 
struct tooltip_data {
	PangoLayout *layout;
	PangoLayout *name_layout;
	GdkPixbuf *prpl_icon;
	GdkPixbuf *status_icon;
	GdkPixbuf *avatar;
	gboolean avatar_is_prpl_icon;
	int avatar_width;
	int avatar_height;
	int name_height;
	int name_width;
	int width;
	int height;
};

static struct tooltip_data * create_tip_for_node(GaimBlistNode *node, gboolean full)
{
	char *tooltip_text = NULL;
	struct tooltip_data *td = g_new0(struct tooltip_data, 1);
	GaimAccount *account = NULL;
	char *tmp, *node_name;

	if(GAIM_BLIST_NODE_IS_BUDDY(node)) {
		account = ((GaimBuddy*)(node))->account;
	} else if(GAIM_BLIST_NODE_IS_CHAT(node)) {
		account = ((GaimChat*)(node))->account;
	}

	td->status_icon = pidgin_blist_get_status_icon(node, PIDGIN_STATUS_ICON_LARGE);
	td->avatar = pidgin_blist_get_buddy_icon(node, !full, FALSE, TRUE);
	td->prpl_icon = pidgin_create_prpl_icon(account, PIDGIN_PRPL_ICON_SMALL);
	tooltip_text = gaim_get_tooltip_text(node, full);
	td->layout = gtk_widget_create_pango_layout(gtkblist->tipwindow, NULL);
	td->name_layout = gtk_widget_create_pango_layout(gtkblist->tipwindow, NULL);

	if (GAIM_BLIST_NODE_IS_BUDDY(node))
		tmp = g_markup_escape_text(gaim_buddy_get_name((GaimBuddy*)node), -1);
	else
		tmp = g_markup_escape_text(gaim_chat_get_name((GaimChat*)node), -1);
	node_name = g_strdup_printf("<span size='x-large' weight='bold'>%s</span>", tmp);

	pango_layout_set_markup(td->layout, tooltip_text, -1);
	pango_layout_set_wrap(td->layout, PANGO_WRAP_WORD);
	pango_layout_set_width(td->layout, 300000);

	pango_layout_get_size (td->layout, &td->width, &td->height);
	td->width = PANGO_PIXELS(td->width);
	td->height = PANGO_PIXELS(td->height);

	pango_layout_set_markup(td->name_layout, node_name, -1);
	pango_layout_set_wrap(td->name_layout, PANGO_WRAP_WORD);
	pango_layout_set_width(td->name_layout, 300000);

	pango_layout_get_size (td->name_layout, &td->name_width, &td->name_height);
	td->name_width = PANGO_PIXELS(td->name_width) + SMALL_SPACE + PRPL_SIZE;
	td->name_height = MAX(PANGO_PIXELS(td->name_height), PRPL_SIZE + SMALL_SPACE);
#if 0  /* PRPL Icon as avatar */
	if(!td->avatar && full) {
		td->avatar = pidgin_create_prpl_icon(account, PIDGIN_PRPL_ICON_LARGE);
		td->avatar_is_prpl_icon = TRUE;
	}
#endif
	td->avatar_width = gdk_pixbuf_get_width(td->avatar);
	td->avatar_height = gdk_pixbuf_get_height(td->avatar);

	g_free(node_name);
	g_free(tooltip_text);
	return td;
}

static void pidgin_blist_paint_tip(GtkWidget *widget, GdkEventExpose *event, GaimBlistNode *node)
{
	GtkStyle *style;
	int current_height, max_width;
	int max_text_width;
	int max_avatar_width;
	GList *l;
	int prpl_col = 0;
        GtkTextDirection dir = gtk_widget_get_direction(widget);
	
	if(gtkblist->tooltipdata == NULL)
		return;

	style = gtkblist->tipwindow->style;
	gtk_paint_flat_box(style, gtkblist->tipwindow->window, GTK_STATE_NORMAL, GTK_SHADOW_OUT,
			NULL, gtkblist->tipwindow, "tooltip", 0, 0, -1, -1);

	max_text_width = 0;
	max_avatar_width = 0;

	for(l = gtkblist->tooltipdata; l; l = l->next)
	{
		struct tooltip_data *td = l->data;

		max_text_width = MAX(max_text_width,
				MAX(td->width, td->name_width));
		max_avatar_width = MAX(max_avatar_width, td->avatar_width);
	}

	max_width = TOOLTIP_BORDER + STATUS_SIZE + SMALL_SPACE + max_text_width + SMALL_SPACE + max_avatar_width + TOOLTIP_BORDER;
	if (dir == GTK_TEXT_DIR_RTL)
		prpl_col = TOOLTIP_BORDER + max_avatar_width + SMALL_SPACE;
	else
		prpl_col = TOOLTIP_BORDER + STATUS_SIZE + SMALL_SPACE + max_text_width - PRPL_SIZE;

	current_height = 12;
	for(l = gtkblist->tooltipdata; l; l = l->next)
	{
		struct tooltip_data *td = l->data;

		if (td->avatar && pidgin_gdk_pixbuf_is_opaque(td->avatar))
			if (dir == GTK_TEXT_DIR_RTL)
				gtk_paint_flat_box(style, gtkblist->tipwindow->window, GTK_STATE_NORMAL, GTK_SHADOW_OUT,
						NULL, gtkblist->tipwindow, "tooltip",
						TOOLTIP_BORDER -1, current_height -1, td->avatar_width +2, td->avatar_height + 2);
			else
				gtk_paint_flat_box(style, gtkblist->tipwindow->window, GTK_STATE_NORMAL, GTK_SHADOW_OUT,
						NULL, gtkblist->tipwindow, "tooltip",
						max_width - (td->avatar_width+ TOOLTIP_BORDER)-1,
						current_height-1,td->avatar_width+2, td->avatar_height+2);

#if GTK_CHECK_VERSION(2,2,0)
		if (dir == GTK_TEXT_DIR_RTL)
			gdk_draw_pixbuf(GDK_DRAWABLE(gtkblist->tipwindow->window), NULL, td->status_icon,
					0, 0, max_width - TOOLTIP_BORDER - STATUS_SIZE, current_height, -1, -1, GDK_RGB_DITHER_NONE, 0, 0);
		else
			gdk_draw_pixbuf(GDK_DRAWABLE(gtkblist->tipwindow->window), NULL, td->status_icon,
					0, 0, TOOLTIP_BORDER, current_height, -1 , -1, GDK_RGB_DITHER_NONE, 0, 0);
		if(td->avatar)
			if (dir == GTK_TEXT_DIR_RTL)
				gdk_draw_pixbuf(GDK_DRAWABLE(gtkblist->tipwindow->window), NULL,
						td->avatar, 0, 0, TOOLTIP_BORDER, current_height, -1, -1, GDK_RGB_DITHER_NONE, 0, 0);
			else
				gdk_draw_pixbuf(GDK_DRAWABLE(gtkblist->tipwindow->window), NULL,
						td->avatar, 0, 0, max_width - (td->avatar_width + TOOLTIP_BORDER),
						current_height, -1 , -1, GDK_RGB_DITHER_NONE, 0, 0);
		if (!td->avatar_is_prpl_icon)
			gdk_draw_pixbuf(GDK_DRAWABLE(gtkblist->tipwindow->window), NULL, td->prpl_icon,
					0, 0,
					prpl_col,
					current_height + ((td->name_height / 2) - (PRPL_SIZE / 2)),
					-1 , -1, GDK_RGB_DITHER_NONE, 0, 0);

#else
		gdk_pixbuf_render_to_drawable(td->status_icon, GDK_DRAWABLE(gtkblist->tipwindow->window), NULL, 0, 0, 12, current_height, -1, -1, GDK_RGB_DITHER_NONE, 0, 0);
		if(td->avatar)
			gdk_pixbuf_render_to_drawable(td->avatar,
					GDK_DRAWABLE(gtkblist->tipwindow->window), NULL, 0, 0,
					max_width - (td->avatar_width + TOOLTIP_BORDER),
					current_height, -1, -1, GDK_RGB_DITHER_NONE, 0, 0);
#endif
		if (dir == GTK_TEXT_DIR_RTL) {
			gtk_paint_layout(style, gtkblist->tipwindow->window, GTK_STATE_NORMAL, FALSE,
					NULL, gtkblist->tipwindow, "tooltip",
					max_width  -(TOOLTIP_BORDER + STATUS_SIZE +SMALL_SPACE) - PANGO_PIXELS(300000), 
					current_height, td->name_layout);
		} else {
			gtk_paint_layout (style, gtkblist->tipwindow->window, GTK_STATE_NORMAL, FALSE,
					NULL, gtkblist->tipwindow, "tooltip",
					TOOLTIP_BORDER + STATUS_SIZE + SMALL_SPACE, current_height, td->name_layout);
		}
		if (dir != GTK_TEXT_DIR_RTL) {
			gtk_paint_layout (style, gtkblist->tipwindow->window, GTK_STATE_NORMAL, FALSE,
					NULL, gtkblist->tipwindow, "tooltip",
					TOOLTIP_BORDER + STATUS_SIZE + SMALL_SPACE, current_height + td->name_height, td->layout);
		} else {
			gtk_paint_layout(style, gtkblist->tipwindow->window, GTK_STATE_NORMAL, FALSE,
					NULL, gtkblist->tipwindow, "tooltip",
					max_width - (TOOLTIP_BORDER + STATUS_SIZE + SMALL_SPACE) - PANGO_PIXELS(300000),
					current_height + td->name_height,
					td->layout);
		}

		current_height += MAX(td->name_height + td->height, td->avatar_height) + TOOLTIP_BORDER;
	}
}


static void pidgin_blist_tooltip_destroy()
{
	while(gtkblist->tooltipdata) {
		struct tooltip_data *td = gtkblist->tooltipdata->data;

		if(td->avatar)
			g_object_unref(td->avatar);
		if(td->status_icon)
			g_object_unref(td->status_icon);
		if(td->prpl_icon)
			g_object_unref(td->prpl_icon);
		g_object_unref(td->layout);
		g_object_unref(td->name_layout);
		g_free(td);
		gtkblist->tooltipdata = g_list_delete_link(gtkblist->tooltipdata, gtkblist->tooltipdata);
	}

	if (gtkblist->tipwindow == NULL)
		return;

	gtk_widget_destroy(gtkblist->tipwindow);
	gtkblist->tipwindow = NULL;
}

static gboolean pidgin_blist_expand_timeout(GtkWidget *tv)
{
	GtkTreePath *path;
	GtkTreeIter iter;
	GaimBlistNode *node;
	GValue val;
	struct _pidgin_blist_node *gtknode;

	if (!gtk_tree_view_get_path_at_pos(GTK_TREE_VIEW(tv), gtkblist->tip_rect.x, gtkblist->tip_rect.y, &path, NULL, NULL, NULL))
		return FALSE;
	gtk_tree_model_get_iter(GTK_TREE_MODEL(gtkblist->treemodel), &iter, path);
	val.g_type = 0;
	gtk_tree_model_get_value (GTK_TREE_MODEL(gtkblist->treemodel), &iter, NODE_COLUMN, &val);
	node = g_value_get_pointer(&val);

	if(!GAIM_BLIST_NODE_IS_CONTACT(node)) {
		gtk_tree_path_free(path);
		return FALSE;
	}

	gtknode = node->ui_data;

	if (!gtknode->contact_expanded) {
		GtkTreeIter i;

		pidgin_blist_expand_contact_cb(NULL, node);

		gtk_tree_view_get_cell_area(GTK_TREE_VIEW(tv), path, NULL, &gtkblist->contact_rect);
		gdk_drawable_get_size(GDK_DRAWABLE(tv->window), &(gtkblist->contact_rect.width), NULL);
		gtkblist->mouseover_contact = node;
		gtk_tree_path_down (path);
		while (gtk_tree_model_get_iter(GTK_TREE_MODEL(gtkblist->treemodel), &i, path)) {
			GdkRectangle rect;
			gtk_tree_view_get_cell_area(GTK_TREE_VIEW(tv), path, NULL, &rect);
			gtkblist->contact_rect.height += rect.height;
			gtk_tree_path_next(path);
		}
	}
	gtk_tree_path_free(path);
	return FALSE;
}

static gboolean buddy_is_displayable(GaimBuddy *buddy)
{
	struct _pidgin_blist_node *gtknode;

	if(!buddy)
		return FALSE;

	gtknode = ((GaimBlistNode*)buddy)->ui_data;

	return (gaim_account_is_connected(buddy->account) &&
			(gaim_presence_is_online(buddy->presence) ||
			 (gtknode && gtknode->recent_signonoff) ||
			 gaim_prefs_get_bool("/gaim/gtk/blist/show_offline_buddies") ||
			 gaim_blist_node_get_bool((GaimBlistNode*)buddy, "show_offline")));
}

static gboolean pidgin_blist_tooltip_timeout(GtkWidget *tv)
{
	GtkTreePath *path;
	GtkTreeIter iter;
	GaimBlistNode *node;
	GValue val;
	int scr_w, scr_h, w, h, x, y;
#if GTK_CHECK_VERSION(2,2,0)
	int mon_num;
	GdkScreen *screen = NULL;
#endif
	gboolean tooltip_top = FALSE;
	struct _pidgin_blist_node *gtknode;
	GdkRectangle mon_size;

	/*
	 * Attempt to free the previous tooltip.  I have a feeling
	 * this is never needed... but just in case.
	 */
	pidgin_blist_tooltip_destroy();

	if (!gtk_tree_view_get_path_at_pos(GTK_TREE_VIEW(tv), gtkblist->tip_rect.x, gtkblist->tip_rect.y, &path, NULL, NULL, NULL))
		return FALSE;
	gtk_tree_model_get_iter(GTK_TREE_MODEL(gtkblist->treemodel), &iter, path);
	val.g_type = 0;
	gtk_tree_model_get_value (GTK_TREE_MODEL(gtkblist->treemodel), &iter, NODE_COLUMN, &val);
	node = g_value_get_pointer(&val);

	gtk_tree_path_free(path);

	gtkblist->tipwindow = gtk_window_new(GTK_WINDOW_POPUP);

	if(GAIM_BLIST_NODE_IS_CHAT(node) || GAIM_BLIST_NODE_IS_BUDDY(node)) {
		struct tooltip_data *td = create_tip_for_node(node, TRUE);
		gtkblist->tooltipdata = g_list_append(gtkblist->tooltipdata, td);
		w = TOOLTIP_BORDER + STATUS_SIZE + SMALL_SPACE +
			MAX(td->width, td->name_width) + SMALL_SPACE + td->avatar_width + TOOLTIP_BORDER;
		h = TOOLTIP_BORDER + MAX(td->height + td->name_height, MAX(STATUS_SIZE, td->avatar_height))
			+ TOOLTIP_BORDER;
	} else if(GAIM_BLIST_NODE_IS_CONTACT(node)) {
		GaimBlistNode *child;
		GaimBuddy *b = gaim_contact_get_priority_buddy((GaimContact *)node);
		int max_text_width = 0;
		int max_avatar_width = 0;
		w = h = 0;

		for(child = node->child; child; child = child->next)
		{
			if(GAIM_BLIST_NODE_IS_BUDDY(child) && buddy_is_displayable((GaimBuddy*)child)) {
				struct tooltip_data *td = create_tip_for_node(child, (b == (GaimBuddy*)child));
				if (b == (GaimBuddy *)child) {
					gtkblist->tooltipdata = g_list_prepend(gtkblist->tooltipdata, td);
				} else {
					gtkblist->tooltipdata = g_list_append(gtkblist->tooltipdata, td);
				}
				max_text_width = MAX(max_text_width, MAX(td->width, td->name_width));
				max_avatar_width = MAX(max_avatar_width, td->avatar_width);
				h += MAX(TOOLTIP_BORDER + MAX(STATUS_SIZE,td->avatar_height) + TOOLTIP_BORDER,
						TOOLTIP_BORDER + td->height + td->name_height + TOOLTIP_BORDER);
			}
		}
		w = TOOLTIP_BORDER + STATUS_SIZE + SMALL_SPACE + max_text_width + SMALL_SPACE + max_avatar_width + TOOLTIP_BORDER;
	} else {
		gtk_widget_destroy(gtkblist->tipwindow);
		gtkblist->tipwindow = NULL;
		return FALSE;
	}

	if (gtkblist->tooltipdata == NULL) {
		gtk_widget_destroy(gtkblist->tipwindow);
		gtkblist->tipwindow = NULL;
		return FALSE;
	}

	gtknode = node->ui_data;

	gtk_widget_set_app_paintable(gtkblist->tipwindow, TRUE);
	gtk_window_set_resizable(GTK_WINDOW(gtkblist->tipwindow), FALSE);
	gtk_widget_set_name(gtkblist->tipwindow, "gtk-tooltips");
	g_signal_connect(G_OBJECT(gtkblist->tipwindow), "expose_event",
			G_CALLBACK(pidgin_blist_paint_tip), NULL);
	gtk_widget_ensure_style (gtkblist->tipwindow);


#if GTK_CHECK_VERSION(2,2,0)
	gdk_display_get_pointer(gdk_display_get_default(), &screen, &x, &y, NULL);
	mon_num = gdk_screen_get_monitor_at_point(screen, x, y);
	gdk_screen_get_monitor_geometry(screen, mon_num, &mon_size);

	scr_w = mon_size.width + mon_size.x;
	scr_h = mon_size.height + mon_size.y;
#else
	scr_w = gdk_screen_width();
	scr_h = gdk_screen_height();
	gdk_window_get_pointer(NULL, &x, &y, NULL);
	mon_size.x = 0;
	mon_size.y = 0;
#endif

#if GTK_CHECK_VERSION(2,2,0)
	if (w > mon_size.width)
	  w = mon_size.width - 10;

	if (h > mon_size.height)
	  h = mon_size.height - 10;
#endif

	if (GTK_WIDGET_NO_WINDOW(gtkblist->window))
		y+=gtkblist->window->allocation.y;

	x -= ((w >> 1) + 4);

	if ((y + h + 4) > scr_h || tooltip_top)
		y = y - h - 5;
	else
		y = y + 6;

	if (y < mon_size.y)
		y = mon_size.y;

	if (y != mon_size.y) {
		if ((x + w) > scr_w)
			x -= (x + w + 5) - scr_w;
		else if (x < mon_size.x)
			x = mon_size.x;
	} else {
		x -= (w / 2 + 10);
		if (x < mon_size.x)
			x = mon_size.x;
	}

	gtk_widget_set_size_request(gtkblist->tipwindow, w, h);
	gtk_window_move(GTK_WINDOW(gtkblist->tipwindow), x, y);
	gtk_widget_show(gtkblist->tipwindow);

	return FALSE;
}

static gboolean pidgin_blist_drag_motion_cb(GtkWidget *tv, GdkDragContext *drag_context,
					      gint x, gint y, guint time, gpointer user_data)
{
	GtkTreePath *path;
	int delay;

	/*
	 * When dragging a buddy into a contact, this is the delay before
	 * the contact auto-expands.
	 */
	delay = 900;

	if (gtkblist->drag_timeout) {
		if ((y > gtkblist->tip_rect.y) && ((y - gtkblist->tip_rect.height) < gtkblist->tip_rect.y))
			return FALSE;
		/* We've left the cell.  Remove the timeout and create a new one below */
		g_source_remove(gtkblist->drag_timeout);
	}

	gtk_tree_view_get_path_at_pos(GTK_TREE_VIEW(tv), x, y, &path, NULL, NULL, NULL);
	gtk_tree_view_get_cell_area(GTK_TREE_VIEW(tv), path, NULL, &gtkblist->tip_rect);

	if (path)
		gtk_tree_path_free(path);
	gtkblist->drag_timeout = g_timeout_add(delay, (GSourceFunc)pidgin_blist_expand_timeout, tv);

	if (gtkblist->mouseover_contact) {
		if ((y < gtkblist->contact_rect.y) || ((y - gtkblist->contact_rect.height) > gtkblist->contact_rect.y)) {
			pidgin_blist_collapse_contact_cb(NULL, gtkblist->mouseover_contact);
			gtkblist->mouseover_contact = NULL;
		}
	}

	return FALSE;
}

static gboolean pidgin_blist_motion_cb (GtkWidget *tv, GdkEventMotion *event, gpointer null)
{
	GtkTreePath *path;
	int delay;

	delay = gaim_prefs_get_int("/gaim/gtk/blist/tooltip_delay");

	if (delay == 0)
		return FALSE;

	if (gtkblist->timeout) {
		if ((event->y > gtkblist->tip_rect.y) && ((event->y - gtkblist->tip_rect.height) < gtkblist->tip_rect.y))
			return FALSE;
		/* We've left the cell.  Remove the timeout and create a new one below */
		pidgin_blist_tooltip_destroy();
		g_source_remove(gtkblist->timeout);
	}

	gtk_tree_view_get_path_at_pos(GTK_TREE_VIEW(tv), event->x, event->y, &path, NULL, NULL, NULL);
	gtk_tree_view_get_cell_area(GTK_TREE_VIEW(tv), path, NULL, &gtkblist->tip_rect);

	if (path)
		gtk_tree_path_free(path);
	gtkblist->timeout = g_timeout_add(delay, (GSourceFunc)pidgin_blist_tooltip_timeout, tv);

	if (gtkblist->mouseover_contact) {
		if ((event->y < gtkblist->contact_rect.y) || ((event->y - gtkblist->contact_rect.height) > gtkblist->contact_rect.y)) {
			pidgin_blist_collapse_contact_cb(NULL, gtkblist->mouseover_contact);
			gtkblist->mouseover_contact = NULL;
		}
	}

	return FALSE;
}

static void pidgin_blist_leave_cb (GtkWidget *w, GdkEventCrossing *e, gpointer n)
{

	if (gtkblist->timeout) {
		g_source_remove(gtkblist->timeout);
		gtkblist->timeout = 0;
	}

	if (gtkblist->drag_timeout) {
		g_source_remove(gtkblist->drag_timeout);
		gtkblist->drag_timeout = 0;
	}

	pidgin_blist_tooltip_destroy();

	if (gtkblist->mouseover_contact &&
		!((e->x > gtkblist->contact_rect.x) && (e->x < (gtkblist->contact_rect.x + gtkblist->contact_rect.width)) &&
		 (e->y > gtkblist->contact_rect.y) && (e->y < (gtkblist->contact_rect.y + gtkblist->contact_rect.height)))) {
			pidgin_blist_collapse_contact_cb(NULL, gtkblist->mouseover_contact);
		gtkblist->mouseover_contact = NULL;
	}
}

static void
toggle_debug(void)
{
	gaim_prefs_set_bool("/gaim/gtk/debug/enabled",
			!gaim_prefs_get_bool("/gaim/gtk/debug/enabled"));
}


/***************************************************
 *            Crap                                 *
 ***************************************************/
static GtkItemFactoryEntry blist_menu[] =
{
	/* Buddies menu */
	{ N_("/_Buddies"), NULL, NULL, 0, "<Branch>", NULL },
	{ N_("/Buddies/New Instant _Message..."), "<CTL>M", pidgindialogs_im, 0, "<StockItem>", PIDGIN_STOCK_TOOLBAR_MESSAGE_NEW },
	{ N_("/Buddies/Join a _Chat..."), "<CTL>C", pidgin_blist_joinchat_show, 0, "<Item>", NULL },
	{ N_("/Buddies/Get User _Info..."), "<CTL>I", pidgindialogs_info, 0, "<StockItem>", PIDGIN_STOCK_TOOLBAR_USER_INFO },
	{ N_("/Buddies/View User _Log..."), "<CTL>L", pidgindialogs_log, 0, "<Item>", NULL },
	{ "/Buddies/sep1", NULL, NULL, 0, "<Separator>", NULL },
	{ N_("/Buddies/Show _Offline Buddies"), NULL, pidgin_blist_edit_mode_cb, 1, "<CheckItem>", NULL },
	{ N_("/Buddies/Show _Empty Groups"), NULL, pidgin_blist_show_empty_groups_cb, 1, "<CheckItem>", NULL },
	{ N_("/Buddies/Show Buddy _Details"), NULL, pidgin_blist_buddy_details_cb, 1, "<CheckItem>", NULL },
	{ N_("/Buddies/Show Idle _Times"), NULL, pidgin_blist_show_idle_time_cb, 1, "<CheckItem>", NULL },
	{ N_("/Buddies/_Sort Buddies"), NULL, NULL, 0, "<Branch>", NULL },
	{ "/Buddies/sep2", NULL, NULL, 0, "<Separator>", NULL },
	{ N_("/Buddies/_Add Buddy..."), "<CTL>B", pidgin_blist_add_buddy_cb, 0, "<StockItem>", GTK_STOCK_ADD },
	{ N_("/Buddies/Add C_hat..."), NULL, pidgin_blist_add_chat_cb, 0, "<StockItem>", GTK_STOCK_ADD },
	{ N_("/Buddies/Add _Group..."), NULL, gaim_blist_request_add_group, 0, "<StockItem>", GTK_STOCK_ADD },
	{ "/Buddies/sep3", NULL, NULL, 0, "<Separator>", NULL },
	{ N_("/Buddies/_Quit"), "<CTL>Q", gaim_core_quit, 0, "<StockItem>", GTK_STOCK_QUIT },

	/* Accounts menu */
	{ N_("/_Accounts"), NULL, NULL, 0, "<Branch>", NULL },
	{ N_("/Accounts/Add\\/Edit"), "<CTL>A", pidgin_accounts_window_show, 0, "<Item>", NULL },

	/* Tools */
	{ N_("/_Tools"), NULL, NULL, 0, "<Branch>", NULL },
	{ N_("/Tools/Buddy _Pounces"), NULL, pidgin_pounces_manager_show, 0, "<Item>", NULL },
	{ N_("/Tools/Plu_gins"), "<CTL>U", pidgin_plugin_dialog_show, 0, "<StockItem>", PIDGIN_STOCK_TOOLBAR_PLUGINS },
	{ N_("/Tools/Pr_eferences"), "<CTL>P", pidgin_prefs_show, 0, "<StockItem>", GTK_STOCK_PREFERENCES },
	{ N_("/Tools/Pr_ivacy"), NULL, pidgin_privacy_dialog_show, 0, "<Item>", NULL },
	{ "/Tools/sep2", NULL, NULL, 0, "<Separator>", NULL },
	{ N_("/Tools/_File Transfers"), "<CTL>T", pidginxfer_dialog_show, 0, "<Item>", NULL },
	{ N_("/Tools/R_oom List"), NULL, pidgin_roomlist_dialog_show, 0, "<Item>", NULL },
	{ N_("/Tools/System _Log"), NULL, gtk_blist_show_systemlog_cb, 0, "<Item>", NULL },
	{ "/Tools/sep3", NULL, NULL, 0, "<Separator>", NULL },
	{ N_("/Tools/Mute _Sounds"), "<CTL>S", pidgin_blist_mute_sounds_cb, 0, "<CheckItem>", NULL },

	/* Help */
	{ N_("/_Help"), NULL, NULL, 0, "<Branch>", NULL },
	{ N_("/Help/Online _Help"), "F1", gtk_blist_show_onlinehelp_cb, 0, "<StockItem>", GTK_STOCK_HELP },
	{ N_("/Help/_Debug Window"), NULL, toggle_debug, 0, "<Item>", NULL },
	{ N_("/Help/_About"), NULL, pidgindialogs_about, 0,  "<StockItem>", PIDGIN_STOCK_ABOUT },
};

/*********************************************************
 * Private Utility functions                             *
 *********************************************************/

static char *gaim_get_tooltip_text(GaimBlistNode *node, gboolean full)
{
	GString *str = g_string_new("");
	GaimPlugin *prpl;
	GaimPluginProtocolInfo *prpl_info = NULL;
	char *tmp;

	if (GAIM_BLIST_NODE_IS_CHAT(node))
	{
		GaimChat *chat;
		GList *cur;
		struct proto_chat_entry *pce;
		char *name, *value;

		chat = (GaimChat *)node;
		prpl = gaim_find_prpl(gaim_account_get_protocol_id(chat->account));
		prpl_info = GAIM_PLUGIN_PROTOCOL_INFO(prpl);

		if (g_list_length(gaim_connections_get_all()) > 1)
		{
			tmp = g_markup_escape_text(chat->account->username, -1);
			g_string_append_printf(str, _("\n<b>Account:</b> %s"), tmp);
			g_free(tmp);
		}

		if (prpl_info->chat_info != NULL)
			cur = prpl_info->chat_info(chat->account->gc);
		else
			cur = NULL;

		while (cur != NULL)
		{
			pce = cur->data;

			if (!pce->secret && (!pce->required &&
				g_hash_table_lookup(chat->components, pce->identifier) == NULL))
			{
				tmp = gaim_text_strip_mnemonic(pce->label);
				name = g_markup_escape_text(tmp, -1);
				g_free(tmp);
				value = g_markup_escape_text(g_hash_table_lookup(
										chat->components, pce->identifier), -1);
				g_string_append_printf(str, "\n<b>%s</b> %s",
							name ? name : "",
							value ? value : "");
				g_free(name);
				g_free(value);
			}

			g_free(pce);
			cur = g_list_remove(cur, pce);
		}
	}
	else if (GAIM_BLIST_NODE_IS_CONTACT(node) || GAIM_BLIST_NODE_IS_BUDDY(node))
	{
		/* NOTE: THIS FUNCTION IS NO LONGER CALLED FOR CONTACTS.
		 * It is only called by create_tip_for_node(), and create_tip_for_node() is never called for a contact.
		 */
		GaimContact *c;
		GaimBuddy *b;
		GaimPresence *presence;
		GaimNotifyUserInfo *user_info;
		char *tmp;
		time_t idle_secs, signon;

		if (GAIM_BLIST_NODE_IS_CONTACT(node))
		{
			c = (GaimContact *)node;
			b = gaim_contact_get_priority_buddy(c);
		}
		else
		{
			b = (GaimBuddy *)node;
			c = gaim_buddy_get_contact(b);
		}

		prpl = gaim_find_prpl(gaim_account_get_protocol_id(b->account));
		prpl_info = GAIM_PLUGIN_PROTOCOL_INFO(prpl);

		presence = gaim_buddy_get_presence(b);
		user_info = gaim_notify_user_info_new();

		/* Account */
		if (full && g_list_length(gaim_connections_get_all()) > 1)
		{
			tmp = g_markup_escape_text(gaim_account_get_username(
									   gaim_buddy_get_account(b)), -1);
			gaim_notify_user_info_add_pair(user_info, _("Account"), tmp);
			g_free(tmp);
		}

		/* Alias */
		/* If there's not a contact alias, the node is being displayed with
		 * this alias, so there's no point in showing it in the tooltip. */
		if (full && b->alias != NULL && b->alias[0] != '\0' &&
		    (c->alias != NULL && c->alias[0] != '\0') &&
		    strcmp(c->alias, b->alias) != 0)
		{
			tmp = g_markup_escape_text(b->alias, -1);
			gaim_notify_user_info_add_pair(user_info, _("Buddy Alias"), tmp);
			g_free(tmp);
		}

		/* Nickname/Server Alias */
		/* I'd like to only show this if there's a contact or buddy
		 * alias, but many people on MSN set long nicknames, which
		 * get ellipsized, so the only way to see the whole thing is
		 * to look at the tooltip. */
		if (full && b->server_alias != NULL && b->server_alias[0] != '\0')
		{
			tmp = g_markup_escape_text(b->server_alias, -1);
			gaim_notify_user_info_add_pair(user_info, _("Nickname"), tmp);
			g_free(tmp);
		}

		/* Logged In */
		signon = gaim_presence_get_login_time(presence);
		if (full && GAIM_BUDDY_IS_ONLINE(b) && signon > 0)
		{
			tmp = gaim_str_seconds_to_string(time(NULL) - signon);
			gaim_notify_user_info_add_pair(user_info, _("Logged In"), tmp);
			g_free(tmp);
		}

		/* Idle */
		if (gaim_presence_is_idle(presence))
		{
			idle_secs = gaim_presence_get_idle_time(presence);
			if (idle_secs > 0)
			{
				tmp = gaim_str_seconds_to_string(time(NULL) - idle_secs);
				gaim_notify_user_info_add_pair(user_info, _("Idle"), tmp);
				g_free(tmp);
			}
		}

		/* Last Seen */
		if (full && !GAIM_BUDDY_IS_ONLINE(b))
		{
			struct _pidgin_blist_node *gtknode = ((GaimBlistNode *)c)->ui_data;
			GaimBlistNode *bnode;
			int lastseen = 0;

			if (!gtknode->contact_expanded || GAIM_BLIST_NODE_IS_CONTACT(node))
			{
				/* We're either looking at a buddy for a collapsed contact or
				 * an expanded contact itself so we show the most recent
				 * (largest) last_seen time for any of the buddies under
				 * the contact. */
				for (bnode = ((GaimBlistNode *)c)->child ; bnode != NULL ; bnode = bnode->next)
				{
					int value = gaim_blist_node_get_int(bnode, "last_seen");
					if (value > lastseen)
						lastseen = value;
				}
			}
			else
			{
				/* We're dealing with a buddy under an expanded contact,
				 * so we show the last_seen time for the buddy. */
				lastseen = gaim_blist_node_get_int(&b->node, "last_seen");
			}

			if (lastseen > 0)
			{
				tmp = gaim_str_seconds_to_string(time(NULL) - lastseen);
				gaim_notify_user_info_add_pair(user_info, _("Last Seen"), tmp);
				g_free(tmp);
			}
		}


		/* Offline? */
		/* FIXME: Why is this status special-cased by the core? -- rlaager */
		if (!GAIM_BUDDY_IS_ONLINE(b)) {
			gaim_notify_user_info_add_pair(user_info, _("Status"), _("Offline"));
		}

		if (prpl_info && prpl_info->tooltip_text)
		{
			/* Additional text from the PRPL */
			prpl_info->tooltip_text(b, user_info, full);
		}

		/* These are Easter Eggs.  Patches to remove them will be rejected. */
		if (!g_ascii_strcasecmp(b->name, "robflynn"))
			gaim_notify_user_info_add_pair(user_info, _("Description"), _("Spooky"));
		if (!g_ascii_strcasecmp(b->name, "seanegn"))
			gaim_notify_user_info_add_pair(user_info, _("Status"), _("Awesome"));
		if (!g_ascii_strcasecmp(b->name, "chipx86"))
			gaim_notify_user_info_add_pair(user_info, _("Status"), _("Rockin'"));

		tmp = gaim_notify_user_info_get_text_with_newline(user_info, "\n");
		g_string_append(str, tmp);
		g_free(tmp);

		gaim_notify_user_info_destroy(user_info);
	}

	gaim_signal_emit(pidgin_blist_get_handle(),
			 "drawing-tooltip", node, str, full);

	return g_string_free(str, FALSE);
}

GdkPixbuf *
pidgin_blist_get_emblem(GaimBlistNode *node)
{
	GaimBuddy *buddy = NULL;
	struct _pidgin_blist_node *gtknode = node->ui_data;
	struct _pidgin_blist_node *gtkbuddynode = NULL;
	GaimPlugin *prpl;
	GaimPluginProtocolInfo *prpl_info;
	const char *name = NULL;
	char *filename, *path;
	GdkPixbuf *ret;
	GaimPresence *p;



	if(GAIM_BLIST_NODE_IS_CONTACT(node)) {
		if(!gtknode->contact_expanded) {
			buddy = gaim_contact_get_priority_buddy((GaimContact*)node);
			gtkbuddynode = ((GaimBlistNode*)buddy)->ui_data;
		}
	} else if(GAIM_BLIST_NODE_IS_BUDDY(node)) {
		buddy = (GaimBuddy*)node;
		gtkbuddynode = node->ui_data;
		if (((struct _pidgin_blist_node*)(node->parent->ui_data))->contact_expanded)
			return pidgin_create_prpl_icon(((GaimBuddy*)node)->account, PIDGIN_PRPL_ICON_SMALL);
	} else if(GAIM_BLIST_NODE_IS_CHAT(node)) {
		return pidgin_create_prpl_icon(((GaimChat*)node)->account, PIDGIN_PRPL_ICON_SMALL);
	} else {
		return NULL;
	}
	
	if (!gaim_privacy_check(buddy->account, gaim_buddy_get_name(buddy))) {
		path = g_build_filename(DATADIR, "pixmaps", "pidgin", "emblems", "16", "blocked.png", NULL);
		ret = gdk_pixbuf_new_from_file(path, NULL);
		g_free(path);
		return ret;
	}
	
	p = gaim_buddy_get_presence(buddy);
	if (gaim_presence_is_status_primitive_active(p, GAIM_STATUS_MOBILE)) {
		path = g_build_filename(DATADIR, "pixmaps", "pidgin", "emblems", "16", "mobile.png", NULL);
		ret = gdk_pixbuf_new_from_file(path, NULL);
		g_free(path);
		return ret;	
	}

	prpl = gaim_find_prpl(gaim_account_get_protocol_id(buddy->account));
	if (!prpl)
		return NULL;

	prpl_info = GAIM_PLUGIN_PROTOCOL_INFO(prpl);
	if (prpl_info && prpl_info->list_emblem)
		name = prpl_info->list_emblem(buddy);

	if (name == NULL)
		return NULL;

	filename = g_strdup_printf("%s.png", name);

	path = g_build_filename(DATADIR, "pixmaps", "pidgin", "emblems", "16", filename, NULL);
	ret = gdk_pixbuf_new_from_file(path, NULL);
	
	g_free(filename);
	g_free(path);

	return ret;
}


GdkPixbuf *
pidgin_blist_get_status_icon(GaimBlistNode *node, PidginStatusIconSize size)
{
	GdkPixbuf *ret;
	const char *protoname = NULL;
	struct _pidgin_blist_node *gtknode = node->ui_data;
	struct _pidgin_blist_node *gtkbuddynode = NULL;
	GaimBuddy *buddy = NULL;
	GaimChat *chat = NULL;
	GtkIconSize icon_size = gtk_icon_size_from_name((size == PIDGIN_STATUS_ICON_LARGE) ? PIDGIN_ICON_SIZE_TANGO_SMALL :
											 PIDGIN_ICON_SIZE_TANGO_EXTRA_SMALL);

	if(GAIM_BLIST_NODE_IS_CONTACT(node)) {
		if(!gtknode->contact_expanded) {
			buddy = gaim_contact_get_priority_buddy((GaimContact*)node);
			gtkbuddynode = ((GaimBlistNode*)buddy)->ui_data;
		}
	} else if(GAIM_BLIST_NODE_IS_BUDDY(node)) {
		buddy = (GaimBuddy*)node;
		gtkbuddynode = node->ui_data;
	} else if(GAIM_BLIST_NODE_IS_CHAT(node)) {
		chat = (GaimChat*)node;
	} else {
		return NULL;
	}

	if(buddy || chat) {
		GaimAccount *account;
		GaimPlugin *prpl;
		GaimPluginProtocolInfo *prpl_info;

		if(buddy)
			account = buddy->account;
		else
			account = chat->account;

		prpl = gaim_find_prpl(gaim_account_get_protocol_id(account));
		if(!prpl)
			return NULL;

		prpl_info = GAIM_PLUGIN_PROTOCOL_INFO(prpl);

		if(prpl_info && prpl_info->list_icon) {
			protoname = prpl_info->list_icon(account, buddy);
		}
	}
	
	if(buddy) {
	  	GaimConversation *conv = gaim_find_conversation_with_account(GAIM_CONV_TYPE_IM,
									     gaim_buddy_get_name(buddy),
									     gaim_buddy_get_account(buddy));
		GaimPresence *p;
		if(conv != NULL) {
			PidginConversation *gtkconv = PIDGIN_CONVERSATION(conv);
			if(gtkconv != NULL && pidgin_conv_is_hidden(gtkconv) && size == PIDGIN_STATUS_ICON_SMALL) {
				return gtk_widget_render_icon (GTK_WIDGET(gtkblist->treeview), PIDGIN_STOCK_STATUS_MESSAGE,
							       icon_size, "GtkTreeView");
			}
		}
		p = gaim_buddy_get_presence(buddy);
	       
		if (GAIM_BUDDY_IS_ONLINE(buddy) && gtkbuddynode && gtkbuddynode->recent_signonoff)
			ret = gtk_widget_render_icon (GTK_WIDGET(gtkblist->treeview), PIDGIN_STOCK_STATUS_LOGIN,
					icon_size, "GtkTreeView");
		else if (gtkbuddynode && gtkbuddynode->recent_signonoff)
			ret = gtk_widget_render_icon (GTK_WIDGET(gtkblist->treeview), PIDGIN_STOCK_STATUS_LOGOUT,
					icon_size, "GtkTreeView");
		else if (gaim_presence_is_status_primitive_active(p, GAIM_STATUS_UNAVAILABLE))
			if (gaim_presence_is_idle(p) && size == PIDGIN_STATUS_ICON_SMALL)
				ret = gtk_widget_render_icon (GTK_WIDGET(gtkblist->treeview), PIDGIN_STOCK_STATUS_BUSY_I,
						icon_size, "GtkTreeView");
			else
				ret = gtk_widget_render_icon (GTK_WIDGET(gtkblist->treeview), PIDGIN_STOCK_STATUS_BUSY,
						icon_size, "GtkTreeView");
		else if (gaim_presence_is_status_primitive_active(p, GAIM_STATUS_AWAY))
		        if (gaim_presence_is_idle(p) && size == PIDGIN_STATUS_ICON_SMALL)
		                ret = gtk_widget_render_icon (GTK_WIDGET(gtkblist->treeview), PIDGIN_STOCK_STATUS_AWAY_I,
		                                icon_size, "GtkTreeView");
		 	else
				ret = gtk_widget_render_icon (GTK_WIDGET(gtkblist->treeview), PIDGIN_STOCK_STATUS_AWAY,
						icon_size, "GtkTreeView");
		else if (gaim_presence_is_status_primitive_active(p, GAIM_STATUS_EXTENDED_AWAY))
			if (gaim_presence_is_idle(p) && size == PIDGIN_STATUS_ICON_SMALL)
		        	ret = gtk_widget_render_icon (GTK_WIDGET(gtkblist->treeview), PIDGIN_STOCK_STATUS_XA_I,
						icon_size, "GtkTreeView");
			else
				ret = gtk_widget_render_icon (GTK_WIDGET(gtkblist->treeview), PIDGIN_STOCK_STATUS_XA,
						icon_size, "GtkTreeView");
		else if (gaim_presence_is_status_primitive_active(p, GAIM_STATUS_OFFLINE))
			ret = gtk_widget_render_icon (GTK_WIDGET(gtkblist->treeview), PIDGIN_STOCK_STATUS_OFFLINE,
					icon_size, "GtkTreeView");
		else if (gaim_presence_is_idle(p) && size == PIDGIN_STATUS_ICON_SMALL)
			ret = gtk_widget_render_icon (GTK_WIDGET(gtkblist->treeview), PIDGIN_STOCK_STATUS_AVAILABLE_I,
					icon_size, "GtkTreeView");
		else
			ret = gtk_widget_render_icon(GTK_WIDGET(gtkblist->treeview), PIDGIN_STOCK_STATUS_AVAILABLE,
					icon_size, "GtkTreeView");
	} else if (chat) {
		ret = gtk_widget_render_icon (GTK_WIDGET(gtkblist->treeview), PIDGIN_STOCK_STATUS_CHAT,
				icon_size, "GtkTreeView");
	} else {
		ret = gtk_widget_render_icon (GTK_WIDGET(gtkblist->treeview), PIDGIN_STOCK_STATUS_PERSON,
				icon_size, "GtkTreeView");
	}

	return ret;
}

static gchar *pidgin_blist_get_name_markup(GaimBuddy *b, gboolean selected)
{
	const char *name;
	char *esc, *text = NULL;
	GaimPlugin *prpl;
	GaimPluginProtocolInfo *prpl_info = NULL;
	GaimContact *contact;
	GaimPresence *presence;
	struct _pidgin_blist_node *gtkcontactnode = NULL;
	char *idletime = NULL, *statustext = NULL;
	time_t t;
	GaimConversation *conv = gaim_find_conversation_with_account(GAIM_CONV_TYPE_IM,
								     gaim_buddy_get_name(b),
								     gaim_buddy_get_account(b));
	PidginConversation *gtkconv;
	gboolean hidden_conv = FALSE;

	if(conv != NULL) {
		gtkconv = PIDGIN_CONVERSATION(conv);
		if(gtkconv != NULL && pidgin_conv_is_hidden(gtkconv)) {
			hidden_conv = TRUE;
		}
	}
	
	/* XXX Good luck cleaning up this crap */

	contact = (GaimContact*)((GaimBlistNode*)b)->parent;
	if(contact)
		gtkcontactnode = ((GaimBlistNode*)contact)->ui_data;

	if(gtkcontactnode && !gtkcontactnode->contact_expanded && contact->alias)
		name = contact->alias;
	else
		name = gaim_buddy_get_alias(b);
	esc = g_markup_escape_text(name, strlen(name));

	presence = gaim_buddy_get_presence(b);

	if (!gaim_prefs_get_bool("/gaim/gtk/blist/show_buddy_icons"))
	{
		if (!selected && gaim_presence_is_idle(presence))
		{
			text = g_strdup_printf("<span color='%s'>%s</span>",
					       dim_grey(), esc);
			g_free(esc);
			if (hidden_conv) {
				char *tmp = text;
				text = g_strdup_printf("<b>%s</b>", text);
				g_free(tmp);
			}
			return text;
		}
		else
			if (hidden_conv) {
				char *tmp = esc;
				esc = g_strdup_printf("<b>%s</b>", esc);
				g_free(tmp);
			}
			return esc;
	}

	prpl = gaim_find_prpl(gaim_account_get_protocol_id(b->account));

	if (prpl != NULL)
		prpl_info = GAIM_PLUGIN_PROTOCOL_INFO(prpl);

	if (prpl_info && prpl_info->status_text && b->account->gc) {
		char *tmp = prpl_info->status_text(b);
		const char *end;

		if(tmp && !g_utf8_validate(tmp, -1, &end)) {
			char *new = g_strndup(tmp,
					g_utf8_pointer_to_offset(tmp, end));
			g_free(tmp);
			tmp = new;
		}

#if !GTK_CHECK_VERSION(2,6,0)
		if(tmp) {
			char buf[32];
			char *c = tmp;
			int length = 0, vis=0;
			gboolean inside = FALSE;
			g_strdelimit(tmp, "\n", ' ');
			gaim_str_strip_char(tmp, '\r');

			while(*c && vis < 20) {
				if(*c == '&')
					inside = TRUE;
				else if(*c == ';')
					inside = FALSE;
				if(!inside)
					vis++;
				c = g_utf8_next_char(c); /* this is fun */
			}

			length = c - tmp;

			if(vis == 20)
				g_snprintf(buf, sizeof(buf), "%%.%ds...", length);
			else
				g_snprintf(buf, sizeof(buf), "%%s ");

			statustext = g_strdup_printf(buf, tmp);

			g_free(tmp);
		}
#else
		if(tmp) {
			g_strdelimit(tmp, "\n", ' ');
			gaim_str_strip_char(tmp, '\r');
		}
		statustext = tmp;
#endif
	}

	if(!gaim_presence_is_online(presence) && !statustext)
		statustext = g_strdup(_("Offline"));
	else if (!statustext)
		text = g_strdup(esc);

	if (gaim_presence_is_idle(presence)) {
		if (gaim_prefs_get_bool("/gaim/gtk/blist/show_idle_time")) {
			time_t idle_secs = gaim_presence_get_idle_time(presence);

			if (idle_secs > 0) {
				int ihrs, imin;

				time(&t);
				ihrs = (t - idle_secs) / 3600;
				imin = ((t - idle_secs) / 60) % 60;

				if (ihrs)
					idletime = g_strdup_printf(_("Idle %dh %02dm"), ihrs, imin);
				else
					idletime = g_strdup_printf(_("Idle %dm"), imin);
			}
			else
				idletime = g_strdup(_("Idle"));

			if (!selected)
				text = g_strdup_printf("<span color='%s'>%s</span>\n"
				"<span color='%s' size='smaller'>%s%s%s</span>",
				dim_grey(), esc, dim_grey(),
				idletime != NULL ? idletime : "",
				(idletime != NULL && statustext != NULL) ? " - " : "",
				statustext != NULL ? statustext : "");
		}
		else if (!selected && !statustext) /* We handle selected text later */
			text = g_strdup_printf("<span color='%s'>%s</span>", dim_grey(), esc);
		else if (!selected && !text)
			text = g_strdup_printf("<span color='%s'>%s</span>\n"
				"<span color='%s' size='smaller'>%s</span>",
				dim_grey(), esc, dim_grey(),
				statustext != NULL ? statustext : "");
	}

	/* Not idle and not selected */
	else if (!selected && !text)
	{
		text = g_strdup_printf("%s\n"
			"<span color='%s' size='smaller'>%s</span>",
			esc, dim_grey(),
			statustext != NULL ? statustext :  "");
	}

	/* It is selected. */
	if ((selected && !text) || (selected && idletime))
		text = g_strdup_printf("%s\n"
			"<span size='smaller'>%s%s%s</span>",
			esc,
			idletime != NULL ? idletime : "",
			(idletime != NULL && statustext != NULL) ? " - " : "",
			statustext != NULL ? statustext :  "");

	g_free(idletime);
	g_free(statustext);
	g_free(esc);
	
	if (hidden_conv) {
		char *tmp = text;
		text = g_strdup_printf("<b>%s</b>", tmp);
		g_free(tmp);
	}
	
	return text;
}

static void pidgin_blist_restore_position()
{
	int blist_x, blist_y, blist_width, blist_height;

	blist_width = gaim_prefs_get_int("/gaim/gtk/blist/width");

	/* if the window exists, is hidden, we're saving positions, and the
	 * position is sane... */
	if (gtkblist && gtkblist->window &&
		!GTK_WIDGET_VISIBLE(gtkblist->window) && blist_width != 0) {

		blist_x      = gaim_prefs_get_int("/gaim/gtk/blist/x");
		blist_y      = gaim_prefs_get_int("/gaim/gtk/blist/y");
		blist_height = gaim_prefs_get_int("/gaim/gtk/blist/height");

		/* ...check position is on screen... */
		if (blist_x >= gdk_screen_width())
			blist_x = gdk_screen_width() - 100;
		else if (blist_x + blist_width < 0)
			blist_x = 100;

		if (blist_y >= gdk_screen_height())
			blist_y = gdk_screen_height() - 100;
		else if (blist_y + blist_height < 0)
			blist_y = 100;

		/* ...and move it back. */
		gtk_window_move(GTK_WINDOW(gtkblist->window), blist_x, blist_y);
		gtk_window_resize(GTK_WINDOW(gtkblist->window), blist_width, blist_height);
		if (gaim_prefs_get_bool("/gaim/gtk/blist/list_maximized"))
			gtk_window_maximize(GTK_WINDOW(gtkblist->window));
	}
}

static gboolean pidgin_blist_refresh_timer(GaimBuddyList *list)
{
	GaimBlistNode *gnode, *cnode;

	if (gtk_blist_obscured || !GTK_WIDGET_VISIBLE(gtkblist->window))
		return TRUE;

	for(gnode = list->root; gnode; gnode = gnode->next) {
		if(!GAIM_BLIST_NODE_IS_GROUP(gnode))
			continue;
		for(cnode = gnode->child; cnode; cnode = cnode->next) {
			if(GAIM_BLIST_NODE_IS_CONTACT(cnode)) {
				GaimBuddy *buddy;

				buddy = gaim_contact_get_priority_buddy((GaimContact*)cnode);

				if (buddy &&
						gaim_presence_is_idle(gaim_buddy_get_presence(buddy)))
					pidgin_blist_update_contact(list, (GaimBlistNode*)buddy);
			}
		}
	}

	/* keep on going */
	return TRUE;
}

static void pidgin_blist_hide_node(GaimBuddyList *list, GaimBlistNode *node, gboolean update)
{
	struct _pidgin_blist_node *gtknode = (struct _pidgin_blist_node *)node->ui_data;
	GtkTreeIter iter;

	if (!gtknode || !gtknode->row || !gtkblist)
		return;

	if(gtkblist->selected_node == node)
		gtkblist->selected_node = NULL;
	if (get_iter_from_node(node, &iter)) {
		gtk_tree_store_remove(gtkblist->treemodel, &iter);
		if(update && (GAIM_BLIST_NODE_IS_CONTACT(node) ||
			GAIM_BLIST_NODE_IS_BUDDY(node) || GAIM_BLIST_NODE_IS_CHAT(node))) {
			pidgin_blist_update(list, node->parent);
		}
	}
	gtk_tree_row_reference_free(gtknode->row);
	gtknode->row = NULL;
}

static const char *require_connection[] =
{
	N_("/Buddies/New Instant Message..."),
	N_("/Buddies/Join a Chat..."),
	N_("/Buddies/Get User Info..."),
	N_("/Buddies/Add Buddy..."),
	N_("/Buddies/Add Chat..."),
	N_("/Buddies/Add Group..."),
};

static const int require_connection_size = sizeof(require_connection)
											/ sizeof(*require_connection);

/**
 * Rebuild dynamic menus and make menu items sensitive/insensitive
 * where appropriate.
 */
static void
update_menu_bar(PidginBuddyList *gtkblist)
{
	GtkWidget *widget;
	gboolean sensitive;
	int i;

	g_return_if_fail(gtkblist != NULL);

	pidgin_blist_update_accounts_menu();

	sensitive = (gaim_connections_get_all() != NULL);

	for (i = 0; i < require_connection_size; i++)
	{
		widget = gtk_item_factory_get_widget(gtkblist->ift, require_connection[i]);
		gtk_widget_set_sensitive(widget, sensitive);
	}

	widget = gtk_item_factory_get_widget(gtkblist->ift, N_("/Buddies/Join a Chat..."));
	gtk_widget_set_sensitive(widget, pidgin_blist_joinchat_is_showable());

	widget = gtk_item_factory_get_widget(gtkblist->ift, N_("/Buddies/Add Chat..."));
	gtk_widget_set_sensitive(widget, pidgin_blist_joinchat_is_showable());

	widget = gtk_item_factory_get_widget(gtkblist->ift, N_("/Tools/Buddy Pounces"));
	gtk_widget_set_sensitive(widget, (gaim_accounts_get_all() != NULL));

	widget = gtk_item_factory_get_widget(gtkblist->ift, N_("/Tools/Privacy"));
	gtk_widget_set_sensitive(widget, (gaim_connections_get_all() != NULL));

	widget = gtk_item_factory_get_widget(gtkblist->ift, N_("/Tools/Room List"));
	gtk_widget_set_sensitive(widget, pidgin_roomlist_is_showable());
}

static void
sign_on_off_cb(GaimConnection *gc, GaimBuddyList *blist)
{
	PidginBuddyList *gtkblist = PIDGIN_BLIST(blist);

	update_menu_bar(gtkblist);
}

static void
plugin_changed_cb(GaimPlugin *p, gpointer *data)
{
	pidgin_blist_update_plugin_actions();
}

static void
unseen_conv_menu()
{
	static GtkWidget *menu = NULL;
	GList *convs = NULL;

	if (menu) {
		gtk_widget_destroy(menu);
		menu = NULL;
	}

	convs = pidgin_conversations_find_unseen_list(GAIM_CONV_TYPE_IM, PIDGIN_UNSEEN_TEXT, TRUE, 0);
	if (!convs)
		/* no conversations added, don't show the menu */
		return;

	menu = gtk_menu_new();

	pidgin_conversations_fill_menu(menu, convs);
	g_list_free(convs);
	gtk_widget_show_all(menu);
	gtk_menu_popup(GTK_MENU(menu), NULL, NULL, NULL, NULL, 3,
			gtk_get_current_event_time());
}

static gboolean
menutray_press_cb(GtkWidget *widget, GdkEventButton *event)
{
	GList *convs;

	switch (event->button) {
		case 1:
			convs = pidgin_conversations_find_unseen_list(GAIM_CONV_TYPE_IM,
															PIDGIN_UNSEEN_TEXT, TRUE, 1);
			if (convs) {
				pidgin_conv_present_conversation((GaimConversation*)convs->data);
				g_list_free(convs);
			}
			break;
		case 3:
			unseen_conv_menu();
			break;
	}
	return TRUE;
}

static void
conversation_updated_cb(GaimConversation *conv, GaimConvUpdateType type,
                        PidginBuddyList *gtkblist)
{
	GList *convs = NULL;
	GList *l = NULL;

	if (type != GAIM_CONV_UPDATE_UNSEEN)
		return;

	if(conv->account != NULL && conv->name != NULL) {
		GaimBuddy *buddy = gaim_find_buddy(conv->account, conv->name);
		if(buddy != NULL)
			pidgin_blist_update_buddy(NULL, (GaimBlistNode *)buddy, TRUE);
	}

	if (gtkblist->menutrayicon) {
		gtk_widget_destroy(gtkblist->menutrayicon);
		gtkblist->menutrayicon = NULL;
	}

	convs = pidgin_conversations_find_unseen_list(GAIM_CONV_TYPE_IM, PIDGIN_UNSEEN_TEXT, TRUE, 0);
	if (convs) {
		GtkWidget *img = NULL;
		GString *tooltip_text = NULL;

		tooltip_text = g_string_new("");
		l = convs;
		while (l != NULL) {
			if (GAIM_IS_GTK_CONVERSATION(l->data)) {
				PidginConversation *gtkconv = PIDGIN_CONVERSATION((GaimConversation *)l->data);

				g_string_append_printf(tooltip_text,
						ngettext("%d unread message from %s\n", "%d unread messages from %s\n", gtkconv->unseen_count),
						gtkconv->unseen_count,
						gtk_label_get_text(GTK_LABEL(gtkconv->tab_label)));
			}
			l = l->next;
		}
		if(tooltip_text->len > 0) {
			/* get rid of the last newline */
			g_string_truncate(tooltip_text, tooltip_text->len -1);
			img = gtk_image_new_from_stock(PIDGIN_STOCK_TOOLBAR_PENDING, 
							gtk_icon_size_from_name(PIDGIN_ICON_SIZE_TANGO_EXTRA_SMALL));

			gtkblist->menutrayicon = gtk_event_box_new();
			gtk_container_add(GTK_CONTAINER(gtkblist->menutrayicon), img);
			gtk_widget_show(img);
			gtk_widget_show(gtkblist->menutrayicon);
			g_signal_connect(G_OBJECT(gtkblist->menutrayicon), "button-press-event", G_CALLBACK(menutray_press_cb), NULL);

			pidgin_menu_tray_append(PIDGIN_MENU_TRAY(gtkblist->menutray), gtkblist->menutrayicon, tooltip_text->str);
		}
		g_string_free(tooltip_text, TRUE);
		g_list_free(convs);
	}
}

static void
conversation_deleting_cb(GaimConversation *conv, PidginBuddyList *gtkblist)
{
	conversation_updated_cb(conv, GAIM_CONV_UPDATE_UNSEEN, gtkblist);
}

/**********************************************************************************
 * Public API Functions                                                           *
 **********************************************************************************/

static void pidgin_blist_new_list(GaimBuddyList *blist)
{
	PidginBuddyList *gtkblist;

	gtkblist = g_new0(PidginBuddyList, 1);
	gtkblist->connection_errors = g_hash_table_new_full(g_direct_hash,
												g_direct_equal, NULL, g_free);
	blist->ui_data = gtkblist;
}

static void pidgin_blist_new_node(GaimBlistNode *node)
{
	node->ui_data = g_new0(struct _pidgin_blist_node, 1);
}

gboolean pidgin_blist_node_is_contact_expanded(GaimBlistNode *node)
{
	if GAIM_BLIST_NODE_IS_BUDDY(node)
		node = node->parent;

	g_return_val_if_fail(GAIM_BLIST_NODE_IS_CONTACT(node), FALSE);

	return ((struct _pidgin_blist_node *)node->ui_data)->contact_expanded;
}

enum {
	DRAG_BUDDY,
	DRAG_ROW,
	DRAG_VCARD,
	DRAG_TEXT,
	DRAG_URI,
	NUM_TARGETS
};

static const char *
item_factory_translate_func (const char *path, gpointer func_data)
{
	return _((char *)path);
}

void pidgin_blist_setup_sort_methods()
{
	pidgin_blist_sort_method_reg("none", _("Manually"), sort_method_none);
#if GTK_CHECK_VERSION(2,2,1)
	pidgin_blist_sort_method_reg("alphabetical", _("Alphabetically"), sort_method_alphabetical);
	pidgin_blist_sort_method_reg("status", _("By status"), sort_method_status);
	pidgin_blist_sort_method_reg("log_size", _("By log size"), sort_method_log);
#endif
	pidgin_blist_sort_method_set(gaim_prefs_get_string("/gaim/gtk/blist/sort_type"));
}

static void _prefs_change_redo_list()
{
	GtkTreeSelection *sel;
	GtkTreeIter iter;
	GaimBlistNode *node = NULL;

	sel = gtk_tree_view_get_selection(GTK_TREE_VIEW(gtkblist->treeview));
	if (gtk_tree_selection_get_selected(sel, NULL, &iter))
	{
		gtk_tree_model_get(GTK_TREE_MODEL(gtkblist->treemodel), &iter, NODE_COLUMN, &node, -1);
	}

	redo_buddy_list(gaim_get_blist(), FALSE, FALSE);
#if GTK_CHECK_VERSION(2,6,0)
	gtk_tree_view_columns_autosize(GTK_TREE_VIEW(gtkblist->treeview));
#endif

	if (node)
	{
		struct _pidgin_blist_node *gtknode;
		GtkTreePath *path;

		gtknode = node->ui_data;
		if (gtknode && gtknode->row)
		{
			path = gtk_tree_row_reference_get_path(gtknode->row);
			gtk_tree_selection_select_path(sel, path);
			gtk_tree_view_scroll_to_cell(GTK_TREE_VIEW(gtkblist->treeview), path, NULL, FALSE, 0, 0);
			gtk_tree_path_free(path);
		}
	}
}

static void _prefs_change_sort_method(const char *pref_name, GaimPrefType type,
									  gconstpointer val, gpointer data)
{
	if(!strcmp(pref_name, "/gaim/gtk/blist/sort_type"))
		pidgin_blist_sort_method_set(val);
}

static void account_modified(GaimAccount *account, PidginBuddyList *gtkblist)
{
	GList *list;
	if (!gtkblist)
		return;

	if ((list = gaim_accounts_get_all_active()) != NULL) {
		gtk_notebook_set_current_page(GTK_NOTEBOOK(gtkblist->notebook), 1);
		g_list_free(list);
	} else
		gtk_notebook_set_current_page(GTK_NOTEBOOK(gtkblist->notebook), 0);

	update_menu_bar(gtkblist);
}

static void
account_status_changed(GaimAccount *account, GaimStatus *old,
					   GaimStatus *new, PidginBuddyList *gtkblist)
{
	if (!gtkblist)
		return;

	update_menu_bar(gtkblist);
}

static gboolean
gtk_blist_window_key_press_cb(GtkWidget *w, GdkEventKey *event, PidginBuddyList *gtkblist)
{
	GtkWidget *imhtml;

	if (!gtkblist)
		return FALSE;

	imhtml = gtk_window_get_focus(GTK_WINDOW(gtkblist->window));

	if (GTK_IS_IMHTML(imhtml) && gtk_bindings_activate(GTK_OBJECT(imhtml), event->keyval, event->state))
		return TRUE;
	return FALSE;
}

static gboolean
headline_hover_close(int x, int y)
{
	GtkWidget *w = gtkblist->headline_hbox;
	if (x <= w->allocation.width && x >= w->allocation.width - HEADLINE_CLOSE_SIZE &&
			y >= 0 && y <= HEADLINE_CLOSE_SIZE)
		return TRUE;
	return FALSE;
}

static gboolean
headline_box_enter_cb(GtkWidget *widget, GdkEventCrossing *event, PidginBuddyList *gtkblist)
{
	gdk_window_set_cursor(widget->window, gtkblist->hand_cursor);

	if (gtkblist->headline_close) {
#if GTK_CHECK_VERSION(2,2,0)
		gdk_draw_pixbuf(widget->window, NULL, gtkblist->headline_close,
#else
		gdk_pixbuf_render_to_drawable(gtkblist->headline_close,
				GDK_DRAWABLE(widget->window), NULL,
#endif
					0, 0,
					widget->allocation.width - 2 - HEADLINE_CLOSE_SIZE, 2,
					HEADLINE_CLOSE_SIZE, HEADLINE_CLOSE_SIZE,
					GDK_RGB_DITHER_NONE, 0, 0);
		gtk_paint_focus(widget->style, widget->window, GTK_STATE_PRELIGHT,
				NULL, widget, NULL,
				widget->allocation.width - HEADLINE_CLOSE_SIZE - 3, 1,
				HEADLINE_CLOSE_SIZE + 2, HEADLINE_CLOSE_SIZE + 2);
	}

	return FALSE;
}

#if 0
static gboolean
headline_box_motion_cb(GtkWidget *widget, GdkEventMotion *event, PidginBuddyList *gtkblist)
{
	gaim_debug_fatal("motion", "%d %d\n", (int)event->x, (int)event->y);
	if (headline_hover_close((int)event->x, (int)event->y))
		gtk_paint_focus(widget->style, widget->window, GTK_STATE_PRELIGHT,
				NULL, widget, NULL,
				widget->allocation.width - HEADLINE_CLOSE_SIZE - 3, 1,
				HEADLINE_CLOSE_SIZE + 2, HEADLINE_CLOSE_SIZE + 2);
	return FALSE;
}
#endif

static gboolean
headline_box_leave_cb(GtkWidget *widget, GdkEventCrossing *event, PidginBuddyList *gtkblist)
{
	gdk_window_set_cursor(widget->window, gtkblist->arrow_cursor);
	if (gtkblist->headline_close) {
		GdkRectangle rect = {widget->allocation.width - 3 - HEADLINE_CLOSE_SIZE, 1,
				HEADLINE_CLOSE_SIZE + 2, HEADLINE_CLOSE_SIZE + 2};
		gdk_window_invalidate_rect(widget->window, &rect, TRUE);
	}
	return FALSE;
}

static void
reset_headline(PidginBuddyList *gtkblist)
{
	gtkblist->headline_callback = NULL;
	gtkblist->headline_data = NULL;
	gtkblist->headline_destroy = NULL;
	pidgin_set_urgent(GTK_WINDOW(gtkblist->window), FALSE);
}

static gboolean
headline_click_callback(gpointer data)
{
	((GSourceFunc)gtkblist->headline_callback)(gtkblist->headline_data);
	reset_headline(gtkblist);
	return FALSE;
}

static gboolean
headline_box_press_cb(GtkWidget *widget, GdkEventButton *event, PidginBuddyList *gtkblist)
{
	gtk_widget_hide(gtkblist->headline_hbox);
	if (gtkblist->headline_callback && !headline_hover_close((int)event->x, (int)event->y))
		g_idle_add((GSourceFunc)headline_click_callback, gtkblist->headline_data);
	else {
		if (gtkblist->headline_destroy)
			gtkblist->headline_destroy(gtkblist->headline_data);
		reset_headline(gtkblist);
	}
	return TRUE;
}

/***********************************/
/* Connection error handling stuff */
/***********************************/

static void
ce_modify_account_cb(GaimAccount *account)
{
	pidgin_account_dialog_show(PIDGIN_MODIFY_ACCOUNT_DIALOG, account);
}

static void
ce_enable_account_cb(GaimAccount *account)
{
	gaim_account_set_enabled(account, gaim_core_get_ui(), TRUE);
}

static void
connection_error_button_clicked_cb(GtkButton *widget, gpointer user_data)
{
	GaimAccount *account;
	char *primary;
	const char *text;
	gboolean enabled;

	account = user_data;
	primary = g_strdup_printf(_("%s disconnected"),
							  gaim_account_get_username(account));
	text = g_hash_table_lookup(gtkblist->connection_errors, account);

	enabled = gaim_account_get_enabled(account, gaim_core_get_ui());
	gaim_request_action(account, _("Connection Error"), primary, text, 2,
						account, 3,
						_("OK"), NULL,
						_("Modify Account"), GAIM_CALLBACK(ce_modify_account_cb),
						enabled ? _("Connect") : _("Re-enable Account"),
						enabled ? GAIM_CALLBACK(gaim_account_connect) :
									GAIM_CALLBACK(ce_enable_account_cb));
	g_free(primary);
	gtk_widget_destroy(GTK_WIDGET(widget));
	g_hash_table_remove(gtkblist->connection_errors, account);
}

/* Add some buttons that show connection errors */
static void
create_connection_error_buttons(gpointer key, gpointer value,
                                gpointer user_data)
{
	GaimAccount *account;
	GaimStatusType *status_type;
	gchar *escaped, *text;
	GtkWidget *button, *label, *image, *hbox;
	GdkPixbuf *pixbuf;

	account = key;
	escaped = g_markup_escape_text((const gchar *)value, -1);
	text = g_strdup_printf(_("<span color=\"red\">%s disconnected: %s</span>"),
	                       gaim_account_get_username(account),
	                       escaped);
	g_free(escaped);

	hbox = gtk_hbox_new(FALSE, 0);

	/* Create the icon */
	if ((status_type = gaim_account_get_status_type_with_primitive(account,
							GAIM_STATUS_OFFLINE))) {
		pixbuf = pidgin_create_prpl_icon_with_status(account, status_type, 0.5);
		if (pixbuf != NULL) {
			image = gtk_image_new_from_pixbuf(pixbuf);
			g_object_unref(pixbuf);

			gtk_box_pack_start(GTK_BOX(hbox), image, FALSE, FALSE,
			                   GAIM_HIG_BOX_SPACE);
		}
	}

	/* Create the text */
	label = gtk_label_new("");
	gtk_label_set_markup(GTK_LABEL(label), text);
	g_free(text);
#if GTK_CHECK_VERSION(2,6,0)
	g_object_set(label, "ellipsize", PANGO_ELLIPSIZE_END, NULL);
#endif
	gtk_box_pack_start(GTK_BOX(hbox), label, TRUE, TRUE,
	                   GAIM_HIG_BOX_SPACE);

	/* Create the actual button and put the icon and text on it */
	button = gtk_button_new();
	gtk_container_add(GTK_CONTAINER(button), hbox);
	g_signal_connect(G_OBJECT(button), "clicked",
	                 G_CALLBACK(connection_error_button_clicked_cb),
	                 account);
	gtk_widget_show_all(button);
	gtk_box_pack_end(GTK_BOX(gtkblist->error_buttons), button,
	                 FALSE, FALSE, 0);
}

void
pidgin_blist_update_account_error_state(GaimAccount *account, const char *text)
{
	GList *l;

	if (text == NULL)
		g_hash_table_remove(gtkblist->connection_errors, account);
	else
		g_hash_table_insert(gtkblist->connection_errors, account, g_strdup(text));

	/* Remove the old error buttons */
	for (l = gtk_container_get_children(GTK_CONTAINER(gtkblist->error_buttons));
			l != NULL;
			l = l->next)
	{
		gtk_widget_destroy(GTK_WIDGET(l->data));
	}

	/* Add new error buttons */
	g_hash_table_foreach(gtkblist->connection_errors,
			create_connection_error_buttons, NULL);
}

static gboolean
paint_headline_hbox  (GtkWidget      *widget,
		      GdkEventExpose *event,
		      gpointer user_data)
{
	gtk_paint_flat_box (widget->style,
		      widget->window,
		      GTK_STATE_NORMAL,
		      GTK_SHADOW_OUT,
		      NULL,
		      widget,
		      "tooltip",
		      widget->allocation.x + 1,
		      widget->allocation.y + 1,
		      widget->allocation.width - 2,
		      widget->allocation.height - 2);
	return FALSE;
}

/* This assumes there are not things like groupless buddies or multi-leveled groups.
 * I'm sure other things in this code assumes that also.
 */
static void
treeview_style_set (GtkWidget *widget,
		    GtkStyle *prev_style,
		    gpointer data)
{
	GaimBuddyList *list = data;
	GaimBlistNode *node = list->root;
	while (node) {
		pidgin_blist_update_group(list, node);
		node = node->next;
	}
}

static void
headline_style_set (GtkWidget *widget,
		    GtkStyle  *prev_style)
{
	GtkTooltips *tooltips;
	GtkStyle *style;

	if (gtkblist->changing_style)
		return;

	tooltips = gtk_tooltips_new ();
#if GLIB_CHECK_VERSION(2,10,0)
	g_object_ref_sink (tooltips);
#else
	g_object_ref(tooltips);
	gtk_object_sink(GTK_OBJECT(tooltips));
#endif

	gtk_tooltips_force_window (tooltips);
	gtk_widget_ensure_style (tooltips->tip_window);
	style = gtk_widget_get_style (tooltips->tip_window);

	gtkblist->changing_style = TRUE;
	gtk_widget_set_style (gtkblist->headline_hbox, style);
	gtkblist->changing_style = FALSE;

	g_object_unref (tooltips);
}

/******************************************/
/* End of connection error handling stuff */
/******************************************/

static int
blist_focus_cb(GtkWidget *widget, gpointer data, PidginBuddyList *gtkblist)
{
	pidgin_set_urgent(GTK_WINDOW(gtkblist->window), FALSE);
	return 0;
}

#if 0
static GtkWidget *
kiosk_page()
{
	GtkWidget *ret = gtk_vbox_new(FALSE, GAIM_HIG_BOX_SPACE);
	GtkWidget *label;
	GtkWidget *entry;
	GtkWidget *bbox;
	GtkWidget *button;

	label = gtk_label_new(NULL);
	gtk_box_pack_start(GTK_BOX(ret), label, TRUE, TRUE, 0);

	label = gtk_label_new(NULL);
	gtk_label_set_markup(GTK_LABEL(label), _("<b>Username:</b>"));
	gtk_misc_set_alignment(GTK_MISC(label), 0.0, 0.5);
	gtk_box_pack_start(GTK_BOX(ret), label, FALSE, FALSE, 0);
	entry = gtk_entry_new();
	gtk_box_pack_start(GTK_BOX(ret), entry, FALSE, FALSE, 0);

	label = gtk_label_new(NULL);
	gtk_label_set_markup(GTK_LABEL(label), _("<b>Password:</b>"));
	gtk_misc_set_alignment(GTK_MISC(label), 0.0, 0.5);
	gtk_box_pack_start(GTK_BOX(ret), label, FALSE, FALSE, 0);
	entry = gtk_entry_new();
	gtk_entry_set_visibility(GTK_ENTRY(entry), FALSE);
	gtk_box_pack_start(GTK_BOX(ret), entry, FALSE, FALSE, 0);

	label = gtk_label_new(" ");
	gtk_box_pack_start(GTK_BOX(ret), label, FALSE, FALSE, 0);

	bbox = gtk_hbutton_box_new();
	button = gtk_button_new_with_mnemonic(_("_Login"));
	gtk_box_pack_start(GTK_BOX(ret), bbox, FALSE, FALSE, 0);
	gtk_container_add(GTK_CONTAINER(bbox), button);


	label = gtk_label_new(NULL);
	gtk_box_pack_start(GTK_BOX(ret), label, TRUE, TRUE, 0);

	gtk_container_set_border_width(GTK_CONTAINER(ret), GAIM_HIG_BORDER);

	gtk_widget_show_all(ret);
	return ret;
}
#endif

static void pidgin_blist_show(GaimBuddyList *list)
{
	void *handle;
	GtkCellRenderer *rend;
	GtkTreeViewColumn *column;
	GtkWidget *menu;
	GtkWidget *ebox;
	GtkWidget *sw;
	GtkWidget *sep;
	GtkWidget *label;
	GList *accounts;
	char *pretty;
	GtkAccelGroup *accel_group;
	GtkTreeSelection *selection;
	GtkTargetEntry dte[] = {{"GAIM_BLIST_NODE", GTK_TARGET_SAME_APP, DRAG_ROW},
				{"application/x-im-contact", 0, DRAG_BUDDY},
				{"text/x-vcard", 0, DRAG_VCARD },
				{"text/uri-list", 0, DRAG_URI},
				{"text/plain", 0, DRAG_TEXT}};
	GtkTargetEntry ste[] = {{"GAIM_BLIST_NODE", GTK_TARGET_SAME_APP, DRAG_ROW},
				{"application/x-im-contact", 0, DRAG_BUDDY},
				{"text/x-vcard", 0, DRAG_VCARD }};
	if (gtkblist && gtkblist->window) {
		gaim_blist_set_visible(gaim_prefs_get_bool("/gaim/gtk/blist/list_visible"));
		return;
	}

	gtkblist = PIDGIN_BLIST(list);

	gtkblist->empty_avatar = gdk_pixbuf_new(GDK_COLORSPACE_RGB, TRUE, 8, 32, 32);
	gdk_pixbuf_fill(gtkblist->empty_avatar, 0x00000000);

	gtkblist->window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
	gtk_window_set_role(GTK_WINDOW(gtkblist->window), "buddy_list");
	gtk_window_set_title(GTK_WINDOW(gtkblist->window), _("Buddy List"));
	g_signal_connect(G_OBJECT(gtkblist->window), "focus-in-event",
			 G_CALLBACK(blist_focus_cb), gtkblist);
	GTK_WINDOW(gtkblist->window)->allow_shrink = TRUE;

	gtkblist->main_vbox = gtk_vbox_new(FALSE, 0);
	gtk_widget_show(gtkblist->main_vbox);
	gtk_container_add(GTK_CONTAINER(gtkblist->window), gtkblist->main_vbox);

	g_signal_connect(G_OBJECT(gtkblist->window), "delete_event", G_CALLBACK(gtk_blist_delete_cb), NULL);
	g_signal_connect(G_OBJECT(gtkblist->window), "configure_event", G_CALLBACK(gtk_blist_configure_cb), NULL);
	g_signal_connect(G_OBJECT(gtkblist->window), "visibility_notify_event", G_CALLBACK(gtk_blist_visibility_cb), NULL);
	g_signal_connect(G_OBJECT(gtkblist->window), "window_state_event", G_CALLBACK(gtk_blist_window_state_cb), NULL);
	g_signal_connect(G_OBJECT(gtkblist->window), "key_press_event", G_CALLBACK(gtk_blist_window_key_press_cb), gtkblist);
	gtk_widget_add_events(gtkblist->window, GDK_VISIBILITY_NOTIFY_MASK);

	/******************************* Menu bar *************************************/
	accel_group = gtk_accel_group_new();
	gtk_window_add_accel_group(GTK_WINDOW (gtkblist->window), accel_group);
	g_object_unref(accel_group);
	gtkblist->ift = gtk_item_factory_new(GTK_TYPE_MENU_BAR, "<GaimMain>", accel_group);
	gtk_item_factory_set_translate_func(gtkblist->ift,
										(GtkTranslateFunc)item_factory_translate_func,
										NULL, NULL);
	gtk_item_factory_create_items(gtkblist->ift, sizeof(blist_menu) / sizeof(*blist_menu),
								  blist_menu, NULL);
	pidgin_load_accels();
	g_signal_connect(G_OBJECT(accel_group), "accel-changed",
														G_CALLBACK(pidgin_save_accels_cb), NULL);
	menu = gtk_item_factory_get_widget(gtkblist->ift, "<GaimMain>");
	gtkblist->menutray = pidgin_menu_tray_new();
	gtk_menu_shell_append(GTK_MENU_SHELL(menu), gtkblist->menutray);
	gtk_widget_show(gtkblist->menutray);
	gtk_widget_show(menu);
	gtk_box_pack_start(GTK_BOX(gtkblist->main_vbox), menu, FALSE, FALSE, 0);

	accountmenu = gtk_item_factory_get_widget(gtkblist->ift, N_("/Accounts"));


	/****************************** Notebook *************************************/
	gtkblist->notebook = gtk_notebook_new();
	gtk_notebook_set_show_tabs(GTK_NOTEBOOK(gtkblist->notebook), FALSE);
	gtk_notebook_set_show_border(GTK_NOTEBOOK(gtkblist->notebook), FALSE);
	gtk_box_pack_start(GTK_BOX(gtkblist->main_vbox), gtkblist->notebook, TRUE, TRUE, 0);

#if 0
	gtk_notebook_append_page(GTK_NOTEBOOK(gtkblist->notebook), kiosk_page(), NULL);
#endif

	/* Translators: Please maintain the use of -> and <- to refer to menu heirarchy */
	pretty = pidgin_make_pretty_arrows(_("<span weight='bold' size='larger'>Welcome to " PIDGIN_NAME "!</span>\n\n"

					       "You have no accounts enabled. Enable your IM accounts from the "
					       "<b>Accounts</b> window at <b>Accounts->Add/Edit</b>. Once you "
					       "enable accounts, you'll be able to sign on, set your status, "
					       "and talk to your friends."));
	label = gtk_label_new(NULL);
	gtk_widget_set_size_request(label, gaim_prefs_get_int("/gaim/gtk/blist/width") - 12, -1);
	gtk_label_set_line_wrap(GTK_LABEL(label), TRUE);
	gtk_misc_set_alignment(GTK_MISC(label), 0.5, 0.2);
	gtk_label_set_markup(GTK_LABEL(label), pretty);
	g_free(pretty);
	gtk_notebook_append_page(GTK_NOTEBOOK(gtkblist->notebook),label, NULL);
	gtkblist->vbox = gtk_vbox_new(FALSE, 0);
	gtk_notebook_append_page(GTK_NOTEBOOK(gtkblist->notebook), gtkblist->vbox, NULL);
	gtk_widget_show_all(gtkblist->notebook);
	if ((accounts = gaim_accounts_get_all_active())) {
		g_list_free(accounts);
		gtk_notebook_set_current_page(GTK_NOTEBOOK(gtkblist->notebook), 1);
	}

	ebox = gtk_event_box_new();
	gtk_box_pack_start(GTK_BOX(gtkblist->vbox), ebox, FALSE, FALSE, 0);
	gtkblist->headline_hbox = gtk_hbox_new(FALSE, 3);
	gtk_container_set_border_width(GTK_CONTAINER(gtkblist->headline_hbox), 6);
	gtk_container_add(GTK_CONTAINER(ebox), gtkblist->headline_hbox);
	gtkblist->headline_image = gtk_image_new_from_pixbuf(NULL);
	gtk_misc_set_alignment(GTK_MISC(gtkblist->headline_image), 0.0, 0);
	gtkblist->headline_label = gtk_label_new(NULL);
	gtk_widget_set_size_request(gtkblist->headline_label,
				    gaim_prefs_get_int("/gaim/gtk/blist/width")-25,-1);
	gtk_label_set_line_wrap(GTK_LABEL(gtkblist->headline_label), TRUE);
	gtk_box_pack_start(GTK_BOX(gtkblist->headline_hbox), gtkblist->headline_image, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(gtkblist->headline_hbox), gtkblist->headline_label, TRUE, TRUE, 0);
	g_signal_connect(gtkblist->headline_hbox,
			 "style-set",
			 G_CALLBACK(headline_style_set),
			 NULL);
	g_signal_connect (gtkblist->headline_hbox,
			  "expose_event",
			  G_CALLBACK (paint_headline_hbox),
			  NULL);
	gtk_widget_set_name(gtkblist->headline_hbox, "gtk-tooltips");

	gtkblist->headline_close = gtk_widget_render_icon(ebox, GTK_STOCK_CLOSE, -1, NULL);
	if (gtkblist->headline_close) {
		GdkPixbuf *scale = gdk_pixbuf_scale_simple(gtkblist->headline_close,
				HEADLINE_CLOSE_SIZE, HEADLINE_CLOSE_SIZE, GDK_INTERP_BILINEAR);
		gdk_pixbuf_unref(gtkblist->headline_close);
		gtkblist->headline_close = scale;
	}

	gtkblist->hand_cursor = gdk_cursor_new (GDK_HAND2);
	gtkblist->arrow_cursor = gdk_cursor_new (GDK_LEFT_PTR);

	g_signal_connect(G_OBJECT(ebox), "enter-notify-event", G_CALLBACK(headline_box_enter_cb), gtkblist);
	g_signal_connect(G_OBJECT(ebox), "leave-notify-event", G_CALLBACK(headline_box_leave_cb), gtkblist);
	g_signal_connect(G_OBJECT(ebox), "button-press-event", G_CALLBACK(headline_box_press_cb), gtkblist);
#if 0
	/* I couldn't get this to work. The idea was to draw the focus-border only
	 * when hovering over the close image. So for now, the focus-border is
	 * always there. -- sad */
	gtk_widget_set_events(ebox, gtk_widget_get_events(ebox) | GDK_POINTER_MOTION_HINT_MASK);
	g_signal_connect(G_OBJECT(ebox), "motion-notify-event", G_CALLBACK(headline_box_motion_cb), gtkblist);
#endif

	/****************************** GtkTreeView **********************************/
	sw = gtk_scrolled_window_new(NULL,NULL);
	gtk_widget_show(sw);
	gtk_scrolled_window_set_shadow_type (GTK_SCROLLED_WINDOW(sw), GTK_SHADOW_NONE);
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(sw), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);

	gtkblist->treemodel = gtk_tree_store_new(BLIST_COLUMNS,
						 GDK_TYPE_PIXBUF, /* Status icon */
						 G_TYPE_BOOLEAN,  /* Status icon visible */
						 G_TYPE_STRING,   /* Name */
						 G_TYPE_STRING,   /* Idle */
						 G_TYPE_BOOLEAN,  /* Idle visible */
						 GDK_TYPE_PIXBUF, /* Buddy icon */
						 G_TYPE_BOOLEAN,  /* Buddy icon visible */
						 G_TYPE_POINTER,  /* Node */
						 GDK_TYPE_COLOR,  /* bgcolor */
						 G_TYPE_BOOLEAN,  /* Group expander */
						 G_TYPE_BOOLEAN,  /* Contact expander */
						 G_TYPE_BOOLEAN,  /* Contact expander visible */
						 GDK_TYPE_PIXBUF, /* Emblem */
						 G_TYPE_BOOLEAN); /* Emblem visible */

	gtkblist->treeview = gtk_tree_view_new_with_model(GTK_TREE_MODEL(gtkblist->treemodel));

	gtk_widget_show(gtkblist->treeview);
	gtk_widget_set_name(gtkblist->treeview, "pidginblist_treeview");

	g_signal_connect(gtkblist->treeview,
			 "style-set",
			 G_CALLBACK(treeview_style_set), list);
	/* Set up selection stuff */
	selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(gtkblist->treeview));
	g_signal_connect(G_OBJECT(selection), "changed", G_CALLBACK(pidgin_blist_selection_changed), NULL);

	/* Set up dnd */
	gtk_tree_view_enable_model_drag_source(GTK_TREE_VIEW(gtkblist->treeview),
										   GDK_BUTTON1_MASK, ste, 3,
										   GDK_ACTION_COPY);
	gtk_tree_view_enable_model_drag_dest(GTK_TREE_VIEW(gtkblist->treeview),
										 dte, 5,
										 GDK_ACTION_COPY | GDK_ACTION_MOVE);

	g_signal_connect(G_OBJECT(gtkblist->treeview), "drag-data-received", G_CALLBACK(pidgin_blist_drag_data_rcv_cb), NULL);
	g_signal_connect(G_OBJECT(gtkblist->treeview), "drag-data-get", G_CALLBACK(pidgin_blist_drag_data_get_cb), NULL);
#ifdef _WIN32
	g_signal_connect(G_OBJECT(gtkblist->treeview), "drag-begin", G_CALLBACK(pidgin_blist_drag_begin), NULL);
#endif

	g_signal_connect(G_OBJECT(gtkblist->treeview), "drag-motion", G_CALLBACK(pidgin_blist_drag_motion_cb), NULL);

	/* Tooltips */
	g_signal_connect(G_OBJECT(gtkblist->treeview), "motion-notify-event", G_CALLBACK(pidgin_blist_motion_cb), NULL);
	g_signal_connect(G_OBJECT(gtkblist->treeview), "leave-notify-event", G_CALLBACK(pidgin_blist_leave_cb), NULL);

	gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(gtkblist->treeview), FALSE);

	column = gtk_tree_view_column_new();
	gtk_tree_view_append_column(GTK_TREE_VIEW(gtkblist->treeview), column);
	gtk_tree_view_column_set_visible(column, FALSE);
	gtk_tree_view_set_expander_column(GTK_TREE_VIEW(gtkblist->treeview), column);

	gtkblist->text_column = column = gtk_tree_view_column_new ();
	rend = pidgin_cell_renderer_expander_new();
	gtk_tree_view_column_pack_start(column, rend, FALSE);
	gtk_tree_view_column_set_attributes(column, rend,
					    "expander-visible", GROUP_EXPANDER_COLUMN,
#if GTK_CHECK_VERSION(2,6,0)
					    "sensitive", GROUP_EXPANDER_COLUMN,
					    "cell-background-gdk", BGCOLOR_COLUMN,
#endif
					    NULL);

	rend = pidgin_cell_renderer_expander_new();
	gtk_tree_view_column_pack_start(column, rend, FALSE);
	gtk_tree_view_column_set_attributes(column, rend,
					    "expander-visible", CONTACT_EXPANDER_COLUMN,
#if GTK_CHECK_VERSION(2,6,0)
					    "sensitive", CONTACT_EXPANDER_COLUMN,
					    "cell-background-gdk", BGCOLOR_COLUMN,
#endif
					    "visible", CONTACT_EXPANDER_VISIBLE_COLUMN,
					    NULL);

	rend = gtk_cell_renderer_pixbuf_new();
	gtk_tree_view_column_pack_start(column, rend, FALSE);
	gtk_tree_view_column_set_attributes(column, rend,
					    "pixbuf", STATUS_ICON_COLUMN,
					    "visible", STATUS_ICON_VISIBLE_COLUMN,
#if GTK_CHECK_VERSION(2,6,0)
					    "cell-background-gdk", BGCOLOR_COLUMN,
#endif
					    NULL);
	g_object_set(rend, "xalign", 0.0, "xpad", 3, "ypad", 0, NULL);

	gtkblist->text_rend = rend = gtk_cell_renderer_text_new();
	gtk_tree_view_column_pack_start (column, rend, TRUE);
	gtk_tree_view_column_set_attributes(column, rend,
#if GTK_CHECK_VERSION(2,6,0)
					    	    "cell-background-gdk", BGCOLOR_COLUMN,
#endif
										"markup", NAME_COLUMN,
										NULL);
	g_signal_connect(G_OBJECT(rend), "edited", G_CALLBACK(gtk_blist_renderer_edited_cb), NULL);
	g_object_set(rend, "ypad", 0, "yalign", 0.5, NULL);
#if GTK_CHECK_VERSION(2,6,0)
	g_object_set(rend, "ellipsize", PANGO_ELLIPSIZE_END, NULL);
#endif
	gtk_tree_view_append_column(GTK_TREE_VIEW(gtkblist->treeview), column);

	rend = gtk_cell_renderer_text_new();
	g_object_set(rend, "xalign", 1.0, "ypad", 0, NULL);
	gtk_tree_view_column_pack_start(column, rend, FALSE);
	gtk_tree_view_column_set_attributes(column, rend,
					    "markup", IDLE_COLUMN,
					    "visible", IDLE_VISIBLE_COLUMN,
#if GTK_CHECK_VERSION(2,6,0)
					    "cell-background-gdk", BGCOLOR_COLUMN,
#endif
					    NULL);

	rend = gtk_cell_renderer_pixbuf_new();
	g_object_set(rend, "xalign", 1.0, "yalign", 0.5, "ypad", 0, NULL);
	gtk_tree_view_column_pack_start(column, rend, FALSE);
	gtk_tree_view_column_set_attributes(column, rend, "pixbuf", EMBLEM_COLUMN,
#if GTK_CHECK_VERSION(2,6,0)
							  "cell-background-gdk", BGCOLOR_COLUMN,
#endif
							  "visible", EMBLEM_VISIBLE_COLUMN, NULL);

	rend = gtk_cell_renderer_pixbuf_new();
	g_object_set(rend, "xalign", 1.0, "ypad", 0, NULL);
	gtk_tree_view_column_pack_start(column, rend, FALSE);
	gtk_tree_view_column_set_attributes(column, rend, "pixbuf", BUDDY_ICON_COLUMN,
#if GTK_CHECK_VERSION(2,6,0)
					    "cell-background-gdk", BGCOLOR_COLUMN,
#endif
					    "visible", BUDDY_ICON_VISIBLE_COLUMN,
					    NULL);


	g_signal_connect(G_OBJECT(gtkblist->treeview), "row-activated", G_CALLBACK(gtk_blist_row_activated_cb), NULL);
	g_signal_connect(G_OBJECT(gtkblist->treeview), "row-expanded", G_CALLBACK(gtk_blist_row_expanded_cb), NULL);
	g_signal_connect(G_OBJECT(gtkblist->treeview), "row-collapsed", G_CALLBACK(gtk_blist_row_collapsed_cb), NULL);
	g_signal_connect(G_OBJECT(gtkblist->treeview), "button-press-event", G_CALLBACK(gtk_blist_button_press_cb), NULL);
	g_signal_connect(G_OBJECT(gtkblist->treeview), "key-press-event", G_CALLBACK(gtk_blist_key_press_cb), NULL);
	g_signal_connect(G_OBJECT(gtkblist->treeview), "popup-menu", G_CALLBACK(pidgin_blist_popup_menu_cb), NULL);

	/* Enable CTRL+F searching */
	gtk_tree_view_set_search_column(GTK_TREE_VIEW(gtkblist->treeview), NAME_COLUMN);
	gtk_tree_view_set_search_equal_func(GTK_TREE_VIEW(gtkblist->treeview), pidgin_tree_view_search_equal_func, NULL, NULL);

	gtk_box_pack_start(GTK_BOX(gtkblist->vbox), sw, TRUE, TRUE, 0);
	gtk_container_add(GTK_CONTAINER(sw), gtkblist->treeview);

	sep = gtk_hseparator_new();
	gtk_box_pack_start(GTK_BOX(gtkblist->vbox), sep, FALSE, FALSE, 0);

	gtkblist->scrollbook = pidgin_scroll_book_new();
	gtk_box_pack_start(GTK_BOX(gtkblist->vbox), gtkblist->scrollbook, FALSE, FALSE, 0);

	/* Create an empty vbox used for showing connection errors */
	gtkblist->error_buttons = gtk_vbox_new(FALSE, 0);
	gtk_box_pack_start(GTK_BOX(gtkblist->vbox), gtkblist->error_buttons, FALSE, FALSE, 0);

	/* Add the statusbox */
	gtkblist->statusbox = pidgin_status_box_new();
	gtk_box_pack_start(GTK_BOX(gtkblist->vbox), gtkblist->statusbox, FALSE, TRUE, 0);
	gtk_widget_set_name(gtkblist->statusbox, "pidginblist_statusbox");
	gtk_container_set_border_width(GTK_CONTAINER(gtkblist->statusbox), 3);
	gtk_widget_show(gtkblist->statusbox);

	/* set the Show Offline Buddies option. must be done
	 * after the treeview or faceprint gets mad. -Robot101
	 */
	gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(gtk_item_factory_get_item (gtkblist->ift, N_("/Buddies/Show Offline Buddies"))),
			gaim_prefs_get_bool("/gaim/gtk/blist/show_offline_buddies"));

	gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(gtk_item_factory_get_item (gtkblist->ift, N_("/Buddies/Show Empty Groups"))),
			gaim_prefs_get_bool("/gaim/gtk/blist/show_empty_groups"));

	gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(gtk_item_factory_get_item (gtkblist->ift, N_("/Tools/Mute Sounds"))),
			gaim_prefs_get_bool("/gaim/gtk/sound/mute"));

	gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(gtk_item_factory_get_item (gtkblist->ift, N_("/Buddies/Show Buddy Details"))),
			gaim_prefs_get_bool("/gaim/gtk/blist/show_buddy_icons"));

	gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(gtk_item_factory_get_item (gtkblist->ift, N_("/Buddies/Show Idle Times"))),
			gaim_prefs_get_bool("/gaim/gtk/blist/show_idle_time"));

	if(!strcmp(gaim_prefs_get_string("/gaim/gtk/sound/method"), "none"))
		gtk_widget_set_sensitive(gtk_item_factory_get_widget(gtkblist->ift, N_("/Tools/Mute Sounds")), FALSE);

	/* Update some dynamic things */
	update_menu_bar(gtkblist);
	pidgin_blist_update_plugin_actions();
	pidgin_blist_update_sort_methods();

	/* OK... let's show this bad boy. */
	pidgin_blist_refresh(list);
	pidgin_blist_restore_position();
	gtk_widget_show_all(GTK_WIDGET(gtkblist->vbox));
	gtk_widget_realize(GTK_WIDGET(gtkblist->window));
	gaim_blist_set_visible(gaim_prefs_get_bool("/gaim/gtk/blist/list_visible"));

	/* start the refresh timer */
	gtkblist->refresh_timer = g_timeout_add(30000, (GSourceFunc)pidgin_blist_refresh_timer, list);

	handle = pidgin_blist_get_handle();

	/* things that affect how buddies are displayed */
	gaim_prefs_connect_callback(handle, "/gaim/gtk/blist/show_buddy_icons",
			_prefs_change_redo_list, NULL);
	gaim_prefs_connect_callback(handle, "/gaim/gtk/blist/show_idle_time",
			_prefs_change_redo_list, NULL);
	gaim_prefs_connect_callback(handle, "/gaim/gtk/blist/show_empty_groups",
			_prefs_change_redo_list, NULL);
	gaim_prefs_connect_callback(handle, "/gaim/gtk/blist/show_offline_buddies",
			_prefs_change_redo_list, NULL);

	/* sorting */
	gaim_prefs_connect_callback(handle, "/gaim/gtk/blist/sort_type",
			_prefs_change_sort_method, NULL);

	/* menus */
	gaim_prefs_connect_callback(handle, "/gaim/gtk/sound/mute",
			pidgin_blist_mute_pref_cb, NULL);
	gaim_prefs_connect_callback(handle, "/gaim/gtk/sound/method",
			pidgin_blist_sound_method_pref_cb, NULL);

	/* Setup some gaim signal handlers. */
	gaim_signal_connect(gaim_accounts_get_handle(), "account-enabled",
			gtkblist, GAIM_CALLBACK(account_modified), gtkblist);
	gaim_signal_connect(gaim_accounts_get_handle(), "account-disabled",
			gtkblist, GAIM_CALLBACK(account_modified), gtkblist);
	gaim_signal_connect(gaim_accounts_get_handle(), "account-removed",
			gtkblist, GAIM_CALLBACK(account_modified), gtkblist);
	gaim_signal_connect(gaim_accounts_get_handle(), "account-status-changed",
			gtkblist, GAIM_CALLBACK(account_status_changed), gtkblist);

	gaim_signal_connect(pidgin_account_get_handle(), "account-modified",
			gtkblist, GAIM_CALLBACK(account_modified), gtkblist);

	gaim_signal_connect(gaim_connections_get_handle(), "signed-on",
						gtkblist, GAIM_CALLBACK(sign_on_off_cb), list);
	gaim_signal_connect(gaim_connections_get_handle(), "signed-off",
						gtkblist, GAIM_CALLBACK(sign_on_off_cb), list);

	gaim_signal_connect(gaim_plugins_get_handle(), "plugin-load",
			gtkblist, GAIM_CALLBACK(plugin_changed_cb), NULL);
	gaim_signal_connect(gaim_plugins_get_handle(), "plugin-unload",
			gtkblist, GAIM_CALLBACK(plugin_changed_cb), NULL);

	gaim_signal_connect(gaim_conversations_get_handle(), "conversation-updated",
						gtkblist, GAIM_CALLBACK(conversation_updated_cb),
						gtkblist);
	gaim_signal_connect(gaim_conversations_get_handle(), "deleting-conversation",
						gtkblist, GAIM_CALLBACK(conversation_deleting_cb),
						gtkblist);

	gtk_widget_hide(gtkblist->headline_hbox);

	/* emit our created signal */
	gaim_signal_emit(handle, "gtkblist-created", list);
}

static void redo_buddy_list(GaimBuddyList *list, gboolean remove, gboolean rerender)
{
	GaimBlistNode *node;

	gtkblist = PIDGIN_BLIST(list);
	if(!gtkblist || !gtkblist->treeview)
		return;

	node = list->root;

	while (node)
	{
		/* This is only needed when we're reverting to a non-GTK+ sorted
		 * status.  We shouldn't need to remove otherwise.
		 */
		if (remove && !GAIM_BLIST_NODE_IS_GROUP(node))
			pidgin_blist_hide_node(list, node, FALSE);

		if (GAIM_BLIST_NODE_IS_BUDDY(node))
			pidgin_blist_update_buddy(list, node, rerender);
		else if (GAIM_BLIST_NODE_IS_CHAT(node))
			pidgin_blist_update(list, node);
		else if (GAIM_BLIST_NODE_IS_GROUP(node))
			pidgin_blist_update(list, node);
		node = gaim_blist_node_next(node, FALSE);
	}

}

void pidgin_blist_refresh(GaimBuddyList *list)
{
	redo_buddy_list(list, FALSE, TRUE);
}

void
pidgin_blist_update_refresh_timeout()
{
	GaimBuddyList *blist;
	PidginBuddyList *gtkblist;

	blist = gaim_get_blist();
	gtkblist = PIDGIN_BLIST(gaim_get_blist());

	gtkblist->refresh_timer = g_timeout_add(30000,(GSourceFunc)pidgin_blist_refresh_timer, blist);
}

static gboolean get_iter_from_node(GaimBlistNode *node, GtkTreeIter *iter) {
	struct _pidgin_blist_node *gtknode = (struct _pidgin_blist_node *)node->ui_data;
	GtkTreePath *path;

	if (!gtknode) {
		return FALSE;
	}

	if (!gtkblist) {
		gaim_debug_error("gtkblist", "get_iter_from_node was called, but we don't seem to have a blist\n");
		return FALSE;
	}

	if (!gtknode->row)
		return FALSE;


	if ((path = gtk_tree_row_reference_get_path(gtknode->row)) == NULL)
		return FALSE;

	if (!gtk_tree_model_get_iter(GTK_TREE_MODEL(gtkblist->treemodel), iter, path)) {
		gtk_tree_path_free(path);
		return FALSE;
	}
	gtk_tree_path_free(path);
	return TRUE;
}

static void pidgin_blist_remove(GaimBuddyList *list, GaimBlistNode *node)
{
	struct _pidgin_blist_node *gtknode = node->ui_data;

	gaim_request_close_with_handle(node);

	pidgin_blist_hide_node(list, node, TRUE);

	if(node->parent)
		pidgin_blist_update(list, node->parent);

	/* There's something I don't understand here - Ethan */
	/* Ethan said that back in 2003, but this g_free has been left commented
	 * out ever since. I can't find any reason at all why this is bad and
	 * valgrind found several reasons why it's good. If this causes problems
	 * comment it out again. Stu */
	/* Of course it still causes problems - this breaks dragging buddies into
	 * contacts, the dragged buddy mysteriously 'disappears'. Stu. */
	/* I think it's fixed now. Stu. */

	if(gtknode) {
		if(gtknode->recent_signonoff_timer > 0)
			gaim_timeout_remove(gtknode->recent_signonoff_timer);

		g_free(node->ui_data);
		node->ui_data = NULL;
	}
}

static gboolean do_selection_changed(GaimBlistNode *new_selection)
{
	GaimBlistNode *old_selection = NULL;

	/* test for gtkblist because crazy timeout means we can be called after the blist is gone */
	if (gtkblist && new_selection != gtkblist->selected_node) {
		old_selection = gtkblist->selected_node;
		gtkblist->selected_node = new_selection;
		if(new_selection)
			pidgin_blist_update(NULL, new_selection);
		if(old_selection)
			pidgin_blist_update(NULL, old_selection);
	}

	return FALSE;
}

static void pidgin_blist_selection_changed(GtkTreeSelection *selection, gpointer data)
{
	GaimBlistNode *new_selection = NULL;
	GtkTreeIter iter;

	if(gtk_tree_selection_get_selected(selection, NULL, &iter)){
		gtk_tree_model_get(GTK_TREE_MODEL(gtkblist->treemodel), &iter,
				NODE_COLUMN, &new_selection, -1);
	}

	/* we set this up as a timeout, otherwise the blist flickers */
	g_timeout_add(0, (GSourceFunc)do_selection_changed, new_selection);
}

static gboolean insert_node(GaimBuddyList *list, GaimBlistNode *node, GtkTreeIter *iter)
{
	GtkTreeIter parent_iter, cur, *curptr = NULL;
	struct _pidgin_blist_node *gtknode = node->ui_data;
	GtkTreePath *newpath;

	if(!iter)
		return FALSE;

	if(node->parent && !get_iter_from_node(node->parent, &parent_iter))
		return FALSE;

	if(get_iter_from_node(node, &cur))
		curptr = &cur;

	if(GAIM_BLIST_NODE_IS_CONTACT(node) || GAIM_BLIST_NODE_IS_CHAT(node)) {
		current_sort_method->func(node, list, parent_iter, curptr, iter);
	} else {
		sort_method_none(node, list, parent_iter, curptr, iter);
	}

	if(gtknode != NULL) {
		gtk_tree_row_reference_free(gtknode->row);
	} else {
		pidgin_blist_new_node(node);
		gtknode = (struct _pidgin_blist_node *)node->ui_data;
	}

	newpath = gtk_tree_model_get_path(GTK_TREE_MODEL(gtkblist->treemodel),
			iter);
	gtknode->row =
		gtk_tree_row_reference_new(GTK_TREE_MODEL(gtkblist->treemodel),
				newpath);

	gtk_tree_path_free(newpath);

	gtk_tree_store_set(gtkblist->treemodel, iter,
			NODE_COLUMN, node,
			-1);

	if(node->parent) {
		GtkTreePath *expand = NULL;
		struct _pidgin_blist_node *gtkparentnode = node->parent->ui_data;

		if(GAIM_BLIST_NODE_IS_GROUP(node->parent)) {
			if(!gaim_blist_node_get_bool(node->parent, "collapsed"))
				expand = gtk_tree_model_get_path(GTK_TREE_MODEL(gtkblist->treemodel), &parent_iter);
		} else if(GAIM_BLIST_NODE_IS_CONTACT(node->parent) &&
				gtkparentnode->contact_expanded) {
			expand = gtk_tree_model_get_path(GTK_TREE_MODEL(gtkblist->treemodel), &parent_iter);
		}
		if(expand) {
			gtk_tree_view_expand_row(GTK_TREE_VIEW(gtkblist->treeview), expand, FALSE);
			gtk_tree_path_free(expand);
		}
	}

	return TRUE;
}

/*This version of pidgin_blist_update_group can take the original buddy
or a group, but has much better algorithmic performance with a pre-known buddy*/
static void pidgin_blist_update_group(GaimBuddyList *list, GaimBlistNode *node)
{
	GaimGroup *group;
	int count;
	gboolean show = FALSE;
	GaimBlistNode* gnode;

	g_return_if_fail(node != NULL);

	if (GAIM_BLIST_NODE_IS_GROUP(node))
		gnode = node;
	else if (GAIM_BLIST_NODE_IS_BUDDY(node))
		gnode = node->parent->parent;
	else if (GAIM_BLIST_NODE_IS_CONTACT(node) || GAIM_BLIST_NODE_IS_CHAT(node))
		gnode = node->parent;
	else
		return;

	group = (GaimGroup*)gnode;

	if(gaim_prefs_get_bool("/gaim/gtk/blist/show_offline_buddies"))
		count = gaim_blist_get_group_size(group, FALSE);
	else
		count = gaim_blist_get_group_online_count(group);

	if (count > 0 || gaim_prefs_get_bool("/gaim/gtk/blist/show_empty_groups"))
		show = TRUE;
	else if (GAIM_BLIST_NODE_IS_BUDDY(node)){ /* Or chat? */
		if (buddy_is_displayable((GaimBuddy*)node))
			show = TRUE;}

	if (show) {
		GtkTreeIter iter;
		GtkTreePath *path;
		gboolean expanded;
		GdkColor bgcolor;
		char *title;


		if(!insert_node(list, gnode, &iter))
			return;

		bgcolor = gtkblist->treeview->style->bg[GTK_STATE_ACTIVE];

		path = gtk_tree_model_get_path(GTK_TREE_MODEL(gtkblist->treemodel), &iter);
		expanded = gtk_tree_view_row_expanded(GTK_TREE_VIEW(gtkblist->treeview), path);
		gtk_tree_path_free(path);

		title = gaim_get_group_title(gnode, expanded);

		gtk_tree_store_set(gtkblist->treemodel, &iter,
				   STATUS_ICON_VISIBLE_COLUMN, FALSE,
				   STATUS_ICON_COLUMN, NULL,
				   NAME_COLUMN, title,
				   NODE_COLUMN, gnode,
				   BGCOLOR_COLUMN, &bgcolor,
				   GROUP_EXPANDER_COLUMN, TRUE,
				   CONTACT_EXPANDER_VISIBLE_COLUMN, FALSE,
				   BUDDY_ICON_VISIBLE_COLUMN, FALSE,
				   IDLE_VISIBLE_COLUMN, FALSE,
				   EMBLEM_VISIBLE_COLUMN, FALSE,
				   -1);
		g_free(title);
	} else {
		pidgin_blist_hide_node(list, gnode, TRUE);
	}
}

static char *gaim_get_group_title(GaimBlistNode *gnode, gboolean expanded)
{
	GaimGroup *group;
	GdkColor textcolor;
	gboolean selected;
	char group_count[12] = "";
	char *mark, *esc;

	group = (GaimGroup*)gnode;
	textcolor = gtkblist->treeview->style->fg[GTK_STATE_ACTIVE];
	selected = gtkblist ? (gtkblist->selected_node == gnode) : FALSE;

	if (!expanded) {
		g_snprintf(group_count, sizeof(group_count), " (%d/%d)",
		           gaim_blist_get_group_online_count(group),
		           gaim_blist_get_group_size(group, FALSE));
	}

	esc = g_markup_escape_text(group->name, -1);
	if (selected)
		mark = g_strdup_printf("<span weight='bold'>%s</span>%s", esc, group_count);
	else
		mark = g_strdup_printf("<span color='#%02x%02x%02x' weight='bold'>%s</span>%s",
				       textcolor.red>>8, textcolor.green>>8, textcolor.blue>>8,
				       esc, group_count);

	g_free(esc);
	return mark;
}

static void buddy_node(GaimBuddy *buddy, GtkTreeIter *iter, GaimBlistNode *node)
{
	GaimPresence *presence;
	GdkPixbuf *status, *avatar, *emblem;
	char *mark;
	char *idle = NULL;
	gboolean expanded = ((struct _pidgin_blist_node *)(node->parent->ui_data))->contact_expanded;
	gboolean selected = (gtkblist->selected_node == node);
	gboolean biglist = gaim_prefs_get_bool("/gaim/gtk/blist/show_buddy_icons");
	presence = gaim_buddy_get_presence(buddy);

	status = pidgin_blist_get_status_icon((GaimBlistNode*)buddy,
						PIDGIN_STATUS_ICON_SMALL);

	avatar = pidgin_blist_get_buddy_icon((GaimBlistNode *)buddy, TRUE, TRUE, TRUE);
	if (!avatar) {
		g_object_ref(G_OBJECT(gtkblist->empty_avatar));
		avatar = gtkblist->empty_avatar;
	} else if ((!GAIM_BUDDY_IS_ONLINE(buddy) || gaim_presence_is_idle(presence))) {
		do_alphashift(avatar, avatar, 77);
	}

	emblem = pidgin_blist_get_emblem((GaimBlistNode*) buddy);
	mark = pidgin_blist_get_name_markup(buddy, selected);

	if (gaim_prefs_get_bool("/gaim/gtk/blist/show_idle_time") &&
		gaim_presence_is_idle(presence) &&
		!gaim_prefs_get_bool("/gaim/gtk/blist/show_buddy_icons"))
	{
		time_t idle_secs = gaim_presence_get_idle_time(presence);

		if (idle_secs > 0)
		{
			time_t t;
			int ihrs, imin;
			time(&t);
			ihrs = (t - idle_secs) / 3600;
			imin = ((t - idle_secs) / 60) % 60;
			idle = g_strdup_printf("%d:%02d", ihrs, imin);
		}
	}

	if (gaim_presence_is_idle(presence))
	{
		if (idle && !selected) {
			char *i2 = g_strdup_printf("<span color='%s'>%s</span>",
						   dim_grey(), idle);
			g_free(idle);
			idle = i2;
		}
	}

	gtk_tree_store_set(gtkblist->treemodel, iter,
			   STATUS_ICON_COLUMN, status,
			   STATUS_ICON_VISIBLE_COLUMN, TRUE,
			   NAME_COLUMN, mark,
			   IDLE_COLUMN, idle,
			   IDLE_VISIBLE_COLUMN, !biglist && idle,
			   BUDDY_ICON_COLUMN, avatar,
			   BUDDY_ICON_VISIBLE_COLUMN, biglist,
			   EMBLEM_COLUMN, emblem,
			   EMBLEM_VISIBLE_COLUMN, emblem,
			   BGCOLOR_COLUMN, NULL,
			   CONTACT_EXPANDER_COLUMN, NULL,
			   CONTACT_EXPANDER_VISIBLE_COLUMN, expanded,
			-1);

	g_free(mark);
	g_free(idle);
	if(status)
		g_object_unref(status);
	if(avatar)
		g_object_unref(avatar);
}

/* This is a variation on the original gtk_blist_update_contact. Here we
	can know in advance which buddy has changed so we can just update that */
static void pidgin_blist_update_contact(GaimBuddyList *list, GaimBlistNode *node)
{
	GaimBlistNode *cnode;
	GaimContact *contact;
	GaimBuddy *buddy;
	struct _pidgin_blist_node *gtknode;

	if (GAIM_BLIST_NODE_IS_BUDDY(node))
		cnode = node->parent;
	else
		cnode = node;

	g_return_if_fail(GAIM_BLIST_NODE_IS_CONTACT(cnode));

	/* First things first, update the group */
	if (GAIM_BLIST_NODE_IS_BUDDY(node))
		pidgin_blist_update_group(list, node);
	else
		pidgin_blist_update_group(list, cnode->parent);

	contact = (GaimContact*)cnode;
	buddy = gaim_contact_get_priority_buddy(contact);

	if (buddy_is_displayable(buddy))
	{
		GtkTreeIter iter;

		if(!insert_node(list, cnode, &iter))
			return;

		gtknode = (struct _pidgin_blist_node *)cnode->ui_data;

		if(gtknode->contact_expanded) {
			GdkPixbuf *status;
			char *mark;

			status = pidgin_blist_get_status_icon(cnode,
					 PIDGIN_STATUS_ICON_SMALL);

			mark = g_markup_escape_text(gaim_contact_get_alias(contact), -1);
			gtk_tree_store_set(gtkblist->treemodel, &iter,
					   STATUS_ICON_COLUMN, status,
					   STATUS_ICON_VISIBLE_COLUMN, TRUE,
					   NAME_COLUMN, mark,
					   IDLE_COLUMN, NULL,
					   IDLE_VISIBLE_COLUMN, FALSE,
					   BGCOLOR_COLUMN, NULL,
					   BUDDY_ICON_COLUMN, NULL,
					   CONTACT_EXPANDER_COLUMN, TRUE,
					   CONTACT_EXPANDER_VISIBLE_COLUMN, TRUE,
					-1);
			g_free(mark);
			if(status)
				g_object_unref(status);
		} else {
			buddy_node(buddy, &iter, cnode);
		}
	} else {
		pidgin_blist_hide_node(list, cnode, TRUE);
	}
}



static void pidgin_blist_update_buddy(GaimBuddyList *list, GaimBlistNode *node, gboolean statusChange)
{
	GaimBuddy *buddy;
	struct _pidgin_blist_node *gtkparentnode;

	g_return_if_fail(GAIM_BLIST_NODE_IS_BUDDY(node));

	if (node->parent == NULL)
		return;

	buddy = (GaimBuddy*)node;

	/* First things first, update the contact */
	pidgin_blist_update_contact(list, node);

	gtkparentnode = (struct _pidgin_blist_node *)node->parent->ui_data;

	if (gtkparentnode->contact_expanded && buddy_is_displayable(buddy))
	{
		GtkTreeIter iter;

		if (!insert_node(list, node, &iter))
			return;

		buddy_node(buddy, &iter, node);

	} else {
		pidgin_blist_hide_node(list, node, TRUE);
	}

}

static void pidgin_blist_update_chat(GaimBuddyList *list, GaimBlistNode *node)
{
	GaimChat *chat;

	g_return_if_fail(GAIM_BLIST_NODE_IS_CHAT(node));

	/* First things first, update the group */
	pidgin_blist_update_group(list, node->parent);

	chat = (GaimChat*)node;

	if(gaim_account_is_connected(chat->account)) {
		GtkTreeIter iter;
		GdkPixbuf *status;
		GdkPixbuf *avatar;
		GdkPixbuf *emblem;
		char *mark;

		if(!insert_node(list, node, &iter))
			return;

		status = pidgin_blist_get_status_icon(node,
				 PIDGIN_STATUS_ICON_SMALL);
		emblem = pidgin_blist_get_emblem(node);
		avatar = pidgin_blist_get_buddy_icon(node, TRUE, FALSE, TRUE);

		mark = g_markup_escape_text(gaim_chat_get_name(chat), -1);

		gtk_tree_store_set(gtkblist->treemodel, &iter,
				STATUS_ICON_COLUMN, status,
				STATUS_ICON_VISIBLE_COLUMN, TRUE,
				BUDDY_ICON_COLUMN, avatar ? avatar : gtkblist->empty_avatar,
				BUDDY_ICON_VISIBLE_COLUMN,  gaim_prefs_get_bool("/gaim/gtk/blist/show_buddy_icons"),
			        EMBLEM_COLUMN, emblem,
				EMBLEM_VISIBLE_COLUMN, emblem != NULL,
				NAME_COLUMN, mark,
				-1);

		g_free(mark);
		if(status)
			g_object_unref(status);
		if(avatar)
			g_object_unref(avatar);
	} else {
		pidgin_blist_hide_node(list, node, TRUE);
	}
}

static void pidgin_blist_update(GaimBuddyList *list, GaimBlistNode *node)
{
	if (list)
		gtkblist = PIDGIN_BLIST(list);
	if(!gtkblist || !gtkblist->treeview || !node)
		return;

	if (node->ui_data == NULL)
		pidgin_blist_new_node(node);

	switch(node->type) {
		case GAIM_BLIST_GROUP_NODE:
			pidgin_blist_update_group(list, node);
			break;
		case GAIM_BLIST_CONTACT_NODE:
			pidgin_blist_update_contact(list, node);
			break;
		case GAIM_BLIST_BUDDY_NODE:
			pidgin_blist_update_buddy(list, node, TRUE);
			break;
		case GAIM_BLIST_CHAT_NODE:
			pidgin_blist_update_chat(list, node);
			break;
		case GAIM_BLIST_OTHER_NODE:
			return;
	}

#if !GTK_CHECK_VERSION(2,6,0)
	gtk_tree_view_columns_autosize(GTK_TREE_VIEW(gtkblist->treeview));
#endif
}


static void pidgin_blist_destroy(GaimBuddyList *list)
{
	if (!gtkblist)
		return;

	gaim_signals_disconnect_by_handle(gtkblist);

	if (gtkblist->headline_close)
		gdk_pixbuf_unref(gtkblist->headline_close);

	gtk_widget_destroy(gtkblist->window);

	pidgin_blist_tooltip_destroy();

	if (gtkblist->refresh_timer)
		g_source_remove(gtkblist->refresh_timer);
	if (gtkblist->timeout)
		g_source_remove(gtkblist->timeout);
	if (gtkblist->drag_timeout)
		g_source_remove(gtkblist->drag_timeout);

	g_hash_table_destroy(gtkblist->connection_errors);
	gtkblist->refresh_timer = 0;
	gtkblist->timeout = 0;
	gtkblist->drag_timeout = 0;
	gtkblist->window = gtkblist->vbox = gtkblist->treeview = NULL;
	gtkblist->treemodel = NULL;
	g_object_unref(G_OBJECT(gtkblist->ift));
	g_object_unref(G_OBJECT(gtkblist->empty_avatar));

	gdk_cursor_unref(gtkblist->hand_cursor);
	gdk_cursor_unref(gtkblist->arrow_cursor);
	gtkblist->hand_cursor = NULL;
	gtkblist->arrow_cursor = NULL;

	g_free(gtkblist);
	accountmenu = NULL;
	gtkblist = NULL;
	gaim_prefs_disconnect_by_handle(pidgin_blist_get_handle());
}

static void pidgin_blist_set_visible(GaimBuddyList *list, gboolean show)
{
	if (!(gtkblist && gtkblist->window))
		return;

	if (show) {
		if(!PIDGIN_WINDOW_ICONIFIED(gtkblist->window) && !GTK_WIDGET_VISIBLE(gtkblist->window))
			gaim_signal_emit(pidgin_blist_get_handle(), "gtkblist-unhiding", gtkblist);
		pidgin_blist_restore_position();
		gtk_window_present(GTK_WINDOW(gtkblist->window));
	} else {
		if(visibility_manager_count) {
			gaim_signal_emit(pidgin_blist_get_handle(), "gtkblist-hiding", gtkblist);
			gtk_widget_hide(gtkblist->window);
		} else {
			if (!GTK_WIDGET_VISIBLE(gtkblist->window))
				gtk_widget_show(gtkblist->window);
			gtk_window_iconify(GTK_WINDOW(gtkblist->window));
		}
	}
}

static GList *
groups_tree(void)
{
	GList *tmp = NULL;
	char *tmp2;
	GaimGroup *g;
	GaimBlistNode *gnode;

	if (gaim_get_blist()->root == NULL)
	{
		tmp2 = g_strdup(_("Buddies"));
		tmp  = g_list_append(tmp, tmp2);
	}
	else
	{
		for (gnode = gaim_get_blist()->root;
			 gnode != NULL;
			 gnode = gnode->next)
		{
			if (GAIM_BLIST_NODE_IS_GROUP(gnode))
			{
				g    = (GaimGroup *)gnode;
				tmp2 = g->name;
				tmp  = g_list_append(tmp, tmp2);
			}
		}
	}

	return tmp;
}

static void
add_buddy_select_account_cb(GObject *w, GaimAccount *account,
							PidginAddBuddyData *data)
{
	/* Save our account */
	data->account = account;
}

static void
destroy_add_buddy_dialog_cb(GtkWidget *win, PidginAddBuddyData *data)
{
	g_free(data);
}

static void
add_buddy_cb(GtkWidget *w, int resp, PidginAddBuddyData *data)
{
	const char *grp, *who, *whoalias;
	GaimGroup *g;
	GaimBuddy *b;
	GaimConversation *c;
	GaimBuddyIcon *icon;

	if (resp == GTK_RESPONSE_OK)
	{
		who = gtk_entry_get_text(GTK_ENTRY(data->entry));
		grp = gtk_entry_get_text(GTK_ENTRY(GTK_COMBO(data->combo)->entry));
		whoalias = gtk_entry_get_text(GTK_ENTRY(data->entry_for_alias));
		if (*whoalias == '\0')
			whoalias = NULL;

		if ((g = gaim_find_group(grp)) == NULL)
		{
			g = gaim_group_new(grp);
			gaim_blist_add_group(g, NULL);
		}

		b = gaim_buddy_new(data->account, who, whoalias);
		gaim_blist_add_buddy(b, NULL, g, NULL);
		gaim_account_add_buddy(data->account, b);

		/*
		 * XXX
		 * It really seems like it would be better if the call to
		 * gaim_account_add_buddy() and gaim_conversation_update() were done in
		 * blist.c, possibly in the gaim_blist_add_buddy() function.  Maybe
		 * gaim_account_add_buddy() should be renamed to
		 * gaim_blist_add_new_buddy() or something, and have it call
		 * gaim_blist_add_buddy() after it creates it.  --Mark
		 *
		 * No that's not good.  blist.c should only deal with adding nodes to the
		 * local list.  We need a new, non-gtk file that calls both
		 * gaim_account_add_buddy and gaim_blist_add_buddy().
		 * Or something.  --Mark
		 */

		c = gaim_find_conversation_with_account(GAIM_CONV_TYPE_IM, who, data->account);
		if (c != NULL) {
			icon = gaim_conv_im_get_icon(GAIM_CONV_IM(c));
			if (icon != NULL)
				gaim_buddy_icon_update(icon);
		}
	}

	gtk_widget_destroy(data->window);
}

static void
pidgin_blist_request_add_buddy(GaimAccount *account, const char *username,
								 const char *group, const char *alias)
{
	GtkWidget *table;
	GtkWidget *label;
	GtkWidget *hbox;
	GtkWidget *vbox;
	GtkWidget *img;
	PidginBuddyList *gtkblist;
	PidginAddBuddyData *data = g_new0(PidginAddBuddyData, 1);

	data->account =
		(account != NULL
		 ? account
		 : gaim_connection_get_account(gaim_connections_get_all()->data));

	img = gtk_image_new_from_stock(PIDGIN_STOCK_DIALOG_QUESTION,
					gtk_icon_size_from_name(PIDGIN_ICON_SIZE_TANGO_HUGE));

	gtkblist = PIDGIN_BLIST(gaim_get_blist());

	data->window = gtk_dialog_new_with_buttons(_("Add Buddy"),
			NULL, GTK_DIALOG_NO_SEPARATOR,
			GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
			GTK_STOCK_ADD, GTK_RESPONSE_OK,
			NULL);

	gtk_dialog_set_default_response(GTK_DIALOG(data->window), GTK_RESPONSE_OK);
	gtk_container_set_border_width(GTK_CONTAINER(data->window), GAIM_HIG_BOX_SPACE);
	gtk_window_set_resizable(GTK_WINDOW(data->window), FALSE);
	gtk_box_set_spacing(GTK_BOX(GTK_DIALOG(data->window)->vbox), GAIM_HIG_BORDER);
	gtk_container_set_border_width(GTK_CONTAINER(GTK_DIALOG(data->window)->vbox), GAIM_HIG_BOX_SPACE);
	gtk_window_set_role(GTK_WINDOW(data->window), "add_buddy");
	gtk_window_set_type_hint(GTK_WINDOW(data->window),
							 GDK_WINDOW_TYPE_HINT_DIALOG);

	hbox = gtk_hbox_new(FALSE, GAIM_HIG_BORDER);
	gtk_container_add(GTK_CONTAINER(GTK_DIALOG(data->window)->vbox), hbox);
	gtk_box_pack_start(GTK_BOX(hbox), img, FALSE, FALSE, 0);
	gtk_misc_set_alignment(GTK_MISC(img), 0, 0);

	vbox = gtk_vbox_new(FALSE, 0);
	gtk_container_add(GTK_CONTAINER(hbox), vbox);

	label = gtk_label_new(
		_("Please enter the screen name of the person you would like "
		  "to add to your buddy list. You may optionally enter an alias, "
		  "or nickname,  for the buddy. The alias will be displayed in "
		  "place of the screen name whenever possible.\n"));

	gtk_widget_set_size_request(GTK_WIDGET(label), 400, -1);
	gtk_label_set_line_wrap(GTK_LABEL(label), TRUE);
	gtk_misc_set_alignment(GTK_MISC(label), 0, 0);
	gtk_box_pack_start(GTK_BOX(vbox), label, FALSE, FALSE, 0);

	hbox = gtk_hbox_new(FALSE, GAIM_HIG_BOX_SPACE);
	gtk_container_add(GTK_CONTAINER(vbox), hbox);

	g_signal_connect(G_OBJECT(data->window), "destroy",
					 G_CALLBACK(destroy_add_buddy_dialog_cb), data);

	table = gtk_table_new(4, 2, FALSE);
	gtk_table_set_row_spacings(GTK_TABLE(table), 5);
	gtk_table_set_col_spacings(GTK_TABLE(table), 5);
	gtk_container_set_border_width(GTK_CONTAINER(table), 0);
	gtk_box_pack_start(GTK_BOX(vbox), table, FALSE, FALSE, 0);

	label = gtk_label_new(_("Screen name:"));
	gtk_misc_set_alignment(GTK_MISC(label), 0, 0.5);
	gtk_table_attach_defaults(GTK_TABLE(table), label, 0, 1, 0, 1);

	data->entry = gtk_entry_new();
	gtk_table_attach_defaults(GTK_TABLE(table), data->entry, 1, 2, 0, 1);
	gtk_widget_grab_focus(data->entry);

	if (username != NULL)
		gtk_entry_set_text(GTK_ENTRY(data->entry), username);
	else
		gtk_dialog_set_response_sensitive(GTK_DIALOG(data->window),
										  GTK_RESPONSE_OK, FALSE);

	gtk_entry_set_activates_default (GTK_ENTRY(data->entry), TRUE);
	pidgin_set_accessible_label (data->entry, label);

	g_signal_connect(G_OBJECT(data->entry), "changed",
					 G_CALLBACK(pidgin_set_sensitive_if_input),
					 data->window);

	label = gtk_label_new(_("Alias:"));
	gtk_misc_set_alignment(GTK_MISC(label), 0, 0.5);
	gtk_table_attach_defaults(GTK_TABLE(table), label, 0, 1, 1, 2);

	data->entry_for_alias = gtk_entry_new();
	gtk_table_attach_defaults(GTK_TABLE(table),
							  data->entry_for_alias, 1, 2, 1, 2);

	if (alias != NULL)
		gtk_entry_set_text(GTK_ENTRY(data->entry_for_alias), alias);

	if (username != NULL)
		gtk_widget_grab_focus(GTK_WIDGET(data->entry_for_alias));

	gtk_entry_set_activates_default (GTK_ENTRY(data->entry_for_alias), TRUE);
	pidgin_set_accessible_label (data->entry_for_alias, label);

	label = gtk_label_new(_("Group:"));
	gtk_misc_set_alignment(GTK_MISC(label), 0, 0.5);
	gtk_table_attach_defaults(GTK_TABLE(table), label, 0, 1, 2, 3);

	data->combo = gtk_combo_new();
	gtk_combo_set_popdown_strings(GTK_COMBO(data->combo), groups_tree());
	gtk_table_attach_defaults(GTK_TABLE(table), data->combo, 1, 2, 2, 3);
	pidgin_set_accessible_label (data->combo, label);

	/* Set up stuff for the account box */
	label = gtk_label_new(_("Account:"));
	gtk_misc_set_alignment(GTK_MISC(label), 0, 0.5);
	gtk_table_attach_defaults(GTK_TABLE(table), label, 0, 1, 3, 4);

	data->account_box = pidgin_account_option_menu_new(account, FALSE,
			G_CALLBACK(add_buddy_select_account_cb), NULL, data);

	gtk_table_attach_defaults(GTK_TABLE(table), data->account_box, 1, 2, 3, 4);
	pidgin_set_accessible_label (data->account_box, label);
	/* End of account box */

	g_signal_connect(G_OBJECT(data->window), "response",
					 G_CALLBACK(add_buddy_cb), data);

	gtk_widget_show_all(data->window);

	if (group != NULL)
		gtk_entry_set_text(GTK_ENTRY(GTK_COMBO(data->combo)->entry), group);
}

static void
add_chat_cb(GtkWidget *w, PidginAddChatData *data)
{
	GHashTable *components;
	GList *tmp;
	GaimChat *chat;
	GaimGroup *group;
	const char *group_name;
	const char *value;

	components = g_hash_table_new_full(g_str_hash, g_str_equal,
									   g_free, g_free);

	for (tmp = data->entries; tmp; tmp = tmp->next)
	{
		if (g_object_get_data(tmp->data, "is_spin"))
		{
			g_hash_table_replace(components,
					g_strdup(g_object_get_data(tmp->data, "identifier")),
					g_strdup_printf("%d",
						gtk_spin_button_get_value_as_int(tmp->data)));
		}
		else
		{
			value = gtk_entry_get_text(tmp->data);
			if (*value != '\0')
				g_hash_table_replace(components,
						g_strdup(g_object_get_data(tmp->data, "identifier")),
						g_strdup(value));
		}
	}

	chat = gaim_chat_new(data->account,
							   gtk_entry_get_text(GTK_ENTRY(data->alias_entry)),
							   components);

	group_name = gtk_entry_get_text(GTK_ENTRY(GTK_COMBO(data->group_combo)->entry));

	if ((group = gaim_find_group(group_name)) == NULL)
	{
		group = gaim_group_new(group_name);
		gaim_blist_add_group(group, NULL);
	}

	if (chat != NULL)
	{
		gaim_blist_add_chat(chat, group, NULL);
	}

	gtk_widget_destroy(data->window);
	g_free(data->default_chat_name);
	g_list_free(data->entries);
	g_free(data);
}

static void
add_chat_resp_cb(GtkWidget *w, int resp, PidginAddChatData *data)
{
	if (resp == GTK_RESPONSE_OK)
	{
		add_chat_cb(NULL, data);
	}
	else
	{
		gtk_widget_destroy(data->window);
		g_free(data->default_chat_name);
		g_list_free(data->entries);
		g_free(data);
	}
}

/*
 * Check the values of all the text entry boxes.  If any required input
 * strings are empty then don't allow the user to click on "OK."
 */
static void
addchat_set_sensitive_if_input_cb(GtkWidget *entry, gpointer user_data)
{
	PidginAddChatData *data;
	GList *tmp;
	const char *text;
	gboolean required;
	gboolean sensitive = TRUE;

	data = user_data;

	for (tmp = data->entries; tmp != NULL; tmp = tmp->next)
	{
		if (!g_object_get_data(tmp->data, "is_spin"))
		{
			required = GPOINTER_TO_INT(g_object_get_data(tmp->data, "required"));
			text = gtk_entry_get_text(tmp->data);
			if (required && (*text == '\0'))
				sensitive = FALSE;
		}
	}

	gtk_dialog_set_response_sensitive(GTK_DIALOG(data->window), GTK_RESPONSE_OK, sensitive);
}

static void
rebuild_addchat_entries(PidginAddChatData *data)
{
	GaimConnection *gc;
	GList *list = NULL, *tmp;
	GHashTable *defaults = NULL;
	struct proto_chat_entry *pce;
	gboolean focus = TRUE;

	g_return_if_fail(data->account != NULL);

	gc = gaim_account_get_connection(data->account);

	while ((tmp = gtk_container_get_children(GTK_CONTAINER(data->entries_box))))
		gtk_widget_destroy(tmp->data);

	g_list_free(data->entries);

	data->entries = NULL;

	if (GAIM_PLUGIN_PROTOCOL_INFO(gc->prpl)->chat_info != NULL)
		list = GAIM_PLUGIN_PROTOCOL_INFO(gc->prpl)->chat_info(gc);

	if (GAIM_PLUGIN_PROTOCOL_INFO(gc->prpl)->chat_info_defaults != NULL)
		defaults = GAIM_PLUGIN_PROTOCOL_INFO(gc->prpl)->chat_info_defaults(gc, data->default_chat_name);

	for (tmp = list; tmp; tmp = tmp->next)
	{
		GtkWidget *label;
		GtkWidget *rowbox;
		GtkWidget *input;

		pce = tmp->data;

		rowbox = gtk_hbox_new(FALSE, 5);
		gtk_box_pack_start(GTK_BOX(data->entries_box), rowbox, FALSE, FALSE, 0);

		label = gtk_label_new_with_mnemonic(pce->label);
		gtk_misc_set_alignment(GTK_MISC(label), 0, 0.5);
		gtk_size_group_add_widget(data->sg, label);
		gtk_box_pack_start(GTK_BOX(rowbox), label, FALSE, FALSE, 0);

		if (pce->is_int)
		{
			GtkObject *adjust;
			adjust = gtk_adjustment_new(pce->min, pce->min, pce->max,
										1, 10, 10);
			input = gtk_spin_button_new(GTK_ADJUSTMENT(adjust), 1, 0);
			gtk_widget_set_size_request(input, 50, -1);
			gtk_box_pack_end(GTK_BOX(rowbox), input, FALSE, FALSE, 0);
		}
		else
		{
			char *value;
			input = gtk_entry_new();
			gtk_entry_set_activates_default(GTK_ENTRY(input), TRUE);
			value = g_hash_table_lookup(defaults, pce->identifier);
			if (value != NULL)
				gtk_entry_set_text(GTK_ENTRY(input), value);
			if (pce->secret)
			{
				gtk_entry_set_visibility(GTK_ENTRY(input), FALSE);
				if (gtk_entry_get_invisible_char(GTK_ENTRY(input)) == '*')
					gtk_entry_set_invisible_char(GTK_ENTRY(input), GAIM_INVISIBLE_CHAR);
			}
			gtk_box_pack_end(GTK_BOX(rowbox), input, TRUE, TRUE, 0);
			g_signal_connect(G_OBJECT(input), "changed",
							 G_CALLBACK(addchat_set_sensitive_if_input_cb), data);
		}

		/* Do the following for any type of input widget */
		if (focus)
		{
			gtk_widget_grab_focus(input);
			focus = FALSE;
		}
		gtk_label_set_mnemonic_widget(GTK_LABEL(label), input);
		pidgin_set_accessible_label(input, label);
		g_object_set_data(G_OBJECT(input), "identifier", (gpointer)pce->identifier);
		g_object_set_data(G_OBJECT(input), "is_spin", GINT_TO_POINTER(pce->is_int));
		g_object_set_data(G_OBJECT(input), "required", GINT_TO_POINTER(pce->required));
		data->entries = g_list_append(data->entries, input);

		g_free(pce);
	}

	g_list_free(list);
	g_hash_table_destroy(defaults);

	/* Set whether the "OK" button should be clickable initially */
	addchat_set_sensitive_if_input_cb(NULL, data);

	gtk_widget_show_all(data->entries_box);
}

static void
addchat_select_account_cb(GObject *w, GaimAccount *account,
						   PidginAddChatData *data)
{
	if (strcmp(gaim_account_get_protocol_id(data->account),
		gaim_account_get_protocol_id(account)) == 0)
	{
		data->account = account;
	}
	else
	{
		data->account = account;
		rebuild_addchat_entries(data);
	}
}

static void
pidgin_blist_request_add_chat(GaimAccount *account, GaimGroup *group,
								const char *alias, const char *name)
{
	PidginAddChatData *data;
	PidginBuddyList *gtkblist;
	GList *l;
	GaimConnection *gc;
	GtkWidget *label;
	GtkWidget *rowbox;
	GtkWidget *hbox;
	GtkWidget *vbox;
	GtkWidget *img;

	if (account != NULL) {
		gc = gaim_account_get_connection(account);

		if (GAIM_PLUGIN_PROTOCOL_INFO(gc->prpl)->join_chat == NULL) {
			gaim_notify_error(gc, NULL, _("This protocol does not support chat rooms."), NULL);
			return;
		}
	} else {
		/* Find an account with chat capabilities */
		for (l = gaim_connections_get_all(); l != NULL; l = l->next) {
			gc = (GaimConnection *)l->data;

			if (GAIM_PLUGIN_PROTOCOL_INFO(gc->prpl)->join_chat != NULL) {
				account = gaim_connection_get_account(gc);
				break;
			}
		}

		if (account == NULL) {
			gaim_notify_error(NULL, NULL,
							  _("You are not currently signed on with any "
								"protocols that have the ability to chat."), NULL);
			return;
		}
	}

	data = g_new0(PidginAddChatData, 1);
	data->account = account;
	data->default_chat_name = g_strdup(name);

	img = gtk_image_new_from_stock(PIDGIN_STOCK_DIALOG_QUESTION,
					gtk_icon_size_from_name(PIDGIN_ICON_SIZE_TANGO_HUGE));

	gtkblist = PIDGIN_BLIST(gaim_get_blist());

	data->sg = gtk_size_group_new(GTK_SIZE_GROUP_HORIZONTAL);

	data->window = gtk_dialog_new_with_buttons(_("Add Chat"),
		NULL, GTK_DIALOG_NO_SEPARATOR,
		GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
		GTK_STOCK_ADD, GTK_RESPONSE_OK,
		NULL);

	gtk_dialog_set_default_response(GTK_DIALOG(data->window), GTK_RESPONSE_OK);
	gtk_container_set_border_width(GTK_CONTAINER(data->window), GAIM_HIG_BOX_SPACE);
	gtk_window_set_resizable(GTK_WINDOW(data->window), FALSE);
	gtk_box_set_spacing(GTK_BOX(GTK_DIALOG(data->window)->vbox), GAIM_HIG_BORDER);
	gtk_container_set_border_width(GTK_CONTAINER(GTK_DIALOG(data->window)->vbox), GAIM_HIG_BOX_SPACE);
	gtk_window_set_role(GTK_WINDOW(data->window), "add_chat");
	gtk_window_set_type_hint(GTK_WINDOW(data->window),
							 GDK_WINDOW_TYPE_HINT_DIALOG);

	hbox = gtk_hbox_new(FALSE, GAIM_HIG_BORDER);
	gtk_container_add(GTK_CONTAINER(GTK_DIALOG(data->window)->vbox), hbox);
	gtk_box_pack_start(GTK_BOX(hbox), img, FALSE, FALSE, 0);
	gtk_misc_set_alignment(GTK_MISC(img), 0, 0);

	vbox = gtk_vbox_new(FALSE, 5);
	gtk_container_add(GTK_CONTAINER(hbox), vbox);

	label = gtk_label_new(
		_("Please enter an alias, and the appropriate information "
		  "about the chat you would like to add to your buddy list.\n"));
	gtk_widget_set_size_request(label, 400, -1);
	gtk_label_set_line_wrap(GTK_LABEL(label), TRUE);
	gtk_misc_set_alignment(GTK_MISC(label), 0, 0);
	gtk_box_pack_start(GTK_BOX(vbox), label, FALSE, FALSE, 0);

	rowbox = gtk_hbox_new(FALSE, 5);
	gtk_box_pack_start(GTK_BOX(vbox), rowbox, FALSE, FALSE, 0);

	label = gtk_label_new(_("Account:"));
	gtk_misc_set_alignment(GTK_MISC(label), 0, 0.5);
	gtk_size_group_add_widget(data->sg, label);
	gtk_box_pack_start(GTK_BOX(rowbox), label, FALSE, FALSE, 0);

	data->account_menu = pidgin_account_option_menu_new(account, FALSE,
			G_CALLBACK(addchat_select_account_cb),
			chat_account_filter_func, data);
	gtk_box_pack_start(GTK_BOX(rowbox), data->account_menu, TRUE, TRUE, 0);
	pidgin_set_accessible_label (data->account_menu, label);

	data->entries_box = gtk_vbox_new(FALSE, 5);
	gtk_container_set_border_width(GTK_CONTAINER(data->entries_box), 0);
	gtk_box_pack_start(GTK_BOX(vbox), data->entries_box, TRUE, TRUE, 0);

	rebuild_addchat_entries(data);

	rowbox = gtk_hbox_new(FALSE, 5);
	gtk_box_pack_start(GTK_BOX(vbox), rowbox, FALSE, FALSE, 0);

	label = gtk_label_new(_("Alias:"));
	gtk_misc_set_alignment(GTK_MISC(label), 0, 0.5);
	gtk_size_group_add_widget(data->sg, label);
	gtk_box_pack_start(GTK_BOX(rowbox), label, FALSE, FALSE, 0);

	data->alias_entry = gtk_entry_new();
	if (alias != NULL)
		gtk_entry_set_text(GTK_ENTRY(data->alias_entry), alias);
	gtk_box_pack_end(GTK_BOX(rowbox), data->alias_entry, TRUE, TRUE, 0);
	gtk_entry_set_activates_default(GTK_ENTRY(data->alias_entry), TRUE);
	pidgin_set_accessible_label (data->alias_entry, label);

	rowbox = gtk_hbox_new(FALSE, 5);
	gtk_box_pack_start(GTK_BOX(vbox), rowbox, FALSE, FALSE, 0);

	label = gtk_label_new(_("Group:"));
	gtk_misc_set_alignment(GTK_MISC(label), 0, 0.5);
	gtk_size_group_add_widget(data->sg, label);
	gtk_box_pack_start(GTK_BOX(rowbox), label, FALSE, FALSE, 0);

	data->group_combo = gtk_combo_new();
	gtk_combo_set_popdown_strings(GTK_COMBO(data->group_combo), groups_tree());
	gtk_box_pack_end(GTK_BOX(rowbox), data->group_combo, TRUE, TRUE, 0);

	if (group)
	{
		gtk_entry_set_text(GTK_ENTRY(GTK_COMBO(data->group_combo)->entry),
						   group->name);
	}
	pidgin_set_accessible_label (data->group_combo, label);

	g_signal_connect(G_OBJECT(data->window), "response",
					 G_CALLBACK(add_chat_resp_cb), data);

	gtk_widget_show_all(data->window);
}

static void
add_group_cb(GaimConnection *gc, const char *group_name)
{
	GaimGroup *group;

	if ((group_name == NULL) || (*group_name == '\0'))
		return;

	group = gaim_group_new(group_name);
	gaim_blist_add_group(group, NULL);
}

static void
pidgin_blist_request_add_group(void)
{
	gaim_request_input(NULL, _("Add Group"), NULL,
					   _("Please enter the name of the group to be added."),
					   NULL, FALSE, FALSE, NULL,
					   _("Add"), G_CALLBACK(add_group_cb),
					   _("Cancel"), NULL, NULL);
}

void
pidgin_blist_toggle_visibility()
{
	if (gtkblist && gtkblist->window) {
		if (GTK_WIDGET_VISIBLE(gtkblist->window)) {
			gaim_blist_set_visible(PIDGIN_WINDOW_ICONIFIED(gtkblist->window) || gtk_blist_obscured);
		} else {
			gaim_blist_set_visible(TRUE);
		}
	}
}

void
pidgin_blist_visibility_manager_add()
{
	visibility_manager_count++;
	gaim_debug_info("gtkblist", "added visibility manager: %d\n", visibility_manager_count);
}

void
pidgin_blist_visibility_manager_remove()
{
	if (visibility_manager_count)
		visibility_manager_count--;
	if (!visibility_manager_count)
		gaim_blist_set_visible(TRUE);
	gaim_debug_info("gtkblist", "removed visibility manager: %d\n", visibility_manager_count);
}

void pidgin_blist_add_alert(GtkWidget *widget)
{
	gtk_container_add(GTK_CONTAINER(gtkblist->scrollbook), widget);
	if (!GTK_WIDGET_HAS_FOCUS(gtkblist->window))
		pidgin_set_urgent(GTK_WINDOW(gtkblist->window), TRUE);
}

void
pidgin_blist_set_headline(const char *text, GdkPixbuf *pixbuf, GCallback callback,
			gpointer user_data, GDestroyNotify destroy)
{
	/* Destroy any existing headline first */
	if (gtkblist->headline_destroy)
		gtkblist->headline_destroy(gtkblist->headline_data);

	gtk_label_set_markup(GTK_LABEL(gtkblist->headline_label), text);
	gtk_image_set_from_pixbuf(GTK_IMAGE(gtkblist->headline_image), pixbuf);

	gtkblist->headline_callback = callback;
	gtkblist->headline_data = user_data;
	gtkblist->headline_destroy = destroy;
	if (!GTK_WIDGET_HAS_FOCUS(gtkblist->window))
		pidgin_set_urgent(GTK_WINDOW(gtkblist->window), TRUE);
	gtk_widget_show_all(gtkblist->headline_hbox);
}

static GaimBlistUiOps blist_ui_ops =
{
	pidgin_blist_new_list,
	pidgin_blist_new_node,
	pidgin_blist_show,
	pidgin_blist_update,
	pidgin_blist_remove,
	pidgin_blist_destroy,
	pidgin_blist_set_visible,
	pidgin_blist_request_add_buddy,
	pidgin_blist_request_add_chat,
	pidgin_blist_request_add_group
};


GaimBlistUiOps *
pidgin_blist_get_ui_ops(void)
{
	return &blist_ui_ops;
}

PidginBuddyList *pidgin_blist_get_default_gtk_blist()
{
	return gtkblist;
}

static void account_signon_cb(GaimConnection *gc, gpointer z)
{
	GaimAccount *account = gaim_connection_get_account(gc);
	GaimBlistNode *gnode, *cnode;
	for(gnode = gaim_get_blist()->root; gnode; gnode = gnode->next)
	{
		if(!GAIM_BLIST_NODE_IS_GROUP(gnode))
			continue;
		for(cnode = gnode->child; cnode; cnode = cnode->next)
		{
			GaimChat *chat;

			if(!GAIM_BLIST_NODE_IS_CHAT(cnode))
				continue;

			chat = (GaimChat *)cnode;

			if(chat->account != account)
				continue;

			if(gaim_blist_node_get_bool((GaimBlistNode*)chat, "gtk-autojoin") ||
					(gaim_blist_node_get_string((GaimBlistNode*)chat,
					 "gtk-autojoin") != NULL))
				serv_join_chat(gc, chat->components);
		}
	}
}

void *
pidgin_blist_get_handle() {
	static int handle;

	return &handle;
}

static gboolean buddy_signonoff_timeout_cb(GaimBuddy *buddy)
{
	struct _pidgin_blist_node *gtknode = ((GaimBlistNode*)buddy)->ui_data;

	gtknode->recent_signonoff = FALSE;
	gtknode->recent_signonoff_timer = 0;

	pidgin_blist_update(NULL, (GaimBlistNode*)buddy);

	return FALSE;
}

static void buddy_signonoff_cb(GaimBuddy *buddy)
{
	struct _pidgin_blist_node *gtknode;

	if(!((GaimBlistNode*)buddy)->ui_data) {
		pidgin_blist_new_node((GaimBlistNode*)buddy);
	}

	gtknode = ((GaimBlistNode*)buddy)->ui_data;

	gtknode->recent_signonoff = TRUE;

	if(gtknode->recent_signonoff_timer > 0)
		gaim_timeout_remove(gtknode->recent_signonoff_timer);
	gtknode->recent_signonoff_timer = gaim_timeout_add(10000,
			(GSourceFunc)buddy_signonoff_timeout_cb, buddy);
}

void pidgin_blist_init(void)
{
	void *gtk_blist_handle = pidgin_blist_get_handle();

	gaim_signal_connect(gaim_connections_get_handle(), "signed-on",
						gtk_blist_handle, GAIM_CALLBACK(account_signon_cb),
						NULL);

	/* Initialize prefs */
	gaim_prefs_add_none("/gaim/gtk/blist");
	gaim_prefs_add_bool("/gaim/gtk/blist/show_buddy_icons", TRUE);
	gaim_prefs_add_bool("/gaim/gtk/blist/show_empty_groups", FALSE);
	gaim_prefs_add_bool("/gaim/gtk/blist/show_idle_time", TRUE);
	gaim_prefs_add_bool("/gaim/gtk/blist/show_offline_buddies", FALSE);
	gaim_prefs_add_bool("/gaim/gtk/blist/list_visible", TRUE);
	gaim_prefs_add_bool("/gaim/gtk/blist/list_maximized", FALSE);
	gaim_prefs_add_string("/gaim/gtk/blist/sort_type", "alphabetical");
	gaim_prefs_add_int("/gaim/gtk/blist/x", 0);
	gaim_prefs_add_int("/gaim/gtk/blist/y", 0);
	gaim_prefs_add_int("/gaim/gtk/blist/width", 250); /* Golden ratio, baby */
	gaim_prefs_add_int("/gaim/gtk/blist/height", 405); /* Golden ratio, baby */
	gaim_prefs_add_int("/gaim/gtk/blist/tooltip_delay", 500);

	/* Register our signals */
	gaim_signal_register(gtk_blist_handle, "gtkblist-hiding",
	                     gaim_marshal_VOID__POINTER, NULL, 1,
	                     gaim_value_new(GAIM_TYPE_SUBTYPE,
	                                    GAIM_SUBTYPE_BLIST));

	gaim_signal_register(gtk_blist_handle, "gtkblist-unhiding",
	                     gaim_marshal_VOID__POINTER, NULL, 1,
	                     gaim_value_new(GAIM_TYPE_SUBTYPE,
	                                    GAIM_SUBTYPE_BLIST));

	gaim_signal_register(gtk_blist_handle, "gtkblist-created",
	                     gaim_marshal_VOID__POINTER, NULL, 1,
	                     gaim_value_new(GAIM_TYPE_SUBTYPE,
	                                    GAIM_SUBTYPE_BLIST));

	gaim_signal_register(gtk_blist_handle, "drawing-tooltip",
	                     gaim_marshal_VOID__POINTER_POINTER_UINT, NULL, 3,
	                     gaim_value_new(GAIM_TYPE_SUBTYPE,
	                                    GAIM_SUBTYPE_BLIST_NODE),
	                     gaim_value_new_outgoing(GAIM_TYPE_BOXED, "GString *"),
	                     gaim_value_new(GAIM_TYPE_BOOLEAN));


	gaim_signal_connect(gaim_blist_get_handle(), "buddy-signed-on", gtk_blist_handle, GAIM_CALLBACK(buddy_signonoff_cb), NULL);
	gaim_signal_connect(gaim_blist_get_handle(), "buddy-signed-off", gtk_blist_handle, GAIM_CALLBACK(buddy_signonoff_cb), NULL);
	gaim_signal_connect(gaim_blist_get_handle(), "buddy-privacy-changed", gtk_blist_handle, GAIM_CALLBACK(pidgin_blist_update_privacy_cb), NULL);
}

void
pidgin_blist_uninit(void) {
	gaim_signals_unregister_by_instance(pidgin_blist_get_handle());
	gaim_signals_disconnect_by_handle(pidgin_blist_get_handle());
}

/*********************************************************************
 * Buddy List sorting functions                                      *
 *********************************************************************/

GList *pidgin_blist_get_sort_methods()
{
	return pidgin_blist_sort_methods;
}

void pidgin_blist_sort_method_reg(const char *id, const char *name, pidgin_blist_sort_function func)
{
	struct pidgin_blist_sort_method *method = g_new0(struct pidgin_blist_sort_method, 1);
	method->id = g_strdup(id);
	method->name = g_strdup(name);
	method->func = func;
	pidgin_blist_sort_methods = g_list_append(pidgin_blist_sort_methods, method);
	pidgin_blist_update_sort_methods();
}

void pidgin_blist_sort_method_unreg(const char *id){
	GList *l = pidgin_blist_sort_methods;

	while(l) {
		struct pidgin_blist_sort_method *method = l->data;
		if(!strcmp(method->id, id)) {
			pidgin_blist_sort_methods = g_list_delete_link(pidgin_blist_sort_methods, l);
			g_free(method->id);
			g_free(method->name);
			g_free(method);
			break;
		}
	}
	pidgin_blist_update_sort_methods();
}

void pidgin_blist_sort_method_set(const char *id){
	GList *l = pidgin_blist_sort_methods;

	if(!id)
		id = "none";

	while (l && strcmp(((struct pidgin_blist_sort_method*)l->data)->id, id))
		l = l->next;

	if (l) {
		current_sort_method = l->data;
	} else if (!current_sort_method) {
		pidgin_blist_sort_method_set("none");
		return;
	}
	if (!strcmp(id, "none")) {
		redo_buddy_list(gaim_get_blist(), TRUE, FALSE);
	} else {
		redo_buddy_list(gaim_get_blist(), FALSE, FALSE);
	}
}

/******************************************
 ** Sort Methods
 ******************************************/

static void sort_method_none(GaimBlistNode *node, GaimBuddyList *blist, GtkTreeIter parent_iter, GtkTreeIter *cur, GtkTreeIter *iter)
{
	GaimBlistNode *sibling = node->prev;
	GtkTreeIter sibling_iter;

	if (cur != NULL) {
		*iter = *cur;
		return;
	}

	while (sibling && !get_iter_from_node(sibling, &sibling_iter)) {
		sibling = sibling->prev;
	}

	gtk_tree_store_insert_after(gtkblist->treemodel, iter,
			node->parent ? &parent_iter : NULL,
			sibling ? &sibling_iter : NULL);
}

#if GTK_CHECK_VERSION(2,2,1)

static void sort_method_alphabetical(GaimBlistNode *node, GaimBuddyList *blist, GtkTreeIter groupiter, GtkTreeIter *cur, GtkTreeIter *iter)
{
	GtkTreeIter more_z;

	const char *my_name;

	if(GAIM_BLIST_NODE_IS_CONTACT(node)) {
		my_name = gaim_contact_get_alias((GaimContact*)node);
	} else if(GAIM_BLIST_NODE_IS_CHAT(node)) {
		my_name = gaim_chat_get_name((GaimChat*)node);
	} else {
		sort_method_none(node, blist, groupiter, cur, iter);
		return;
	}


	if (!gtk_tree_model_iter_children(GTK_TREE_MODEL(gtkblist->treemodel), &more_z, &groupiter)) {
		gtk_tree_store_insert(gtkblist->treemodel, iter, &groupiter, 0);
		return;
	}

	do {
		GValue val;
		GaimBlistNode *n;
		const char *this_name;
		int cmp;

		val.g_type = 0;
		gtk_tree_model_get_value (GTK_TREE_MODEL(gtkblist->treemodel), &more_z, NODE_COLUMN, &val);
		n = g_value_get_pointer(&val);

		if(GAIM_BLIST_NODE_IS_CONTACT(n)) {
			this_name = gaim_contact_get_alias((GaimContact*)n);
		} else if(GAIM_BLIST_NODE_IS_CHAT(n)) {
			this_name = gaim_chat_get_name((GaimChat*)n);
		} else {
			this_name = NULL;
		}

		cmp = gaim_utf8_strcasecmp(my_name, this_name);

		if(this_name && (cmp < 0 || (cmp == 0 && node < n))) {
			if(cur) {
				gtk_tree_store_move_before(gtkblist->treemodel, cur, &more_z);
				*iter = *cur;
				return;
			} else {
				gtk_tree_store_insert_before(gtkblist->treemodel, iter,
						&groupiter, &more_z);
				return;
			}
		}
		g_value_unset(&val);
	} while (gtk_tree_model_iter_next (GTK_TREE_MODEL(gtkblist->treemodel), &more_z));

	if(cur) {
		gtk_tree_store_move_before(gtkblist->treemodel, cur, NULL);
		*iter = *cur;
		return;
	} else {
		gtk_tree_store_append(gtkblist->treemodel, iter, &groupiter);
		return;
	}
}

static void sort_method_status(GaimBlistNode *node, GaimBuddyList *blist, GtkTreeIter groupiter, GtkTreeIter *cur, GtkTreeIter *iter)
{
	GtkTreeIter more_z;

	GaimBuddy *my_buddy, *this_buddy;

	if(GAIM_BLIST_NODE_IS_CONTACT(node)) {
		my_buddy = gaim_contact_get_priority_buddy((GaimContact*)node);
	} else if(GAIM_BLIST_NODE_IS_CHAT(node)) {
		if (cur != NULL) {
			*iter = *cur;
			return;
		}

		gtk_tree_store_append(gtkblist->treemodel, iter, &groupiter);
		return;
	} else {
		sort_method_none(node, blist, groupiter, cur, iter);
		return;
	}


	if (!gtk_tree_model_iter_children(GTK_TREE_MODEL(gtkblist->treemodel), &more_z, &groupiter)) {
		gtk_tree_store_insert(gtkblist->treemodel, iter, &groupiter, 0);
		return;
	}

	do {
		GValue val;
		GaimBlistNode *n;
		gint name_cmp;
		gint presence_cmp;

		val.g_type = 0;
		gtk_tree_model_get_value (GTK_TREE_MODEL(gtkblist->treemodel), &more_z, NODE_COLUMN, &val);
		n = g_value_get_pointer(&val);

		if(GAIM_BLIST_NODE_IS_CONTACT(n)) {
			this_buddy = gaim_contact_get_priority_buddy((GaimContact*)n);
		} else {
			this_buddy = NULL;
		}

		name_cmp = gaim_utf8_strcasecmp(
			gaim_contact_get_alias(gaim_buddy_get_contact(my_buddy)),
			(this_buddy
			 ? gaim_contact_get_alias(gaim_buddy_get_contact(this_buddy))
			 : NULL));

		presence_cmp = gaim_presence_compare(
			gaim_buddy_get_presence(my_buddy),
			this_buddy ? gaim_buddy_get_presence(this_buddy) : NULL);

		if (this_buddy == NULL ||
			(presence_cmp < 0 ||
			 (presence_cmp == 0 &&
			  (name_cmp < 0 || (name_cmp == 0 && node < n)))))
		{
			if (cur != NULL)
			{
				gtk_tree_store_move_before(gtkblist->treemodel, cur, &more_z);
				*iter = *cur;
				return;
			}
			else
			{
				gtk_tree_store_insert_before(gtkblist->treemodel, iter,
											 &groupiter, &more_z);
				return;
			}
		}

		g_value_unset(&val);
	}
	while (gtk_tree_model_iter_next(GTK_TREE_MODEL(gtkblist->treemodel),
									&more_z));

	if (cur) {
		gtk_tree_store_move_before(gtkblist->treemodel, cur, NULL);
		*iter = *cur;
		return;
	} else {
		gtk_tree_store_append(gtkblist->treemodel, iter, &groupiter);
		return;
	}
}

static void sort_method_log(GaimBlistNode *node, GaimBuddyList *blist, GtkTreeIter groupiter, GtkTreeIter *cur, GtkTreeIter *iter)
{
	GtkTreeIter more_z;

	int log_size = 0, this_log_size = 0;
	const char *buddy_name, *this_buddy_name;

	if(cur && (gtk_tree_model_iter_n_children(GTK_TREE_MODEL(gtkblist->treemodel), &groupiter) == 1)) {
		*iter = *cur;
		return;
	}

	if(GAIM_BLIST_NODE_IS_CONTACT(node)) {
		GaimBlistNode *n;
		for (n = node->child; n; n = n->next)
			log_size += gaim_log_get_total_size(GAIM_LOG_IM, ((GaimBuddy*)(n))->name, ((GaimBuddy*)(n))->account);
		buddy_name = gaim_contact_get_alias((GaimContact*)node);
	} else if(GAIM_BLIST_NODE_IS_CHAT(node)) {
		/* we don't have a reliable way of getting the log filename
		 * from the chat info in the blist, yet */
		if (cur != NULL) {
			*iter = *cur;
			return;
		}

		gtk_tree_store_append(gtkblist->treemodel, iter, &groupiter);
		return;
	} else {
		sort_method_none(node, blist, groupiter, cur, iter);
		return;
	}


	if (!gtk_tree_model_iter_children(GTK_TREE_MODEL(gtkblist->treemodel), &more_z, &groupiter)) {
		gtk_tree_store_insert(gtkblist->treemodel, iter, &groupiter, 0);
		return;
	}

	do {
		GValue val;
		GaimBlistNode *n;
		GaimBlistNode *n2;
		int cmp;

		val.g_type = 0;
		gtk_tree_model_get_value (GTK_TREE_MODEL(gtkblist->treemodel), &more_z, NODE_COLUMN, &val);
		n = g_value_get_pointer(&val);
		this_log_size = 0;

		if(GAIM_BLIST_NODE_IS_CONTACT(n)) {
			for (n2 = n->child; n2; n2 = n2->next)
				this_log_size += gaim_log_get_total_size(GAIM_LOG_IM, ((GaimBuddy*)(n2))->name, ((GaimBuddy*)(n2))->account);
			this_buddy_name = gaim_contact_get_alias((GaimContact*)n);
		} else {
			this_buddy_name = NULL;
		}

		cmp = gaim_utf8_strcasecmp(buddy_name, this_buddy_name);

		if (!GAIM_BLIST_NODE_IS_CONTACT(n) || log_size > this_log_size ||
				((log_size == this_log_size) &&
				 (cmp < 0 || (cmp == 0 && node < n)))) {
			if (cur != NULL) {
				gtk_tree_store_move_before(gtkblist->treemodel, cur, &more_z);
				*iter = *cur;
				return;
			} else {
				gtk_tree_store_insert_before(gtkblist->treemodel, iter,
						&groupiter, &more_z);
				return;
			}
		}
		g_value_unset(&val);
	} while (gtk_tree_model_iter_next (GTK_TREE_MODEL(gtkblist->treemodel), &more_z));

	if (cur != NULL) {
		gtk_tree_store_move_before(gtkblist->treemodel, cur, NULL);
		*iter = *cur;
		return;
	} else {
		gtk_tree_store_append(gtkblist->treemodel, iter, &groupiter);
		return;
	}
}

#endif

static void
plugin_act(GtkObject *obj, GaimPluginAction *pam)
{
	if (pam && pam->callback)
		pam->callback(pam);
}

static void
build_plugin_actions(GtkWidget *menu, GaimPlugin *plugin)
{
	GtkWidget *menuitem;
	GaimPluginAction *action = NULL;
	GList *actions, *l;

	actions = GAIM_PLUGIN_ACTIONS(plugin, NULL);

	for (l = actions; l != NULL; l = l->next)
	{
		if (l->data)
		{
			action = (GaimPluginAction *) l->data;
			action->plugin = plugin;
			action->context = NULL;

			menuitem = gtk_menu_item_new_with_label(action->label);
			gtk_menu_shell_append(GTK_MENU_SHELL(menu), menuitem);

			g_signal_connect(G_OBJECT(menuitem), "activate",
					G_CALLBACK(plugin_act), action);
			g_object_set_data_full(G_OBJECT(menuitem), "plugin_action",
								   action,
								   (GDestroyNotify)gaim_plugin_action_free);
			gtk_widget_show(menuitem);
		}
		else
			pidgin_separator(menu);
	}

	g_list_free(actions);
}

static void
modify_account_cb(GtkWidget *widget, gpointer data)
{
	pidgin_account_dialog_show(PIDGIN_MODIFY_ACCOUNT_DIALOG, data);
}

static void
enable_account_cb(GtkCheckMenuItem *widget, gpointer data)
{
	GaimAccount *account = data;
	const GaimSavedStatus *saved_status;

	saved_status = gaim_savedstatus_get_current();
	gaim_savedstatus_activate_for_account(saved_status, account);

	gaim_account_set_enabled(account, PIDGIN_UI, TRUE);
}

static void
disable_account_cb(GtkCheckMenuItem *widget, gpointer data)
{
	GaimAccount *account = data;

	gaim_account_set_enabled(account, PIDGIN_UI, FALSE);
}

void
pidgin_blist_update_accounts_menu(void)
{
	GtkWidget *menuitem = NULL, *submenu = NULL;
	GtkAccelGroup *accel_group = NULL;
	GList *l = NULL, *accounts = NULL;
	gboolean disabled_accounts = FALSE;

	if (accountmenu == NULL)
		return;

	/* Clear the old Accounts menu */
	for (l = gtk_container_get_children(GTK_CONTAINER(accountmenu)); l; l = l->next) {
		menuitem = l->data;

		if (menuitem != gtk_item_factory_get_widget(gtkblist->ift, N_("/Accounts/Add\\/Edit")))
			gtk_widget_destroy(menuitem);
	}

	for (accounts = gaim_accounts_get_all(); accounts; accounts = accounts->next) {
		char *buf = NULL;
		char *accel_path_buf = NULL;
		GtkWidget *image = NULL;
		GaimConnection *gc = NULL;
		GaimAccount *account = NULL;
		GaimStatus *status = NULL;
		GdkPixbuf *pixbuf = NULL;

		account = accounts->data;
		accel_group = gtk_menu_get_accel_group(GTK_MENU(accountmenu));

		if(gaim_account_get_enabled(account, PIDGIN_UI)) {
			buf = g_strconcat(gaim_account_get_username(account), " (",
					gaim_account_get_protocol_name(account), ")", NULL);
			menuitem = gtk_image_menu_item_new_with_label(buf);
			accel_path_buf = g_strconcat(N_("<GaimMain>/Accounts/"), buf, NULL);
			g_free(buf);
			status = gaim_account_get_active_status(account);
			pixbuf = pidgin_create_prpl_icon_with_status(account, gaim_status_get_type(status), 0.5);
			if (pixbuf != NULL)
			{
				if (!gaim_account_is_connected(account))
					gdk_pixbuf_saturate_and_pixelate(pixbuf, pixbuf,
							0.0, FALSE);
				image = gtk_image_new_from_pixbuf(pixbuf);
				g_object_unref(G_OBJECT(pixbuf));
				gtk_widget_show(image);
				gtk_image_menu_item_set_image(GTK_IMAGE_MENU_ITEM(menuitem), image);
			}
			gtk_menu_shell_append(GTK_MENU_SHELL(accountmenu), menuitem);
			gtk_widget_show(menuitem);

			submenu = gtk_menu_new();
			gtk_menu_set_accel_group(GTK_MENU(submenu), accel_group);
			gtk_menu_set_accel_path(GTK_MENU(submenu), accel_path_buf);
			g_free(accel_path_buf);
			gtk_menu_item_set_submenu(GTK_MENU_ITEM(menuitem), submenu);
			gtk_widget_show(submenu);


			menuitem = gtk_menu_item_new_with_mnemonic(_("_Edit Account"));
			g_signal_connect(G_OBJECT(menuitem), "activate",
					G_CALLBACK(modify_account_cb), account);
			gtk_menu_shell_append(GTK_MENU_SHELL(submenu), menuitem);
			gtk_widget_show(menuitem);

			pidgin_separator(submenu);

			gc = gaim_account_get_connection(account);
			if (gc && GAIM_CONNECTION_IS_CONNECTED(gc)) {
				GaimPlugin *plugin = NULL;

				plugin = gc->prpl;
				if (GAIM_PLUGIN_HAS_ACTIONS(plugin)) {
					GList *l, *ll = NULL;
					GaimPluginAction *action = NULL;

					for (l = ll = GAIM_PLUGIN_ACTIONS(plugin, gc); l; l = l->next) {
						if (l->data) {
							action = (GaimPluginAction *)l->data;
							action->plugin = plugin;
							action->context = gc;

							menuitem = gtk_menu_item_new_with_label(action->label);
							gtk_menu_shell_append(GTK_MENU_SHELL(submenu), menuitem);
							g_signal_connect(G_OBJECT(menuitem), "activate",
									G_CALLBACK(plugin_act), action);
							g_object_set_data_full(G_OBJECT(menuitem), "plugin_action", action, (GDestroyNotify)gaim_plugin_action_free);
							gtk_widget_show(menuitem);
						} else
							pidgin_separator(submenu);
					}
				} else {
					menuitem = gtk_menu_item_new_with_label(_("No actions available"));
					gtk_menu_shell_append(GTK_MENU_SHELL(submenu), menuitem);
					gtk_widget_set_sensitive(menuitem, FALSE);
					gtk_widget_show(menuitem);
				}
			} else {
				menuitem = gtk_menu_item_new_with_label(_("No actions available"));
				gtk_menu_shell_append(GTK_MENU_SHELL(submenu), menuitem);
				gtk_widget_set_sensitive(menuitem, FALSE);
				gtk_widget_show(menuitem);
			}

			pidgin_separator(submenu);

			menuitem = gtk_menu_item_new_with_mnemonic(_("_Disable"));
			g_signal_connect(G_OBJECT(menuitem), "activate",
					G_CALLBACK(disable_account_cb), account);
			gtk_menu_shell_append(GTK_MENU_SHELL(submenu), menuitem);
			gtk_widget_show(menuitem);
		} else {
			disabled_accounts = TRUE;
		}
	}

	if(disabled_accounts) {
		pidgin_separator(accountmenu);
		menuitem = gtk_menu_item_new_with_label(_("Enable Account"));
		gtk_menu_shell_append(GTK_MENU_SHELL(accountmenu), menuitem);
		gtk_widget_show(menuitem);

		submenu = gtk_menu_new();
		gtk_menu_set_accel_group(GTK_MENU(submenu), accel_group);
		gtk_menu_set_accel_path(GTK_MENU(submenu), N_("<GaimMain>/Accounts/Enable Account"));
		gtk_menu_item_set_submenu(GTK_MENU_ITEM(menuitem), submenu);
		gtk_widget_show(submenu);

		for (accounts = gaim_accounts_get_all(); accounts; accounts = accounts->next) {
			char *buf = NULL;
			GtkWidget *image = NULL;
			GaimAccount *account = NULL;
			GdkPixbuf *pixbuf = NULL;

			account = accounts->data;

			if(!gaim_account_get_enabled(account, PIDGIN_UI)) {

				disabled_accounts = TRUE;

				buf = g_strconcat(gaim_account_get_username(account), " (",
						gaim_account_get_protocol_name(account), ")", NULL);
				menuitem = gtk_image_menu_item_new_with_label(buf);
				g_free(buf);
				pixbuf = pidgin_create_prpl_icon(account, PIDGIN_PRPL_ICON_SMALL);
				if (pixbuf != NULL)
				{
					if (!gaim_account_is_connected(account))
						gdk_pixbuf_saturate_and_pixelate(pixbuf, pixbuf, 0.0, FALSE);
					image = gtk_image_new_from_pixbuf(pixbuf);
					g_object_unref(G_OBJECT(pixbuf));
					gtk_widget_show(image);
					gtk_image_menu_item_set_image(GTK_IMAGE_MENU_ITEM(menuitem), image);
				}
				g_signal_connect(G_OBJECT(menuitem), "activate",
						G_CALLBACK(enable_account_cb), account);
				gtk_menu_shell_append(GTK_MENU_SHELL(submenu), menuitem);
				gtk_widget_show(menuitem);
			}
		}
	}
}

static GList *plugin_submenus = NULL;

void
pidgin_blist_update_plugin_actions(void)
{
	GtkWidget *menuitem, *submenu;
	GaimPlugin *plugin = NULL;
	GList *l;
	GtkAccelGroup *accel_group;

	GtkWidget *pluginmenu = gtk_item_factory_get_widget(gtkblist->ift, N_("/Tools"));

	g_return_if_fail(pluginmenu != NULL);

	/* Remove old plugin action submenus from the Tools menu */
	for (l = plugin_submenus; l; l = l->next)
		gtk_widget_destroy(GTK_WIDGET(l->data));
	g_list_free(plugin_submenus);
	plugin_submenus = NULL;

	accel_group = gtk_menu_get_accel_group(GTK_MENU(pluginmenu));

	/* Add a submenu for each plugin with custom actions */
	for (l = gaim_plugins_get_loaded(); l; l = l->next) {
		char *path;

		plugin = (GaimPlugin *) l->data;

		if (GAIM_IS_PROTOCOL_PLUGIN(plugin))
			continue;

		if (!GAIM_PLUGIN_HAS_ACTIONS(plugin))
			continue;

		menuitem = gtk_image_menu_item_new_with_label(_(plugin->info->name));
		gtk_menu_shell_append(GTK_MENU_SHELL(pluginmenu), menuitem);
		gtk_widget_show(menuitem);

		plugin_submenus = g_list_append(plugin_submenus, menuitem);

		submenu = gtk_menu_new();
		gtk_menu_item_set_submenu(GTK_MENU_ITEM(menuitem), submenu);
		gtk_widget_show(submenu);

		gtk_menu_set_accel_group(GTK_MENU(submenu), accel_group);
		path = g_strdup_printf("%s/Tools/%s", gtkblist->ift->path, plugin->info->name);
		gtk_menu_set_accel_path(GTK_MENU(submenu), path);
		g_free(path);

		build_plugin_actions(submenu, plugin);
	}
}

static void
sortmethod_act(GtkCheckMenuItem *checkmenuitem, char *id)
{
	if (gtk_check_menu_item_get_active(checkmenuitem))
	{
		pidgin_set_cursor(gtkblist->window, GDK_WATCH);
		/* This is redundant. I think. */
		/* pidgin_blist_sort_method_set(id); */
		gaim_prefs_set_string("/gaim/gtk/blist/sort_type", id);

		pidgin_clear_cursor(gtkblist->window);
	}
}

void
pidgin_blist_update_sort_methods(void)
{
	GtkWidget *menuitem = NULL, *activeitem = NULL;
	PidginBlistSortMethod *method = NULL;
	GList *l;
	GSList *sl = NULL;
	GtkWidget *sortmenu;
	const char *m = gaim_prefs_get_string("/gaim/gtk/blist/sort_type");

	if ((gtkblist == NULL) || (gtkblist->ift == NULL))
		return;

	sortmenu = gtk_item_factory_get_widget(gtkblist->ift, N_("/Buddies/Sort Buddies"));

	if (sortmenu == NULL)
		return;

	/* Clear the old menu */
	for (l = gtk_container_get_children(GTK_CONTAINER(sortmenu)); l; l = l->next) {
		menuitem = l->data;
		gtk_widget_destroy(GTK_WIDGET(menuitem));
	}

	for (l = pidgin_blist_sort_methods; l; l = l->next) {
		method = (PidginBlistSortMethod *) l->data;
		menuitem = gtk_radio_menu_item_new_with_label(sl, _(method->name));
		if (!strcmp(m, method->id))
			activeitem = menuitem;
		sl = gtk_radio_menu_item_get_group(GTK_RADIO_MENU_ITEM(menuitem));
		gtk_menu_shell_append(GTK_MENU_SHELL(sortmenu), menuitem);
		g_signal_connect(G_OBJECT(menuitem), "toggled",
				 G_CALLBACK(sortmethod_act), method->id);
		gtk_widget_show(menuitem);
	}
	if (activeitem)
		gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(activeitem), TRUE);
}
