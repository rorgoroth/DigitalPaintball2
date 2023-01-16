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
#include "bot_files.h"
#include "bot_waypoints.h"


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
cvar_t *bot_min_bots = NULL;
cvar_t *sv_gravity = NULL;
qboolean g_admin_modified_bots = false;
#ifdef _DEBUG
cvar_t *bot_remove_near_waypoints = NULL;
#endif

void FreeObservations (void);
void BotInitAim (void);
void UpdatePlayerPosHistory(int msec);
void RemoveBot (edict_t *ent, qboolean manually_removed);
void BotUpdateWaypoints (void);
void BotInitObservations (const char *mapname);


void BotInitMap (const char *mapname, int game_mode)
{
	int i;
	BotInitAim();
	BotClearObjectives();
	BotClearGoals();
	memset(bots.movement, 0, sizeof(bots.movement));
	memset(bots.goals, 0, sizeof(bots.goals));
	BotReadWaypoints(mapname);
	BotInitObservations(mapname);
	Q_strncpyz(bots.levelname, mapname, sizeof(bots.levelname));
	bots.game_mode = game_mode;
	bots.defending_team = 1; // default siege team to start for warmup.

	bots.count = 0; // make sure bots are cleared out, otherwise things can get screwy.

	for (i = 0; i < bots.num_to_readd; ++i)
	{
		AddBot(bots.names_to_readd[i], false);
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
	g_bot_export.apiversion = BOT_API_VERSION;
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
	g_bot_export.GetRandomWaypointPositions = BotGetRandomWaypointPositions;
	g_bot_export.RemoveBot = RemoveBot;

	// If the API version is mismatched, this could potentially stomp on memory or something.
	if (import->apiversion == BOT_API_VERSION)
	{
		bi = *import;
	}

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
	bot_min_bots = bi.cvar("bot_min_bots", "0", 0);
	bots_vs_humans->modified = false;
	bot_min_players->modified = false;
	bot_min_bots->modified = false;
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


int g_time_to_next_connection_check_ms = 0;

void BotUpdateConnections (int msec)
{
	g_time_to_next_connection_check_ms -= msec;
	if (g_time_to_next_connection_check_ms <= 0 && bots.level_time > 10.0) // wait a few seconds after the map changes so bots don't disconnect while players are loading and make teams unbalanced.
	{
		int num_active_players = bi.GetNumPlayersOnTeams();
		int num_players_total = bi.GetNumPlayersTotal();
		int num_active_humans = num_active_players - bots.count;
		int num_humans_total = num_players_total - bots.count;
		int bots_needed = -1;

		// Go back to using bot settings if everybody has disconnected.
		if (num_players_total == 0)
		{
			g_admin_modified_bots = false;
		}

		g_time_to_next_connection_check_ms = 3000 + nu_rand(1000); // Every 3 - 4 seconds, check to see if we need to add a bot

		// TODO: Force bots and humans to be on different teams.
		if (bots_vs_humans->value > 0)
		{
			bots_needed = ceil(bots_vs_humans->value * num_active_humans);
		}

		// If players on the server is less than this, bots will fill in
		if (bot_min_players->value > 0)
		{
			if (num_active_humans > 0)
			{
				int fill_count = bot_min_players->value - num_active_humans;

				if (fill_count > bots_needed)
				{
					bots_needed = fill_count;
				}
			}
			else
			{
				bots_needed = 0;
			}
		}

		// If players on server, guarantee there are at least this many bots
		if (bot_min_bots->value > 0)
		{
			if (num_active_humans > 0)
			{
				if (bots_needed < bot_min_bots->value)
				{
					bots_needed = bot_min_bots->value;
				}
			}
			else
			{
				bots_needed = 0;
			}
		}

		// If an admin explicitly adds or removes bots, ignore the settings and just leave the bots as-is
		if (bots_needed >= 0 && !g_admin_modified_bots)
		{
			int bot_diff;

			if (bots.count > bots_needed)
			{
				int r = rand() % bots.count;
				edict_t *bot_ent = bots.ents[r];
				RemoveBot(bot_ent, false);
			}
			else if (bots.count < bots_needed)
			{
				AddBot(NULL, false);
			}

			bot_diff = abs(bots.count - bots_needed);

			if (bot_diff > 1)
			{
				g_time_to_next_connection_check_ms /= bot_diff; // (dis)connect bots faster based on how many are needed.
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

	// If one of the bot cvars changes, start auto-managing the bots again, in case the admin added/removed a bot, then changed a setting later.
	if (bots_vs_humans->modified || bot_min_players->modified || bot_min_bots->modified)
	{
		bots_vs_humans->modified = false;
		bot_min_players->modified = false;
		bot_min_bots->modified = false;
		g_admin_modified_bots = false;
	}
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


qboolean NameUnused (const char *name)
{
	edict_t *ent = bi.GetNextPlayerEnt(NULL, false);

	while (ent)
	{
		const char *other_name = bi.GetClientName(ent);

		if (Q_strcaseeq(name, other_name))
		{
			return false;
		}

		ent = bi.GetNextPlayerEnt(ent, false);
	}

	return true;
}

qboolean GetBotNameFromFile (char *name_out, size_t size)
{
	int num_lines = 0;
	char line[MAX_QPATH];
	FILE *fp;

	fp = FS_OpenFile("configs/botnames.txt", "rb");
	
	if (!fp)
	{
		fp = FS_OpenFile("configs/botnames_sample.txt", "rb");
	}

	if (fp)
	{
		qboolean first_pass = true;

		if (num_lines <= 0)
		{
			num_lines = 0;
			while(FS_ReadLine(fp, line, sizeof(line), true))
			{
				++num_lines;
			}
		}

		fseek(fp, 0, 0);
		rewind(fp);

		if (num_lines > 0)
		{
			int random_line_index = (int)nu_rand(num_lines); // TODO: Check if a name is already used.
			int line_index = 0;

retry:
			while (FS_ReadLine(fp, line, sizeof(line), true))
			{
				if (line_index >= random_line_index)
				{
					if (NameUnused(line))
					{
						Q_strncpyz(name_out, line, size);
						fclose(fp);
						return true;
					}
				}
				++line_index;
			}

			// Went through the whole file from a random line looking for a name but failed.  Try once more from the start.
			if (first_pass)
			{
				random_line_index = 0;
				first_pass = false;
				rewind(fp);
				goto retry;
			}
		}

		fclose(fp); // shouldn't ever get here, but just to be safe.
	}

	return false;
}


void AddBot (const char *name, qboolean manually_added)
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
		if (!GetBotNameFromFile(default_bot_name, sizeof(default_bot_name)))
		{
			Com_sprintf(default_bot_name, sizeof(default_bot_name), "DPBot%02d", bots.count + 1);
		}
		name = default_bot_name;
	}

	SetBotUserInfo(userinfo, sizeof(userinfo), name);
	ent = bi.AddBotClient(userinfo);

	if (ent)
	{
		bots.ents[bots.count] = ent;
		bots.count++;

		bi.dprintf("Adding bot: %s\n", name);

		if (manually_added)
		{
			g_admin_modified_bots = true;
		}
	}
	else
	{
		bi.dprintf("Failed to add bot.  Server full?  Try increasing maxclients.\n");
	}
}
