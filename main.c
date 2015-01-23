#define _GNU_SOURCE

#define cpsize_t sizeof(char*)
#define csize_t sizeof(char)
#define scr_size_t sizeof(screen)

#define LIMIT_HOSTNAME 20
#define LIMIT_NAME_NEW 20

#define VERSION_OLD 0x1
#define VERSION_NEW 0x2

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h> // exec

// libc6
#include <argp.h>

#include <pcre.h>

//#define DEBUG

#include <fish-util.h>
#include <fish-utils.h>

#define sct(type, name) struct type name = {0};
#define try(x) if(x) { \
        _();        \
        const char *err = perr();       \
        R(err);         \
        warn("Error: %s", _s);         \
    };

typedef struct screen {
    int pid;
    char *name;
    char *state; // Detached/Attached
    char *date;
} screen;

/* Is initted by compiler.
 */
struct {
   bool create;
   char *next_arg;

   char *regex_or_name;

   char hostname[LIMIT_HOSTNAME];

   /* 
    * data = {
    *    vec: [ screen, screen, screen, ... ], // detached
    *    vec: [ screen, screen, screen, ... ], // attached
    * }
    */
   vec *data[2];

   int *pids;
   char **names;

   int total_num;

   char *rl_l;
   size_t rl_s; // no init necessary

   int version;
} g;

error_t argp_parser(int key, char *arg, struct argp_state *state) {
    if (key == 'n') {
        g.create = true;
    }
    else if (key == 'h') {
        argp_state_help(state, stdout, ARGP_HELP_STD_HELP);
        exit(0);
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
        'h', // 0, //'n', // key
        0, // name
        OPTION_ARG_OPTIONAL, // flags
        0, // text
        0
    }, 
    {
        "new", // long opt (--new)
        'n', // short opt (-n)
        0, // name (for long usage message, leave as 0 for optional args)
        OPTION_ARG_OPTIONAL, // flags
        "Create new screen if no match.",
        0
    }, 
    {0}
};

int get_screen_version();

void data_init() {
    g.data[0] = vec_new();
    g.data[1] = vec_new();
}

// 0 -- detached
// 1 -- attached
void data_push(int which, screen *s) {
    vec *v = g.data[which];
    vec_add(v, s);
}

void cleanup() {
    fish_util_cleanup();
    fish_utils_cleanup();
    vec_destroy_flags(g.data[0], VEC_DESTROY_DEEP);
    vec_destroy_flags(g.data[1], VEC_DESTROY_DEEP);
    free(g.rl_l);
    free(g.pids);
    free(g.names);
}

void load_data() {
    sys_die(true);

    FILE *f = sysr("screen -ls");

    while (getline(&g.rl_l, &g.rl_s, f) != -1) {
        char *matches[5] = {0};

        // Date only in newer screen, and anyway not used.
        char *re, *name, *date, *state;
        int pid;
        if (g.version == VERSION_OLD) {
            re = "^ \\s* (\\d+) \\. (.+?) \\s+ ( \\( .+? \\) ) "; //x

            if (! match_matches_flags(g.rl_l, re, matches, PCRE_EXTENDED)) 
                continue;

            // malloc'd => ok to store
            pid = atoi(matches[1]);
            name = matches[2];
            state = matches[3];
        }
        else if (g.version == VERSION_NEW) {
            re = "^ \\s* (\\d+) \\. (.+?) \\s+ ( \\(.+?\\) ) \\s+ ( \\(.+?\\) ) "; //x
            if (! match_matches_flags(g.rl_l, re, matches, PCRE_EXTENDED)) 
                continue;

            // malloc'd => ok to store
            pid = atoi(matches[1]);
            name = matches[2];
            date = matches[3];
            state = matches[4];
        }
        else {
            piepr;
        }

        debug("name, %s", name);
        if (date) debug("date, %s", date);
        debug("state, %s", state);
        debug("pid, %d", pid);

        if (match(name, g.hostname)) { // watch for special chars
            debug("name matched: %s, %s", name, g.hostname);
            name = " ";
        }

        if (g.regex_or_name) {
            if (! match(name, g.regex_or_name)) continue;
        }

        screen *t = malloc(scr_size_t);
        t->pid = pid;

        t->name = name;
        t->date = date;
        t->state = state;

        if (!strncmp(state, "(Attached)", 10)) {
            debug("pushing attached.");
            data_push(1, t);
        }
        else if (!strncmp(state, "(Detached)", 10)) {
            debug("pushing detached.");
            data_push(0, t);
        }
        else {
            piep;
        }
    }
    pclose(f);
}

void check_has_match(bool *has_match, bool *exec, bool *tried_new) {
    if (vec_size(g.data[0]) == 0 && vec_size(g.data[1]) == 0) {
        debug("nothing.");

        *has_match = false;
        *tried_new = false;

        if (!g.create) {
            if (! g.regex_or_name) 
                _exit(1);

            _();
            CY(g.regex_or_name);
            ask("Create %s", _s);

            if (yes_no_flags(F_DEFAULT_YES, 0)) 
                *exec = true;

            *tried_new = true;
        }
    }
    else {
        *has_match = true;
    }
}

char *menu_field(int length, int pid, int state) {
    int len = int_length(pid);
    char *s = str(len + 1);
    sprintf(s, "%d", pid);
    
    char *f = f_field(length, s, len + 1);
    _();
    if (state == 0) CY(f);
    else BR(f);

    free(s);
    free(f);

    char *ret = str(strlen(_s) + 1);
    strcpy(ret, _s);
    return ret;
}

void menu_line(int idx, int pid, char *name, int state) {
    char *field = menu_field(6, pid, state);
    _();
    Y(name);
    printf("%2d. %s %s\n", idx, field, _s);
    free(field);
}

void menu() {
    int idx = 0;
    for (int which = 0; which < 2; which++) {
        vec *v = g.data[which];
        for (int i = 0; i < vec_size(v); i++) {
            idx++;
            screen *scr = vec_get(v, i);

            menu_line(idx, scr->pid, scr->name, which);
        }
    }
    say ("\nor ‘n’ for new");
}

int load_pids_and_names() {
    int n_det = vec_size(g.data[0]);
    int n_att = vec_size(g.data[1]);
    int n[2] = { n_det, n_att };
    int num = n_att + n_det;
    g.pids = calloc(num, sizeof(int));
    g.names = calloc(num, sizeof(char*));
    int idx = -1;
    for (int which = 0; which < 2; which++) {
        for (int i = 0; i < n[which]; i++) {
            vec *v = g.data[which];
            screen *scr = (screen*) vec_get(v, i);
            idx++;
            debug("idx %d, which %d, storing pid %d, name %s", idx, which, scr->pid, scr->name);
            g.pids[idx] = scr->pid;
            g.names[idx] = scr->name;
        }
    }

    debug("pids[0] = %d", g.pids[0]);
    return num;
}

void screen_new(char *name) { // taint
    // use system path
    try(
        execlp("screen", "screen", "-S", name, NULL)
    );
}

void screen_go(int pid) {
    debug("going to pid %d", pid);
    char *p = str(int_length(pid) + 1);
    sprintf(p, "%d", pid);
    try(
        execlp("screen", "screen", "-rd", p, NULL)
   );
}

void init() {
    fish_utils_init();
#ifdef DEBUG
    verbose_cmds(true);
#else
    verbose_cmds(false);
#endif
    g.rl_l = NULL;

    g.version = get_screen_version();
    if (!g.version)
        err("Couldn't get screen version.");

    g.total_num = 0;
    
    data_init();
    if (gethostname(g.hostname, LIMIT_HOSTNAME)) {
        warn(perr());
        strcpy(g.hostname, "unknown");
    }

    debug("hostname: %s", g.hostname);
}

int get_sel() {
    int sel = -1;
    menu();
    int num = g.total_num;
    while (sel == -1) {
        say("");
        printf("(1 - %d): ", num);

        int rc = getline(&g.rl_l, &g.rl_s, stdin);
        if (rc == -1) 
            _exit(1);

        chop(g.rl_l);
        if (match(g.rl_l, "^ \\s* $")) continue;

        if (!strncmp(g.rl_l, "n\0", 2)) {
            printf("name: ");
            if (getline(&g.rl_l, &g.rl_s, stdin) != -1) {
                chop(g.rl_l);

                // check if exists XX

                screen_new(g.rl_l); // done
            }
            // if it works, it execs, so done, else loops again
        }
        else if (is_int_strn(g.rl_l, strlen(g.rl_l))) {
            int i = atoi(g.rl_l);
            if (i <= num && i > 0) 
                sel = i - 1; // got it
        }
        // regex match
        else {
            for (int i = 0; i < num; i++) {
                char *name = g.names[i];
                debug("checking name: %s", name);
                // take first match
                if (match(name, g.rl_l)) {
                    sel = i;
                    break;
                }
            }
        }
    }
    return sel;
}

void _exit(int status) {
    cleanup();
    exit(status);
}

int main(int argc, char **argv) {
    init();

    /* Can also init with: static struct argp args */
    struct argp args = {0};
    args.args_doc = "[regex/name]"; // text in usage msg, after args
    //args.doc = "Before options \v after options"; // help msg

    args.options = options;
    args.parser = (argp_parser_t) argp_parser;

    int arg_index;

    try(
        argp_parse(&args, argc, argv, 
            0,
            &arg_index, 
            0 // input = extra data for parsing function
            )
    )

    // doesn't happen i think
    if (arg_index < argc) {
        g.regex_or_name = argv[arg_index];
    }
    else {
        g.regex_or_name = g.next_arg;
    }

    debug("Make new: %s", g.create ? "yes" : "no");
    debug("regex or name is %s", g.regex_or_name);

    load_data();
    bool do_exec = false;
    bool has_match = false;
    bool tried_new = false; // not necessary
    check_has_match(&has_match, &do_exec, &tried_new);
    if (! has_match) {
        if (do_exec) {
            // use system path
            try(
                execlp("screen", "screen", "-R", g.regex_or_name, NULL)
            );
        }
        else {
            _exit(1);
        }

        // done
    }

    // has_match:
    int i;
    if ((i = vec_size(g.data[0])))
        debug("Last detached is %s", ((screen*) (vec_last(g.data[0])))->name);
    if ((i = vec_size(g.data[1])))
        debug("Last attached is %s", ((screen*) (vec_last(g.data[1])))->name);

    int num = load_pids_and_names();
    g.total_num = num;
    debug("Loaded %d pids and names", num);
    int sel = -1;
    if (num == 1) {
        sel = 0;
        debug("Doing only first");
    }
    else {
        sel = get_sel();
    }

    int pid = g.pids[sel];
    debug("Got sel: %d, pid %d", sel, pid);
    screen_go(pid); //exec, how to cleanup? XX
}

int get_screen_version() {
    int version_maj, version_min = -1;

    char *cmd = "screen -v";
    sys_die(0); // exit value is 1 for some reason
    FILE *f = sysr(cmd);
    if (!f) return 0;
    char *re = "^ Screen \\s+ version \\s+ (\\d+) \\. (\\d+) "; //x

    char *matches[3];
    while (getline(&g.rl_l, &g.rl_s, f) != -1) {
        if (match_matches_flags(g.rl_l, re, matches, PCRE_EXTENDED))  {
            char *v1 = matches[1];
            char *v2 = matches[2];
            version_maj = atoi(v1);
            version_min = atoi(v2);
            //info ("Maj %d min %d", version_maj, version_min);
            break;
        }
    }
    return 
        version_maj == -1 ? 0 :
        version_min == -1 ? 0 :
        version_maj < 4 ? VERSION_OLD :
        (version_maj == 4 && version_min < 2) ? VERSION_OLD :
        VERSION_NEW;
}


