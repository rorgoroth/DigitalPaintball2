/*
Copyright (c) 2014-2020 Nathan "jitspoe" Wulf, Digital Paint

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

#include "bot_main.h"
#include "bot_manager.h"
#include "bot_goals.h"

#ifdef WIN32
#define EXPORT __declspec(dllexport)
#else
#define EXPORT
#endif

bot_export_t g_bot_export;
bot_import_t bi;
bot_render_import_t ri;
botmanager_t bots;
cvar_t *skill = NULL;
cvar_t *bot_debug = NULL;
cvar_t *bots_vs_humans = NULL;
cvar_t *bot_min_players = NULL;
cvar_t *sv_gravity = NULL;
#ifdef _DEBUG
cvar_t *bot_remove_near_waypoints = NULL;
#endif

void FreeObservations (void);
void BotInitAim (void);
void UpdatePlayerPosHistory(int msec);
void RemoveBot (edict_t *ent);
void BotUpdateWaypoints (void);


void BotInitMap (const char *mapname, int game_mode)
{
	int i;
	BotInitAim();
	BotClearObjectives();
	BotClearGoals();
	memset(bots.movement, 0, sizeof(bots.movement));
	memset(bots.goals, 0, sizeof(bots.goals));
	BotReadWaypoints(mapname);
	Q_strncpyz(bots.levelname, mapname, sizeof(bots.levelname));
	bots.game_mode = game_mode;
	bots.defending_team = 1; // default siege team to start for warmup.

	bots.count = 0; // make sure bots are cleared out, otherwise things can get screwy.

	for (i = 0; i < bots.num_to_readd; ++i)
	{
		AddBot(bots.names_to_readd[i]);
	}

	bots.num_to_readd = 0;
	bots.last_waypoint_add_time = 0.0f;
}


void BotShutdownMap (void)
{
	bi.dprintf("DP Botlib closing map.\n");
	BotWriteWaypoints(bots.levelname);
	BotClearObjectives();
	BotClearGoals();
	memset(bots.movement, 0, sizeof(bots.movement));
	FreeObservations();
}


EXPORT bot_export_t *GetBotAPI (bot_import_t *import)
{
	bi = *import;
	g_bot_export.apiversion = BOT_API_VERSION; // need to actually validate this at some point.
	g_bot_export.Init = BotInitLibrary;
	g_bot_export.InitMap = BotInitMap;
	g_bot_export.ShutdownMap = BotShutdownMap;
	g_bot_export.Shutdown = BotShutdown;
	g_bot_export.GameEvent = BotHandleGameEvent;
	g_bot_export.RunFrame = BotRunFrame;
	g_bot_export.Command = BotCommand;
	g_bot_export.ExitLevel = BotExitLevel;
	g_bot_export.ObservePlayerInput = BotObservePlayerInput;
	g_bot_export.AddObjective = BotAddObjective;
	g_bot_export.RemoveObjective = BotRemoveObjective;
	g_bot_export.ClearObjectives = BotClearObjectives;
	g_bot_export.PlayerDie = BotPlayerDie; // todo
	g_bot_export.SetDefendingTeam = BotSetDefendingTeam;

	return &g_bot_export;
}


EXPORT void SetBotRendererAPI (bot_render_import_t *import)
{
	ri = *import;
}


void BotInitLibrary (void)
{
	bi.dprintf("DP Botlib Initialized.\n");
	memset(&bots, 0, sizeof(bots));
	skill = bi.cvar("skill", "0", 0);
	bot_debug = bi.cvar("bot_debug", "0", 0);
	bots_vs_humans = bi.cvar("bots_vs_humans", "0", 0);
	bot_min_players = bi.cvar("bot_min_players", "0", 0);
	sv_gravity = bi.cvar("sv_gravity", "800", 0);
#ifdef _DEBUG
	bot_remove_near_waypoints = bi.cvar("bot_remove_near_waypoints", "0", 0);
#endif
	BotInitAim();
#ifdef BOT_DEBUG
	memset(bots.goal_debug_spheres, -1, sizeof(bots.goal_debug_spheres));
#endif
}


void BotShutdown (void)
{
}


int g_time_since_last_connection_check = 0;

void BotUpdateConnections (int msec)
{
	// Every 5 seconds, check to see if we need to add a bot
	g_time_since_last_connection_check += msec;
	if (g_time_since_last_connection_check > 5000)
	{
		int num_players = bi.GetNumPlayersOnTeams();
		int num_humans = num_players - bots.count;
		int bots_needed = -1;

		g_time_since_last_connection_check = 0;

		if (bots_vs_humans->value > 0)
		{
			bots_needed = ceil(bots_vs_humans->value * num_humans);
		}

		if (bot_min_players->value > 0)
		{
			if (num_humans > 0)
			{
				bots_needed = bot_min_players->value - num_humans;

				if (bots_needed < 0)
				{
					bots_needed = 0;
				}
			}
			else
			{
				bots_needed = 0;
			}
		}

		if (bots_needed >= 0)
		{
			if (bots.count > bots_needed)
			{
				int r = rand() % bots.count;
				edict_t *bot_ent = bots.ents[r];
				RemoveBot(bot_ent);
			}
			else if (bots.count < bots_needed)
			{
				AddBot(NULL);
			}
		}
	}
}



void BotRunFrame (int msec, float level_time)
{
	bots.level_time = level_time;
	BotUpdateGoals(msec);
	BotUpdateMovement(msec);
	BotUpdateWaypoints();
	UpdatePlayerPosHistory(msec);
	BotUpdateConnections(msec);
}


// Called when the game changes levels
void BotExitLevel (void)
{
	int i;

	for (i = 0; i < bots.count; ++i)
	{
		edict_t *ent = bots.ents[i];
		const char *name = bi.GetClientName(ent);
		Q_strncpyz(bots.names_to_readd[i], name, sizeof(bots.names_to_readd[i]));
	}

	bots.num_to_readd = bots.count;
	bots.count = 0; // all clients and bots will disappear when the map changes and must be readded.
}


static void SetBotUserInfo (char *userinfo, size_t size, const char *name)
{
	memset(userinfo, 0, size);
	Info_SetValueForKey(userinfo, "name", name);
	Info_SetValueForKey(userinfo, "hand", "2"); // Bot is centerhanded
	Info_SetValueForKey(userinfo, "skin", "male/pb2b");
}


void AddBot (const char *name)
{
	char userinfo[MAX_INFO_STRING];
	edict_t *ent;
	char default_bot_name[32];

	if (bots.count >= MAX_BOTS)
	{
		bi.dprintf("You cannot add more than %d bots.\n", MAX_BOTS);
	}

	if (!name || !*name)
	{
		Com_sprintf(default_bot_name, sizeof(default_bot_name), "DPBot%02d", bots.count + 1);
		name = default_bot_name;
	}

	SetBotUserInfo(userinfo, sizeof(userinfo), name);
	ent = bi.AddBotClient(userinfo);

	if (ent)
	{
		bots.ents[bots.count] = ent;
		bots.count++;

		bi.dprintf("Adding bot: %s\n", name);
	}
	else
	{
		bi.dprintf("Failed to add bot.  Server full?  Try increasing maxclients.\n");
	}
}
