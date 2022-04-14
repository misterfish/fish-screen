#define _GNU_SOURCE

#define LIMIT_HOSTNAME 20
#define LIMIT_NAME_NEW 20

#define VERSION_OLD 0x1
#define VERSION_NEW 0x2

#define ENV_TERM "xterm-256color"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h> // exec, getlogin
#include <signal.h>

#include <stdarg.h>

#include <argp.h>

#include <assert.h>

#include <fish-util.h>
#include <fish-utils.h>

#include "fish-screen.h"

/* For the adios err message, print ... if args are longer.
 * Doing it on the cmd and each arg, instead of one ... at the end of the
 * whole thing.
 * Which isn't that intuitive really.
 */
#define ARG_PRINT_LENGTH 20

#define sct(type, name) struct type name = {0};

/* Try to exec. If fails, exit.
 * Last arg to exec is NULL, so it's our last vararg too.
 * ## to eat the preceding comma if empty. \
 */

#define adios(file, ...) do { \
  cleanup_us(); \
  cleanup_them(); \
  execlp(file, file, ##__VA_ARGS__); \
  /* we failed */ \
  adios_failed(file, ##__VA_ARGS__); \
} while (0);

const int FIELD_WIDTH = 10;

/* First called with failed = false. Point is to clean up.
 * If exec fails in macro, called again, with failed = true. Clean up again
 * and exit.
 */
void adios_failed (char *file, ...) {
  fish_util_init ();
  fish_utils_init ();

  vec *args = vec_new ();
  int size = 0;

  int len;
  char *arg;
  // without \0
  len = strnlen (file, ARG_PRINT_LENGTH);

  arg = malloc (1 + len * sizeof (char));
  // \0 guaranteed
  snprintf (arg, len + 1, "%s", file);
  size += len + 1;
  vec_add (args, arg);
  if (len == ARG_PRINT_LENGTH) { // too big
    arg[len-1] = '.';
    arg[len-2] = '.';
    arg[len-3] = '.';
  }

  va_list ap;
  va_start (ap, file);
  int num_args = 1; // includes file
  while ((arg = va_arg (ap, char*))) {
    len = strnlen (arg, ARG_PRINT_LENGTH);
    char *a = malloc (1 + 1 + len * sizeof (char));
    snprintf (a, len + 1 + 1, " %s", arg);
    size += len + 1 + 1;
    vec_add (args, a);
    if (len == ARG_PRINT_LENGTH) { // too long
      a[len-1 + 1] = '.';
      a[len-2 + 1] = '.';
      a[len-3 + 1] = '.';
    }
    num_args++;
  }
  va_end (ap);

  char *msg = str (size);
  for (int i = 0; i < num_args; i++) {
    strcat (msg, (char*)vec_get (args, i)); //-snprintf-
  }

  vec_destroy_deep (args);

  const char *pe = perr ();

  _ ();
  BR (msg);
  warn ("Couldn't run command «%s» (%s)", _s, pe);

  free (msg);

  cleanup_them ();

  exit (1);
}

error_t argp_parser (int key, char *arg, struct argp_state *state) {
  if (key == 'n') {
    g.create = true;
  }
  else if (key == 'p') {
    int as_int = atoi (arg);
    if (!f_is_int_str (arg) || as_int <= 0) {
      _ ();
      BR (arg);
      err ("Need positive integer for pid (got %s)", _s);
    }
    // --- posix: signed integer.
    g.pid = (pid_t) as_int;
  }
  else if (key == 'h') {
    argp_state_help (state, stdout, ARGP_HELP_STD_HELP);
    exit (0);
  }
  else if (key == ARGP_KEY_ARG) {
    // assuming not cleaned up.
    g.next_arg = arg;
  }
  return 0;
}

// OPTION_ARG_OPTIONAL means the value is optional (not the arg)

static struct argp_option options[] = {
  {
    0,
    'h',
    0,
    OPTION_ARG_OPTIONAL,
    0,
    0
  },
  {
    "new", // long opt (--new)
    'n', // short opt (-n)
    0, // name (for long usage message, leave as 0 for optional args)
    OPTION_ARG_OPTIONAL, // flags
    "Create new screen if no match.",
    0 // group
  },
  {
    "pid",
    'p',
    "pid",
    0, // flags
    "Attach to the screen with this pid.",
    0
  },
  {0}
};

void data_init () {
  for (int i = 0; i < g.num_states; i++)
    g.data[i] = vec_new ();
}

void data_push (enum state which, struct screen *s) {
  vec *v = g.data[which];
  vec_add (v, s);
}

void cleanup_us () {
  for (int i = 0; i < g.num_states; i++)
    vec_destroy_f (g.data[i], VEC_DESTROY_DEEP);
  free (g.rl_l);
  free (g.pids);
  free (g.names);
  free (g.is_multi);
}

void cleanup_them () {
  fish_utils_cleanup ();
  fish_util_cleanup ();
}

void load_data () {
  f_sys_die (true);

  FILE *f = sysr ("screen -ls");

  while (getline (&g.rl_l, &g.rl_s, f) != -1) {
    char *matches[5] = {0};

    // Date only in newer screen, and anyway not used.
    char *re, *name, *date, *state;
    int pid;
    if (g.version == VERSION_OLD) {
      re = "^ \\s* (\\d+) \\. (.+?) \\s+ ( \\( .+? \\) ) "; //x

      if (! match_matches_flags (g.rl_l, re, matches, F_REGEX_EXTENDED))
        continue;

      // malloc'd => ok to store
      pid = atoi (matches[1]);
      name = matches[2];
      state = matches[3];
    }
    else if (g.version == VERSION_NEW) {
      re = "^ \\s* (\\d+) \\. (.+?) \\s+ ( \\(.+?\\) ) \\s+ ( \\(.+?\\) ) "; //x
      if (! match_matches_flags (g.rl_l, re, matches, F_REGEX_EXTENDED))
        continue;

      // malloc'd => ok to store
      pid = atoi (matches[1]);
      name = matches[2];
      date = matches[3];
      state = matches[4];
    }
    else {
      piepr;
    }

    debug ("name, %s", name);
    if (date) debug ("date, %s", date);
    debug ("state, %s", state);
    debug ("pid, %d", pid);

    if (match (name, g.hostname)) { // watch for special chars
      debug ("name matched: %s, %s", name, g.hostname);
      name = " ";
    }

    if (g.pid) {
      if (pid != g.pid)
        continue;
    }
    else if (g.regex_or_name) {
      if (! match (name, g.regex_or_name))
        continue;
    }

    struct screen *t = malloc (sizeof (struct screen));
    t->pid = pid;

    t->name = name;
    t->date = date;
    t->state = state;

    if (!strncmp (state, "(Attached)", 10))
      data_push (state_attached, t);
    else if (!strncmp (state, "(Detached)", 10))
      data_push (state_detached, t);
    else if (!strncmp (state, "(Multi, attached)", 17))
      data_push (state_multi_attached, t);
    else if (!strncmp (state, "(Multi, detached)", 17))
      data_push (state_multi_detached, t);
    else {
      iwarn ("Nou dat is gek: state is %s", state);
    }
  }
  pclose (f);
}

bool data_empty () {
  for (int i = 0; i < g.num_states; i++)
    if (vec_size (g.data[i]) != 0)
      return false;
  return true;
}

void check_has_match (bool *has_match, bool *exec, bool *tried_new) {
  if (! data_empty ()) {
    *has_match = true;
    return;
  }
  debug ("nothing.");

  *has_match = false;
  *tried_new = false;

  if (!g.create) {
    if (! g.regex_or_name)
      exit_with_cleanup (1);

    _ ();
    CY (g.regex_or_name);
    ask ("create %s", _s);

    if (f_yes_no_flags (F_DEFAULT_YES, 0))
      *exec = true;

    *tried_new = true;
  }
}

char *menu_field (int length, int pid, enum state state) {
  int len = f_int_length (pid);
  char *s = str (len + 1);
  sprintf (s, "%d", pid);

  char *f = f_field (length, s, len + 1);
  _ ();
  if (state == state_detached) CY (f);
  else if (state == state_attached) BR (f);
  else if (state == state_multi_detached) G (f);
  else if (state == state_multi_attached) G (f);

  free (s);
  free (f);

  char *ret = str (strlen (_s) + 1);
  strcpy (ret, _s);
  return ret;
}

void menu_line (int idx, int pid, char *name, enum state state) {
  char *field = menu_field (FIELD_WIDTH, pid, state);
  _ ();
  char *multi = "";
  if (state == state_multi_attached || state == state_multi_detached)
    multi = "[multi] ";
  Y (name);
  printf ("%2d. %s %s%s\n", idx, field, multi, _s);
  free (field);
}

void menu_section (enum state which, int *idx) {
  vec *v = g.data[which];
  for (int i = 0; i < vec_size (v); i++) {
    (*idx)++;
    struct screen *scr = vec_get (v, i);
    menu_line (*idx, scr->pid, scr->name, which);
  }
}

void menu () {
  int idx = 0;
  menu_section (state_detached, &idx);
  menu_section (state_multi_detached, &idx);
  menu_section (state_attached, &idx);
  menu_section (state_multi_attached, &idx);
  say ("\nor ‘n’ for new");
}

// --- vectors already filtered on name/regex if applied.
int get_num_screens () {
  int n = 0;
  for (int i = 0; i < g.num_states; i++)
    n += vec_size (g.data [i]);
  return n;
}

int load_pids_and_names () {
  int num = get_num_screens ();
  g.pids = calloc (num, sizeof (int));
  g.names = calloc (num, sizeof (char*));
  g.is_multi = calloc (num, sizeof (bool));
  int idx = -1;
  for (int which = 0; which < g.num_states; which++) {
    int n = vec_size (g.data[which]);
    for (int i = 0; i < n; i++) {
      vec *v = g.data[which];
      struct screen *scr = (struct screen*) vec_get (v, i);
      idx++;
      debug ("idx %d, which %d, storing pid %d, name %s", idx, which, scr->pid, scr->name);
      g.pids[idx] = scr->pid;
      g.names[idx] = scr->name;
      g.is_multi[idx] = which == state_multi_attached || state_multi_detached;
    }
  }

  debug ("pids[0] = %d", g.pids[0]);
  return num;
}

void screen_new (char *name) { // taint
  adios ("screen", "-S", name, NULL);

  piep;
}

bool is_empty_string (const char *s) {
  return !strncmp ("", s, 1);
}

void screen_go (int pid, const char *name, bool multi) {
  debug ("going to pid %d name %s", pid, name);
  /* Have to use static, or else it's hard to free it (it's passed as an
   * arg to adios).
   */
  static char p[100] = {0};
  assert ((size_t) f_int_length (pid) + 1 + strlen (name) + 1 < 100);
  if (is_empty_string (name) || *name == ' ') {
    sprintf (p, "%d", pid);
    adios ("screen", "-rd", p, NULL);
  }
  if (multi) {
    sprintf (p, "%s/%d.%s", getlogin (), pid, name);
    adios ("screen", "-r", p, NULL);
  }
  sprintf (p, "%d.%s", pid, name);
  adios ("screen", "-rd", p, NULL);

  /* To test failure
  adios ("screen12312838123812381283", "-rd1234567891234567890", p, NULL);
  adios ("screen12312838123812381283", "as", p, NULL);
  adios ("screen123", "-rd1234567891234567890", p, NULL);
   */
}

void signal_handler () {
  exit_with_cleanup ();
}

void init () {
  fish_utils_init ();
#ifdef DEBUG
  f_verbose_cmds (true);
#else
  f_verbose_cmds (false);
#endif
  f_autoflush ();
  f_sig (SIGINT, signal_handler);

  g.num_states = __num_states;
  g.data = calloc (g.num_states, sizeof (vec *));

  g.rl_l = NULL;
  g.version = get_screen_version ();
  if (!g.version)
    err ("Couldn't get screen version.");
  g.total_num = 0;

  data_init ();
  if (gethostname (g.hostname, LIMIT_HOSTNAME)) {
    warn_perr ("");
    strcpy (g.hostname, "unknown");
  }

  debug ("hostname: %s", g.hostname);
}

int get_sel () {
  int sel = -1;
  menu ();
  int num = g.total_num;
  while (sel == -1) {
    say ("");
    printf ("(1 - %d): ", num);

    int rc = getline (&g.rl_l, &g.rl_s, stdin);
    if (rc == -1)
      exit_with_cleanup (1);

    f_chop (g.rl_l);
    if (match (g.rl_l, "^ \\s* $")) continue;

    if (!strncmp (g.rl_l, "n\0", 2)) {
      printf ("name: ");
      if (getline (&g.rl_l, &g.rl_s, stdin) != -1) {
        f_chop (g.rl_l);

        size_t len = strlen (g.rl_l); // \0 guaranteed
        // This variable will leak -- unavoidable.
        char *name = str (len + 1);
        sprintf (name, "%s", g.rl_l);

        // check if exists XX

        screen_new (name); // done
      }
      // if it works, it execs, so done, else loops again
    }
    else if (f_is_int_strn (g.rl_l, strlen (g.rl_l))) {
      int i = atoi (g.rl_l);
      if (i <= num && i > 0)
        sel = i - 1; // got it
    }
    /* Regex match
     */
    else {
      for (int i = 0; i < num; i++) {
        char *name = g.names[i];
        debug ("checking name: %s", name);
        /* Take first match
         */
        if (match (name, g.rl_l)) {
          sel = i;
          break;
        }
      }
    }
  }
  return sel;
}

void exit_with_cleanup (int status) {
  cleanup_us ();
  cleanup_them ();
  exit (status);
}

int main (int argc, char **argv) {
  init ();

  sct (argp, args)

  args.args_doc = "[regex/name]"; // text in usage msg, after args
  //args.doc = "Before options \v after options"; // help msg

  args.options = options;
  args.parser = (argp_parser_t) argp_parser;

  int arg_index;

  if (argp_parse (&args, argc, argv, 0, &arg_index, 0))
    piepr1;

  // --- probably doesn't happen
  if (arg_index < argc) {
    g.regex_or_name = argv[arg_index];
  }
  else {
    g.regex_or_name = g.next_arg;
  }

  load_data ();
  bool do_exec = false;
  bool has_match = false;
  bool tried_new = false; // not necessary
  check_has_match (&has_match, &do_exec, &tried_new);
  if (! has_match) {
    if (do_exec) {
      adios ("screen", "-R", g.regex_or_name, "-T", ENV_TERM, NULL);
    }
    else {
      exit_with_cleanup (1);
    }
    // --- done.
  }

  // --- inits and loads g.pids and g.names.
  int num = load_pids_and_names ();
  g.total_num = num;
  debug ("Loaded %d pids and names", num);
  int sel = -1;
  if (num == 1) {
    sel = 0;
    debug ("Doing only first");
  }
  else {
    sel = get_sel ();
  }

  int pid = g.pids[sel];
  char *name = g.names[sel];
  debug ("Got sel: %d, pid %d", sel, pid);
  screen_go (pid, name, g.is_multi[sel]);
}

int get_screen_version () {
  int version_maj, version_min = -1;

  char *cmd = "screen -v";
  f_sys_die (0); // exit value is 1 for some reason
  FILE *f = sysr (cmd);
  if (!f)
    return 0;
  char *re = "^ Screen \\s+ version \\s+ (\\d+) \\. (\\d+) "; //x

  char *matches[3];
  while (getline (&g.rl_l, &g.rl_s, f) != -1) {
    if (match_matches_flags (g.rl_l, re, matches, F_REGEX_EXTENDED))  {
      char *v1 = matches[1];
      char *v2 = matches[2];
      version_maj = atoi (v1);
      version_min = atoi (v2);
      break;
    }
  }
  sysclose_f (f, cmd, F_QUIET); // silence warning
  return
    version_maj == -1 ? 0 :
    version_min == -1 ? 0 :
    version_maj < 4 ? VERSION_OLD :
    (version_maj == 4 && version_min < 1) ? VERSION_OLD :
    VERSION_NEW;
}
