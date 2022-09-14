
#include "../client.h"
#pragma pack(push, 8)
#include "discord_game_sdk.h"
#pragma pack(pop)


void CL_Discord (void)
{
	// xfiretest
	char *keys[3], *values[3];
	int pic;
	char *picname;
	int i;
	static char szLastValue[64] = "Not Playing";

	keys[0] = "Team";
	values[0] = "Not Playing";

	// Hack to get player's team color from the HUD
	for (i = STAT_PB_TEAM1; i <= STAT_PB_TEAM4; i++)
	{
		if (pic = cl.frame.playerstate.stats[i])
		{
			picname = cl.configstrings[CS_IMAGES + pic];

			if (picname)
			{
				if (strncmp(picname, "i_red", 5) == 0 && strchr(picname, '2'))
					values[0] = "Red";
				else if (strncmp(picname, "i_blue", 5) == 0 && strchr(picname, '2'))
					values[0] = "Blue";
				else if (strncmp(picname, "i_yellow", 5) == 0 && strchr(picname, '2'))
					values[0] = "Yellow";
				else if (strncmp(picname, "i_purple", 5) == 0 && strchr(picname, '2'))
					values[0] = "Purple";
			}
		}
	}

	if (!Q_streq(szLastValue, values[0]))
	{
		// todo XfireSetCustomGameData(1, keys, values);
		Q_strncpyz(szLastValue, values[0], sizeof(szLastValue));
	}
}
