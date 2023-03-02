/*
Copyright (c) 2014 Nathan "jitspoe" Wulf, Digital Paint

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

// This file deals with functions that observe and record player movement, so bots can replay that information later to navigate maps with complex jumps.
//

#include "bot_main.h"
#include "../game/game.h"
#include "bot_manager.h"
#include "bot_observe.h"
#include "bot_debug.h"


// These aren't used anymore -- not precise enough.  Now we only record input on spawns.
#define PLAYER_OBSERVE_MIN_MOVE_SPEED 3 // start recording players moving this fast
#define PLAYER_PATH_MIN_DIST 250 // quake units.  to cut back on short paths from strafing back and forth.

extern cvar_t *bot_use_player_input;

player_observation_t		g_player_observations[MAX_PLAYERS_TO_RECORD];
player_recorded_paths_t		g_player_paths;


usercmd_t g_lastplayercmd; // used for BotCopyPlayer
usercmd_t g_playercmd; // used for BotCopyPlayer


void BotInitObservations (const char *mapname)
{
	if (!g_player_paths.path_capacity)
	{
		g_player_paths.path_capacity = MAX_RECORDED_PATHS;
		g_player_paths.paths = bi.TagMalloc(sizeof(player_recorded_path_t) * MAX_RECORDED_PATHS, 0/*TAG_LEVEL*/);
	}

	// Each path allocation has been allocated with TAG_LEVEL, so it *should* free up after level change.

	g_player_paths.num_paths = 0;
	// TODO: load/save paths on map change.
}


void BotAddPlayerObservation (player_observation_t *observation)
{
	int i, total_points = observation->current_index;
	int path_index = g_player_paths.num_paths;

	if (VectorSquareDistance(observation->start_pos, observation->end_pos) < PLAYER_PATH_MIN_DIST * PLAYER_PATH_MIN_DIST)
	{
		if (bot_debug->value)
			bi.dprintf("Path too short.  Discarded.\n");
		return;
	}

	if (path_index < g_player_paths.path_capacity)
	{
		int total_msec = 0;
		player_recorded_path_t *recorded_path = g_player_paths.paths + path_index;
		player_input_data_t *input_data = bi.TagMalloc(sizeof(player_input_data_t) * total_points, TAG_LEVEL);

		for (i = 0; i < total_points; ++i)
		{
			// todo: Compression (only record points where input has changed)
			input_data[i] = observation->input_data[i];
			total_msec += observation->input_data[i].msec;
			// TODO: Probably need to store positions every 0.5s or so so we can identify if the bot went off course (ex: collided with another player or didn't start at the correct location)
		}

		VectorCopy(observation->start_pos, recorded_path->start_pos);
		VectorCopy(observation->end_pos, recorded_path->end_pos);
		recorded_path->time = total_msec / 1000.0f;
		recorded_path->input_data = input_data;
		recorded_path->total_points = total_points;

		if (bot_debug->value)
		{
			bi.dprintf("Path %d added.\n", path_index);
		}

		++g_player_paths.num_paths;

		if (bot_debug->value)
		{
			DrawDebugLine(observation->start_pos, observation->end_pos, 1.0, .8, .1, 20.0f, -1);
			//DrawDebugSphere(observation->end_pos, 8.0f, 1.0f, 0.f, 0.5f, 20.0f, -1);
		}
	}
	else
	{
		// TODO: Randomly pick one to overwrite?
		if (bot_debug->value)
			bi.dprintf("Paths full.\n");
	}

	// todo: when full, either increase capacity or start replacing similar paths, or randomly replace paths.  Make sure path is not in use first.  Also try to keep a similar number of paths for each team.
}


float XYPMVelocitySquared (const short *vel)
{
	float x = (float)vel[0] * 0.125f; // velocity is increased 8x's when cast to a short.
	float y = (float)vel[1] * 0.125f;

	return x * x + y * y;
}


void BotCancelObservation (const edict_t *ent)
{
	int player_index = bi.GetPlayerIndexFromEnt(ent);

	if (player_index < MAX_PLAYERS_TO_RECORD)
	{
		player_observation_t *observation = g_player_observations + player_index;

		if (observation->path_active)
		{
			bi.dprintf("Observation path cancelled.\n");
			observation->path_active = false;
			observation->current_index = 0;
		}
	}
}


void BotConvertPmoveToObservationPoint (player_input_data_t *input_data, const edict_t *ent, const pmove_t *pm)
{
	short forward = pm->cmd.forwardmove;
	short side = pm->cmd.sidemove;
	short up = pm->cmd.upmove;

	// It's possible players have these set to wonky values, so limit them to +/-400
	if (forward > 400)
		forward = 400;
	else if (forward < -400)
		forward = -400;

	if (up > 400)
		up = 400;
	else if (up < -400)
		up = -400;

	if (side > 400)
		side = 400;
	else if (side < -400)
		side = -400;

	input_data->angle_yaw = pm->cmd.angles[YAW] + ent->client->ps.pmove.delta_angles[YAW];
	input_data->angle_pitch = pm->cmd.angles[PITCH] + ent->client->ps.pmove.delta_angles[PITCH];
	input_data->forward = forward / PMOVE_8BIT_SCALE; // no need for this much precision.  8 bits should be plenty, even if a joystick is used.
	input_data->side = side / PMOVE_8BIT_SCALE;
	input_data->up = up / PMOVE_8BIT_SCALE;
	input_data->msec = pm->cmd.msec;
	input_data->buttons = pm->cmd.buttons;
	// Should probably only record positions at a smaller interval to save memory, but just save every input frame for testing for now.
	input_data->x = ent->s.origin[0] * 8.0f;
	input_data->y = ent->s.origin[1] * 8.0f;
	input_data->z = ent->s.origin[2] * 8.0f;
}


void BotCompleteObservationPath (const edict_t *ent, player_observation_t *observation)
{
	if (bot_debug->value)
	{
		bi.dprintf("Stopping path\n");
	}

	VectorCopy(ent->s.origin, observation->end_pos);
	BotAddPlayerObservation(observation);
	observation->path_active = false;
}


void BotAddObservationPoint (player_observation_t *observation, const edict_t *ent, const pmove_t *pm)
{
	if (observation->current_index < MAX_PLAYER_OBSERVATION_PATH_POINTS)
	{
		if (pm->cmd.msec > 0)
		{
			BotConvertPmoveToObservationPoint(observation->input_data + observation->current_index, ent, pm);
			++observation->current_index;
		}
		else
		{
			if (bot_debug->value)
			{
				bi.dprintf("Observed msec == 0.\n"); // breakpoint
			}
		}
	}
	else
	{
		if  (bot_debug->value)
		{
			bi.dprintf("Path length exceeded.\n");
		}
		//BotCompleteObservationPath(observation);
	}
}

#define TIME_SINCE_SPAWN_TO_RECORD_PLAYER_INPUT 2.0f

void BotAddPotentialWaypointFromPmove(player_observation_t *observation, const edict_t *ent, const pmove_t *pm);

// Called for each player input packet sent after pmove is calculated, while the player is alive
void BotObservePlayerInput (unsigned int player_index, const edict_t *ent, const pmove_t *pm)
{
	//float time_since_round_start;

	if (bots.count < 1 || !bot_use_player_input->value)
	{
		return;
	}

	//time_since_round_start = bots.level_time - bots.round_start_time;
	// todo: cvar to disable this.
	// todo: reset observation data on player disconnect/map change/etc.
	//BotAddPotentialNavmeshFromPmove(ent, pm);

	// For now, record player input since start of round, since the bots will spawn exactly where players spawn and we can just play the input back for round start behavior.


	if (player_index < MAX_PLAYERS_TO_RECORD)
	{
		player_observation_t *observation = g_player_observations + player_index;

		BotAddPotentialWaypointFromPmove(observation, ent, pm);

#if 1 //  -- TODO: Figure out a better way for bots to follow player paths
		if (!observation->path_active)
		{
			// Unfortunately, the way round spawning works, we don't know if the player just spawned, so check for a large jump in position and assume a spawn happened
			float dist = Distance(ent->s.origin, observation->last_pos);
			qboolean spawned_or_teleported = false;

			if (dist > 128.0f) // 128+ unit difference in one frame, we must have respawned or teleported.
			{
				spawned_or_teleported = true;
			}

			VectorCopy(ent->s.origin, observation->last_pos);

			//float xy_velocity_sq = XYPMVelocitySquared(pm->s.velocity);

			// todo: start on jump as well
			//if (xy_velocity_sq > PLAYER_OBSERVE_MIN_MOVE_SPEED * PLAYER_OBSERVE_MIN_MOVE_SPEED && bi.IsGroundEntityWorld(pm->groundentity))
			if (spawned_or_teleported)
			{
				if (bot_debug->value)
				{
					bi.dprintf("Starting path\n");
				}

				VectorCopy(ent->s.origin, observation->start_pos);
				observation->path_active = true;
				observation->current_index = 0;
				observation->time_started = bots.level_time;
			}
		}
		else
		{
			if (bots.level_time - observation->time_started > TIME_SINCE_SPAWN_TO_RECORD_PLAYER_INPUT) // Ensure we wait a couple seconds before seeing if the player has stopped
			{
				if (XYPMVelocitySquared(pm->s.velocity) < PLAYER_OBSERVE_MIN_MOVE_SPEED * PLAYER_OBSERVE_MIN_MOVE_SPEED && bi.IsGroundEntityWorld(pm->groundentity))
				{
					BotCompleteObservationPath(ent, observation);
				}
			}
		}

		if (observation->path_active)
		{
			BotAddObservationPoint(observation, ent, pm);
		}

		// temp debug
		{
			float vel = sqrt(pm->s.velocity[0] * pm->s.velocity[0] + pm->s.velocity[1] * pm->s.velocity[1] + pm->s.velocity[2] * pm->s.velocity[2]);
			float b = vel / 10000.0f;
			float g = pm->cmd.upmove > 0 ? 1.0f : 0.0f; //vel / 16000.0f;
			float r = vel / 4000.0f;

			if (b > 1.0f)
				b = 1.0f;

			if (g > 1.0f)
				g = 1.0f;

			if (r > 1.0f)
				r = 1.0f;

			//DrawDebugLine(ent->s.origin, observation->last_pos, r, g, b, 70.0f, -1);
		}

		VectorCopy(ent->s.origin, observation->last_pos);
		observation->last_pm = *pm;
#endif
	}

	g_playercmd = pm->cmd;
}


// Called at level changes and shutdowns
void FreeObservations (void)
{
	if (g_player_paths.num_paths > 0)
	{
		bi.TagFree(g_player_paths.paths);
		g_player_paths.path_capacity = 0;
		g_player_paths.num_paths = 0;
	}
}


// TODO: Finish this and cancel on map change
void BotCancelObservations()
{

}


// TODO: Complete observations on round end.
void BotCompleteObservations()
{
}
