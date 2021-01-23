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

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/wait.h>
#include <netdb.h>
#include <signal.h>
#include <time.h>

#include "terminal.h"
/* New Code for the nettoe board matrix, not used yet.
 * #include "matrices.h"
 */
#include "misc.h"
#include "network.h"

#define GAME_PRIVATE_PROTOTYPES
#include "game.h"
#include "board.h"

/* Has network partner reported EOF? */
int has_given_eof = 0;

enum {
  I_AM_SERVER = 0,
  I_AM_CLIENT = 1,
};

#define ASK_SERVER	I_AM_CLIENT
#define ASK_CLIENT	I_AM_SERVER

static char player1_name[MAX_PNAME_LEN];
static char player2_name[MAX_PNAME_LEN];

static int game_mode = 0;
static int who_starts = 0;
static int computer_AI = 0;
static int winner = GAME_IS_ALIVE;

static int player1_status  = 0;
static int player2_status  = 0;
static int computer_status = 0;

static int sock = -1;

static void main_menu (void);
static void cpu_AI_menu (void);
static void network_menu (void);

void print_header (int type);
static void init_one_player_game (void);
static void init_two_player_game (void);
static void do_one_player_game (void);
static void do_two_player_game (void);
static void show_game (void);
static void show_drawed_game (void);
static void show_waiting_for_move (const char *name);
static void get_player1_move (void);
static void get_player2_move (void);
extern void get_cpu_move (int);

static void server_start (void);
static void client_start (void);
static void init_server_network_game (void);
static void init_client_network_game (void);

static void recv_matrix (int fd);
static void send_matrix (int fd);

static void quit_game (void);

/* Reset colour at interrupts SIGINT, SIGKILL, SIGTERM.
 * Use ECMA-48 character code. */
static void reset_color (int sig)
{
  (void) sig;	/* Unused variable, silence warning.  */
  printf ("%c[0m\n", 0x1B);
  exit (EXIT_SUCCESS);
}

int main (int argc, char *argv[])
{

  NO_BEEP   = 0; /* beeps are enabled by default */
                 /* --no-beep disable beeps      */
  NO_COLORS = 0; /* colors are enabled by default */
                 /* --no-colors disable colors    */

  parse_cmd_args(argc, argv);

  /* Remove any elevated access. */
  setegid(getgid());
  setgid(getgid());
  seteuid(getuid());
  setuid(getuid());

  srandom(time(0));

#if USE_TERMINFO || USE_TERMCAP
  setup_terminfo();
#endif

  /* Reset colour at normal exit. */
  atexit(nettoe_term_set_default_color);
  /* And at abnormal exits. */
  signal(SIGINT, reset_color);
  signal(SIGKILL, reset_color);
  signal(SIGTERM, reset_color);

  while (1)
    main_menu ();

  quit_game ();

  return EXIT_SUCCESS;
}

#define SINGLE_DASHED_LINE \
	"+-----------------------------------------------------------+\n"
#define BROKEN_DASHED_LINE \
	"+--------------------------+--------------------------------+\n"
#define SPACES_AND_BARS \
	"           |   |           |              |    |    \n"
#define BLOCKERS_AND_BARS \
	"        ---|---|---        |          ----|----|---- \n"

#define YOU_WIN \
	"\n You win !\n"
#define COMPUTER_WINS \
	"\n Computer wins !\n"
#define IS_A_DRAW \
	"\n This is a draw !\n"
#define COMPUTER_CONSIDERS \
	"\n Computer is considering its move ...\n"
#define WAITING_FOR_NAMED \
	"\n Waiting for %s to choose whether to play again, or not ...\n"
#define WANT_TO_PLAY \
	"\n Do you want to play again ? [y/n]: "
#define UNKNOWN_ANSWER \
	" Unknown answer. I suppose you want to play again.\n"

void print_header (int type)
{
  nettoe_term_clear ();

  nettoe_term_reset_color();
  printf (SINGLE_DASHED_LINE);
  printf ("|");
  nettoe_term_set_color (COLOR_BLUE, ATTRIB_BRIGHT);
  printf (" netToe %-5.5s ", PACKAGE_VERSION);
  nettoe_term_reset_color();
  printf ("|-XOX-OXO-OOO-XXX-|");
  nettoe_term_set_color (COLOR_BLUE, ATTRIB_BRIGHT);
  printf (" The Net Tic Tac Toe Game ");
  nettoe_term_reset_color();
  printf ("|\n");

  if (type == 0)
     printf (SINGLE_DASHED_LINE);
  else
     printf (BROKEN_DASHED_LINE);

  nettoe_term_set_default_color();
}

static void main_menu (void)
{
  int selection = 0;
  char answer[20];

  /* Make sure this is an interactive session.
   * Otherwise, quit silently.
   */
  if (!isatty (STDIN_FILENO))
    exit (EXIT_FAILURE);

  print_header (0);
  nettoe_term_reset_color();
  printf ("\n   [ ");
  nettoe_term_set_color (COLOR_RED, ATTRIB_BRIGHT);
  printf ("Main Menu");
  nettoe_term_reset_color();
  printf (" ]\n\n");

  /* Player vs CPU */
  printf ("   (");
  nettoe_term_set_color (COLOR_RED, ATTRIB_BRIGHT);
  printf ("1");
  nettoe_term_reset_color();
  printf (") Player vs ");
  nettoe_term_set_color (COLOR_BLUE, ATTRIB_BRIGHT);
  printf ("C");
  nettoe_term_reset_color();
  printf ("PU\n");

  /* Player vs Player */
  printf ("   (");
  nettoe_term_set_color (COLOR_RED, ATTRIB_BRIGHT);
  printf ("2");
  nettoe_term_reset_color();
  printf (") Player vs ");
  nettoe_term_set_color (COLOR_BLUE, ATTRIB_BRIGHT);
  printf ("P");
  nettoe_term_reset_color();
  printf ("layer\n");

  /* Two players over network */
  printf ("   (");
  nettoe_term_set_color (COLOR_RED, ATTRIB_BRIGHT);
  printf ("3");
  nettoe_term_reset_color();
  printf (") Two players over ");
  nettoe_term_set_color (COLOR_BLUE, ATTRIB_BRIGHT);
  putchar ('n');
  nettoe_term_reset_color();
  printf ("etwork\n");

  /* Info */
  puts ("");
  printf ("   (");
  nettoe_term_set_color (COLOR_RED, ATTRIB_BRIGHT);
  printf ("4");
  nettoe_term_reset_color();
  printf (") ");
  nettoe_term_set_color (COLOR_BLUE, ATTRIB_BRIGHT);
  putchar ('I');
  nettoe_term_reset_color();
  printf ("nfo\n");

  /* Quit */
  printf ("   (");
  nettoe_term_set_color (COLOR_RED, ATTRIB_BRIGHT);
  printf ("q");
  nettoe_term_reset_color();
  printf (") ");
  nettoe_term_set_color (COLOR_BLUE, ATTRIB_BRIGHT);
  putchar ('Q');
  nettoe_term_reset_color();
  printf ("uit\n");

  while ( selection == 0 )
  {
    nettoe_term_set_color (COLOR_BLUE, ATTRIB_BRIGHT);
    printf ("\n Choose an action: ");
    nettoe_term_set_color (COLOR_RED, ATTRIB_BRIGHT);

    fflush (stdin);
    if (!fgets (answer, sizeof(answer), stdin))
      exit(EXIT_FAILURE);

    if ( sscanf (answer, " %d", &selection) == 0 )
    {
      char *p = answer;

      while (*p == ' ')
	++p;
      switch (*p)
	{
	  case 'c':
	  case 'C': selection = 1;
		    break;
	  case 'p':
	  case 'P': selection = 2;
		    break;
	  case 'n':
	  case 'N': selection = 3;
		    break;
	  case 'i':
	  case 'I': selection = 4;
		    break;
	  case 'q':
	  case 'Q': selection = 5;
		    break;
	  default:  selection = 0;
	}
    }

    if ( (selection < 1) || (selection > 5) )
    {
      selection = 0;
    }
  }
  nettoe_term_set_default_color ();

  /* Reset scores every time a new main mode has been selected. */
  player1_status  = 0;
  player2_status  = 0;
  computer_status = 0;
  has_given_eof = 0;

  if (selection == 1)
    {
      cpu_AI_menu ();

      if (computer_AI)
	init_one_player_game ();
    }
  else if (selection == 2)
    {
      init_two_player_game ();
    }
  else if (selection == 3)
    {
      network_menu ();
    }
  else if (selection == 4)
    {
      print_header (0);
      print_infos_screen ();
    }
  else if (selection == 5)
    {
      quit_game ();
    }
  else
    {
      nettoe_term_set_color (COLOR_RED, ATTRIB_BRIGHT);
      printf ("\n Error:");
      nettoe_term_reset_color();
      printf (" Incorrect choice.\n");
      nettoe_term_set_default_color ();
      quit_game ();
    }
}

static void cpu_AI_menu (void)
{
  int selection = 0;
  char answer[20];

  computer_AI = 0;	/* Unset level. */
  print_header (0);

  nettoe_term_reset_color();
  printf ("\n   [ ");
  nettoe_term_set_color (COLOR_RED, ATTRIB_BRIGHT);
  printf ("Level of difficulty");
  nettoe_term_reset_color();
  printf (" ]\n\n");

  printf ("   (");
  nettoe_term_set_color (COLOR_RED, ATTRIB_BRIGHT);
  printf ("1");
  nettoe_term_reset_color();
  printf (") ");
  nettoe_term_set_color (COLOR_BLUE, ATTRIB_BRIGHT);
  putchar ('W');
  nettoe_term_reset_color();
  printf ("eak \n");

  printf ("   (");
  nettoe_term_set_color (COLOR_RED, ATTRIB_BRIGHT);
  printf ("2");
  nettoe_term_reset_color();
  printf (") ");
  nettoe_term_set_color (COLOR_BLUE, ATTRIB_BRIGHT);
  putchar ('N');
  nettoe_term_reset_color();
  printf ("ormal\n");

  printf ("   (");
  nettoe_term_set_color (COLOR_RED, ATTRIB_BRIGHT);
  printf ("3");
  nettoe_term_reset_color();
  printf (") ");
  nettoe_term_set_color (COLOR_BLUE, ATTRIB_BRIGHT);
  putchar ('B');
  nettoe_term_reset_color();
  printf ("etter\n");

  puts ("");
  printf ("   (");
  nettoe_term_set_color (COLOR_RED, ATTRIB_BRIGHT);
  printf ("q");
  nettoe_term_reset_color();
  printf (") ");
  nettoe_term_set_color (COLOR_BLUE, ATTRIB_BRIGHT);
  putchar ('R');
  nettoe_term_reset_color();
  printf ("eturn to ");
  nettoe_term_set_color (COLOR_BLUE, ATTRIB_BRIGHT);
  putchar ('m');
  nettoe_term_reset_color();
  printf ("ain menu\n");

  while ( selection == 0 )
  {
    nettoe_term_set_color (COLOR_BLUE, ATTRIB_BRIGHT);
    printf ("\n Choice: ");
    nettoe_term_set_color (COLOR_RED, ATTRIB_BRIGHT);

    fflush (stdin);
    if (!fgets (answer, sizeof(answer), stdin))
      exit(EXIT_FAILURE);

    if ( sscanf (answer, "%d", &selection) == 0 )
    {
      char *p = answer;

      while (*p == ' ')
	++p;
      switch (*p)
	{
	  case 'w':
	  case 'W': selection = 1;
		    break;
	  case 'n':
	  case 'N': selection = 2;
		    break;
	  case 'b':
	  case 'B': selection = 3;
		    break;
	  case 'm':
	  case 'M':
	  case 'q':
	  case 'Q':
	  case 'r':
	  case 'R': selection = 4;
		    break;
	  default:  selection = 0;
	}
    }

    if ( (selection < 1) || (selection > 4) )
    {
      selection = 0;
    }
  }

  nettoe_term_set_default_color ();

  if (selection == 1)
    {
      computer_AI = 1;
    }
  else if (selection == 2)
    {
      computer_AI = 2;
    }
  else if (selection == 3)
    {
      computer_AI = 3;
    }
  /* Return to main menu, leaving level unset. */
}

static void network_menu (void)
{
  int selection = 0;
  char answer[MAX_PNAME_LEN];

  print_header (0);
  nettoe_term_reset_color();
  printf ("\n   [ ");
  nettoe_term_set_color (COLOR_RED, ATTRIB_BRIGHT);
  printf ("Network game options");
  nettoe_term_reset_color();
  printf (" ]\n\n");

  printf ("   (");
  nettoe_term_set_color (COLOR_RED, ATTRIB_BRIGHT);
  printf ("1");
  nettoe_term_reset_color();
  printf (") ");
  nettoe_term_set_color (COLOR_BLUE, ATTRIB_BRIGHT);
  putchar ('H');
  nettoe_term_reset_color();
  printf ("ost the game\n");

  printf ("   (");
  nettoe_term_set_color (COLOR_RED, ATTRIB_BRIGHT);
  printf ("2");
  nettoe_term_reset_color();
  printf (") ");
  nettoe_term_set_color (COLOR_BLUE, ATTRIB_BRIGHT);
  putchar ('C');
  nettoe_term_reset_color();
  printf ("onnect to host\n");

  puts ("");
  printf ("   (");
  nettoe_term_set_color (COLOR_RED, ATTRIB_BRIGHT);
  printf ("q");
  nettoe_term_reset_color();
  printf (") ");
  nettoe_term_set_color (COLOR_BLUE, ATTRIB_BRIGHT);
  putchar ('B');
  nettoe_term_reset_color();
  printf ("ack to ");
  nettoe_term_set_color (COLOR_BLUE, ATTRIB_BRIGHT);
  putchar ('m');
  nettoe_term_reset_color();
  printf ("ain menu\n");

  while ( selection == 0 )
  {
    nettoe_term_set_color (COLOR_BLUE, ATTRIB_BRIGHT);
    printf ("\n Choose an action: ");
    nettoe_term_set_color (COLOR_RED, ATTRIB_BRIGHT);

    fflush (stdin);
    if (!fgets (answer, sizeof(answer), stdin))
      exit(EXIT_FAILURE);

    if ( sscanf (answer, "%d", &selection) == 0 )
    {
      char *p = answer;

      while (*p == ' ')
	++p;
      switch (*p)
	{
	  case 'h':
	  case 'H': selection = 1;
		    break;
	  case 'c':
	  case 'C': selection = 2;
		    break;
	  case 'b':
	  case 'B':
	  case 'm':
	  case 'M':
	  case 'q':
	  case 'Q': selection = 3;
		    break;
	  default:  selection = 0;
	}
    }

    if ( (selection < 1) || (selection > 3) )
    {
      selection = 0;
    }
  }

  nettoe_term_reset_color ();

  if (selection == 1)
    {
      signal(SIGPIPE, SIG_IGN);
      server_start ();
    }
  else if (selection == 2)
    {
      signal(SIGPIPE, SIG_IGN);
      client_start ();
    }

  /* Third choice returns to main menu.  */

  if ( has_given_eof )
    {
      printf("\n\n Your opponent has resigned the game by cutting connections !\n");
      printf (" (A carrage return continues.   )\b\b\b");
      fflush (stdin);
      getchar();
      return;
    }
}

static void init_one_player_game (void)
{
  who_starts = who_start_first ();

  nettoe_term_reset_color();
  printf ("\n Please state your name: ");
  nettoe_term_set_color (COLOR_BLUE, ATTRIB_BRIGHT);
  get_player_pname(player1_name, MAX_PNAME_LEN);

  nettoe_term_set_default_color ();

  while ((check_pname(player1_name, MAX_PNAME_LEN)) != 0)
    {
      printf (" Error: name cannot be more than %d chars long.\n",
	      MAX_PNAME_LEN);
      printf ("\n Please state your name: ");
      get_player_pname(player1_name, MAX_PNAME_LEN);
    }

  nettoe_term_reset_color();
  printf ("\n Who will start first ?  ");
  sleep ( 1 );

  if (who_starts == 1)
    printf ("You will begin ! ");
  else
    printf ("Computer will begin ! ");

  nettoe_term_set_default_color ();
  fflush (stdout);
  sleep ( 3 );

  do_one_player_game ();
}

static void init_two_player_game (void)
{
  who_starts = who_start_first ();

  nettoe_term_reset_color();
  printf ("\n Player 1, please type your name: ");
  nettoe_term_set_color (COLOR_BLUE, ATTRIB_BRIGHT);
  get_player_pname(player1_name, MAX_PNAME_LEN);
  nettoe_term_reset_color ();

  while ((check_pname(player1_name, MAX_PNAME_LEN)) != 0)
    {
      printf (" Error: name cannot be more than %d chars long.\n",
	      MAX_PNAME_LEN);
      printf ("\n Player 1, please type your name: ");
      get_player_pname(player1_name, MAX_PNAME_LEN);
    }

  printf ("\n Player 2, please type your name: ");
  nettoe_term_set_color (COLOR_BLUE, ATTRIB_BRIGHT);
  get_player_pname(player2_name, MAX_PNAME_LEN);
  nettoe_term_reset_color ();

  while ((check_pname(player2_name, MAX_PNAME_LEN)) != 0)
    {
      printf (" Error: name cannot be more than %d chars long.\n",
	      MAX_PNAME_LEN);
      printf ("\n Player 2, please type your name: ");
      get_player_pname(player2_name, MAX_PNAME_LEN);
    }

  nettoe_term_reset_color();
  printf ("\n Who will start first ?  ");
  sleep ( 1 );

  nettoe_term_set_color (COLOR_BLUE, ATTRIB_BRIGHT);
  printf ("%s", (who_starts == 1) ? player1_name : player2_name);
  nettoe_term_reset_color();
  printf (" will begin ! ");
  nettoe_term_set_default_color ();
  fflush (stdout);
  sleep ( 3 );

  do_two_player_game ();
}

static void do_one_player_game (void)
{
  char y_n[12];
  winner = GAME_IS_ALIVE;
  game_mode = 1;
  init_matrix ();

  do
    {
      if (who_starts == 1)
	{
	  show_game ();
	  get_player1_move ();
	}
      else
	{
	  printf (COMPUTER_CONSIDERS);
	  get_cpu_move (computer_AI);
	}

      winner = game_check ();
      if ( winner != GAME_IS_ALIVE )
	break;

      if (who_starts == 1)
	{
	  printf (COMPUTER_CONSIDERS);
	  get_cpu_move (computer_AI);
	}
      else
	{
	  show_game ();
	  get_player1_move ();
	}

      winner = game_check ();
      if ( winner != GAME_IS_ALIVE)
	break;
    }
  while (winner == GAME_IS_ALIVE);

  /* Switch roles of players for next round.
   * An easy arithmetic trick.
   */
  who_starts = (1 + 2) - who_starts;

  if (winner == GAME_IS_X_WIN)
    {
      player1_status++;
      show_game ();
      nettoe_term_set_color (COLOR_RED, ATTRIB_BRIGHT);
      printf (YOU_WIN);
    }
  else if (winner == GAME_IS_O_WIN)
    {
      computer_status++;
      show_game ();
      nettoe_term_set_color (COLOR_RED, ATTRIB_BRIGHT);
      printf (COMPUTER_WINS);
    }
  else /* Draw */
    show_drawed_game ();

  nettoe_term_set_default_color ();
  nettoe_term_reset_color();
  printf (WANT_TO_PLAY);
  if (scanf ("%11s", y_n) != 1)
    y_n[0] = 'n', y_n[1] = '\0';
  getchar();
  nettoe_term_set_default_color ();

  if ( *y_n == 'y' || *y_n == 'Y' )
    do_one_player_game ();
  else if ( *y_n == 'n' || *y_n == 'N' )
    return;
  else
    {
      nettoe_term_reset_color();
      printf (UNKNOWN_ANSWER);
      nettoe_term_set_default_color ();
      sleep ( 2 );
      do_one_player_game ();
    }
}

static void do_two_player_game (void)
{
  char y_n[12];
  winner = GAME_IS_ALIVE;
  game_mode = 2;
  init_matrix ();

  do
    {
      show_game ();

      if (who_starts == 1)
	get_player1_move ();
      else
	get_player2_move ();

      winner = game_check ();
      if ( winner != GAME_IS_ALIVE )
	break;

      show_game ();

      if (who_starts == 1)
	get_player2_move ();
      else
	get_player1_move ();

      winner = game_check ();
      if ( winner != GAME_IS_ALIVE )
	break;
    }
  while (winner == GAME_IS_ALIVE);

  /* Switch roles of players for next round.
   * An easy arithmetic trick.
   */
  who_starts = (1 + 2) - who_starts;

  if (winner == GAME_IS_X_WIN || winner == GAME_IS_O_WIN)
    {
      if (winner == GAME_IS_X_WIN)
	player1_status++;
      else
	player2_status++;

      show_game ();
      nettoe_term_set_color (COLOR_BLUE, ATTRIB_BRIGHT);

      printf ("\n %s", (winner == GAME_IS_X_WIN) ? player1_name
						 : player2_name);
      nettoe_term_reset_color();
      printf (" wins !\n");
    }
  else /* Draw */
    show_drawed_game ();

  printf (WANT_TO_PLAY);
  if (scanf ("%11s", y_n) != 1)
    y_n[0] = 'n', y_n[1] = '\0';
  getchar();

  if ( *y_n == 'y' || *y_n == 'Y' )
    do_two_player_game ();
  else if ( *y_n == 'n' || *y_n == 'N' )
    return;
  else
    {
      printf (UNKNOWN_ANSWER);
      sleep ( 2 );
      do_two_player_game ();
    }
}

static void show_board_section (char p1, char p2, char p3,
				char *n1, char *n2, char *n3)
{
  printf ("         %c | %c | %c         |", p1, p2, p3);
  nettoe_term_set_color (COLOR_BLUE, ATTRIB_BRIGHT);
  printf ("           %.2s", n1);
  nettoe_term_reset_color();
  printf (" |");
  nettoe_term_set_color (COLOR_BLUE, ATTRIB_BRIGHT);
  printf (" %.2s", n2);
  nettoe_term_reset_color();
  printf (" |");
  nettoe_term_set_color (COLOR_BLUE, ATTRIB_BRIGHT);
  printf (" %.2s \n", n3);
  nettoe_term_reset_color();
}

static void show_game (void)
{
  print_header (1);

  nettoe_term_set_color (COLOR_RED, ATTRIB_BRIGHT);
  printf ("        Current game");
  nettoe_term_reset_color();
  printf ("       |        ");
  nettoe_term_set_color (COLOR_RED, ATTRIB_BRIGHT);
  printf ("Coordinate matrix\n");
  nettoe_term_reset_color();

  printf (SPACES_AND_BARS);
  show_board_section (c11, c12, c13, "a1", "a2", "a3");
  printf (BLOCKERS_AND_BARS);
  show_board_section (c21, c22, c23, "b1", "b2", "b3");
  printf (BLOCKERS_AND_BARS);
  show_board_section (c31, c32, c33, "c1", "c2", "c3");
  printf (SPACES_AND_BARS);
  nettoe_term_set_default_color();

  nettoe_term_reset_color();
  printf (BROKEN_DASHED_LINE);
  nettoe_term_set_color (COLOR_RED, ATTRIB_BRIGHT);
  printf ("  Score:");
  nettoe_term_set_color (COLOR_BLUE, ATTRIB_BRIGHT);
  printf (" %s", player1_name);
  nettoe_term_reset_color();
  printf (" %d,", player1_status);
  nettoe_term_set_color (COLOR_BLUE, ATTRIB_BRIGHT);
  printf (" %s", (game_mode <= 1) ? "Computer" : player2_name);
  nettoe_term_reset_color();
  printf (" %d.\n", (game_mode <= 1) ? computer_status : player2_status);
  printf (SINGLE_DASHED_LINE);
  nettoe_term_set_default_color();
}

static void show_drawed_game ()
{
  show_game ();
  printf (IS_A_DRAW);
}

static void show_waiting_for_move (const char *name)
{
  show_game ();
  printf("\n Waiting for ");
  nettoe_term_set_color(COLOR_BLUE, ATTRIB_BRIGHT);
  printf("%s", name);
  nettoe_term_reset_color();
  printf(" to move ... ");
  fflush(stdout);
}

static void get_player_move (const char *name, char pawn)
{
  char move[4];

  nettoe_beep ();

  nettoe_term_set_color (COLOR_BLUE, ATTRIB_BRIGHT);
  printf("\n %s", name);
  nettoe_term_reset_color();
  printf(", playing %c, it is your turn: ", pawn);

  while (1)
    {
      if (scanf ("%3s", move) != 1)
	move[0] = '\0';

      if (attempt_move (move, pawn) == VALID_MOVE)
	break;

      fflush(stdin);
      printf(" Invalid move. Try again: ");
    } /* while(1) */
}

static void get_player1_move (void)
{
  get_player_move (player1_name, 'X');
}

static void get_player2_move (void)
{
  get_player_move (player2_name, 'O');
}

static void server_start (void)
{
  char local_ip_address[INET6_ADDRSTRLEN];
  char peer_ip_address[INET6_ADDRSTRLEN];

  print_header (0);

  nettoe_term_reset_color();
  printf ("\n   [ ");
  nettoe_term_set_color (COLOR_RED, ATTRIB_BRIGHT);
  printf ("Host the game");
  nettoe_term_reset_color();
  printf (" ]\n\n");

  printf ("    Player name: ");
  nettoe_term_set_color (COLOR_BLUE, ATTRIB_BRIGHT);
  get_player_pname(player1_name, MAX_PNAME_LEN);
  nettoe_term_reset_color();
  local_ip_address[0] = '\0';
  give_local_IP (local_ip_address, sizeof(local_ip_address));

  printf ("\n   You should now tell your IP address to any interested\n");
  printf ("   competitor. There might be more than one address, but\n");
  printf ("   \"%s\" should do.\n", local_ip_address);
  printf ("\n   Connected players:\n");
  printf ("   ------------------\n");
  printf ("   -");
  nettoe_term_set_color (COLOR_BLUE, ATTRIB_BRIGHT);
  printf (" %s", player1_name);
  nettoe_term_reset_color();
  printf (" (");
  nettoe_term_set_color (COLOR_RED, ATTRIB_BRIGHT);
  printf ("you");
  nettoe_term_reset_color();
  printf (")\n");

  sock = establish_listening_socket (SERVER_PORT_NUMBER, peer_ip_address,
				     sizeof(peer_ip_address));

  if (sock < 0)
    {
      printf ("   (A carrage return continues.   )\b\b\b");
      fflush (stdin);
      getchar();
      return;
    }

  write_to_socket (sock, player1_name, sizeof(player1_name));

  sleep ( 2 );

  read_from_socket (sock, player2_name, sizeof(player2_name));
  nettoe_term_reset_color();
  printf ("   -");
  nettoe_term_set_color (COLOR_BLUE, ATTRIB_BRIGHT);
  printf (" %s", player2_name);
  nettoe_term_reset_color();
  printf (" (");
  nettoe_term_set_color (COLOR_RED, ATTRIB_BRIGHT);
  printf ("your opponent");
  nettoe_term_reset_color();
  printf (")\n");

  printf ("\n   Two players are present. Let us play !\n");
  sleep ( 3 );

  printf ("\n   Who will start first ?  ");

  who_starts = who_start_first ();

  nettoe_term_set_color (COLOR_BLUE, ATTRIB_BRIGHT);

  if (who_starts == 1)
    {
      printf ("%s", player1_name);
      write_to_socket (sock, player1_name, sizeof(player1_name));
    }
  else
    {
      printf ("%s", player2_name);
      write_to_socket (sock, player2_name, sizeof(player2_name));
    }

  nettoe_term_reset_color();
  printf (" will begin !\n");
  printf ("\n   Starting game ... ");
  fflush (stdout);

  nettoe_term_set_default_color ();

  sleep ( 2 );

  if ( ! has_given_eof )
    init_server_network_game ();

  return;
}

static void client_start (void)
{
  char buf[MAXDATASIZE];
  char host_ip_number[INET6_ADDRSTRLEN + 1];

  print_header (0);

  nettoe_term_reset_color();
  printf ("\n   [ ");
  nettoe_term_set_color (COLOR_RED, ATTRIB_BRIGHT);
  printf ("Connect to host");
  nettoe_term_reset_color();
  printf (" ]\n\n");
  printf ("    Player name: ");
  nettoe_term_set_color (COLOR_BLUE, ATTRIB_BRIGHT);
  get_player_pname(player2_name, MAX_PNAME_LEN);
  nettoe_term_reset_color();
  printf ("\n    Host to connect to (IP or hostname): ");
  nettoe_term_set_color (COLOR_BLUE, ATTRIB_BRIGHT);
  if (scanf (" %46s", host_ip_number) != 1)
    host_ip_number[0] = '\0';
  nettoe_term_reset_color();
  printf ("\n   Connecting ... ");

  sock = connect_to_socket (host_ip_number, SERVER_PORT_NUMBER);

  if (sock < 0)
    {
      printf ("   (A carrage return continues.   )\b\b\b");
      fflush (stdin);
      getchar();
      return;
    }
  nettoe_term_set_color (COLOR_RED, ATTRIB_BRIGHT);
  printf ("connected !\n\n");
  nettoe_term_reset_color();
  printf ("   The players are:\n\n");
  read_from_socket (sock, player1_name, sizeof(player1_name));

  printf ("   -");
  nettoe_term_set_color (COLOR_BLUE, ATTRIB_BRIGHT);
  printf (" %s", player1_name);
  nettoe_term_reset_color();
  printf (" (");
  nettoe_term_set_color (COLOR_RED, ATTRIB_BRIGHT);
  printf ("your opponent");
  nettoe_term_reset_color();
  printf (")\n");

  printf ("   -");
  nettoe_term_set_color (COLOR_BLUE, ATTRIB_BRIGHT);
  printf (" %s", player2_name);
  nettoe_term_reset_color();
  printf (" (");
  nettoe_term_set_color (COLOR_RED, ATTRIB_BRIGHT);
  printf ("you");
  nettoe_term_reset_color();
  printf (")\n");

  write_to_socket (sock, player2_name, sizeof(player2_name));

  printf ("\n   Two players are present. Let us play !\n");
  sleep ( 3 );

  printf ("\n   Who will start first ?  ");

  if ( ! has_given_eof )
  {
    read_from_socket (sock, buf, MAX_PNAME_LEN);

    if ( has_given_eof )
      return;

    nettoe_term_set_color (COLOR_BLUE, ATTRIB_BRIGHT);
    printf ("%s", buf);
    nettoe_term_reset_color();
    printf (" will begin !\n");
    printf ("\n   Starting game ... ");
    fflush (stdout);
    nettoe_term_set_default_color ();
    sleep ( 3 );

    init_client_network_game ();
  }
}

static void recv_remote_play (int sd, int dir, const char *name)
{
  char buf[MAXDATASIZE];

  show_waiting_for_move (name);

  buf[0] = 'n';
  while ( strncmp (buf, "y", 2) && ! has_given_eof )
    read_from_socket (sd, buf, 2);

  recv_matrix (sd);

  buf[0] = 'n';
  while ( strncmp (buf, (dir == ASK_SERVER) ? "S" : "C", 2)
	  && ! has_given_eof )
    read_from_socket (sd, buf, 2);
}

static void send_local_play (int sd, int dir)
{
  show_game ();
  if (dir == I_AM_SERVER)
    get_player1_move ();
  else
    get_player2_move ();
  write_to_socket (sd, "y", 2);
  send_matrix (sd);
  write_to_socket (sd, (dir == I_AM_SERVER) ? "S" : "C", 2);
}

static void init_server_network_game (void)
{
  char y_n[12];
  char yes_no[2];
  char buf[MAXDATASIZE];

  /* Player 1 is local player. */

  game_mode = 3;

  init_matrix ();

  write_to_socket (sock, (who_starts == 1) ? "1" : "2", 1);

  do
    {
      if (who_starts == 1)
	send_local_play (sock, I_AM_SERVER);
      else
	{
	  recv_remote_play (sock, ASK_CLIENT, player2_name);

	  if ( has_given_eof )
	    break;
	}

      winner = game_check ();
      if ( winner != GAME_IS_ALIVE )
	break;

      if (who_starts == 1)
	{
	  recv_remote_play (sock, ASK_CLIENT, player2_name);

	  if ( has_given_eof )
	    break;
	}
      else
	send_local_play (sock, I_AM_SERVER);

      winner = game_check ();
      if ( winner != GAME_IS_ALIVE )
	break;
    }
  while (winner == GAME_IS_ALIVE && ! has_given_eof);

  /* Switch roles of players for next round.
   * An easy arithmetic trick.
   */
  who_starts = (1 + 2) - who_starts;

  if (winner == GAME_IS_X_WIN)
    {
      player1_status++;
      show_game ();
      printf (YOU_WIN);
    }
  else if (winner == GAME_IS_O_WIN)
    {
      player2_status++;
      show_game ();
      nettoe_term_set_color(COLOR_BLUE, ATTRIB_BRIGHT);
      printf("\n %s", player2_name);
      nettoe_term_reset_color();
      printf (" wins !\n");
    }
  else if (winner == GAME_IS_DRAW)	/* Draw */
    show_drawed_game ();
  else	/* EOF */
    return;

  winner = GAME_IS_ALIVE;

  nettoe_beep();
  printf (WANT_TO_PLAY);
  if (scanf ("%11s", y_n) != 1)
    y_n[0] = 'n', y_n[1] = '\0';
  getchar();

  if ( *y_n == 'n' || *y_n == 'N' )
    {
      write_to_socket (sock, "y", 2);
      write_to_socket (sock, "n", 2);
      sleep ( 3 );
      close (sock);
    }
  else
    {
      if ( *y_n != 'y' && *y_n != 'Y' )
	{
	  printf (UNKNOWN_ANSWER);
	  sleep ( 2 );
	}
      write_to_socket (sock, "y", 2);
      write_to_socket (sock, "y", 2);
      printf (WAITING_FOR_NAMED, player2_name);

      buf[0] = 'n';
      while ( strncmp (buf, "y", 2) && ! has_given_eof )
        read_from_socket (sock, buf, 2);

      read_from_socket (sock, yes_no, 2);

      if (!has_given_eof
          && (!strncmp (yes_no, "y", 2) || !strncmp (yes_no, "Y", 2)))
	{
	  printf ("\n %s wants to play again.\n", player2_name);
	  printf (" Starting ... ");
	  fflush (stdout);
	  sleep ( 4 );
	  init_server_network_game ();
	}
    }
}

static void init_client_network_game (void)
{
  char y_n[12];
  char yes_no[2];
  char buf[MAXDATASIZE];

  /* Player 2 is local player. */

  game_mode = 3;
  init_matrix ();

  read_from_socket (sock, buf, 1);

  who_starts = strncmp (buf, "1", 1) ? 2 : 1;

  do
    {
      /* Player 2 is local player. */
      if (who_starts == 1)
	{
	  recv_remote_play (sock, ASK_SERVER, player1_name);

	  if ( has_given_eof )
	    break;
	}
      else
	send_local_play (sock, I_AM_CLIENT);

      winner = game_check ();
      if ( winner != GAME_IS_ALIVE )
	break;

      if (who_starts == 1)
	send_local_play (sock, I_AM_CLIENT);
      else
	{
	  recv_remote_play (sock, ASK_SERVER, player1_name);

	  if ( has_given_eof )
	    break;
	}

      winner = game_check ();
      if ( winner != GAME_IS_ALIVE )
	break;
    }
  while (winner == GAME_IS_ALIVE && ! has_given_eof);

  /* Switch roles of players for next round.
   * An easy arithmetic trick.
   */
  who_starts = (1 + 2) - who_starts;

  if (winner == GAME_IS_X_WIN)
    {
      player1_status++;
      show_game ();
      nettoe_term_set_color(COLOR_BLUE, ATTRIB_BRIGHT);
      printf("\n %s", player1_name);
      nettoe_term_reset_color();
      printf (" wins !\n");
    }
  else if (winner == GAME_IS_O_WIN)
    {
      player2_status++;
      show_game ();
      printf (YOU_WIN);
    }
  else if (winner == GAME_IS_DRAW)	/* Draw */
    show_drawed_game ();
  else	/* EOF */
    return;

  winner = GAME_IS_ALIVE;
  printf (WAITING_FOR_NAMED, player1_name);

  buf[0] = 'n';
  while ( strncmp (buf, "y", 2) && ! has_given_eof )
    read_from_socket (sock, buf, 2);

  read_from_socket (sock, yes_no, 2);

  if (has_given_eof)
    return;

  if (!strncmp (yes_no, "y", 2) || !strncmp (yes_no, "Y", 2))
    {
      printf ("\n %s wants to play again. What about you ?\n", player1_name);
      nettoe_beep();
      printf (WANT_TO_PLAY);
      if (scanf ("%11s", y_n) != 1)
	y_n[0] = 'n', y_n[1] = '\0';
      getchar();

      if ( *y_n == 'n' || *y_n == 'N' )
	{
	  write_to_socket (sock, "y", 2);
	  write_to_socket (sock, "n", 2);
	  printf ("Ending session ...\n");
	  close (sock);
	}
      else
	{
	  if ( *y_n != 'y' && *y_n != 'Y' )
	    {
	      printf (UNKNOWN_ANSWER);
	      sleep ( 2 );
	    }
	  write_to_socket (sock, "y", 2);
	  write_to_socket (sock, "y", 2);
	  printf (" Starting ... ");
	  fflush (stdout);
	  sleep ( 4 );
	  init_client_network_game ();
	}
    }
  else
    {
      printf ("\n %s doesn't want to play again. Sorry.\n", player1_name);
      sleep ( 3 );
      close (sock);
      return;
    }
}

static void send_matrix (int fd )
{
  write_to_socket (fd, &c11, 1);
  write_to_socket (fd, &c12, 1);
  write_to_socket (fd, &c13, 1);
  write_to_socket (fd, &c21, 1);
  write_to_socket (fd, &c22, 1);
  write_to_socket (fd, &c23, 1);
  write_to_socket (fd, &c31, 1);
  write_to_socket (fd, &c32, 1);
  write_to_socket (fd, &c33, 1);
}

static void recv_matrix ( int fd )
{
  read_from_socket (fd, &c11, 1);
  read_from_socket (fd, &c12, 1);
  read_from_socket (fd, &c13, 1);
  read_from_socket (fd, &c21, 1);
  read_from_socket (fd, &c22, 1);
  read_from_socket (fd, &c23, 1);
  read_from_socket (fd, &c31, 1);
  read_from_socket (fd, &c32, 1);
  read_from_socket (fd, &c33, 1);
}

static void quit_game (void)
{
  nettoe_term_reset_color();

  printf("\n Goodbye !\n\n");

  printf("netToe Copyright 2000,2001 Gabriele Giorgetti\n");
  printf("       Copyright 2009-2014 Mats Erik Andersson\n");
  printf("netToe project homepage: %s\n\n", HOMEPAGE);

  close (sock);

  exit (EXIT_SUCCESS);
}
