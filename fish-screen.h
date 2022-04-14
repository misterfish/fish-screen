/* Only the fish-screen binary should include this file, hence no guards,
 * lots of globals etc.
 */

#include <assert.h>

struct screen {
  int pid;
  char *name;
  char *state; // Detached/Attached
  char *date;
};

enum state {
  state_detached=0,
  state_attached,
  state_multi_detached,
  state_multi_attached,
  __num_states,
};

struct {
   bool create;
   char *next_arg;

   char *regex_or_name;
   pid_t pid;

   char hostname[LIMIT_HOSTNAME];

   int num_states;
   // --- indexed by enum state
   vec **data;

   int *pids;
   char **names;
   bool *is_multi;

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
