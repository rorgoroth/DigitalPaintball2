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

#pragma once

#ifndef _BOT_MANAGER_H_
#define _BOT_MANAGER_H_

#include "bot_goals.h"
#include "bot_waypoints.h"

//typedef struct edict_s edict_t;

#define MAX_BOTS 64


// For following observed player paths
typedef struct botfollowpath_s {
	qboolean		on_path; // Are we actively following a waypoint path?
	qboolean		started;
	int				path_index; // Which path are we following?
	int				index_in_path; // Where are we along that path?
	int				msec_to_reach_start; // time we've been trying to reach the start point (so we can have a timeout failure)
} botfollowpath_t;


typedef struct botmovedata_s {
	int					timeleft; // time left for ucmd's.
	float				last_trace_dist;
	float				yawspeed; // degrees/sec
	float				pitchspeed;
	float				aimtimeleft; // when aiming, how much time is left before we reach our desired aim direction (so this can be randomized and not match the frame dt exactly)
	vec3_t				desired_angles;
	short				forward; // forward/back
	short				side; // strafe left/right
	short				up; // jump/crouch
	edict_t				*aim_target;
	vec3_t				last_target_pos;
	int					last_target_msec; // time since target was last seen, in ms.
	qboolean			shooting;
	float				shooting_commit_time; // how much time left this bot is going to commit to shooting.
	short				time_since_last_turn; // used for wandering
	short				time_til_try_path; // used for wandering
	float				last_yaw;
	float				last_pitch;
	vec3_t				last_pos;
	vec3_t				velocity; // approximate velocity based on position/time.
	botfollowpath_t		path_info; // for following observed player paths
	bot_waypoint_path_t	waypoint_path;
	qboolean			stop; // defend/camp/whatever
	qboolean			need_jump; // need to jump next time we're on the ground.
} botmovedata_t;


typedef struct botmanager_s {
	// Note: Anything with a bot index needs to be updated in BotHandleDisconnect()
	edict_t				*ents[MAX_BOTS];
	botmovedata_t		movement[MAX_BOTS];
	botgoal_t			goals[MAX_BOTS];
	int					goal_debug_spheres[MAX_BOTS];
	char				names_to_readd[MAX_BOTS][64]; // bots to re-add after map change

	int					count; // total number of bots currently in the map
	int					num_to_readd; // number of bots to re-add
	float				level_time;
	float				last_waypoint_add_time;
	char				levelname[MAX_QPATH];
	int					time_since_last_pathfind;
	int					game_mode; // corresponds to the game's "ctfmode"
	int					defending_team; // for siege mode
	qboolean			admin_modified_bots;
	qboolean			between_rounds; // In between rounds, so do some silly nonsense.
	float				round_start_time; // level_time at which the round started.
} botmanager_t;

extern botmanager_t bots;

#endif // _BOT_MANAGER_H_
