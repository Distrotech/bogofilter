/** Default flags for DB_ENV->open() */

#define MAGIC_DBE 0xdbe
#define MAGIC_DBH 0xdb4

/** implementation internal type to keep track of database environments
 * we have opened. */
typedef struct {
    int		magic;
    DB_ENV	*dbe;		/* stores the environment handle */
    char	*directory;	/* stores the home directory for this environment */
} dbe_t;

/* public -- used in datastore.c */
typedef int	dsm_function		(void *vhandle);
/* private -- used in datastore_db_*.c */
typedef DB_ENV *dsm_get_env_dbe		(dbe_t *env);
typedef const char *dsm_database_name	(const char *db_file);
typedef DB_ENV *dsm_recover_open	(const char *db_file, DB **dbp);
typedef int	dsm_auto_commit_flags	(void);
typedef int	dsm_get_rmw_flag	(int open_mode);
typedef void	dsm_init_config		(void *vhandle, u_int32_t numlocks, u_int32_t numobjs);
typedef int	dsm_lock		(void *handle, int open_mode);
typedef ex_t	dsm_common_close	(DB_ENV *dbe, const char *db_file);
typedef int	dsm_sync		(DB_ENV *dbe, int ret);
typedef void	dsm_log_flush		(DB_ENV *dbe);
typedef dbe_t  *dsm_env_init		(const char *directory);
typedef void	dsm_cleanup_lite	(dbe_t *env);

typedef struct {
    /* public -- used in datastore.c */
    dsm_function	        *dsm_begin;
    dsm_function        	*dsm_abort;
    dsm_function        	*dsm_commit;
    /* private -- used in datastore_db_*.c */
    dsm_env_init		*dsm_env_init;
    dsm_cleanup_lite		*dsm_cleanup_lite;
    dsm_get_env_dbe		*dsm_get_env_dbe;
    dsm_database_name		*dsm_database_name;
    dsm_recover_open		*dsm_recover_open;
    dsm_auto_commit_flags	*dsm_auto_commit_flags;
    dsm_get_rmw_flag		*dsm_get_rmw_flag;
    dsm_lock			*dsm_lock;
    dsm_common_close		*dsm_common_close;
    dsm_sync			*dsm_sync;
    dsm_log_flush		*dsm_log_flush;
} dsm_t;

/** implementation internal type to keep track of databases
 * we have opened. */
typedef struct {
    int		magic;
    char	*path;
    char	*name;
    int		fd;		/* file descriptor of data base file */
    dbmode_t	open_mode;	/* datastore open mode, DS_READ/DS_WRITE */
    DB		*dbp;		/* data base handle */
    bool	locked;
    bool	is_swapped;	/* set if CPU and data base endianness differ */
    bool	created;	/* if newly created; for datastore.c (to add .WORDLIST_VERSION) */
    dbe_t	*dbenv;		/* "parent" environment */
    DB_TXN	*txn;		/* transaction in progress or NULL */
    /** OO database methods */
    dsm_t	*dsm;
} dbh_t;

#define DBT_init(dbt)		(memset(&dbt, 0, sizeof(DBT)))
