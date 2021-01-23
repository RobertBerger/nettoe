#ifndef __GAME_H__
#define __GAME_H__ 1

/* netToe Version 1.5.1
 *
 * Copyright 2000,2001 Gabriele Giorgetti <ggdev@users.sourceforge.net>
 *           2009-2014 Mats Erik Andersson <meand@users.sourceforge.net>
 *
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*/


    #if defined(GAME_PRIVATE_PROTOTYPES)
        #define EXTERN
    #else
        #define EXTERN  extern
    #endif



#define HOMEPAGE     "http://nettoe.sourceforge.net/"
#define AUTHOR_EMAIL "<ggdev@users.sourceforge.net>"
#define AUTHOR2_EMAIL "<meand@users.sourceforge.net>"

#ifndef RELEASE_DATE
# ifdef PACKAGE_RELEASE_DATE
#  define RELEASE_DATE PACKAGE_RELEASE_DATE
# else
#  define RELEASE_DATE "unknown"
# endif
#endif

#ifndef SERVER_PORT_NUMBER
# ifdef NETTOE_PORT
#  define SERVER_PORT_NUMBER NETTOE_PORT
# else
#  define SERVER_PORT_NUMBER 7501
# endif
#endif

#define MAXDATASIZE 50

#define MAX_PNAME_LEN 32 /* this define max chars for the player names*/

EXTERN int NO_BEEP;

EXTERN int NO_COLORS;

EXTERN int addrfamily;

#endif /* __GAME_H__ */
