/**
 * @file gl_glx.c
 * @brief This file contains ALL Linux specific stuff having to do with the OpenGL refresh
 */

/*
Copyright (C) 1997-2001 Id Software, Inc.

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

*/


#include <termios.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/vt.h>
#include <signal.h>
#include <dlfcn.h>
#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/keysym.h>
#include <GL/glx.h>

#ifdef HAVE_DGA /* makefile */
#ifdef __x86_64__
#ifndef XMD_H
#define XMD_H
#endif
#endif
#include <X11/extensions/xf86dga.h>
#ifdef _XF86DGA_H_
#define HAVE_XF86_DGA
#endif
#endif /* HAVE_DGA */

#ifdef HAVE_VIDMODE /* makefile */
#include <X11/extensions/xf86vmode.h>
#ifdef _XF86VIDMODE_H_
#define HAVE_XF86_VIDMODE
#endif
#endif /* HAVE_VIDMODE */

/* include this after vidmode */
#include "../ref_gl/gl_local.h"
#include "../client/keys.h"

#include "rw_linux.h"
#include "glw_linux.h"

glwstate_t glw_state;

static Display *dpy = NULL;
static int scrnum;
static Window win;
static GLXContext ctx = NULL;
static Atom wmDeleteWindow;

#define KEY_MASK (KeyPressMask | KeyReleaseMask)
#define MOUSE_MASK (ButtonPressMask | ButtonReleaseMask | \
		    PointerMotionMask | ButtonMotionMask )
#define X_MASK (KEY_MASK | MOUSE_MASK | VisibilityChangeMask | StructureNotifyMask )

/*****************************************************************************/
/* MOUSE                                                                     */
/*****************************************************************************/

/* this is inside the renderer shared lib, so these are called from vid_so */

static bool_t	mouse_avail;
static int	mx, my;

static int	win_x, win_y;

static cvar_t	*m_filter;
static cvar_t	*in_mouse;
static cvar_t	*in_dgamouse;

static cvar_t	*r_fakeFullscreen;

#ifdef HAVE_XF86_VIDMODE
static XF86VidModeModeInfo **vidmodes;
static int num_vidmodes;
static XF86VidModeGamma oldgamma;
static bool_t vidmode_ext = false;
#endif /* HAVE_XF86_VIDMODE */

/* static int default_dotclock_vidmode; */
static bool_t vidmode_active = false;

/* static bool_t	mlooking; */

static bool_t mouse_active = false;
static bool_t dgamouse = false;

/* state struct passed in Init */
static in_state_t	*in_state;

static cvar_t *sensitivity;
static cvar_t *lookstrafe;
static cvar_t *m_side;
static cvar_t *m_yaw;
static cvar_t *m_pitch;
static cvar_t *m_forward;
static cvar_t *freelook;

/* stencilbuffer shadows */
bool_t have_stencil = false;

/**
 * @brief
 * @sa
 */
static Cursor CreateNullCursor(Display *display, Window root)
{
	Pixmap cursormask;
	XGCValues xgc;
	GC gc;
	XColor dummycolour;
	Cursor cursor;

	cursormask = XCreatePixmap(display, root, 1, 1, 1/*depth*/);
	xgc.function = GXclear;
	gc =  XCreateGC(display, cursormask, GCFunction, &xgc);
	XFillRectangle(display, cursormask, gc, 0, 0, 1, 1);
	dummycolour.pixel = 0;
	dummycolour.red = 0;
	dummycolour.flags = 04;
	cursor = XCreatePixmapCursor(display, cursormask, cursormask,
		&dummycolour,&dummycolour, 0,0);
	XFreePixmap(display,cursormask);
	XFreeGC(display,gc);
	return cursor;
}

/**
 * @brief
 * @sa uninstall_grabs
 */
static void install_grabs(void)
{
	/* inviso cursor */
	XDefineCursor(dpy, win, CreateNullCursor(dpy, win));

	if (vid_grabmouse->value && ! vid_fullscreen->value)
		XGrabPointer(dpy, win, True, 0, GrabModeAsync, GrabModeAsync, win, None, CurrentTime);

	if (! in_dgamouse->value && ( !vid_grabmouse->value || vid_fullscreen->value ) ) {
		XWarpPointer(dpy, None, win, 0, 0, 0, 0, 0, 0);
		sensitivity->value = 1;
	} else if (in_dgamouse->value) {
#ifdef HAVE_XF86_DGA
		int MajorVersion, MinorVersion;

		if (!XF86DGAQueryVersion(dpy, &MajorVersion, &MinorVersion)) {
			/* unable to query, probalby not supported */
			ri.Con_Printf( PRINT_ALL, "Failed to detect XF86DGA Mouse\n" );
			ri.Cvar_Set( "in_dgamouse", "0" );
		} else {
			dgamouse = true;
			ri.Con_Printf( PRINT_DEVELOPER, "...using XF86DGA Mouse\n" );
			XF86DGADirectVideo(dpy, DefaultScreen(dpy), XF86DGADirectMouse);
			XWarpPointer(dpy, None, win, 0, 0, 0, 0, 0, 0);
		}
#else
		ri.Con_Printf( PRINT_ALL, "...XF86DGA is not compiled in\n" );
		ri.Cvar_Set( "in_dgamouse", "0" );
		XWarpPointer(dpy, None, win, 0, 0, 0, 0, vid.width / 2, vid.height / 2);
#endif /* HAVE_XF86_DGA */
	} else
		XWarpPointer(dpy, None, win, 0, 0, 0, 0, vid.width / 2, vid.height / 2);

	if (vid_grabmouse->value || vid_fullscreen->value)
		XGrabKeyboard(dpy, win, False, GrabModeAsync, GrabModeAsync, CurrentTime);

	mouse_active = true;

	XSync(dpy, True);
}

/**
 * @brief
 * @sa install_grabs
 */
static void uninstall_grabs(void)
{
	if (!dpy || !win)
		return;

#ifdef HAVE_XF86_DGA
	if (dgamouse) {
		dgamouse = false;
		XF86DGADirectVideo(dpy, DefaultScreen(dpy), 0);
	}
#endif /* HAVE_XF86_DGA */

	XUngrabPointer(dpy, CurrentTime);
	XUngrabKeyboard(dpy, CurrentTime);

	/* inviso cursor */
	XUndefineCursor(dpy, win);

	mouse_active = false;
}

/**
 * @brief
 * @param in_state_p
 * @sa RW_IN_Shutdown
 */
void RW_IN_Init(in_state_t *in_state_p)
{
	in_state = in_state_p;

	/* mouse variables */
	m_filter = ri.Cvar_Get ("m_filter", "0", 0);
	in_mouse = ri.Cvar_Get ("in_mouse", "1", CVAR_ARCHIVE);
#ifdef HAVE_XF86_DGA
	in_dgamouse = ri.Cvar_Get ("in_dgamouse", "1", CVAR_ARCHIVE);
#else
	in_dgamouse = ri.Cvar_Get ("in_dgamouse", "0", CVAR_ARCHIVE);
#endif
	freelook = ri.Cvar_Get( "freelook", "0", 0 );
	lookstrafe = ri.Cvar_Get ("lookstrafe", "0", 0);
	sensitivity = ri.Cvar_Get ("sensitivity", "2", 0);
	m_pitch = ri.Cvar_Get ("m_pitch", "0.022", 0);

	m_yaw = ri.Cvar_Get ("m_yaw", "0.022", 0);
	m_forward = ri.Cvar_Get ("m_forward", "1", 0);
	m_side = ri.Cvar_Get ("m_side", "0.8", 0);

	mx = my = 0.0;
	mouse_avail = true;
}

/**
 * @brief
 * @sa RW_IN_Activate
 */
void RW_IN_Shutdown(void)
{
	RW_IN_Activate (false);

	if (mouse_avail)
		mouse_avail = false;
}

/**
 * @brief
 * @sa
 */
void RW_IN_Commands (void)
{
}

/**
 * @brief
 * @param x pointer to x coordinate
 * @param y pointer to y coordinate
 * @sa
 */
void RW_IN_GetMousePos (int *x, int *y)
{
	if ( mx < 1 )
		mx = 1;
	if ( my < 1 )
		my = 1;
	if ( mx >= vid.width )
		mx = vid.width - 1;
	if ( my >= vid.height )
		my = vid.height - 1;
	*x = mx;
	*y = my;
}

/**
 * @brief
 * @sa IN_ActivateMouse
 */
static void IN_DeactivateMouse( void )
{
	if (!mouse_avail || !dpy || !win)
		return;

	if (mouse_active) {
		uninstall_grabs();
		mouse_active = false;
	}
}

/**
 * @brief
 * @sa IN_DeactivateMouse
 */
static void IN_ActivateMouse( void )
{
	if (!mouse_avail || !dpy || !win)
		return;

	if (!mouse_active) {
		mx = vid.width / 2;
		my = vid.height / 2;
		install_grabs();
		mouse_active = true;
	}
}

/**
 * @brief
 * @sa
 */
void RW_IN_Frame (void)
{
}

/**
 * @brief
 * @sa RW_IN_Shutdown
 * @sa IN_ActivateMouse
 * @sa IN_DeactivateMouse
 */
void RW_IN_Activate(bool_t active)
{
	if (active || vidmode_active)
		IN_ActivateMouse();
	else
		IN_DeactivateMouse ();
}

/*****************************************************************************/
/* KEYBOARD                                                                  */
/*****************************************************************************/

/**
 * @brief
 * @sa
 */
static int XLateKey(XKeyEvent *ev)
{
	int key = 0;
	char buf[64];
	KeySym keysym;

	XLookupString(ev, buf, sizeof buf, &keysym, 0);

	switch(keysym) {
	case XK_KP_Page_Up:
		key = K_KP_PGUP;
		break;
	case XK_Page_Up:
		key = K_PGUP;
		break;
	case XK_KP_Page_Down:
		key = K_KP_PGDN;
		break;
	case XK_Page_Down:
		key = K_PGDN;
		break;
	case XK_KP_Home:
		key = K_KP_HOME;
		break;
	case XK_Home:
		key = K_HOME;
		break;
	case XK_KP_End:
		key = K_KP_END;
		break;
	case XK_End:
		key = K_END;
		break;
	case XK_KP_Left:
		key = K_KP_LEFTARROW;
		break;
	case XK_Left:
		key = K_LEFTARROW;
		break;
	case XK_KP_Right:
		key = K_KP_RIGHTARROW;
		break;
	case XK_Right:
		key = K_RIGHTARROW;
		break;
	case XK_KP_Down:
		key = K_KP_DOWNARROW;
		break;
	case XK_Down:
		key = K_DOWNARROW;
		break;
	case XK_KP_Up:
		key = K_KP_UPARROW;
		break;
	case XK_Up:
		key = K_UPARROW;
		break;
	case XK_Escape:
		key = K_ESCAPE;
		break;
	case XK_KP_Enter:
		key = K_KP_ENTER;
		break;
	case XK_Return:
		key = K_ENTER;
		break;
	case XK_Tab:
		key = K_TAB;
		break;
	case XK_F1:
		key = K_F1;
		break;
	case XK_F2:
		key = K_F2;
		break;
	case XK_F3:
		key = K_F3;
		break;
	case XK_F4:
		key = K_F4;
		break;
	case XK_F5:
		key = K_F5;
		break;
	case XK_F6:
		key = K_F6;
		break;
	case XK_F7:
		key = K_F7;
		break;
	case XK_F8:
		key = K_F8;
		break;
	case XK_F9:
		key = K_F9;
		break;
	case XK_F10:
		key = K_F10;
		break;
	case XK_F11:
		key = K_F11;
		break;
	case XK_F12:
		key = K_F12;
		break;
	case XK_BackSpace:
		key = K_BACKSPACE;
		break;
	case XK_KP_Delete:
		key = K_KP_DEL;
		break;
	case XK_Delete:
		key = K_DEL;
		break;
	case XK_Pause:
		key = K_PAUSE;
		break;
	case XK_Shift_L:
	case XK_Shift_R:
		key = K_SHIFT;
		break;
	case XK_Execute:
	case XK_Control_L:
	case XK_Control_R:
		key = K_CTRL;
		break;
	case XK_Alt_L:
	case XK_Meta_L:
	case XK_Alt_R:
	case XK_Meta_R:
		key = K_ALT;
		break;
	case XK_KP_Begin:
		key = K_KP_5;
		break;
	case XK_Insert:
		key = K_INS;
		break;
	case XK_KP_Insert:
		key = K_KP_INS;
		break;
	case XK_KP_Multiply:
		key = '*';
		break;
	case XK_KP_Add:
		key = K_KP_PLUS;
		break;
	case XK_KP_Subtract:
		key = K_KP_MINUS;
		break;
	case XK_KP_Divide:
		key = K_KP_SLASH;
		break;

#if 0
	case XK_exclam: key = '1'; break;
	case XK_at: key = '2'; break;
	case XK_numbersign: key = '3'; break;
	case XK_dollar: key = '4'; break;
	case XK_percent: key = '5'; break;
	case XK_asciicircum: key = '6'; break;
	case XK_ampersand: key = '7'; break;
	case XK_asterisk: key = '8'; break;
	case XK_parenleft: key = '9'; break;
	case XK_parenright: key = '0'; break;

	case XK_space:
	case XK_KP_Space: key = K_SPACE; break;
	/* weird french keyboards .. */
	/* NOTE: console toggle is hardcoded in cl_keys.c, can't be unbound */
	/*   cleaner would be .. using hardware key codes instead of the key syms */
	/*   could also add a new K_KP_CONSOLE */
	case XK_twosuperior: key = '~'; break;

	case 0x05f: key = '-';break;/* [_] */
	case 0x02b: key = '=';break;/* [+] */
	case 0x07c: key = '\'';break;/* [|] */
	case 0x07d: key = '[';break;/* [}] */
	case 0x07b: key = ']';break;/* [{] */
	case 0x022: key = '\'';break;/* ["] */
	case 0x03a: key = ';';break;/* [:] */
	case 0x03f: key = '/';break;/* [?] */
	case 0x03e: key = '.';break;/* [>] */
	case 0x03c: key = ',';break;/* [<] */
#endif

	default:
		key = *(unsigned char*)buf;
		if (key >= 'A' && key <= 'Z')
			key = key - 'A' + 'a';
		if (key >= 1 && key <= 26) /* ctrl+alpha */
			key = key + 'a' - 1;
		break;
	}

	return key;
}


/**
 * @brief
 * @sa
 */
static void HandleEvents(void)
{
	XEvent event;
	int b, middlex, middley;
	bool_t dowarp = false;

	if (!dpy)
		return;

	middlex = vid.width / 2;
	middley = vid.height / 2;

	while (XPending(dpy)) {
		XNextEvent(dpy, &event);

		switch(event.type) {
		case KeyPress:
		case KeyRelease:
			if (in_state && in_state->Key_Event_fp)
				in_state->Key_Event_fp (XLateKey(&event.xkey), event.type == KeyPress);
			break;

		case MotionNotify:
			if (mouse_active) {
#ifdef HAVE_XF86_DGA
				if (dgamouse) {
					mx += (event.xmotion.x + win_x) * sensitivity->value;
					my += (event.xmotion.y + win_y) * sensitivity->value;
				}
				else
#endif /* HAVE_XF86_DGA */
				{
					int xoffset = event.xmotion.x - middlex;
					int yoffset = event.xmotion.y - middley;

					if (xoffset != 0 || yoffset != 0) {
						mx += xoffset;
						my += yoffset;

						XSelectInput(dpy, win, X_MASK & ~PointerMotionMask);
						XWarpPointer(dpy, None, win, 0, 0, 0, 0, middlex, middley);
						XSelectInput(dpy, win, X_MASK);
					}
				}
			}
			break;


		case ButtonPress:
			b=-1;
			if (event.xbutton.button == 1)
				b = 0;
			else if (event.xbutton.button == 2)
				b = 2;
			else if (event.xbutton.button == 3)
				b = 1;
			else if (event.xbutton.button == 4)
				in_state->Key_Event_fp (K_MWHEELUP, true);
			else if (event.xbutton.button == 5)
				in_state->Key_Event_fp (K_MWHEELDOWN, true);
			if (b>=0 && in_state && in_state->Key_Event_fp)
				in_state->Key_Event_fp (K_MOUSE1 + b, true);
			break;

		case ButtonRelease:
			b=-1;
			if (event.xbutton.button == 1)
				b = 0;
			else if (event.xbutton.button == 2)
				b = 2;
			else if (event.xbutton.button == 3)
				b = 1;
			else if (event.xbutton.button == 4)
				in_state->Key_Event_fp (K_MWHEELUP, false);
			else if (event.xbutton.button == 5)
				in_state->Key_Event_fp (K_MWHEELDOWN, false);
			if (b>=0 && in_state && in_state->Key_Event_fp)
				in_state->Key_Event_fp (K_MOUSE1 + b, false);
			break;

		case CreateNotify :
			win_x = event.xcreatewindow.x;
			win_y = event.xcreatewindow.y;
			break;

		case ConfigureNotify :
			win_x = event.xconfigure.x;
			win_y = event.xconfigure.y;
			break;

        case ClientMessage:
			if (event.xclient.data.l[0] == wmDeleteWindow)
				ri.Cmd_ExecuteText("quit", EXEC_NOW);
			break;

		case MapNotify:
			if( vid_grabmouse->value ){
				XGrabPointer( dpy, win, True, 0, GrabModeAsync,
					GrabModeAsync, win, None, CurrentTime);
			}
			break;

		case UnmapNotify:
			if( vid_grabmouse->value )
				XUngrabPointer( dpy, CurrentTime);
			break;

		case VisibilityNotify:
			/* invisible -> visible */
			break;
		}
	}

	if (vid_grabmouse->modified)
	{
		vid_grabmouse->modified = false;
		if ( ! vid_grabmouse->value )
		{
			XUngrabPointer(dpy, CurrentTime);
			ri.Con_Printf( PRINT_ALL, "Ungrab mouse\n" );
		}
		else
		{
			XGrabPointer(dpy, win, True, 0, GrabModeAsync, GrabModeAsync, win, None, CurrentTime);
			ri.Con_Printf( PRINT_ALL, "Grab mouse\n" );
		}
	}

	if (dowarp && vid_grabmouse->value)
		/* move the mouse to the window center again */
		XWarpPointer(dpy,None,win,0,0,0,0, vid.width/2,vid.height/2);
}

Key_Event_fp_t Key_Event_fp;

/**
 * @brief
 * @sa
 */
void KBD_Init(Key_Event_fp_t fp)
{
	Key_Event_fp = fp;
}

/**
 * @brief
 * @sa
 */
void KBD_Update(void)
{
	/* get events from x server */
	HandleEvents();
}

/**
 * @brief
 * @sa
 */
void KBD_Close(void)
{
}

bool_t GLimp_InitGL (void);

/**
 * @brief
 * @sa
 */
rserr_t GLimp_SetMode( unsigned *pwidth, unsigned *pheight, int mode, bool_t fullscreen )
{
	int width, height;
	int attrib[] = {
		GLX_RGBA,
		GLX_RED_SIZE, 1,
		GLX_GREEN_SIZE, 1,
		GLX_BLUE_SIZE, 1,
		GLX_DOUBLEBUFFER,
		GLX_DEPTH_SIZE, 1,
		GLX_STENCIL_SIZE, 1,
		None
	};
	int attrib_nostencil[] = {
		GLX_RGBA,
		GLX_RED_SIZE, 1,
		GLX_GREEN_SIZE, 1,
		GLX_BLUE_SIZE, 1,
		GLX_DOUBLEBUFFER,
		GLX_DEPTH_SIZE, 1,
		None
	};
	Window root;
	XVisualInfo *visinfo;
	XSetWindowAttributes attr;
	unsigned long mask;

#ifdef HAVE_XF86_VIDMODE
	int MajorVersion, MinorVersion;
	int i, best_fit, best_dist, dist, x, y;
#endif /* HAVE_XF86_VIDMODE */

	r_fakeFullscreen = ri.Cvar_Get( "r_fakeFullscreen", "0", CVAR_ARCHIVE);

	ri.Con_Printf( PRINT_ALL, "Initializing OpenGL display\n");

	if (fullscreen)
		ri.Con_Printf (PRINT_ALL, "...setting fullscreen mode %d:", mode );
	else
		ri.Con_Printf (PRINT_ALL, "...setting mode %d:", mode );

	if ( !ri.Vid_GetModeInfo( &width, &height, mode ) )
	{
		ri.Con_Printf( PRINT_ALL, " invalid mode\n" );
		return rserr_invalid_mode;
	}

	ri.Con_Printf( PRINT_ALL, " %d %d\n", width, height );

	/* destroy the existing window */
	GLimp_Shutdown ();

	/* Mesa VooDoo hacks */
	if (fullscreen)
		putenv("MESA_GLX_FX=fullscreen");
	else
		putenv("MESA_GLX_FX=window");

	if (!(dpy = XOpenDisplay(NULL))) {
		fprintf(stderr, "Error couldn't open the X display\n");
		return rserr_invalid_mode;
	}

	scrnum = DefaultScreen(dpy);
	root = RootWindow(dpy, scrnum);

#ifdef HAVE_XF86_VIDMODE
	/* Get video mode list */
	MajorVersion = MinorVersion = 0;
	if (!XF86VidModeQueryVersion(dpy, &MajorVersion, &MinorVersion)) {
		vidmode_ext = false;
	} else {
		ri.Con_Printf(PRINT_ALL, "Using XFree86-VidModeExtension Version %d.%d\n",
			MajorVersion, MinorVersion);
		vidmode_ext = true;
	}
#endif /* HAVE_XF86_VIDMODE */

	visinfo = qglXChooseVisual(dpy, scrnum, attrib);
	if (!visinfo) {
		fprintf(stderr, "Error couldn't get an RGB, Double-buffered, Stencil, Depth visual\n");
		visinfo = qglXChooseVisual(dpy, scrnum, attrib_nostencil);
	}

	if (!visinfo){
		fprintf(stderr, "Error couldn't get an RGB, Double-buffered, Depth visual\n");
		return rserr_invalid_mode;
	}

	gl_state.hwgamma = false;

	/* do some pantsness */
	if ( qglXGetConfig ) {
		int red_bits, blue_bits, green_bits, depth_bits, alpha_bits;

		qglXGetConfig(dpy, visinfo, GLX_RED_SIZE, &red_bits);
		qglXGetConfig(dpy, visinfo, GLX_BLUE_SIZE, &blue_bits);
		qglXGetConfig(dpy, visinfo, GLX_GREEN_SIZE, &green_bits);
		qglXGetConfig(dpy, visinfo, GLX_DEPTH_SIZE, &depth_bits);
		qglXGetConfig(dpy, visinfo, GLX_ALPHA_SIZE, &alpha_bits);

		ri.Con_Printf(PRINT_ALL, "I: got %d bits of red\n", red_bits);
		ri.Con_Printf(PRINT_ALL, "I: got %d bits of blue\n", blue_bits);
		ri.Con_Printf(PRINT_ALL, "I: got %d bits of green\n", green_bits);
		ri.Con_Printf(PRINT_ALL, "I: got %d bits of depth\n", depth_bits);
		ri.Con_Printf(PRINT_ALL, "I: got %d bits of alpha\n", alpha_bits);
	}

	/* stencilbuffer shadows */
	if ( qglXGetConfig ) {
		int stencil_bits;

		if (!qglXGetConfig(dpy, visinfo, GLX_STENCIL_SIZE, &stencil_bits)) {
			ri.Con_Printf(PRINT_ALL, "I: got %d bits of stencil\n", stencil_bits);
			if (stencil_bits >= 1) {
				have_stencil = true;
			}
		}
	} else {
		have_stencil = true;
	}

#ifdef HAVE_XF86_VIDMODE
	if (vidmode_ext) {	/* Get video mode list */
		MajorVersion = MinorVersion = 0;

		XF86VidModeGetAllModeLines(dpy, scrnum, &num_vidmodes, &vidmodes);

		/* Are we going fullscreen?  If so, let's change video mode */
		if (fullscreen && !r_fakeFullscreen->value) {
			best_dist = 9999999;
			best_fit = -1;

			for (i = 0; i < num_vidmodes; i++) {
				if (width > vidmodes[i]->hdisplay ||
					height > vidmodes[i]->vdisplay)
					continue;

				x = width - vidmodes[i]->hdisplay;
				y = height - vidmodes[i]->vdisplay;
				dist = (x * x) + (y * y);
				if (dist < best_dist) {
					best_dist = dist;
					best_fit = i;
				}
			}

			if (best_fit != -1) {
				/* change to the mode */
				XF86VidModeSwitchToMode(dpy, scrnum, vidmodes[best_fit]);
				XF86VidModeSetViewPort(dpy, scrnum, 0, 0);
				width = vidmodes[best_fit]->hdisplay;
				height = vidmodes[best_fit]->vdisplay;
				vidmode_active = true;

				if (XF86VidModeGetGamma(dpy, scrnum, &oldgamma)) {
					gl_state.hwgamma = true;
					/* We can not reliably detect hardware gamma
					   changes across software gamma calls, which
					   can reset the flag, so change it anyway */
					vid_gamma->modified = true;
					ri.Con_Printf( PRINT_ALL, "Using hardware gamma\n");
				}
			} else {
				fullscreen = false;
				vidmode_active = false;
			}
		}
	}
#endif /* HAVE_XF86_VIDMODE */

	/* window attributes */
	attr.background_pixel = 0;
	attr.border_pixel = 0;
	attr.colormap = XCreateColormap(dpy, root, visinfo->visual, AllocNone);
	attr.event_mask = X_MASK;
	if (vidmode_active) {
		mask = CWBackPixel | CWColormap | CWSaveUnder | CWBackingStore |
			CWEventMask | CWOverrideRedirect;
		attr.override_redirect = True;
		attr.backing_store = NotUseful;
		attr.save_under = False;
	} else
		mask = CWBackPixel | CWBorderPixel | CWColormap | CWEventMask;

	win = XCreateWindow(dpy, root, 0, 0, width, height,
						0, visinfo->depth, InputOutput,
						visinfo->visual, mask, &attr);
	XStoreName(dpy, win, "UFO:AI");

	wmDeleteWindow = XInternAtom(dpy, "WM_DELETE_WINDOW", False);
	XSetWMProtocols(dpy, win, &wmDeleteWindow, 1);

	XMapWindow(dpy, win);

#ifdef HAVE_XF86_VIDMODE
	if (vidmode_active) {
		XMoveWindow(dpy, win, 0, 0);
		XRaiseWindow(dpy, win);
		XWarpPointer(dpy, None, win, 0, 0, 0, 0, 0, 0);
	}
#endif /* HAVE_XF86_VIDMODE */

	XFlush(dpy);

	ctx = qglXCreateContext(dpy, visinfo, NULL, True);

	qglXMakeCurrent(dpy, win, ctx);

	*pwidth = width;
	*pheight = height;

	/* let the sound and input subsystems know about the new window */
	ri.Vid_NewWindow (width, height);

	qglXMakeCurrent(dpy, win, ctx);

	return rserr_ok;
}

/**
 * @brief
 * @sa GLimp_Init
 *
 * This routine does all OS specific shutdown procedures for the OpenGL
 * subsystem.  Under OpenGL this means NULLing out the current DC and
 * HGLRC, deleting the rendering context, and releasing the DC acquired
 * for the window.  The state structure is also nulled out.
 */
void GLimp_Shutdown( void )
{
	uninstall_grabs();
	mouse_active = false;
	dgamouse = false;

	if (dpy) {
		if (ctx)
			qglXDestroyContext(dpy, ctx);
		if (win)
			XDestroyWindow(dpy, win);
#ifdef HAVE_XF86_VIDMODE
		/*revert to original gamma-settings */
		if ( gl_state.hwgamma )
			XF86VidModeSetGamma(dpy, scrnum, &oldgamma);
		if (vidmode_active)
			XF86VidModeSwitchToMode(dpy, scrnum, vidmodes[0]);
#endif /* HAVE_XF86_VIDMODE */
		XCloseDisplay(dpy);
	}
	ctx = NULL;
	dpy = NULL;
	win = 0;
	ctx = NULL;
}

/**
 * @brief
 * @sa GLimp_Init
 * the default X error handler exits the application
 * I found out that on some hosts some operations would raise X errors (GLXUnsupportedPrivateRequest)
 * but those don't seem to be fatal .. so the default would be to just ignore them
 * our implementation mimics the default handler behaviour (not completely cause I'm lazy)
 */
int qXErrorHandler(Display *dpy, XErrorEvent *ev)
{
	static char buf[1024];
	XGetErrorText(dpy, ev->error_code, buf, 1024);
	ri.Con_Printf( PRINT_ALL, "X Error of failed request: %s\n", buf);
	ri.Con_Printf( PRINT_ALL, "  Major opcode of failed request: %d\n", ev->request_code, buf);
	ri.Con_Printf( PRINT_ALL, "  Minor opcode of failed request: %d\n", ev->minor_code);
	ri.Con_Printf( PRINT_ALL, "  Serial number of failed request: %d\n", ev->serial);
	return 0;
}

/**
 * @brief
 * @sa GLimp_Shutdown
 * @sa InitSig
 */
static void signal_handler(int sig)
{
	printf("Received signal %d, exiting...\n", sig);
	GLimp_Shutdown();
	exit(0);
}

/**
 * @brief
 * @sa signal_handler
 */
void InitSig(void)
{
	signal(SIGHUP, signal_handler);
	signal(SIGQUIT, signal_handler);
	signal(SIGILL, signal_handler);
	signal(SIGTRAP, signal_handler);
	signal(SIGIOT, signal_handler);
	signal(SIGBUS, signal_handler);
	signal(SIGFPE, signal_handler);
	signal(SIGSEGV, signal_handler);
	signal(SIGTERM, signal_handler);
}

/**
 * @brief
 * @sa GLimp_Shutdown
 * This routine is responsible for initializing the OS specific portions
 * of OpenGL.
 */
bool_t GLimp_Init( void *hinstance, void *wndproc )
{
	InitSig();

	/* set up our custom error handler for X failures */
	XSetErrorHandler(&qXErrorHandler);

	return true;
}

/**
 * @brief
 * @sa GLimp_EndFrame
 */
void GLimp_BeginFrame( float camera_seperation )
{
}

/**
 * @brief
 * @sa GLimp_BeginFrame
 * Responsible for doing a swapbuffers and possibly for other stuff
 * as yet to be determined.  Probably better not to make this a GLimp
 * function and instead do a call to GLimp_SwapBuffers.
 */
void GLimp_EndFrame (void)
{
	qglFlush();
	qglXSwapBuffers(dpy, win);
}

/**
 * @brief
 * @sa
 */
void GLimp_SetGamma( unsigned char red[256], unsigned char green[256], unsigned char blue[256] )
{
#ifdef HAVE_XF86_VIDMODE
	float g = vid_gamma->value;
	XF86VidModeGamma gamma;

	assert(gl_state.hwgamma);

	gamma.red = g;
	gamma.green = g;
	gamma.blue = g;
	XF86VidModeSetGamma(dpy, scrnum, &gamma);
#endif /* HAVE_XF86_VIDMODE */
}

/**
 * @brief
 * @sa
 */
void GLimp_AppActivate( bool_t active )
{
}
