
/* This file contains fragments of code common to datastore_t?db.c */

/* Get the current process id */
unsigned long db_handle_pid(void *vhandle){
  dbh_t *handle = vhandle;
  return handle->pid;
}

/* Get the database name */
char *db_handle_name(void *vhandle){
  dbh_t *handle = vhandle;
  return handle->name;
}

/* Get the database filename */
char *db_handle_filename(void *vhandle){
  dbh_t *handle = vhandle;
  return handle->filename;
}

