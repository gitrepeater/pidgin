/* gtkxcellrendererprogress.h
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
 *
 */
#ifndef _PIDGINCELLRENDERERPROGRESS_H_
#define _PIDGINCELLRENDERERPROGRESS_H_

#include <gtk/gtkcellrenderer.h>

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */


#define GAIM_TYPE_GTK_CELL_RENDERER_PROGRESS         (pidgin_cell_renderer_progress_get_type())
#define PIDGIN_CELL_RENDERER_PROGRESS(obj)         (G_TYPE_CHECK_INSTANCE_CAST((obj), GAIM_TYPE_GTK_CELL_RENDERER_PROGRESS, PidginCellRendererProgress))
#define PIDGIN_CELL_RENDERER_PROGRESS_CLASS(klass)	(G_TYPE_CHECK_CLASS_CAST ((klass), GAIM_TYPE_GTK_CELL_RENDERER_PROGRESS, PidginCellRendererProgressClass))
#define GAIM_IS_GTK_CELL_PROGRESS_PROGRESS(obj)		(G_TYPE_CHECK_INSTANCE_TYPE ((obj), GAIM_TYPE_GTK_CELL_RENDERER_PROGRESS))
#define GAIM_IS_GTK_CELL_PROGRESS_PROGRESS_CLASS(klass)	(G_TYPE_CHECK_CLASS_TYPE ((klass), GAIM_TYPE_GTK_CELL_RENDERER_PROGRESS))
#define PIDGIN_CELL_RENDERER_PROGRESS_GET_CLASS(obj)         (G_TYPE_INSTANCE_GET_CLASS ((obj), GAIM_TYPE_GTK_CELL_RENDERER_PROGRESS, PidginCellRendererProgressClass))

typedef struct _PidginCellRendererProgress PidginCellRendererProgress;
typedef struct _PidginCellRendererProgressClass PidginCellRendererProgressClass;

struct _PidginCellRendererProgress {
	GtkCellRenderer parent;

	gdouble progress;
	gchar *text;
	gboolean text_set;
};

struct _PidginCellRendererProgressClass {
	GtkCellRendererClass parent_class;
};

GType            pidgin_cell_renderer_progress_get_type     (void);
GtkCellRenderer  *pidgin_cell_renderer_progress_new          (void);


#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* _PIDGINCELLRENDERERPROGRESS_H_ */
