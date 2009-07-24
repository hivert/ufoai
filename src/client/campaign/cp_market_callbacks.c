/**
 * @file cp_market_callbacks.c
 */

/*
Copyright (C) 2002-2009 UFO:AI Team

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
#include "../cl_menu.h"
#include "../cl_ugv.h"
#include "../menu/m_nodes.h"
#include "../menu/m_popup.h"
#include "cp_campaign.h"
#include "cp_market.h"
#include "cp_market_callbacks.h"

#define MAX_BUYLIST		64

#define MAX_MARKET_MENU_ENTRIES 22

/**
 * @brief An entry in the buylist.
 * @note The pointers are used XOR - there can be only one (used).
 */
typedef struct buyListEntry_s {
	const objDef_t *item;			/**< Item pointer (see also csi.ods[] and base->storage.num[] etc...) */
	const ugv_t *ugv;				/**< Used for mixed UGV (characters) and FILTER_UGVITEM (items) list.
									 * If not NULL it's a pointer to the correct UGV-struct (duh)
									 * otherwise a FILTER_UGVITEM-item is set in "item". */
	const aircraft_t *aircraft;	/**< Used for aircraft production - aircraft template */
} buyListEntry_t;

typedef struct buyList_s {
	buyListEntry_t l[MAX_BUYLIST];	/** The actual list */
	int length;		/**< Amount of entries on the list. */
	int scroll;		/**< Scroll Position. Start of the buylist index - due to scrolling. */
} buyList_t;

static buyList_t buyList;	/**< Current buylist that is shown in the menu. */
static const objDef_t *currentSelectedMenuEntry;	/**< Current selected entry on the list. */
static int buyCat = FILTER_S_PRIMARY;	/**< Category of items in the menu.
										 * @sa itemFilterTypes_t */

/** @brief Max values for Buy/Sell factors. */
static const int MAX_BS_FACTORS = 500;


/**
 * @brief Set the number of item to buy or sell.
 */
static inline int BS_GetBuySellFactor (void)
{
	return 1;
}

static const objDef_t *BS_GetObjectDefition (const buyListEntry_t *entry)
{
	assert(entry);
	if (entry->item)
		return entry->item;
	else if (entry->ugv)
		return NULL;
	else if (entry->aircraft)
		return NULL;

	Com_Error(ERR_DROP, "You should not check an empty buy list entry");
}


/**
 * @brief Prints general information about aircraft for Buy/Sell menu.
 * @param[in] aircraftTemplate Aircraft type.
 * @sa UP_AircraftDescription
 * @sa UP_AircraftItemDescription
 */
static void BS_MarketAircraftDescription (const aircraft_t *aircraftTemplate)
{
	const technology_t *tech;

	/* Break if no aircraft was given or if  it's no sample-aircraft (i.e. template). */
	if (!aircraftTemplate || aircraftTemplate != aircraftTemplate->tpl)
		return;

	tech = aircraftTemplate->tech;
	assert(tech);
	UP_AircraftDescription(tech);
	Cvar_Set("mn_aircraftname", _(aircraftTemplate->name));
	Cvar_Set("mn_item", aircraftTemplate->id);
}

/**
 * @brief
 * @param[in] base
 * @param[in] itemNum
 * @param[out] min
 * @param[out] max
 * @param[out] value
 */
static inline qboolean BS_GetMinMaxValueByItemID (const base_t *base, int itemNum, int *min, int *max, int *value)
{
	assert(base);

	if (itemNum < 0 || itemNum + buyList.scroll >= buyList.length)
		return qfalse;

	if (buyCat == FILTER_UGVITEM && buyList.l[itemNum + buyList.scroll].ugv) {
		/** @todo compute something better */
		*min = 0;
		*value = 10000;
		*max = 20000;
	} else if (buyCat == FILTER_AIRCRAFT && buyList.l[itemNum + buyList.scroll].aircraft) {
		const aircraft_t *aircraft = buyList.l[itemNum + buyList.scroll].aircraft;
		if (!aircraft)
			return qfalse;
		*value = AIR_GetStorageSupply(base, aircraft->id, qtrue);
		*max = AIR_GetStorageSupply(base, aircraft->id, qtrue) + AIR_GetStorageSupply(base, aircraft->id, qfalse);
		*min = 0;
	} else {
		const objDef_t *item = BS_GetObjectDefition(&buyList.l[itemNum + buyList.scroll]);
		if (!item)
			return qfalse;
		*value = base->storage.num[item->idx];
		*max = base->storage.num[item->idx] + ccs.eMarket.num[item->idx];
		*min = 0;
	}

	return qtrue;
}

/**
 * @brief Update the GUI by calling a console function
 * @sa BS_BuyType
 */
static void BS_UpdateItem (const base_t *base, int itemNum)
{
	int min, max, value;

	if (BS_GetMinMaxValueByItemID(base, itemNum, &min, &max, &value))
		MN_ExecuteConfunc("buy_updateitem %d %d %d %d", itemNum, value, min, max);
}

/**
 * @brief
 * @sa BS_MarketClick_f
 * @sa BS_AddToList
 */
static void BS_MarketScroll_f (void)
{
	int i;
	base_t *base = B_GetCurrentSelectedBase();

	if (!base || buyCat >= MAX_FILTERTYPES || buyCat < 0)
		return;

	if (Cmd_Argc() < 2) {
		Com_Printf("Usage: %s <scrollpos>\n", Cmd_Argv(0));
		return;
	}

	buyList.scroll = atoi(Cmd_Argv(1));
	assert(buyList.scroll >= 0);
	assert(!((buyList.length > MAX_MARKET_MENU_ENTRIES && buyList.scroll >= buyList.length - MAX_MARKET_MENU_ENTRIES)));

	/* now update the menu pics */
	for (i = 0; i < MAX_MARKET_MENU_ENTRIES; i++) {
		MN_ExecuteConfunc("buy_autoselli %i", i);
	}

	/* get item list */
	for (i = buyList.scroll; i < buyList.length - buyList.scroll; i++) {
		if (i >= MAX_MARKET_MENU_ENTRIES)
			break;
		else {
			const objDef_t *od = BS_GetObjectDefition(&buyList.l[i]);
			/* Check whether the item matches the proper filter, storage in current base and market. */
			if (od && (base->storage.num[od->idx] || ccs.eMarket.num[od->idx]) && INV_ItemMatchesFilter(od, buyCat)) {
				MN_ExecuteConfunc("buy_show %i", i - buyList.scroll);
				BS_UpdateItem(base, i - buyList.scroll);
				if (ccs.autosell[od->idx])
					MN_ExecuteConfunc("buy_autoselle %i", i - buyList.scroll);
				else
					MN_ExecuteConfunc("buy_autoselld %i", i - buyList.scroll);
			}
		}
	}
}

/**
 * @brief Select one entry on the list.
 * @sa BS_MarketScroll_f
 * @sa BS_AddToList
 */
static void BS_MarketClick_f (void)
{
	int num;

	if (Cmd_Argc() < 2) {
		Com_Printf("Usage: %s <num>\n", Cmd_Argv(0));
		return;
	}

	num = atoi(Cmd_Argv(1));
	if (num >= buyList.length || num < 0)
		return;

	Cvar_Set("mn_item", "");

	switch (buyCat) {
	case FILTER_AIRCRAFT:
		assert(buyList.l[num].aircraft);
		BS_MarketAircraftDescription(buyList.l[num].aircraft->tpl);
		break;
	case FILTER_CRAFTITEM:
		UP_AircraftItemDescription(buyList.l[num].item);
		Cvar_Set("mn_aircraftname", "");	/** @todo Use craftitem name here? */
		break;
	case FILTER_UGVITEM:
		if (buyList.l[num].ugv) {
			UP_UGVDescription(buyList.l[num].ugv);
			currentSelectedMenuEntry = NULL;
		} else {
			INV_ItemDescription(buyList.l[num].item);
			currentSelectedMenuEntry = buyList.l[num].item;
		}
		break;
	case MAX_FILTERTYPES:
		break;
	default:
		INV_ItemDescription(buyList.l[num].item);
		currentSelectedMenuEntry = buyList.l[num].item;
		break;
	}

	/* update selected element */
	MN_ExecuteConfunc("buy_selectitem %i", num);
}

/** @brief Market text nodes buffers */
static linkedList_t *bsMarketNames;
static linkedList_t *bsMarketStorage;
static linkedList_t *bsMarketMarket;
static linkedList_t *bsMarketPrices;

/**
 * @brief Appends a new entry to the market buffers
 * @sa BS_MarketScroll_f
 * @sa BS_MarketClick_f
 */
static void BS_AddToList (const char *name, int storage, int market, int price)
{
	LIST_AddString(&bsMarketNames, _(name));
	LIST_AddString(&bsMarketStorage, va("%i", storage));
	LIST_AddString(&bsMarketMarket, va("%i", market));
	LIST_AddString(&bsMarketPrices, va(_("%i c"), price));
}

/**
 * @brief Updates the Buy/Sell menu list.
 * @sa BS_BuyType_f
 */
static void BS_BuyType (const base_t *base)
{
	const objDef_t *od;
	int i, j = 0;
	char tmpbuf[MAX_VAR];

	if (!base || buyCat >= MAX_FILTERTYPES || buyCat < 0)
		return;

	CL_UpdateCredits(ccs.credits);

	bsMarketNames = NULL;
	bsMarketStorage = NULL;
	bsMarketMarket = NULL;
	bsMarketPrices = NULL;
	MN_ResetData(TEXT_STANDARD);

	/* 'normal' items */
	switch (buyCat) {
	case FILTER_AIRCRAFT:	/* Aircraft */
		{
		const technology_t* tech;
		const aircraft_t *aircraftTemplate;
		for (i = 0, j = 0, aircraftTemplate = ccs.aircraftTemplates; i < ccs.numAircraftTemplates; i++, aircraftTemplate++) {
			if (aircraftTemplate->type == AIRCRAFT_UFO || aircraftTemplate->price == -1)
				continue;
			tech = aircraftTemplate->tech;
			assert(tech);
			if (RS_Collected_(tech) || RS_IsResearched_ptr(tech)) {
				if (j >= buyList.scroll && j < MAX_MARKET_MENU_ENTRIES) {
					MN_ExecuteConfunc("buy_autoselli %i", j - buyList.scroll);
					MN_ExecuteConfunc("buy_show %i", j - buyList.scroll);
				}
				BS_AddToList(aircraftTemplate->name, AIR_GetStorageSupply(base, aircraftTemplate->id, qtrue),
						AIR_GetStorageSupply(base, aircraftTemplate->id, qfalse), aircraftTemplate->price);
				if (j >= MAX_BUYLIST)
					Com_Error(ERR_DROP, "Increase the MAX_BUYLIST value to handle that much items\n");
				buyList.l[j].item = NULL;
				buyList.l[j].ugv = NULL;
				buyList.l[j].aircraft = aircraftTemplate;
				buyList.length = j + 1;
				BS_UpdateItem(base, j - buyList.scroll);
				j++;
			}
		}
		}
		break;
	case FILTER_CRAFTITEM:	/* Aircraft items */
		/* get item list */
		for (i = 0, j = 0, od = csi.ods; i < csi.numODs; i++, od++) {
			if (od->notOnMarket)
				continue;
			/* Check whether the item matches the proper filter, storage in current base and market. */
			if (od->tech && (base->storage.num[i] || ccs.eMarket.num[i])
			 && (RS_Collected_(od->tech) || RS_IsResearched_ptr(od->tech))
			 && INV_ItemMatchesFilter(od, FILTER_CRAFTITEM)) {
				if (j >= buyList.scroll && j < MAX_MARKET_MENU_ENTRIES) {
					MN_ExecuteConfunc("buy_show %i", j - buyList.scroll);
					if (ccs.autosell[i])
						MN_ExecuteConfunc("buy_autoselle %i", j - buyList.scroll);
					else
						MN_ExecuteConfunc("buy_autoselld %i", j - buyList.scroll);
				}
				BS_AddToList(od->name, base->storage.num[i], ccs.eMarket.num[i], ccs.eMarket.ask[i]);
				if (j >= MAX_BUYLIST)
					Com_Error(ERR_DROP, "Increase the MAX_FILTERLIST value to handle that much items\n");
				buyList.l[j].item = od;
				buyList.l[j].ugv = NULL;
				buyList.l[j].aircraft = NULL;
				buyList.length = j + 1;
				BS_UpdateItem(base, j - buyList.scroll);
				j++;
			}
		}
		break;
	case FILTER_UGVITEM:	/* Heavy equipment like UGVs and it's weapons/ammo. */
		{
		/* Get item list. */
		j = 0;
		for (i = 0; i < numUGV; i++) {
			/** @todo Add this entry to the list */
			ugv_t *ugv = &ugvs[i];
			const technology_t* tech = RS_GetTechByProvided(ugv->id);
			assert(tech);
			if (RS_IsResearched_ptr(tech)) {
				const int hiredRobot = E_CountHiredRobotByType(base, ugv);
				const int unhiredRobot = E_CountUnhiredRobotsByType(ugv);

				if (hiredRobot + unhiredRobot <= 0)
					continue;

				if (j >= buyList.scroll && j < MAX_MARKET_MENU_ENTRIES) {
					MN_ExecuteConfunc("buy_show %i", j - buyList.scroll);
				}

				BS_AddToList(tech->name,
					hiredRobot,			/* numInStorage */
					unhiredRobot,			/* numOnMarket */
					ugv->price);

				if (j >= MAX_BUYLIST)
					Com_Error(ERR_DROP, "Increase the MAX_BUYLIST value to handle that much entries.\n");
				buyList.l[j].item = NULL;
				buyList.l[j].ugv = ugv;
				buyList.l[j].aircraft = NULL;
				buyList.length = j + 1;
				BS_UpdateItem(base, j - buyList.scroll);
				j++;
			}
		}

		for (i = 0, od = csi.ods; i < csi.numODs; i++, od++) {
			if (od->notOnMarket)
				continue;

			/* Check whether the item matches the proper filter, storage in current base and market. */
			if (od->tech && INV_ItemMatchesFilter(od, FILTER_UGVITEM) && (base->storage.num[i] || ccs.eMarket.num[i])) {
				BS_AddToList(od->name, base->storage.num[i], ccs.eMarket.num[i], ccs.eMarket.ask[i]);
				/* Set state of Autosell button. */
				if (j >= buyList.scroll && j < MAX_MARKET_MENU_ENTRIES) {
					MN_ExecuteConfunc("buy_show %i", j - buyList.scroll);
					if (ccs.autosell[i])
						MN_ExecuteConfunc("buy_autoselle %i", j - buyList.scroll);
					else
						MN_ExecuteConfunc("buy_autoselld %i", j - buyList.scroll);
				}

				if (j >= MAX_BUYLIST)
					Com_Error(ERR_DROP, "Increase the MAX_BUYLIST value to handle that much items\n");
				buyList.l[j].item = od;
				buyList.l[j].ugv = NULL;
				buyList.l[j].aircraft = NULL;
				buyList.length = j + 1;
				BS_UpdateItem(base, j - buyList.scroll);
				j++;
			}
		}
		}
		break;
	default:	/* Normal items */
		if (buyCat < MAX_SOLDIER_FILTERTYPES || buyCat == FILTER_DUMMY) {
			/* get item list */
			for (i = 0, j = 0, od = csi.ods; i < csi.numODs; i++, od++) {
				if (od->notOnMarket)
					continue;
				/* Check whether the item matches the proper filter, storage in current base and market. */
				if (od->tech && (base->storage.num[i] || ccs.eMarket.num[i]) && INV_ItemMatchesFilter(od, buyCat)) {
					BS_AddToList(od->name, base->storage.num[i], ccs.eMarket.num[i], ccs.eMarket.ask[i]);
					/* Set state of Autosell button. */
					if (j >= buyList.scroll && j < MAX_MARKET_MENU_ENTRIES) {
						MN_ExecuteConfunc("buy_show %i", j - buyList.scroll);
						if (ccs.autosell[i])
							MN_ExecuteConfunc("buy_autoselle %i", j - buyList.scroll);
						else
							MN_ExecuteConfunc("buy_autoselld %i", j - buyList.scroll);
					}

					if (j >= MAX_BUYLIST)
						Com_Error(ERR_DROP, "Increase the MAX_BUYLIST value to handle that much items\n");
					buyList.l[j].item = od;
					buyList.l[j].ugv = NULL;
					buyList.l[j].aircraft = NULL;
					buyList.length = j + 1;
					BS_UpdateItem(base, j - buyList.scroll);
					j++;
				}
			}
		}
		break;
	}

	for (; j < MAX_MARKET_MENU_ENTRIES; j++) {
		/* Hide the rest of the entries. */
		MN_ExecuteConfunc("buy_autoselli %i", j);
		MN_ExecuteConfunc("buy_hide %i", j);
	}

	/* Update some menu cvars. */
	/* Set up base capacities. */
	Com_sprintf(tmpbuf, sizeof(tmpbuf), "%i/%i", base->capacities[CAP_ITEMS].cur,
		base->capacities[CAP_ITEMS].max);
	Cvar_Set("mn_bs_storage", tmpbuf);

	/* select first item */
	if (buyList.length) {
		switch (buyCat) {	/** @sa BS_MarketClick_f */
		case FILTER_AIRCRAFT:
			BS_MarketAircraftDescription(buyList.l[0].aircraft);
			break;
		case FILTER_CRAFTITEM:
			Cvar_Set("mn_aircraftname", "");	/** @todo Use craftitem name here? See also BS_MarketClick_f */
			/* Select current item or first one. */
			if (currentSelectedMenuEntry)
				UP_AircraftItemDescription(currentSelectedMenuEntry);
			else
				UP_AircraftItemDescription(buyList.l[0].item);
			break;
		case FILTER_UGVITEM:
			/** @todo select first heavy item */
			if (currentSelectedMenuEntry)
				INV_ItemDescription(currentSelectedMenuEntry);
			else if (buyList.l[0].ugv)
				UP_UGVDescription(buyList.l[0].ugv);
			else if (buyList.l[0].item)
				INV_ItemDescription(buyList.l[0].item);
			break;
		default:
			assert(buyCat != MAX_FILTERTYPES);
			/* Select current item or first one. */
			if (currentSelectedMenuEntry)
				INV_ItemDescription(currentSelectedMenuEntry);
			else
				INV_ItemDescription(buyList.l[0].item);
			break;
		}
	} else {
		/* reset description */
		INV_ItemDescription(NULL);
	}

	MN_RegisterLinkedListText(TEXT_MARKET_NAMES, bsMarketNames);
	MN_RegisterLinkedListText(TEXT_MARKET_STORAGE, bsMarketStorage);
	MN_RegisterLinkedListText(TEXT_MARKET_MARKET, bsMarketMarket);
	MN_RegisterLinkedListText(TEXT_MARKET_PRICES, bsMarketPrices);
}

/**
 * @brief Init function for Buy/Sell menu.
 */
static void BS_BuyType_f (void)
{
	base_t *base = B_GetCurrentSelectedBase();

	if (Cmd_Argc() == 2) {
		buyCat = INV_GetFilterTypeID(Cmd_Argv(1));

		if (buyCat == FILTER_DISASSEMBLY)
			buyCat--;
		if (buyCat < 0) {
			buyCat = MAX_FILTERTYPES - 1;
			if (buyCat == FILTER_DISASSEMBLY)
				buyCat--;
		} else if (buyCat >= MAX_FILTERTYPES) {
			buyCat = 0;
		}

		Cvar_Set("mn_itemtype", INV_GetFilterType(buyCat));
		currentSelectedMenuEntry = NULL;
	}

	BS_BuyType(base);
	buyList.scroll = 0;
	MN_ExecuteConfunc("sync_market_scroll 0 %d", buyList.scroll);
	MN_ExecuteConfunc("market_scroll %d", buyList.scroll);
	MN_ExecuteConfunc("market_click 0");
}

/**
 * @brief Buys aircraft or craftitem.
 * @sa BS_SellAircraft_f
 */
static void BS_BuyAircraft_f (void)
{
	int num, freeSpace;
	const aircraft_t *aircraftTemplate;
	base_t *base = B_GetCurrentSelectedBase();

	if (Cmd_Argc() < 2) {
		Com_Printf("Usage: %s <num>\n", Cmd_Argv(0));
		return;
	}

	if (!base)
		return;

	num = atoi(Cmd_Argv(1));
	if (num < 0 || num >= buyList.length)
		return;

	if (buyCat == FILTER_AIRCRAFT) {
		/* We cannot buy aircraft if there is no power in our base. */
		if (!B_GetBuildingStatus(base, B_POWER)) {
			MN_Popup(_("Note"), _("No power supplies in this base.\nHangars are not functional."));
			return;
		}
		/* We cannot buy aircraft without any hangar. */
		if (!B_GetBuildingStatus(base, B_HANGAR) && !B_GetBuildingStatus(base, B_SMALL_HANGAR)) {
			MN_Popup(_("Note"), _("Build a hangar first."));
			return;
		}
		aircraftTemplate = buyList.l[num].aircraft;
		freeSpace = AIR_CalculateHangarStorage(aircraftTemplate, base, 0);

		/* Check free space in hangars. */
		if (freeSpace < 0) {
			Com_Printf("BS_BuyAircraft_f: something bad happened, AIR_CalculateHangarStorage returned -1!\n");
			return;
		}

		if (freeSpace == 0) {
			MN_Popup(_("Notice"), _("You cannot buy this aircraft.\nNot enough space in hangars.\n"));
			return;
		} else {
			assert(aircraftTemplate);
			if (ccs.credits < aircraftTemplate->price) {
				MN_Popup(_("Notice"), _("You cannot buy this aircraft.\nNot enough credits.\n"));
				return;
			} else {
				/* Hangar capacities are being updated in AIR_NewAircraft().*/
				CL_UpdateCredits(ccs.credits - aircraftTemplate->price);
				AIR_NewAircraft(base, aircraftTemplate->id);
				Cmd_ExecuteString(va("buy_type %s", INV_GetFilterType(FILTER_AIRCRAFT)));
			}
		}
	}
}

/**
 * @brief Sells aircraft or craftitem.
 * @sa BS_BuyAircraft_f
 */
static void BS_SellAircraft_f (void)
{
	int num, j;
	qboolean found = qfalse;
	qboolean teamNote = qfalse;
	qboolean aircraftOutNote = qfalse;
	base_t *base = B_GetCurrentSelectedBase();

	if (Cmd_Argc() < 2) {
		Com_Printf("Usage: %s <num>\n", Cmd_Argv(0));
		return;
	}

	if (!base)
		return;

	num = atoi(Cmd_Argv(1));
	if (num < 0 || num >= buyList.length)
		return;

	if (buyCat == FILTER_AIRCRAFT) {
		aircraft_t *aircraft;
		const aircraft_t *aircraftTemplate = buyList.l[num].aircraft;
		if (!aircraftTemplate)
			return;

		for (j = 0, aircraft = base->aircraft; j < base->numAircraftInBase; j++, aircraft++) {
			if (!strncmp(aircraft->id, aircraftTemplate->id, MAX_VAR)) {
				if (aircraft->teamSize) {
					teamNote = qtrue;
					continue;
				}
				if (!AIR_IsAircraftInBase(aircraft)) {
					/* aircraft is not in base */
					aircraftOutNote = qtrue;
					continue;
				}
				found = qtrue;
				break;
			}
		}
		/* ok, we've found an empty aircraft (no team) in a base
		 * so now we can sell it */
		if (found) {
			/* sell off any items which are mounted on it */
			for (j = 0; j < aircraft->maxWeapons; j++) {
				BS_ProcessCraftItemSale(base, aircraft->weapons[j].item, 1);
				BS_ProcessCraftItemSale(base, aircraft->weapons[j].ammo, 1);
			}

			BS_ProcessCraftItemSale(base, aircraft->shield.item, 1);
			/* there should be no ammo here, but checking can't hurt */
			BS_ProcessCraftItemSale(base, aircraft->shield.ammo, 1);

			for (j = 0; j < aircraft->maxElectronics; j++) {
				BS_ProcessCraftItemSale(base, aircraft->electronics[j].item, 1);
				/* there should be no ammo here, but checking can't hurt */
				BS_ProcessCraftItemSale(base, aircraft->electronics[j].ammo, 1);
			}

			Com_DPrintf(DEBUG_CLIENT, "BS_SellAircraft_f: Selling aircraft with IDX %i\n", aircraft->idx);
			/* the capacities are also updated here */
			AIR_DeleteAircraft(base, aircraft);

			CL_UpdateCredits(ccs.credits + aircraftTemplate->price);
			/* reinit the menu */
			BS_BuyType(base);
		} else {
			if (teamNote)
				MN_Popup(_("Note"), _("You can't sell an aircraft if it still has a team assigned"));
			else if (aircraftOutNote)
				MN_Popup(_("Note"), _("You can't sell an aircraft that is not in base"));
			else
				Com_DPrintf(DEBUG_CLIENT, "BS_SellAircraft_f: There are no aircraft available (with no team assigned) for selling\n");
		}
	}
}

/**
 * @brief Buy one item of a given type.
 * @sa BS_SellItem_f
 * @sa BS_SellAircraft_f
 * @sa BS_BuyAircraft_f
 */
static void BS_BuyItem_f (void)
{
	int num;
	base_t *base = B_GetCurrentSelectedBase();

	if (Cmd_Argc() < 2) {
		Com_Printf("Usage: %s <num>\n", Cmd_Argv(0));
		return;
	}

	if (!base)
		return;

	if (buyCat == FILTER_AIRCRAFT) {
		Com_DPrintf(DEBUG_CLIENT, "BS_BuyItem_f: Redirects to BS_BuyAircraft_f\n");
		BS_BuyAircraft_f();
		return;
	}

	num = atoi(Cmd_Argv(1));
	if (num < 0 || num >= buyList.length)
		return;

	Cmd_ExecuteString(va("buy_selectitem %i", num + buyList.scroll));

	if (buyCat == FILTER_UGVITEM && buyList.l[num + buyList.scroll].ugv) {
		/* The list entry is an actual ugv/robot */
		const ugv_t *ugv = buyList.l[num + buyList.scroll].ugv;
		qboolean ugvWeaponBuyable;

		UP_UGVDescription(ugv);

		if (ccs.credits >= ugv->price && E_CountUnhiredRobotsByType(ugv) > 0) {
			/* Check if we have a weapon for this ugv in the market and there is enough storage-room for it. */
			const objDef_t *ugvWeapon = INVSH_GetItemByID(ugv->weapon);
			if (!ugvWeapon)
				Com_Error(ERR_DROP, "BS_BuyItem_f: Could not get weapon '%s' for ugv/tank '%s'.", ugv->weapon, ugv->id);

			ugvWeaponBuyable = qtrue;
			if (!ccs.eMarket.num[ugvWeapon->idx])
				ugvWeaponBuyable = qfalse;

			if (base->capacities[CAP_ITEMS].max - base->capacities[CAP_ITEMS].cur <
				UGV_SIZE + ugvWeapon->size) {
				MN_Popup(_("Not enough storage space"), _("You cannot buy this item.\nNot enough space in storage.\nBuild more storage facilities."));
				ugvWeaponBuyable = qfalse;
			}

			if (ugvWeaponBuyable && E_HireRobot(base, ugv)) {
				/* Move the item into the storage. */
				B_UpdateStorageAndCapacity(base, ugvWeapon, 1, qfalse, qfalse);
				ccs.eMarket.num[ugvWeapon->idx]--;

				/* Update Display/List and credits. */
				BS_BuyType(base);
				CL_UpdateCredits(ccs.credits - ugv->price);	/** @todo make this depend on market as well? */
			} else {
				Com_Printf("Could not buy this item.\n");
			}
		}
	} else {
		/* Normal item (or equipment for UGVs/Robots if buyCategory==BUY_HEAVY) */
		const objDef_t *item = BS_GetObjectDefition(&buyList.l[num + buyList.scroll]);
		assert(item);
		currentSelectedMenuEntry = item;
		INV_ItemDescription(item);
		Com_DPrintf(DEBUG_CLIENT, "BS_BuyItem_f: item %s\n", item->id);
		BS_CheckAndDoBuyItem(base, item, BS_GetBuySellFactor());
		/* reinit the menu */
		BS_BuyType(base);
		BS_UpdateItem(base, num);
	}
}

/**
 * @brief Sell one item of a given type.
 * @sa BS_BuyItem_f
 * @sa BS_SellAircraft_f
 * @sa BS_BuyAircraft_f
 */
static void BS_SellItem_f (void)
{
	int num;
	base_t *base = B_GetCurrentSelectedBase();

	if (Cmd_Argc() < 2) {
		Com_Printf("Usage: %s <num>\n", Cmd_Argv(0));
		return;
	}

	if (!base)
		return;

	if (buyCat == FILTER_AIRCRAFT) {
		Com_DPrintf(DEBUG_CLIENT, "BS_SellItem_f: Redirects to BS_SellAircraft_f\n");
		BS_SellAircraft_f();
		return;
	}

	num = atoi(Cmd_Argv(1));
	if (num < 0 || num >= buyList.length)
		return;

	Cmd_ExecuteString(va("buy_selectitem %i\n", num + buyList.scroll));
	if (buyCat == FILTER_UGVITEM && buyList.l[num + buyList.scroll].ugv) {
		employee_t *employee;
		/* The list entry is an actual ugv/robot */
		const ugv_t *ugv = buyList.l[num + buyList.scroll].ugv;
		const objDef_t *ugvWeapon;

		UP_UGVDescription(ugv);

		/* Check if we have a weapon for this ugv in the market to sell it. */
		ugvWeapon = INVSH_GetItemByID(ugv->weapon);
		if (!ugvWeapon)
			Com_Error(ERR_DROP, "BS_BuyItem_f: Could not get wepaon '%s' for ugv/tank '%s'.", ugv->weapon, ugv->id);

		employee = E_GetHiredRobot(base, ugv);
		if (!E_UnhireEmployee(employee)) {
			/** @todo message - Couldn't fire employee. */
			Com_DPrintf(DEBUG_CLIENT, "Couldn't sell/fire robot/ugv.\n");
		} else {
			if (base->storage.num[ugvWeapon->idx]) {
				/* If we have a weapon we sell it as well. */
				B_UpdateStorageAndCapacity(base, ugvWeapon, -1, qfalse, qfalse);
				ccs.eMarket.num[ugvWeapon->idx]++;
			}
			BS_BuyType(base);
			CL_UpdateCredits(ccs.credits + ugv->price);	/** @todo make this depend on market as well? */
		}
	} else {
		const objDef_t *item = BS_GetObjectDefition(&buyList.l[num + buyList.scroll]);
		/* don't sell more items than we have */
		const int numItems = min(base->storage.num[item->idx], BS_GetBuySellFactor());
		/* Normal item (or equipment for UGVs/Robots if buyCategory==BUY_HEAVY) */
		assert(item);
		currentSelectedMenuEntry = item;
		INV_ItemDescription(item);

		/* don't sell more items than we have */
		if (numItems) {
			/* reinit the menu */
			B_UpdateStorageAndCapacity(base, item, -numItems, qfalse, qfalse);
			ccs.eMarket.num[item->idx] += numItems;

			BS_BuyType(base);
			CL_UpdateCredits(ccs.credits + ccs.eMarket.bid[item->idx] * numItems);
			BS_UpdateItem(base, num);
		}
	}
}

static void BS_BuySellItem_f (void)
{
	int num;
	float value;

	if (Cmd_Argc() < 3) {
		Com_Printf("Usage: %s <num> <value>\n", Cmd_Argv(0));
		return;
	}

	num = atoi(Cmd_Argv(1));
	value = atof(Cmd_Argv(2));
	if (value == 0)
		return;

	if (value > 0) {
		BS_BuyItem_f();
	} else {
		BS_SellItem_f();
	}
}

/**
 * @brief Enable or disable autosell option for given itemtype.
 */
static void BS_Autosell_f (void)
{
	int num;
	const objDef_t *item;
	base_t *base = B_GetCurrentSelectedBase();

	/* Can be called from everywhere. */
	if (!base)
		return;

	if (Cmd_Argc() < 2) {
		Com_Printf("Usage: %s <num>\n", Cmd_Argv(0));
		return;
	}

	num = atoi(Cmd_Argv(1));
	Com_DPrintf(DEBUG_CLIENT, "BS_Autosell_f: listnumber %i\n", num);
	if (num < 0 || num >= buyList.length)
		return;

	item = BS_GetObjectDefition(&buyList.l[num + buyList.scroll]);
	assert(item);

	if (ccs.autosell[item->idx]) {
		ccs.autosell[item->idx] = qfalse;
		Com_DPrintf(DEBUG_CLIENT, "item name: %s, autosell false\n", item->name);
	} else {
		/* Don't allow to enable autosell for items not researched. */
		if (!RS_IsResearched_ptr(item->tech))
			return;
		ccs.autosell[item->idx] = qtrue;
		Com_DPrintf(DEBUG_CLIENT, "item name: %s, autosell true\n", item->name);
	}

	/* Reinit the menu. */
	BS_BuyType(base);
}



void BS_InitCallbacks(void)
{
	Cmd_AddCommand("buy_type", BS_BuyType_f, NULL);
	Cmd_AddCommand("market_click", BS_MarketClick_f, "Click function for buy menu text node");
	Cmd_AddCommand("market_scroll", BS_MarketScroll_f, "Scroll function for buy menu");
	Cmd_AddCommand("mn_buysell", BS_BuySellItem_f, NULL);
	Cmd_AddCommand("mn_buy", BS_BuyItem_f, NULL);
	Cmd_AddCommand("mn_sell", BS_SellItem_f, NULL);
	Cmd_AddCommand("buy_autosell", BS_Autosell_f, "Enable or disable autosell option for given item.");

	memset(&buyList, 0, sizeof(buyList));
	buyList.length = -1;
}

void BS_ShutdownCallbacks(void)
{
	Cmd_RemoveCommand("buy_type");
	Cmd_RemoveCommand("market_click");
	Cmd_RemoveCommand("market_scroll");
	Cmd_RemoveCommand("mn_buysell");
	Cmd_RemoveCommand("mn_buy");
	Cmd_RemoveCommand("mn_sell");
	Cmd_RemoveCommand("buy_autosell");
}
