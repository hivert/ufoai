/**
 * @file g_utils.c
 * @brief Misc utility functions for game module.
 */

/*
All original materal Copyright (C) 2002-2007 UFO: Alien Invasion team.

Original file from Quake 2 v3.21: quake2-2.31/game/g_utils.c
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

#include "g_local.h"
#include <time.h>

/**
 * @brief Marks the edict as free
 * @sa G_Spawn
 */
void G_FreeEdict (edict_t *ent)
{
	/* unlink from world */
	gi.UnlinkEdict(ent);

	memset(ent, 0, sizeof(*ent));
	ent->classname = "freed";
	ent->inuse = qfalse;
}

/**
 * @brief Call the 'use' function for the given edict and all its group members
 * @param[in] ent The edict to call the use function for
 * @sa G_ClientUseEdict
 */
qboolean G_UseEdict (edict_t *ent)
{
	if (!ent) {
		Com_DPrintf(DEBUG_GAME, "G_UseEdict: No edict given\n");
		return qfalse;
	}

	/* no use function assigned */
	if (!ent->use)
		return qfalse;

	if (!ent->use(ent))
		return qfalse;

	/* only the master edict is calling the opening for the other group parts */
	if (!(ent->flags & FL_GROUPSLAVE)) {
		edict_t* chain = ent->groupChain;
		while (chain) {
			G_UseEdict(chain);
			chain = chain->groupChain;
		}
	}

	return qtrue;
}

/**
 * @brief Searches for the obj that has the given firedef
 */
static const objDef_t* G_GetObjectForFiredef (const fireDef_t* fd)
{
	int i, j, k;
	const fireDef_t *csiFD;
	const objDef_t *od;

	/* For each object ... */
	for (i = 0; i < gi.csi->numODs; i++) {
		od = &gi.csi->ods[i];
		/* For each weapon-entry in the object ... */
		for (j = 0; j < od->numWeapons; j++) {
			/* For each fire-definition in the weapon entry  ... */
			for (k = 0; k < od->numFiredefs[j]; k++) {
				csiFD = &od->fd[j][k];
				if (csiFD == fd)
					return od;
			}
		}
	}

	Com_DPrintf(DEBUG_GAME, "Could nor find a objDef_t for fireDef_t '%s'\n", fd->name);

	return NULL;
}

/**
 * @brief Return the corresponding weapon name for a give firedef
 * @sa G_GetObjectForFiredef
 */
const char* G_GetWeaponNameForFiredef (const fireDef_t *fd)
{
	const objDef_t* obj = G_GetObjectForFiredef(fd);
	if (!obj)
		return "unknown";
	else
		return obj->id;
}

/**
 * @brief Returns the player name for a give player number
 */
const char* G_GetPlayerName (int pnum)
{
	if (pnum >= game.sv_maxplayersperteam)
		return "";
	else
		return game.players[pnum].pers.netname;
}

/**
 * @brief Prints stats to game console and stats log file
 * @sa G_PrintActorStats
 */
void G_PrintStats (const char *buffer)
{
	struct tm *t;
	char tbuf[32];
	time_t aclock;

	time(&aclock);
	t = localtime(&aclock);

	Com_sprintf(tbuf, sizeof(tbuf), "%4i/%02i/%02i %02i:%02i:%02i", t->tm_year + 1900, t->tm_mon + 1, t->tm_mday, t->tm_hour, t->tm_min, t->tm_sec);
	Com_Printf("[STATS] %s - %s\n", tbuf, buffer);
	if (logstatsfile)
		fprintf(logstatsfile, "[STATS] %s - %s\n", tbuf, buffer);
}

/**
 * @brief Prints stats about who killed who with what and how
 * @sa G_Damage
 * @sa G_PrintStats
 */
void G_PrintActorStats (const edict_t *victim, const edict_t *attacker, const fireDef_t *fd)
{
	const char *victimName, *attackerName;
	char buffer[512];

	if (victim->pnum != attacker->pnum) {
		victimName = G_GetPlayerName(victim->pnum);
		if (!*victimName) { /* empty string */
			switch (victim->team) {
			case TEAM_CIVILIAN:
				victimName = "civilian";
				break;
			case TEAM_ALIEN:
				victimName = "alien";
				break;
			default:
				victimName = "unknown";
				break;
			}
		}
		attackerName = G_GetPlayerName(attacker->pnum);
		if (!*attackerName) { /* empty string */
			switch (attacker->team) {
			case TEAM_CIVILIAN:
				attackerName = "civilian";
				break;
			case TEAM_ALIEN:
				attackerName = "alien";
				break;
			default:
				attackerName = "unknown";
				break;
			}
		}
		if (victim->team != attacker->team) {
			Com_sprintf(buffer, sizeof(buffer), "%s (%s) %s %s (%s) with %s of %s",
				attackerName, attacker->chr.name,
				(victim->HP == 0 ? "kills" : "stuns"),
				victimName, victim->chr.name, fd->name, G_GetWeaponNameForFiredef(fd));
		} else {
			Com_sprintf(buffer, sizeof(buffer), "%s (%s) %s %s (%s) (teamkill) with %s of %s",
				attackerName, attacker->chr.name,
				(victim->HP == 0 ? "kills" : "stuns"),
				victimName, victim->chr.name, fd->name, G_GetWeaponNameForFiredef(fd));
		}
	} else {
		attackerName = G_GetPlayerName(attacker->pnum);
		Com_sprintf(buffer, sizeof(buffer), "%s %s %s (own team) with %s of %s",
			attackerName, (victim->HP == 0 ? "kills" : "stuns"),
			victim->chr.name, fd->name, G_GetWeaponNameForFiredef(fd));
	}
	G_PrintStats(buffer);
}

/**
 * @brief Searches all active entities for the next one that holds
 * the matching string at fieldofs (use the offsetof() macro) in the structure.
 *
 * @note Searches beginning at the edict after from, or the beginning if NULL
 * @return NULL will be returned if the end of the list is reached.
 */
edict_t *G_Find (edict_t * from, int fieldofs, char *match)
{
	char *s;

	if (!from)
		from = g_edicts;
	else
		from++;

	for (; from < &g_edicts[globals.num_edicts]; from++) {
		if (!from->inuse)
			continue;
		s = *(char **) ((byte *) from + fieldofs);
		if (!s)
			continue;
		if (!Q_stricmp(s, match))
			return from;
	}

	return NULL;
}


/**
 * @brief Returns entities that have origins within a spherical area
 * @param[in] from The origin that is the center of the circle
 * @param[in] org origin
 * @param[in] rad radius to search an edict in
 */
edict_t *G_FindRadius (edict_t * from, vec3_t org, float rad, entity_type_t type)
{
	vec3_t eorg;
	int j;

	if (!from)
		from = g_edicts;
	else
		from++;
	for (; from < &g_edicts[globals.num_edicts]; from++) {
		if (!from->inuse)
			continue;
		for (j = 0; j < 3; j++)
			eorg[j] = org[j] - (from->origin[j] + (from->mins[j] + from->maxs[j]) * 0.5);
		if (VectorLength(eorg) > rad)
			continue;
		if (type != ET_NULL && from->type != type)
			continue;
		return from;
	}

	return NULL;
}

/**
 * @brief creates an entity list
 * @param[out] entList A list of all active inline model entities
 * @sa G_RecalcRouting
 * @sa G_LineVis
 */
void G_GenerateEntList (const char *entList[MAX_EDICTS])
{
	int i;
	edict_t *ent;

	/* generate entity list */
	for (i = 0, ent = g_edicts; ent < &g_edicts[globals.num_edicts]; ent++)
		if (ent->inuse && ent->model && *ent->model == '*' && ent->solid == SOLID_BSP)
			entList[i++] = ent->model;
	entList[i] = NULL;
}

/**
 * @sa G_CompleteRecalcRouting
 * @sa Grid_RecalcRouting
 */
void G_RecalcRouting (const edict_t * self)
{
	const char *entList[MAX_EDICTS];
	/* generate entity list */
	G_GenerateEntList(entList);
	/* recalculate routing */
	gi.GridRecalcRouting(gi.routingMap, self->model, entList);
}

/**
 * @sa G_RecalcRouting
 */
void G_CompleteRecalcRouting (void)
{
	edict_t *ent;

	/* generate entity list */
	for (ent = g_edicts; ent < &g_edicts[globals.num_edicts]; ent++)
		if (ent->inuse && ent->model && *ent->model == '*' && ent->solid == SOLID_BSP) {
			Com_DPrintf(DEBUG_GAME, "Processing entity %i: inuse:%i model:%s solid:%i\n", ent->number, ent->inuse, ent->model, ent->solid);
			G_RecalcRouting(ent);
		} else {
			Com_DPrintf(DEBUG_GAME, "Did not process entity %i: inuse:%i model:%s solid:%i\n", ent->number, ent->inuse, ent->model, ent->solid);
		}
}

/**
 * @brief Check the world against triggers for the current entity (actor)
 */
int G_TouchTriggers (edict_t *ent)
{
	int i, num, usedNum = 0;
	edict_t *touch[MAX_EDICTS];

	if (ent->type != ET_ACTOR || (ent->state & STATE_DEAD))
		return 0;

	num = gi.BoxEdicts(ent->absmin, ent->absmax, touch, MAX_EDICTS, AREA_TRIGGERS);

	/* be careful, it is possible to have an entity in this
	 * list removed before we get to it (killtriggered) */
	Com_DPrintf(DEBUG_GAME, "G_TouchTriggers: Found %i possible triggers.\n", num);
	for (i = 0; i < num; i++) {
		edict_t *hit = touch[i];
		if (!hit->inuse)
			continue;
		if (!hit->touch)
			continue;
		if (hit->touch(hit, ent))
			usedNum++;
	}
	return usedNum;
}
