#ifndef BOGOCONFIG_H
#define BOGOCONFIG_H

/* Global variables */

extern const char *spam_header_name;
extern const char *user_config_file;
int process_args(int argc, char **argv);
void read_config_file(const char *filename, bool home_dir);

#endif
