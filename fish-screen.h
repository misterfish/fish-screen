struct screen {
    int pid;
    char *name;
    char *state; // Detached/Attached
    char *date;
};

struct {
   bool create;
   char *next_arg;

   char *regex_or_name;
   pid_t pid;

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

int get_screen_version();
void adios(char *file, ...);
void cleanup_us();
void cleanup_them();
void exit_with_cleanup();
