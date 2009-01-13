/**
 * @file media.c Account API
 * @ingroup core
 *
 * Pidgin
 *
 * Pidgin is the legal property of its developers, whose names are too numerous
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
 */

#include <string.h>
#include "debug.h"
#include "internal.h"
#include "connection.h"
#include "media.h"
#include "pidgin.h"

#include "gtkmedia.h"

#ifdef USE_VV

#include <gst/interfaces/xoverlay.h>

typedef enum
{
	/* Waiting for response */
	PIDGIN_MEDIA_WAITING = 1,
	/* Got request */
	PIDGIN_MEDIA_REQUESTED,
	/* Accepted call */
	PIDGIN_MEDIA_ACCEPTED,
	/* Rejected call */
	PIDGIN_MEDIA_REJECTED,
} PidginMediaState;

struct _PidginMediaPrivate
{
	PurpleMedia *media;
	gchar *screenname;
	GstElement *send_level;
	GstElement *recv_level;

	GtkWidget *calling;
	GtkWidget *accept;
	GtkWidget *reject;
	GtkWidget *hangup;
	GtkWidget *mute;

	GtkWidget *send_progress;
	GtkWidget *recv_progress;

	PidginMediaState state;

	GtkWidget *display;
	GtkWidget *send_widget;
	GtkWidget *recv_widget;
	GtkWidget *local_video;
	GtkWidget *remote_video;
};

#define PIDGIN_MEDIA_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), PIDGIN_TYPE_MEDIA, PidginMediaPrivate))

static void pidgin_media_class_init (PidginMediaClass *klass);
static void pidgin_media_init (PidginMedia *media);
static void pidgin_media_dispose (GObject *object);
static void pidgin_media_finalize (GObject *object);
static void pidgin_media_get_property (GObject *object, guint prop_id, GValue *value, GParamSpec *pspec);
static void pidgin_media_set_property (GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec);
static void pidgin_media_set_state(PidginMedia *gtkmedia, PidginMediaState state);

static GtkHBoxClass *parent_class = NULL;



enum {
	MESSAGE,
	ERROR,
	LAST_SIGNAL
};
static guint pidgin_media_signals[LAST_SIGNAL] = {0};

enum {
	PROP_0,
	PROP_MEDIA,
	PROP_SCREENNAME,
	PROP_SEND_LEVEL,
	PROP_RECV_LEVEL
};

GType
pidgin_media_get_type(void)
{
	static GType type = 0;

	if (type == 0) {
		static const GTypeInfo info = {
			sizeof(PidginMediaClass),
			NULL,
			NULL,
			(GClassInitFunc) pidgin_media_class_init,
			NULL,
			NULL,
			sizeof(PidginMedia),
			0,
			(GInstanceInitFunc) pidgin_media_init,
			NULL
		};
		type = g_type_register_static(GTK_TYPE_HBOX, "PidginMedia", &info, 0);
	}
	return type;
}


static void
pidgin_media_class_init (PidginMediaClass *klass)
{
	GObjectClass *gobject_class = (GObjectClass*)klass;
/*	GtkContainerClass *container_class = (GtkContainerClass*)klass; */
	parent_class = g_type_class_peek_parent(klass);

	gobject_class->dispose = pidgin_media_dispose;
	gobject_class->finalize = pidgin_media_finalize;
	gobject_class->set_property = pidgin_media_set_property;
	gobject_class->get_property = pidgin_media_get_property;

	g_object_class_install_property(gobject_class, PROP_MEDIA,
			g_param_spec_object("media",
			"PurpleMedia",
			"The PurpleMedia associated with this media.",
			PURPLE_TYPE_MEDIA,
			G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE));
	g_object_class_install_property(gobject_class, PROP_SCREENNAME,
			g_param_spec_string("screenname",
			"Screenname",
			"The screenname of the user this session is with.",
			NULL,
			G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE));
	g_object_class_install_property(gobject_class, PROP_SEND_LEVEL,
			g_param_spec_object("send-level",
			"Send level",
			"The GstElement of this media's send 'level'",
			GST_TYPE_ELEMENT,
			G_PARAM_READWRITE));
	g_object_class_install_property(gobject_class, PROP_RECV_LEVEL,
			g_param_spec_object("recv-level",
			"Receive level",
			"The GstElement of this media's recv 'level'",
			GST_TYPE_ELEMENT,
			G_PARAM_READWRITE));

	pidgin_media_signals[MESSAGE] = g_signal_new("message", G_TYPE_FROM_CLASS(klass),
					G_SIGNAL_RUN_LAST, 0, NULL, NULL,
					g_cclosure_marshal_VOID__STRING,
					G_TYPE_NONE, 1, G_TYPE_STRING);
	pidgin_media_signals[ERROR] = g_signal_new("error", G_TYPE_FROM_CLASS(klass),
					G_SIGNAL_RUN_LAST, 0, NULL, NULL,
					g_cclosure_marshal_VOID__STRING,
					G_TYPE_NONE, 1, G_TYPE_STRING);

	g_type_class_add_private(klass, sizeof(PidginMediaPrivate));
}

static void
pidgin_media_mute_toggled(GtkToggleButton *toggle, PidginMedia *media)
{
	purple_media_mute(media->priv->media,
			gtk_toggle_button_get_active(toggle));
}

static void
pidgin_media_init (PidginMedia *media)
{
	media->priv = PIDGIN_MEDIA_GET_PRIVATE(media);
	media->priv->calling = gtk_label_new("Calling...");
	media->priv->hangup = gtk_button_new_with_mnemonic("_Hangup");
	media->priv->accept = gtk_button_new_with_mnemonic("_Accept");
	media->priv->reject = gtk_button_new_with_mnemonic("_Reject");
	media->priv->mute = gtk_toggle_button_new_with_mnemonic("_Mute");

	g_signal_connect(media->priv->mute, "toggled",
			G_CALLBACK(pidgin_media_mute_toggled), media);

	gtk_box_pack_start(GTK_BOX(media), media->priv->calling, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(media), media->priv->hangup, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(media), media->priv->accept, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(media), media->priv->reject, FALSE, FALSE, 0);
	gtk_box_pack_end(GTK_BOX(media), media->priv->mute, FALSE, FALSE, 0);

	gtk_widget_show_all(media->priv->accept);
	gtk_widget_show_all(media->priv->reject);

	media->priv->display = gtk_vbox_new(TRUE, PIDGIN_HIG_BOX_SPACE);
}

static gboolean
level_message_cb(GstBus *bus, GstMessage *message, PidginMedia *gtkmedia)
{
	const GstStructure *s;
	gchar *name;

	gdouble rms_db;
	gdouble percent;
	const GValue *list;
	const GValue *value;

	GstElement *src = GST_ELEMENT(GST_MESSAGE_SRC(message));

	if (message->type != GST_MESSAGE_ELEMENT)
		return TRUE;

	s = gst_message_get_structure(message);

	if (strcmp(gst_structure_get_name(s), "level"))
		return TRUE;

	list = gst_structure_get_value(s, "rms");

	/* Only bother with the first channel. */
	value = gst_value_list_get_value(list, 0);
	rms_db = g_value_get_double(value);

	percent = pow(10, rms_db / 20) * 5;

	if(percent > 1.0)
		percent = 1.0;

	name = gst_element_get_name(src);
	if (!strcmp(name, "sendlevel"))	
		gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(gtkmedia->priv->send_progress), percent);
	else
		gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(gtkmedia->priv->recv_progress), percent);

	g_free(name);
	return TRUE;
}


static void
pidgin_media_disconnect_levels(PurpleMedia *media, PidginMedia *gtkmedia)
{
	GstElement *element = purple_media_get_pipeline(media);
	gulong handler_id = g_signal_handler_find(G_OBJECT(gst_pipeline_get_bus(GST_PIPELINE(element))),
						  G_SIGNAL_MATCH_FUNC | G_SIGNAL_MATCH_DATA, 0, 0, 
						  NULL, G_CALLBACK(level_message_cb), gtkmedia);
	if (handler_id)
		g_signal_handler_disconnect(G_OBJECT(gst_pipeline_get_bus(GST_PIPELINE(element))),
					    handler_id);
}

static void
pidgin_media_dispose(GObject *media)
{
	PidginMedia *gtkmedia = PIDGIN_MEDIA(media);
	purple_debug_info("gtkmedia", "pidgin_media_dispose\n");

	if (gtkmedia->priv->media) {
		GstElement *videosendbin = NULL, *videorecvbin = NULL;

		purple_media_get_elements(gtkmedia->priv->media, NULL, NULL,
					  &videosendbin, &videorecvbin);

		if (videorecvbin) {
			gst_element_set_locked_state(videorecvbin, TRUE);
			gst_element_set_state(videorecvbin, GST_STATE_NULL);
		}
		if (videosendbin) {
			gst_element_set_locked_state(videosendbin, TRUE);
			gst_element_set_state(videosendbin, GST_STATE_NULL);
		}

		pidgin_media_disconnect_levels(gtkmedia->priv->media, gtkmedia);
		g_object_unref(gtkmedia->priv->media);
		gtkmedia->priv->media = NULL;
	}

	if (gtkmedia->priv->send_level) {
		gst_object_unref(gtkmedia->priv->send_level);
		gtkmedia->priv->send_level = NULL;
	}

	if (gtkmedia->priv->recv_level) {
		gst_object_unref(gtkmedia->priv->recv_level);
		gtkmedia->priv->recv_level = NULL;
	}

	G_OBJECT_CLASS(parent_class)->dispose(media);
}

static void
pidgin_media_finalize(GObject *media)
{
	PidginMedia *gtkmedia = PIDGIN_MEDIA(media);
	purple_debug_info("gtkmedia", "pidgin_media_finalize\n");

	if (gtkmedia->priv->display) {
		gtk_widget_destroy(gtkmedia->priv->display);
		gtkmedia->priv->display = NULL;
	}

	G_OBJECT_CLASS(parent_class)->finalize(media);
}

static void
pidgin_media_emit_message(PidginMedia *gtkmedia, const char *msg)
{
	g_signal_emit(gtkmedia, pidgin_media_signals[MESSAGE], 0, msg);
}

GtkWidget *
pidgin_media_get_display_widget(GtkWidget *gtkmedia)
{
	return PIDGIN_MEDIA_GET_PRIVATE(gtkmedia)->display;
}

static gboolean
create_window (GstBus *bus, GstMessage *message, PidginMedia *gtkmedia)
{
	char *name;

	if (GST_MESSAGE_TYPE(message) != GST_MESSAGE_ELEMENT)
		return TRUE;

	if (!gst_structure_has_name(message->structure, "prepare-xwindow-id"))
		return TRUE;

	name = gst_object_get_name(GST_MESSAGE_SRC (message));

	/* The XOverlay's name is the sink's name with a suffix */
	if (!strncmp(name, "purplevideosink", strlen("purplevideosink")))
		gst_x_overlay_set_xwindow_id(GST_X_OVERLAY(GST_MESSAGE_SRC(message)),
					     GDK_WINDOW_XWINDOW(gtkmedia->priv->remote_video->window));
	else if (!strncmp(name, "purplelocalvideosink", strlen("purplelocalvideosink")))
		gst_x_overlay_set_xwindow_id(GST_X_OVERLAY(GST_MESSAGE_SRC(message)),
					     GDK_WINDOW_XWINDOW(gtkmedia->priv->local_video->window));
	g_free(name);
	return TRUE;
}

static gboolean
realize_cb_cb(GstElement *element)
{
	gst_element_set_locked_state(element, FALSE);
	gst_element_set_state(element, GST_STATE_PLAYING);
	return FALSE;
}

static void
realize_cb(GtkWidget *widget, GstElement *element)
{
	g_timeout_add(0, (GSourceFunc)realize_cb_cb, element);
}

static void
pidgin_media_error_cb(PidginMedia *media, const char *error, PidginMedia *gtkmedia)
{
	g_signal_emit(gtkmedia, pidgin_media_signals[ERROR], 0, error);
}

static gboolean
plug_delete_event_cb(GtkWidget *widget, gpointer data)
{
	return TRUE;
}

static gboolean
plug_removed_cb(GtkWidget *widget, gpointer data)
{
	return TRUE;
}

static void
socket_realize_cb(GtkWidget *widget, gpointer data)
{
	gtk_socket_add_id(GTK_SOCKET(widget),
			gtk_plug_get_id(GTK_PLUG(data)));
}

static void
pidgin_media_ready_cb(PurpleMedia *media, PidginMedia *gtkmedia, const gchar *sid)
{
	GstElement *pipeline = purple_media_get_pipeline(media);
	GtkWidget *send_widget = NULL, *recv_widget = NULL;
	GstElement *audiosendbin = NULL, *audiosendlevel = NULL;
	GstElement *videosendbin = NULL;
	gboolean audiorecvbool = FALSE;
	gboolean videorecvbool = FALSE;
	GstBus *bus;
	gboolean is_initiator;

	PurpleMediaSessionType type = purple_media_get_session_type(media, sid);
	if (type & PURPLE_MEDIA_AUDIO) {
		if (!audiosendbin && (type & PURPLE_MEDIA_SEND_AUDIO)) {
			purple_media_audio_init_src(&audiosendbin, &audiosendlevel);
			purple_media_set_src(media, sid, audiosendbin);
			gst_element_set_state(audiosendbin, GST_STATE_PLAYING);
		}
		if (!audiorecvbool && (type & PURPLE_MEDIA_RECV_AUDIO)) {
			audiorecvbool = TRUE;
		}
	} else if (type & PURPLE_MEDIA_VIDEO) {
		if (!videosendbin && (type & PURPLE_MEDIA_SEND_VIDEO)) {
			purple_media_video_init_src(&videosendbin);
			gst_element_set_locked_state(videosendbin, TRUE);
			purple_media_set_src(media, sid, videosendbin);
		}
		if (!videorecvbool && (type & PURPLE_MEDIA_RECV_VIDEO)) {
			videorecvbool = TRUE;
		}
	}

	if (gtkmedia->priv->recv_widget == NULL
			&& (videorecvbool || audiorecvbool)) {
		recv_widget = gtk_hbox_new(FALSE, PIDGIN_HIG_BOX_SPACE);	
		gtk_box_pack_start(GTK_BOX(gtkmedia->priv->display),
				recv_widget, TRUE, TRUE, 0);
		gtk_widget_show(recv_widget);
	} else
		recv_widget = gtkmedia->priv->recv_widget;
	if (gtkmedia->priv->send_widget == NULL
			&& (videosendbin || audiosendbin)) {
		send_widget = gtk_hbox_new(FALSE, PIDGIN_HIG_BOX_SPACE);
		gtk_box_pack_start(GTK_BOX(gtkmedia->priv->display),
				send_widget, TRUE, TRUE, 0);
		gtk_widget_show(send_widget);
	} else
		send_widget = gtkmedia->priv->send_widget;

	if (videorecvbool) {
		GtkWidget *aspect;
		GtkWidget *remote_video;
		GtkWidget *plug;
		GtkWidget *socket;

		aspect = gtk_aspect_frame_new(NULL, 0.5, 0.5, 4.0/3.0, FALSE);
		gtk_frame_set_shadow_type(GTK_FRAME(aspect), GTK_SHADOW_IN);
		gtk_box_pack_start(GTK_BOX(recv_widget), aspect, TRUE, TRUE, 0);

		plug = gtk_plug_new(0);
		g_signal_connect(G_OBJECT(plug), "delete-event",
				G_CALLBACK(plug_delete_event_cb), plug);
		gtk_widget_show(plug);

		socket = gtk_socket_new();
		g_signal_connect(G_OBJECT(socket), "realize",
				G_CALLBACK(socket_realize_cb), plug);
		g_signal_connect(G_OBJECT(socket), "plug-removed",
				G_CALLBACK(plug_removed_cb), NULL);
		gtk_container_add(GTK_CONTAINER(aspect), socket);
		gtk_widget_show(socket);

		remote_video = gtk_drawing_area_new();
		gtk_container_add(GTK_CONTAINER(plug), remote_video);
		gtk_widget_set_size_request (GTK_WIDGET(remote_video), 100, -1);
		gtk_widget_show(remote_video);
		gtk_widget_show(aspect);

		gtkmedia->priv->remote_video = remote_video;
	}
	if (videosendbin) {
		GtkWidget *aspect;
		GtkWidget *local_video;
		GtkWidget *plug;
		GtkWidget *socket;

		aspect = gtk_aspect_frame_new(NULL, 0.5, 0.5, 4.0/3.0, FALSE);
		gtk_frame_set_shadow_type(GTK_FRAME(aspect), GTK_SHADOW_IN);
		gtk_box_pack_start(GTK_BOX(send_widget), aspect, TRUE, TRUE, 0);

		plug = gtk_plug_new(0);
		g_signal_connect(G_OBJECT(plug), "delete-event",
				G_CALLBACK(plug_delete_event_cb), plug);
		gtk_widget_show(plug);

		socket = gtk_socket_new();
		g_signal_connect(G_OBJECT(socket), "realize",
				G_CALLBACK(socket_realize_cb), plug);
		g_signal_connect(G_OBJECT(socket), "plug-removed",
				G_CALLBACK(plug_removed_cb), NULL);
		gtk_container_add(GTK_CONTAINER(aspect), socket);
		gtk_widget_show(socket);

		local_video = gtk_drawing_area_new();
		g_signal_connect(G_OBJECT(local_video), "realize",
				G_CALLBACK(realize_cb), videosendbin);
		gtk_container_add(GTK_CONTAINER(plug), local_video);
		gtk_widget_set_size_request (GTK_WIDGET(local_video), 100, -1);

		gtk_widget_show(local_video);
		gtk_widget_show(aspect);

		gtkmedia->priv->local_video = local_video;
	}

	if (audiorecvbool) {
		gtkmedia->priv->recv_progress = gtk_progress_bar_new();
		gtk_widget_set_size_request(gtkmedia->priv->recv_progress, 10, 70);
		gtk_progress_bar_set_orientation(GTK_PROGRESS_BAR(gtkmedia->priv->recv_progress),
						 GTK_PROGRESS_BOTTOM_TO_TOP);
		gtk_box_pack_end(GTK_BOX(recv_widget),
				   gtkmedia->priv->recv_progress, FALSE, FALSE, 0);
		gtk_widget_show(gtkmedia->priv->recv_progress);
	}
	if (audiosendbin) {
		gtkmedia->priv->send_progress = gtk_progress_bar_new();
		gtk_widget_set_size_request(gtkmedia->priv->send_progress, 10, 70);
		gtk_progress_bar_set_orientation(GTK_PROGRESS_BAR(gtkmedia->priv->send_progress),
						 GTK_PROGRESS_BOTTOM_TO_TOP);
		gtk_box_pack_end(GTK_BOX(send_widget),
				   gtkmedia->priv->send_progress, FALSE, FALSE, 0);
		gtk_widget_show(gtkmedia->priv->send_progress);

		gtk_widget_show(gtkmedia->priv->mute);
	}

	bus = gst_pipeline_get_bus(GST_PIPELINE(pipeline));

	if (videorecvbool || videosendbin)
		g_signal_connect(bus, "sync-message::element",
				G_CALLBACK(create_window), gtkmedia);

	if (audiorecvbool || audiosendbin)
		g_signal_connect(G_OBJECT(bus), "message::element",
				G_CALLBACK(level_message_cb), gtkmedia);

	gst_object_unref(bus);

	if (send_widget != NULL)
		gtkmedia->priv->send_widget = send_widget;
	if (recv_widget != NULL)
		gtkmedia->priv->recv_widget = recv_widget;

	g_object_get(G_OBJECT(media), "initiator", &is_initiator, NULL);

	if (is_initiator == FALSE) {
		gchar *message;
		if (type & PURPLE_MEDIA_AUDIO && type & PURPLE_MEDIA_VIDEO) {
			message = g_strdup_printf(_("%s wishes to start an audio/video session with you."),
						  gtkmedia->priv->screenname);
		} else if (type & PURPLE_MEDIA_AUDIO) {
			message = g_strdup_printf(_("%s wishes to start an audio session with you."),
						  gtkmedia->priv->screenname);
		} else if (type & PURPLE_MEDIA_VIDEO) {
			message = g_strdup_printf(_("%s wishes to start a video session with you."),
						  gtkmedia->priv->screenname);
		}
		pidgin_media_emit_message(gtkmedia, message);
		g_free(message);
	}
}

static void
pidgin_media_state_changed_cb(PurpleMedia *media,
		PurpleMediaStateChangedType type,
		gchar *sid, gchar *name, PidginMedia *gtkmedia)
{
	purple_debug_info("gtkmedia", "type: %d sid: %s name: %s\n",
			type, sid, name);
	if (sid == NULL && name == NULL) {
		if (type == PURPLE_MEDIA_STATE_CHANGED_END) {
			pidgin_media_emit_message(gtkmedia,
					_("The call has been terminated."));
			gtk_widget_destroy(GTK_WIDGET(gtkmedia));
			
		} else if (type == PURPLE_MEDIA_STATE_CHANGED_REJECTED) {
			pidgin_media_emit_message(gtkmedia,
					_("You have rejected the call."));
		}
	} else if (type == PURPLE_MEDIA_STATE_CHANGED_NEW &&
			sid != NULL && name != NULL) {
		pidgin_media_ready_cb(media, gtkmedia, sid);
	} else if (type == PURPLE_MEDIA_STATE_CHANGED_CONNECTED) {
		GstElement *audiosendbin = NULL, *audiorecvbin = NULL;
		GstElement *videosendbin = NULL, *videorecvbin = NULL;

		pidgin_media_emit_message(gtkmedia, _("Call in progress."));
		pidgin_media_set_state(gtkmedia, PIDGIN_MEDIA_ACCEPTED);

		purple_media_get_elements(media, &audiosendbin, &audiorecvbin,
					  &videosendbin, &videorecvbin);

		if (audiorecvbin || audiosendbin || videorecvbin || videosendbin)
			gtk_widget_show(gtkmedia->priv->display);
	}
}

static void
pidgin_media_set_property (GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec)
{
	PidginMedia *media;
	g_return_if_fail(PIDGIN_IS_MEDIA(object));

	media = PIDGIN_MEDIA(object);
	switch (prop_id) {
		case PROP_MEDIA:
		{
			gboolean initiator;
			if (media->priv->media)
				g_object_unref(media->priv->media);
			media->priv->media = g_value_get_object(value);
			g_object_ref(media->priv->media);

			g_object_get(G_OBJECT(media->priv->media),
					"initiator", &initiator, NULL);
			if (initiator == TRUE)
				pidgin_media_set_state(media, PIDGIN_MEDIA_WAITING);
			else
				pidgin_media_set_state(media, PIDGIN_MEDIA_REQUESTED);

			g_signal_connect_swapped(G_OBJECT(media->priv->accept), "clicked", 
				 G_CALLBACK(purple_media_accept), media->priv->media);
			g_signal_connect_swapped(G_OBJECT(media->priv->reject), "clicked",
				 G_CALLBACK(purple_media_reject), media->priv->media);
			g_signal_connect_swapped(G_OBJECT(media->priv->hangup), "clicked",
				 G_CALLBACK(purple_media_hangup), media->priv->media);

			g_signal_connect(G_OBJECT(media->priv->media), "error",
				G_CALLBACK(pidgin_media_error_cb), media);
			g_signal_connect(G_OBJECT(media->priv->media), "state-changed",
				G_CALLBACK(pidgin_media_state_changed_cb), media);
			break;
		}
		case PROP_SCREENNAME:
			if (media->priv->screenname)
				g_free(media->priv->screenname);
			media->priv->screenname = g_value_dup_string(value);
			break;
		case PROP_SEND_LEVEL:
			if (media->priv->send_level)
				gst_object_unref(media->priv->send_level);
			media->priv->send_level = g_value_get_object(value);
			g_object_ref(media->priv->send_level);
			break;
		case PROP_RECV_LEVEL:
			if (media->priv->recv_level)
				gst_object_unref(media->priv->recv_level);
			media->priv->recv_level = g_value_get_object(value);
			g_object_ref(media->priv->recv_level);
			break;
		default:
			G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
			break;
	}
}

static void
pidgin_media_get_property (GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
	PidginMedia *media;
	g_return_if_fail(PIDGIN_IS_MEDIA(object));

	media = PIDGIN_MEDIA(object);

	switch (prop_id) {
		case PROP_MEDIA:
			g_value_set_object(value, media->priv->media);
			break;
		case PROP_SCREENNAME:
			g_value_set_string(value, media->priv->screenname);
			break;
		case PROP_SEND_LEVEL:
			g_value_set_object(value, media->priv->send_level);
			break;
		case PROP_RECV_LEVEL:
			g_value_set_object(value, media->priv->recv_level);
			break;
		default:
			G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
			break;
	}
}

GtkWidget *
pidgin_media_new(PurpleMedia *media, const gchar *screenname)
{
	PidginMedia *gtkmedia = g_object_new(pidgin_media_get_type(),
					     "media", media,
					     "screenname", screenname, NULL);
	return GTK_WIDGET(gtkmedia);
}

static void
pidgin_media_set_state(PidginMedia *gtkmedia, PidginMediaState state)
{
	gtkmedia->priv->state = state;
	switch (state) {
		case PIDGIN_MEDIA_WAITING:
			gtk_widget_show(gtkmedia->priv->calling);
			gtk_widget_hide(gtkmedia->priv->accept);
			gtk_widget_hide(gtkmedia->priv->reject);
			gtk_widget_show(gtkmedia->priv->hangup);
			break;
		case PIDGIN_MEDIA_REQUESTED:
			gtk_widget_hide(gtkmedia->priv->calling);
			gtk_widget_show(gtkmedia->priv->accept);
			gtk_widget_show(gtkmedia->priv->reject);
			gtk_widget_hide(gtkmedia->priv->hangup);
			break;
		case PIDGIN_MEDIA_ACCEPTED:
			gtk_widget_show(gtkmedia->priv->hangup);
			gtk_widget_hide(gtkmedia->priv->calling);
			gtk_widget_hide(gtkmedia->priv->accept);
			gtk_widget_hide(gtkmedia->priv->reject);
			break;
		default:
			break;
	}
}

#endif  /* USE_VV */
