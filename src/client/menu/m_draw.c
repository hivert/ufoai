/**
 * @file m_draw.c
 */

/*
Copyright (C) 1997-2008 UFO:AI Team

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

#include "../client.h"
#include "../renderer/r_draw.h"
#include "m_main.h"
#include "m_nodes.h"
#include "m_internal.h"
#include "m_draw.h"
#include "m_actions.h"
#include "m_font.h"
#include "m_input.h"
#include "m_timer.h" /* define MN_HandleTimers */
#include "m_dragndrop.h"
#include "m_tooltip.h"
#include "node/m_node_abstractnode.h"

cvar_t *mn_debugmenu;
cvar_t *mn_show_tooltips;

static const int TOOLTIP_DELAY = 500; /* delay that msecs before showing tooltip */
static qboolean tooltipVisible = qfalse;
static menuTimer_t *tooltipTimer;

/**
 * @brief Node we will draw over
 * @sa MN_CaptureDrawOver
 * @sa nodeBehaviour_t.drawOverMenu
 */
static menuNode_t *drawOverNode = NULL;

/**
 * @brief Capture a node we will draw over all nodes per menu
 * @note The node must be captured every frames
 * @todo it can be better to capture the draw over only one time (need new event like mouseEnter, mouseLeave)
 */
void MN_CaptureDrawOver (menuNode_t *node)
{
	drawOverNode = node;
}

static void MN_DrawBorder (const menuNode_t *node)
{
	vec2_t nodepos;

	MN_GetNodeAbsPos(node, nodepos);
	/** @todo use GL_LINE_LOOP + array here */
	/* left */
	R_DrawFill(nodepos[0] - node->padding - node->border, nodepos[1] - node->padding - node->border,
		node->border, node->size[1] + (node->padding*2) + (node->border*2), ALIGN_UL, node->bordercolor);
	/* right */
	R_DrawFill(nodepos[0] + node->size[0] + node->padding, nodepos[1] - node->padding - node->border,
		node->border, node->size[1] + (node->padding*2) + (node->border*2), ALIGN_UL, node->bordercolor);
	/* top */
	R_DrawFill(nodepos[0] - node->padding, nodepos[1] - node->padding - node->border,
		node->size[0] + (node->padding*2), node->border, ALIGN_UL, node->bordercolor);
	/* down */
	R_DrawFill(nodepos[0] - node->padding, nodepos[1] + node->size[1] + node->padding,
		node->size[0] + (node->padding*2), node->border, ALIGN_UL, node->bordercolor);
}

static int debugTextPositionY = 0;
static int debugPositionX = 0;
#define DEBUG_PANEL_WIDTH 300

static void MN_HilightNode (menuNode_t *node, vec4_t color)
{
	static vec4_t grey = {0.7, 0.7, 0.7, 1.0};
	vec2_t pos;
	int width;
	int lineDefinition[4];
	const char* text;

	if (node->parent) {
		MN_HilightNode (node->parent, grey);
	}

	MN_GetNodeAbsPos(node, pos);

	text = va("%s (%s)", node->name, node->behaviour->name);
	R_FontTextSize("f_small_bold", text, DEBUG_PANEL_WIDTH, LONGLINES_PRETTYCHOP, &width, NULL, NULL, NULL);

	R_ColorBlend(color);
	R_FontDrawString("f_small_bold", ALIGN_UL, debugPositionX+20, debugTextPositionY, debugPositionX+20, debugTextPositionY, DEBUG_PANEL_WIDTH, DEBUG_PANEL_WIDTH, 0, text, 0, 0, NULL, qfalse, LONGLINES_PRETTYCHOP);
	debugTextPositionY += 15;

	if (debugPositionX != 0) {
		lineDefinition[0] = debugPositionX + 20;
		lineDefinition[2] = pos[0] + node->size[0];
	} else {
		lineDefinition[0] = debugPositionX + 20 + width;
		lineDefinition[2] = pos[0];
	}
	lineDefinition[1] = debugTextPositionY - 5;
	lineDefinition[3] = pos[1];
	R_DrawLine(lineDefinition, 1);
	R_ColorBlend(NULL);

	/* exclude rect */
	if (node->excludeRectNum) {
		int i;
		vec4_t trans = {1, 1, 1, 1};
		Vector4Copy(color, trans);
		trans[3] = trans[3] / 2;
		for (i = 0; i < node->excludeRectNum; i++) {
			const int x = pos[0] + node->excludeRect[i].pos[0];
			const int y = pos[1] + node->excludeRect[i].pos[1];
			R_DrawFill(x, y, node->excludeRect[i].size[0], node->excludeRect[i].size[1], ALIGN_UL, trans);
		}
	}

	/* bounded box */
	R_DrawRect(pos[0] - 1, pos[1] - 1, node->size[0] + 2, node->size[1] + 2, color, 2.0, 0x3333);
}

/**
 * @brief Prints active node names for debugging
 */
static void MN_DrawDebugMenuNodeNames (void)
{
	static vec4_t red = {1.0, 0.0, 0.0, 1.0};
	static vec4_t green = {0.0, 0.5, 0.0, 1.0};
	static vec4_t white = {1, 1.0, 1.0, 1.0};
	static vec4_t background = {0.0, 0.0, 0.0, 0.5};
	menuNode_t *hoveredNode;
	int stackPosition;

	debugTextPositionY = 100;

	/* x panel position with hysteresis */
	if (mousePosX < VID_NORM_WIDTH / 3)
		debugPositionX = VID_NORM_WIDTH - DEBUG_PANEL_WIDTH;
	if (mousePosX > 2 * VID_NORM_WIDTH / 3)
		debugPositionX = 0;

	/* background */
	R_DrawFill(debugPositionX, debugTextPositionY, DEBUG_PANEL_WIDTH, VID_NORM_HEIGHT - debugTextPositionY - 100, ALIGN_UL, background);

	/* menu stack */
	R_ColorBlend(white);
	R_FontDrawString("f_small_bold", ALIGN_UL, debugPositionX, debugTextPositionY, debugPositionX, debugTextPositionY, 200, 200, 0, "menu stack:", 0, 0, NULL, qfalse, LONGLINES_PRETTYCHOP);
	debugTextPositionY += 15;
	for (stackPosition = 0; stackPosition < mn.menuStackPos; stackPosition++) {
		menuNode_t *menu = mn.menuStack[stackPosition];
		R_ColorBlend(white);
		R_FontDrawString("f_small_bold", ALIGN_UL, debugPositionX+20, debugTextPositionY, debugPositionX+20, debugTextPositionY, 200, 200, 0, menu->name, 0, 0, NULL, qfalse, LONGLINES_PRETTYCHOP);
		debugTextPositionY += 15;
	}

	/* hovered node */
	hoveredNode = MN_GetHoveredNode();
	if (hoveredNode) {
		R_ColorBlend(white);
		R_FontDrawString("f_small_bold", ALIGN_UL, debugPositionX, debugTextPositionY, debugPositionX, debugTextPositionY, 200, 200, 0, "-----------------------", 0, 0, NULL, qfalse, LONGLINES_PRETTYCHOP);
		debugTextPositionY += 15;

		R_ColorBlend(white);
		R_FontDrawString("f_small_bold", ALIGN_UL, debugPositionX, debugTextPositionY, debugPositionX, debugTextPositionY, 200, 200, 0, "hovered node:", 0, 0, NULL, qfalse, LONGLINES_PRETTYCHOP);
		debugTextPositionY += 15;
		MN_HilightNode(hoveredNode, red);
	}

	/* target node */
	if (MN_DNDIsDragging()) {
		menuNode_t *targetNode = MN_DNDGetTargetNode();
		if (targetNode) {
			R_ColorBlend(white);
			R_FontDrawString("f_small_bold", ALIGN_UL, debugPositionX, debugTextPositionY, debugPositionX, debugTextPositionY, 200, 200, 0, "-----------------------", 0, 0, NULL, qfalse, LONGLINES_PRETTYCHOP);
			debugTextPositionY += 15;

			R_ColorBlend(green);
			R_FontDrawString("f_small_bold", ALIGN_UL, debugPositionX, debugTextPositionY, debugPositionX, debugTextPositionY, 200, 200, 0, "drag and drop target node:", 0, 0, NULL, qfalse, LONGLINES_PRETTYCHOP);
			debugTextPositionY += 15;
			MN_HilightNode(targetNode, green);
		}
	}
	R_ColorBlend(NULL);
}


static void MN_CheckTooltipDelay (menuNode_t *node, menuTimer_t *timer)
{
	tooltipVisible = qtrue;
}

static void MN_DrawNode (menuNode_t *node) {
	vec2_t nodepos;
	menuNode_t *child;

	/* skip invisible, virtual, and undrawable nodes */
	if (node->invis || node->behaviour->isVirtual)
		return;
	/* if construct */
	if (!MN_CheckVisibility(node))
		return;

	/** @todo remove it when its possible */
	MN_GetNodeAbsPos(node, nodepos);

	/* check node size x and y value to check whether they are zero */
	if (node->size[0] && node->size[1]) {
		if (node->bgcolor) {
			/** @todo remove it when its possible */
			R_DrawFill(nodepos[0], nodepos[1], node->size[0], node->size[1], 0, node->bgcolor);
		}
		if (node->border && node->bordercolor) {
			/** @todo remove it when its possible */
			MN_DrawBorder(node);
		}
	}

	/** @todo remove it when its possible */
	if (node->border && node->bordercolor && node->size[0] && node->size[1] && node->pos)
		MN_DrawBorder(node);

	/* draw the node */
	if (node->behaviour->draw) {
		node->behaviour->draw(node);
	}

	/* draw all child */
	for (child = node->firstChild; child; child = child->next) {
		MN_DrawNode(child);
	}
}

/**
 * @brief Draws the menu stack
 * @todo move DrawMenusTest here
 * @sa SCR_UpdateScreen
 */
void MN_DrawMenus (void)
{
	menuNode_t *hoveredNode;
	menuNode_t *menu;
	int windowId;
	qboolean mouseMoved = qfalse;

	assert(mn.menuStackPos >= 0);

	mouseMoved = MN_CheckMouseMove();
	hoveredNode = MN_GetHoveredNode();

	/* handle delay time for tooltips */
	if (mouseMoved && tooltipTimer != NULL) {
		MN_TimerRelease(tooltipTimer);
		tooltipTimer = NULL;
		tooltipVisible = qfalse;
	} else if (!mouseMoved && tooltipTimer == NULL && mn_show_tooltips->integer && hoveredNode) {
		tooltipTimer = MN_AllocTimer(NULL, TOOLTIP_DELAY, MN_CheckTooltipDelay);
		MN_TimerStart(tooltipTimer);
	}

	MN_HandleTimers();

	/* under a fullscreen, menu should not be visible */
	windowId = MN_GetLastFullScreenWindow();
	if (windowId < 0)
		return;

	/* draw all visible menus */
	for (;windowId < mn.menuStackPos; windowId++) {
		menu = mn.menuStack[windowId];

		/* update the layout */
		menu->behaviour->doLayout(menu);

		drawOverNode = NULL;

		MN_DrawNode(menu);

		/* draw a node over the menu */
		if (drawOverNode && drawOverNode->behaviour->drawOverMenu) {
			drawOverNode->behaviour->drawOverMenu(drawOverNode);
		}
	}

	/* draw tooltip */
	if (hoveredNode && tooltipVisible && !MN_DNDIsDragging()) {
		if (hoveredNode->behaviour->drawTooltip) {
			hoveredNode->behaviour->drawTooltip(hoveredNode, mousePosX, mousePosY);
		} else {
			MN_Tooltip(hoveredNode, mousePosX, mousePosY);
		}
	}

	/** @todo remove it (or clean up) when it possible. timeout? */
	if (hoveredNode && hoveredNode->timePushed) {
		if (hoveredNode->timePushed + hoveredNode->timeOut < cls.realtime) {
			hoveredNode->timePushed = 0;
			hoveredNode->invis = qtrue;
			/* only timeout this once, otherwise there is a new timeout after every new stack push */
			if (hoveredNode->timeOutOnce)
				hoveredNode->timeOut = 0;
			Com_DPrintf(DEBUG_CLIENT, "MN_DrawMenus: timeout for node '%s'\n", hoveredNode->name);
		}
	}

	/* draw a special notice */
	menu = MN_GetActiveMenu();
	if (cl.time < cl.msgTime) {
		if (menu && (menu->u.window.noticePos[0] || menu->u.window.noticePos[1]))
			MN_DrawNotice(menu->u.window.noticePos[0], menu->u.window.noticePos[1]);
		else
			MN_DrawNotice(500, 110);
	}

	/* debug information */
	if (mn_debugmenu->integer == 2) {
		MN_DrawDebugMenuNodeNames();
	}
}

void MN_DrawCursor (void)
{
	MN_DrawDragAndDrop(mousePosX, mousePosY);
}

void MN_DrawMenusInit (void)
{
	mn_debugmenu = Cvar_Get("mn_debugmenu", "0", 0, "Prints node names for debugging purposes - valid values are 1 and 2");
	mn_show_tooltips = Cvar_Get("mn_show_tooltips", "1", CVAR_ARCHIVE, "Show tooltips in menus and hud");
}

/**
 * @brief Displays a message over all menus.
 * @sa HUD_DisplayMessage
 * @param[in] time is a ms values
 * @param[in] text text is already translated here
 */
void MN_DisplayNotice (const char *text, int time)
{
	cl.msgTime = cl.time + time;
	Q_strncpyz(cl.msgText, text, sizeof(cl.msgText));
}
