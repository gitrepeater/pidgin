gestures_SOURCES = [
	'gestures.c',
	'gstroke.h',
	'gstroke-internal.h',
	'stroke.c',
	'stroke-draw.c'
]

if PLUGINS
	gestures = library('gestures', gestures_SOURCES,
	    build_by_default: false,
	    dependencies : [x11, libpurple_dep, libpidgin_dep, glib],
	    name_prefix : '',
	    install : false, install_dir : PIDGIN_PLUGINDIR)
endif
