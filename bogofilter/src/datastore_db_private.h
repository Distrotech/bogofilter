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

typedef		int dsm_function		(void *vhandle);
typedef		DB_ENV *dsm_get_env_dbe		(dbe_t *env);
typedef		DB_ENV *dsm_recover_open	(const char *db_file, DB **dbp);
typedef		int	dsm_auto_commit_flags	(void);
typedef		int	dsm_get_rmw_flag	(int open_mode);
typedef 	void dsm_init_config		(void *vhandle, u_int32_t numlocks, u_int32_t numobjs);

typedef struct {
    dsm_get_env_dbe		*dsm_get_env_dbe;
    dsm_recover_open		*dsm_recover_open;
    dsm_auto_commit_flags	*dsm_auto_commit_flags;
    dsm_get_rmw_flag		*dsm_get_rmw_flag;
    dsm_function	        *dsm_begin;
    dsm_function        	*dsm_abort;
    dsm_function        	*dsm_commit;
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
