#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// The name of the file to which profile data is written is inherited by the
// forked processes, which never recalculate this, thereby attempting to
// overwrite the file belonging to the parent process. Recalculation can be
// forced by calling __llvm_profile_initialize_file() on the child, but this is
// not enough because nginx will have santized the environment.

void __llvm_profile_initialize_file(void);

static const char *profile_file;

static void datadog_fixup_profile_file_child(void);

__attribute__((constructor)) static void datadog_fixup_profile_file_init() {
  profile_file = getenv("LLVM_PROFILE_FILE");
  if (!profile_file) {
    fprintf(stderr, "Environment variable LLVM_PROFILE_FILE is undefined");
    abort();
  }

  // work around the profile not being updated when the new pattern is the
  // same as the new one.
  // See
  // https://github.com/llvm/llvm-project/blob/82c5d350d200ccc5365d40eac187b9ec967af727/compiler-rt/lib/profile/InstrProfilingFile.c#L870
  char *modified_profile_file = malloc(strlen(profile_file) + 3);
  if (profile_file[0] == '/') {
    modified_profile_file[0] = '/';
    strcpy(&modified_profile_file[1], profile_file);
  } else {
    modified_profile_file[0] = '.';
    modified_profile_file[1] = '/';
    strcpy(&modified_profile_file[2], profile_file);
  }
  profile_file = modified_profile_file;

  int ret = pthread_atfork(0, 0, datadog_fixup_profile_file_child);
  if (ret != 0) {
    perror("Calling pthread_atfork");
    abort();
  }
}

static void datadog_fixup_profile_file_child() {
  setenv("LLVM_PROFILE_FILE", profile_file, 1);
  __llvm_profile_initialize_file();
}
