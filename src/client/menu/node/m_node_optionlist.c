/**
 * @file m_node_optionlist.c
 * @todo manage disabled option
 */

/*
Copyright (C) 2002-2010 UFO: Alien Invasion.

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

#include "../m_main.h"
#include "../m_parse.h"
#include "../m_actions.h"
#include "../m_font.h"
#include "../m_icon.h"
#include "../m_render.h"
#include "m_node_abstractoption.h"
#include "m_node_abstractnode.h"
#include "m_node_optionlist.h"
#include "m_node_panel.h"
#include "m_node_option.h"

#include "../../client.h" /* gettext _() */

#define EXTRADATA(node) MN_EXTRADATA(node, abstractOptionExtraData_t)

#define CORNER_SIZE 25
#define MID_SIZE 1
#define MARGE 3

/**
 * @brief Update the scroll according to the number
 * of items and the size of the node
 */
static void MN_OptionListNodeUpdateScroll (menuNode_t *node)
{
	const char *font;
	int fontHeight;
	qboolean updated;
	int elements;

	font = MN_GetFontFromNode(node);
	fontHeight = MN_FontGetHeight(font);

	elements = (node->size[1] - node->padding - node->padding) / fontHeight;
	updated = MN_SetScroll(&EXTRADATA(node).scrollY, -1, elements, EXTRADATA(node).count);
	if (updated && EXTRADATA(node).onViewChange)
		MN_ExecuteEventActions(node, EXTRADATA(node).onViewChange);
}

static void MN_OptionListNodeDraw (menuNode_t *node)
{
	static const int panelTemplate[] = {
		CORNER_SIZE, MID_SIZE, CORNER_SIZE,
		CORNER_SIZE, MID_SIZE, CORNER_SIZE,
		MARGE
	};
	menuNode_t* option;
	const char *ref;
	const char *font;
	int fontHeight;
	vec2_t pos;
	const char* image;
	int currentY;
	const float *textColor;
	static vec4_t disabledColor = {0.5, 0.5, 0.5, 1.0};
	int count = 0;

	ref = MN_AbstractOptionGetCurrentValue(node);
	if (ref == NULL)
		return;

	MN_GetNodeAbsPos(node, pos);

	image = MN_GetReferenceString(node, node->image);
	if (image)
		MN_DrawPanel(pos, node->size, image, 0, 0, panelTemplate);

	font = MN_GetFontFromNode(node);
	fontHeight = MN_FontGetHeight(font);
	currentY = pos[1] + node->padding;

	/* skip option over current position */
	option = MN_AbstractOptionGetFirstOption(node);
	while (option && count < EXTRADATA(node).scrollY.viewPos) {
		option = option->next;
		count++;
	}

	/* draw all available options for this selectbox */
	for (; option; option = option->next) {
		const char *label;
		int decX = pos[0] + node->padding;
		/* outside the node */
		if (currentY + fontHeight > pos[1] + node->size[1] - node->padding) {
			count++;
			break;
		}

		/* draw the hover effect */
		if (OPTIONEXTRADATA(option).hovered)
			MN_DrawFill(pos[0] + node->padding, currentY, node->size[0] - node->padding - node->padding, fontHeight, node->color);

		/* text color */
		if (!strcmp(OPTIONEXTRADATA(option).value, ref)) {
			textColor = node->selectedColor;
		} else if (node->disabled || option->disabled) {
			textColor = disabledColor;
		} else {
			textColor = node->color;
		}

		if (OPTIONEXTRADATA(option).icon) {
			iconStatus_t iconStatus = ICON_STATUS_NORMAL;
			if (option->disabled)
				iconStatus = ICON_STATUS_DISABLED;
			R_Color(NULL);
			MN_DrawIconInBox(OPTIONEXTRADATA(option).icon, iconStatus, decX, currentY, OPTIONEXTRADATA(option).icon->size[0], fontHeight);
			decX += OPTIONEXTRADATA(option).icon->size[0] + fontHeight / 4;
		}

		/* print the option label */
		label = OPTIONEXTRADATA(option).label;
		if (label[0] == '_')
			label = _(label + 1);

		R_Color(textColor);
		MN_DrawString(font, ALIGN_UL, decX, currentY,
			pos[0], currentY, node->size[0] - node->padding - node->padding, node->size[1],
			0, label, 0, 0, NULL, qfalse, LONGLINES_PRETTYCHOP);

		/* next entries' position */
		currentY += fontHeight;
		count++;
	}
	R_Color(NULL);

	/* count number of options (current architecture doesn't allow to know if the data change) */
	for (; option; option = option->next) {
		count++;
	}

	if (EXTRADATA(node).count != count) {
		EXTRADATA(node).count = count;
	}

	MN_OptionListNodeUpdateScroll(node);
}

static menuNode_t* MN_OptionListNodeGetOptionAtPosition (menuNode_t * node, int x, int y)
{
	menuNode_t* option;
	vec2_t pos;
	int fontHeight;
	int currentY;
	int count = 0;
	const char *font;

	MN_GetNodeAbsPos(node, pos);
	currentY = pos[1] + node->padding;

	font = MN_GetFontFromNode(node);
	fontHeight = MN_FontGetHeight(font);

	option = MN_AbstractOptionGetFirstOption(node);
	while (option && count < EXTRADATA(node).scrollY.viewPos) {
		option = option->next;
		count++;
	}

	/* now draw all available options for this selectbox */
	for (; option; option = option->next) {
		if (y < currentY + fontHeight)
			return option;
		if (currentY + fontHeight > pos[1] + node->size[1] - node->padding)
			break;
		currentY += fontHeight;
	}
	return NULL;
}

/**
 * @brief Handles selectboxes clicks
 */
static void MN_OptionListNodeClick (menuNode_t * node, int x, int y)
{
	menuNode_t* option;

	if (MN_AbstractOptionGetCurrentValue(node) == NULL)
		return;

	/* select the right option */
	option = MN_OptionListNodeGetOptionAtPosition(node, x, y);

	/* update the status */
	if (option)
		MN_AbstractOptionSetCurrentValue(node, OPTIONEXTRADATA(option).value);
}

/**
 * @brief Auto scroll the list
 */
static void MN_OptionListNodeMouseWheel (menuNode_t *node, qboolean down, int x, int y)
{
	qboolean updated;
	updated = MN_SetScroll(&EXTRADATA(node).scrollY, EXTRADATA(node).scrollY.viewPos + (down ? 1 : -1), -1, -1);
	if (EXTRADATA(node).onViewChange && updated)
		MN_ExecuteEventActions(node, EXTRADATA(node).onViewChange);

	if (node->onWheelUp && !down)
		MN_ExecuteEventActions(node, node->onWheelUp);
	if (node->onWheelDown && down)
		MN_ExecuteEventActions(node, node->onWheelDown);
	if (node->onWheel)
		MN_ExecuteEventActions(node, node->onWheel);
}

/**
 * @brief Called before loading. Used to set default attribute values
 */
static void MN_OptionListNodeLoading (menuNode_t *node)
{
	Vector4Set(node->color, 1, 1, 1, 1);
	EXTRADATA(node).versionId = -1;
	node->padding = 3;
}

static void MN_OptionListNodeLoaded (menuNode_t *node)
{
}

void MN_RegisterOptionListNode (nodeBehaviour_t *behaviour)
{
	behaviour->name = "optionlist";
	behaviour->extends = "abstractoption";
	behaviour->draw = MN_OptionListNodeDraw;
	behaviour->leftClick = MN_OptionListNodeClick;
	behaviour->mouseWheel = MN_OptionListNodeMouseWheel;
	behaviour->loading = MN_OptionListNodeLoading;
	behaviour->loaded = MN_OptionListNodeLoaded;
	behaviour->drawItselfChild = qtrue;
}
