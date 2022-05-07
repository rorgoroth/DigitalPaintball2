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

#include "bot_main.h"
#include "bot_files.h"

/*
============
FS_CreatePath

Creates any directories needed to store the given filename
============
*/
int _mkdir();
int mkdir();

void FS_CreatePath (char *path)
{
	char	*ofs;
	
	for (ofs = path + 1; *ofs; ++ofs)
	{
		if (*ofs == '/' || *ofs == '\\')
		{	// create the directory
			*ofs = 0;
#ifdef WIN32
			_mkdir(path);
#else
			mkdir(path, 0777);
#endif
			*ofs = '/';
		}
	}
}


FILE *FS_OpenFile (const char *filename, const char *mode)
{
	const char *gamedir = BASEDIRNAME;
	cvar_t *game = bi.cvar("game", gamedir, 0);
	char filename_full[MAX_QPATH];

	Q_snprintfz(filename_full, sizeof(filename_full), "%s/%s", gamedir, filename);
	return fopen(filename_full, mode);
}


qboolean FS_ReadLine (FILE *fp, char *line, size_t size, qboolean ignore_comments)
{
	int col = 0; // column
	qboolean is_comment = false;

	if (fp)
	{
		int c = fgetc(fp);

		while (c != EOF)
		{
			if (c != '\r')
			{
				if (col < size)
				{
					if (c == '/' && ignore_comments)
					{
						int nextc = getc(fp);
						if (nextc == '/')
						{
							is_comment = true;
						}
						else
						{
							ungetc(nextc, fp);
						}
					}

					if (!is_comment)
					{
						line[col] = c;
						++col;
					}
				}
			}

			c = fgetc(fp);

			// Break out of the loop on newline, unless the whole line was a comment, in which case we read another character to move to the next line.
			if (c == '\n')
			{
				if (is_comment && col <= 0)
				{
					c = fgetc(fp);
					is_comment = false;
				}
				else
				{
					break;
				}
			}
		}

		if (EOF)
		{
			if (col == 0)
			{
				return false;
			}
		}

		line[col] = 0; // null terminate
		return true;
	}
	else
	{
		return false;
	}
}
