/** \ingroup popt
 * \file popt/popt.c
 */

/* (C) 19982000 Red Hat, Inc. -- Licensing details are in the COPYING
   file accompanying popt source distributions, available from
   ftp://ftp.rpm.org/pub/rpm/dist */

#undef	MYDEBUG

#include "system.h"

#if HAVE_FLOAT_H
#include <float.h>
#endif
#include <math.h>

#include "findme.h"
#include "poptint.h"

#ifndef HAVE_STRERROR
static char * strerror(int errno) {
    extern int sys_nerr;
    extern char * sys_errlist[];

    if ((0 <= errno) && (errno < sys_nerr))
	return sys_errlist[errno];
    else
	return POPT_("unknown errno");
}
#endif

void poptSetExecPath(poptContext con, const char * path, int allowAbsolute)
{
    con->execPath = _free(con->execPath);
    con->execPath = xstrdup(path);
    con->execAbsolute = allowAbsolute;
}

static void invokeCallbacksPRE(poptContext con, const struct poptOption * opt)
{
    if (opt != NULL)
    for (; opt->longName || opt->shortName || opt->arg; opt++) {
	if (opt->arg == NULL) continue;		/* XXX program error. */
	if ((opt->argInfo & POPT_ARG_MASK) == POPT_ARG_INCLUDE_TABLE) {
	    /* Recurse on included sub-tables. */
	    invokeCallbacksPRE(con, opt->arg);
	} else if ((opt->argInfo & POPT_ARG_MASK) == POPT_ARG_CALLBACK &&
		   (opt->argInfo & POPT_CBFLAG_PRE))
	{   /*@-castfcnptr@*/
	    poptCallbackType cb = (poptCallbackType)opt->arg;
	    /*@=castfcnptr@*/
	    /* Perform callback. */
	    cb(con, POPT_CALLBACK_REASON_PRE, NULL, NULL, opt->descrip);
	}
    }
}

static void invokeCallbacksPOST(poptContext con, const struct poptOption * opt)
{
    if (opt != NULL)
    for (; opt->longName || opt->shortName || opt->arg; opt++) {
	if (opt->arg == NULL) continue;		/* XXX program error. */
	if ((opt->argInfo & POPT_ARG_MASK) == POPT_ARG_INCLUDE_TABLE) {
	    /* Recurse on included sub-tables. */
	    invokeCallbacksPOST(con, opt->arg);
	} else if ((opt->argInfo & POPT_ARG_MASK) == POPT_ARG_CALLBACK &&
		   (opt->argInfo & POPT_CBFLAG_POST))
	{   /*@-castfcnptr@*/
	    poptCallbackType cb = (poptCallbackType)opt->arg;
	    /*@=castfcnptr@*/
	    /* Perform callback. */
	    cb(con, POPT_CALLBACK_REASON_POST, NULL, NULL, opt->descrip);
	}
    }
}

static void invokeCallbacksOPTION(poptContext con,
				  const struct poptOption * opt,
				  const struct poptOption * myOpt,
				  /*@null@*/ const void * myData, int shorty)
{
    const struct poptOption * cbopt = NULL;

    if (opt != NULL)
    for (; opt->longName || opt->shortName || opt->arg; opt++) {
	if ((opt->argInfo & POPT_ARG_MASK) == POPT_ARG_INCLUDE_TABLE) {
	    /* Recurse on included sub-tables. */
	    if (opt->arg != NULL)	/* XXX program error */
		invokeCallbacksOPTION(con, opt->arg, myOpt, myData, shorty);
	} else if ((opt->argInfo & POPT_ARG_MASK) == POPT_ARG_CALLBACK &&
		  !(opt->argInfo & POPT_CBFLAG_SKIPOPTION)) {
	    /* Save callback info. */
	    cbopt = opt;
	} else if (cbopt != NULL &&
		   ((myOpt->shortName && opt->shortName && shorty &&
			myOpt->shortName == opt->shortName) ||
		    (myOpt->longName && opt->longName &&
		/*@-nullpass@*/		/* LCL: opt->longName != NULL */
			!strcmp(myOpt->longName, opt->longName)))
		/*@=nullpass@*/
		   )
	{   /*@-castfcnptr@*/
	    poptCallbackType cb = (poptCallbackType)cbopt->arg;
	    /*@=castfcnptr@*/
	    const void * cbData = (cbopt->descrip ? cbopt->descrip : myData);
	    /* Perform callback. */
	    if (cb != NULL)	/* XXX program error */
		cb(con, POPT_CALLBACK_REASON_OPTION, myOpt,
			con->os->nextArg, cbData);
	    /* Terminate (unless explcitly continuing). */
	    if (!(cbopt->argInfo & POPT_CBFLAG_CONTINUE))
		return;
	}
    }
}

poptContext poptGetContext(const char * name, int argc, const char ** argv,
			   const struct poptOption * options, int flags)
{
    poptContext con = malloc(sizeof(*con));

    if (con == NULL) return NULL;	/* XXX can't happen */
    memset(con, 0, sizeof(*con));

    con->os = con->optionStack;
    con->os->argc = argc;
    /*@-dependenttrans@*/	/* FIX: W2DO? */
    con->os->argv = argv;
    /*@=dependenttrans@*/
    con->os->argb = NULL;

    if (!(flags & POPT_CONTEXT_KEEP_FIRST))
	con->os->next = 1;			/* skip argv[0] */

    con->leftovers = calloc( (argc + 1), sizeof(char *) );
    /*@-dependenttrans@*/	/* FIX: W2DO? */
    con->options = options;
    /*@=dependenttrans@*/
    con->aliases = NULL;
    con->numAliases = 0;
    con->flags = flags;
    con->execs = NULL;
    con->numExecs = 0;
    con->finalArgvAlloced = argc * 2;
    con->finalArgv = calloc( con->finalArgvAlloced, sizeof(*con->finalArgv) );
    con->execAbsolute = 1;
    con->arg_strip = NULL;

    if (getenv("POSIXLY_CORRECT") || getenv("POSIX_ME_HARDER"))
	con->flags |= POPT_CONTEXT_POSIXMEHARDER;

    if (name)
	/*@-nullpass@*/		/* FIX: malloc can return NULL. */
	con->appName = strcpy(malloc(strlen(name) + 1), name);
	/*@=nullpass@*/

    invokeCallbacksPRE(con, con->options);

    return con;
}

static void cleanOSE(struct optionStackEntry *os)
{
    os->nextArg = _free(os->nextArg);
    os->argv = _free(os->argv);
    os->argb = PBM_FREE(os->argb);
}

void poptResetContext(poptContext con)
{
    int i;

    if (con == NULL) return;
    while (con->os > con->optionStack) {
	cleanOSE(con->os--);
    }
    con->os->argb = PBM_FREE(con->os->argb);
    con->os->currAlias = NULL;
    con->os->nextCharArg = NULL;
    con->os->nextArg = NULL;
    con->os->next = 1;			/* skip argv[0] */

    con->numLeftovers = 0;
    con->nextLeftover = 0;
    con->restLeftover = 0;
    con->doExec = NULL;

    if (con->finalArgv != NULL)
    for (i = 0; i < con->finalArgvCount; i++)
	/*@-unqualifiedtrans@*/		/* FIX: typedef double indirection. */
	con->finalArgv[i] = _free(con->finalArgv[i]);
	/*@=unqualifiedtrans@*/

    con->finalArgvCount = 0;
    con->arg_strip = PBM_FREE(con->arg_strip);
    /*@-nullstate@*/	/* FIX: con->finalArgv != NULL */
    return;
    /*@=nullstate@*/
}

/* Only one of longName, shortName may be set at a time */
static int handleExec(poptContext con, /*@null@*/ const char * longName,
		char shortName)
{
    int i;

    if (con->execs == NULL || con->numExecs <= 0) /* XXX can't happen */
	return 0;
    i = con->numExecs - 1;
    if (longName) {
	while (i >= 0 && (!con->execs[i].longName ||
	    strcmp(con->execs[i].longName, longName))) i--;
    } else {
	while (i >= 0 &&
	    con->execs[i].shortName != shortName) i--;
    }

    if (i < 0) return 0;

    if (con->flags & POPT_CONTEXT_NO_EXEC)
	return 1;

    if (con->doExec == NULL) {
	con->doExec = con->execs + i;
	return 1;
    }

    /* We already have an exec to do; remember this option for next
       time 'round */
    if ((con->finalArgvCount + 1) >= (con->finalArgvAlloced)) {
	con->finalArgvAlloced += 10;
	con->finalArgv = realloc(con->finalArgv,
			sizeof(*con->finalArgv) * con->finalArgvAlloced);
    }

    i = con->finalArgvCount++;
    if (con->finalArgv != NULL)	/* XXX can't happen */
    {	char *s  = malloc((longName ? strlen(longName) : 0) + 3);
	if (s != NULL) {	/* XXX can't happen */
	    if (longName)
		sprintf(s, "--%s", longName);
	    else
		sprintf(s, "-%c", shortName);
	    con->finalArgv[i] = s;
	} else
	    con->finalArgv[i] = NULL;
    }

    /*@-nullstate@*/	/* FIX: con->finalArgv[] == NULL */
    return 1;
    /*@=nullstate@*/
}

/* Only one of longName, shortName may be set at a time */
static int handleAlias(poptContext con,
		/*@null@*/ const char * longName, char shortName,
		/*@keep@*/ /*@null@*/ const char * nextCharArg)
{
    int rc;
    int i;

    if (con->os->currAlias && con->os->currAlias->longName && longName &&
	/*@-nullpass@*/	/* LCL: con->os->currAlias->longName != NULL */
	!strcmp(con->os->currAlias->longName, longName))
	/*@=nullpass@*/
	return 0;
    if (con->os->currAlias && shortName &&
	    shortName == con->os->currAlias->shortName)
	return 0;

    if (con->aliases == NULL || con->numAliases <= 0) /* XXX can't happen */
	return 0;
    i = con->numAliases - 1;
    if (longName) {
	while (i >= 0 && (!con->aliases[i].longName ||
	    strcmp(con->aliases[i].longName, longName))) i--;
    } else {
	while (i >= 0 &&
	    con->aliases[i].shortName != shortName) i--;
    }

    if (i < 0) return 0;

    if ((con->os - con->optionStack + 1) == POPT_OPTION_DEPTH)
	return POPT_ERROR_OPTSTOODEEP;

    if (nextCharArg && *nextCharArg)
	con->os->nextCharArg = nextCharArg;

    con->os++;
    con->os->next = 0;
    con->os->stuffed = 0;
    con->os->nextArg = NULL;
    con->os->nextCharArg = NULL;
    con->os->currAlias = con->aliases + i;
    rc = poptDupArgv(con->os->currAlias->argc, con->os->currAlias->argv,
		&con->os->argc, &con->os->argv);
    con->os->argb = NULL;

    return (rc ? rc : 1);
}

static int execCommand(poptContext con)
    /*@modifies fileSystem @*/
{
    const char ** argv;
    int argc = 0;
    const char ** sargv;
    int sargc = 0;
    int rc;

    if (con->doExec == NULL || con->doExec->script == NULL) /*XXX can't happen*/
	return POPT_ERROR_NOARG;
    rc = poptParseArgvString(con->doExec->script, &sargc, &sargv);
    if (rc) return rc;

    if (sargv == NULL || sargc < 1 ||
	(!con->execAbsolute && strchr(sargv[0], '/')))
	    return POPT_ERROR_NOARG;

    argv = malloc(sizeof(*argv) *
			(6 + sargc + con->numLeftovers + con->finalArgvCount));
    if (argv == NULL) return POPT_ERROR_MALLOC;	/* XXX can't happen */

    if (!strchr(sargv[0], '/') && con->execPath) {
	char *s = alloca(strlen(con->execPath) + strlen(sargv[0]) + sizeof("/"));
	sprintf(s, "%s/%s", con->execPath, sargv[0]);
	argv[argc] = s;
    } else {
	argv[argc] = findProgramPath(sargv[0]);
    }
    if (argv[argc++] == NULL) return POPT_ERROR_NOARG;

    if (sargc > 1) {
	memcpy(argv + argc, sargv + 1, sizeof(*argv) * (sargc - 1));
	argc += (sargc - 1);
    }

    if (con->finalArgv != NULL && con->finalArgvCount > 0) {
	memcpy(argv + argc, con->finalArgv,
		sizeof(*argv) * con->finalArgvCount);
	argc += con->finalArgvCount;
    }

    if (con->leftovers != NULL && con->numLeftovers > 0) {
	argv[argc++] = "--";
	memcpy(argv + argc, con->leftovers, sizeof(*argv) * con->numLeftovers);
	argc += con->numLeftovers;
    }

    argv[argc++] = NULL;

#ifdef __hpux
    (void) setresuid(getuid(), getuid(),-1);
#else
/*
 * XXX " ... on BSD systems setuid() should be preferred over setreuid()"
 * XXX 	sez' Timur Bakeyev <mc@bat.ru>
 * XXX	from Norbert Warmuth <nwarmuth@privat.circular.de>
 */
#if defined(HAVE_SETUID)
    (void) setuid(getuid());
#elif defined (HAVE_SETREUID)
    (void) setreuid(getuid(), getuid()); /*hlauer: not portable to hpux9.01 */
#else
    ; /* Can't drop privileges */
#endif
#endif

    if (argv[0] == NULL)
	return POPT_ERROR_NOARG;
#ifdef MYDEBUG
    {	const char ** arg;
	fprintf(stderr, "==> execvp(%s):", argv[0]);
	for (arg = argv; *arg; arg++)
	    fprintf(stderr, " %s", *arg);
	fprintf(stderr, "\n");
    }
#endif

    (void) execvp(argv[0], (char *const *)argv);
    return POPT_ERROR_ERRNO;
}

/*@observer@*/ /*@null@*/ static const struct poptOption *
findOption(const struct poptOption * opt, /*@null@*/ const char * longName,
		char shortName,
		/*@null@*/ /*@out@*/ poptCallbackType * callback,
		/*@null@*/ /*@out@*/ const void ** callbackData,
		int singleDash)
	/*@modifies *callback, *callbackData */
{
    const struct poptOption * cb = NULL;

    /* This happens when a single - is given */
    if (singleDash && !shortName && (longName && *longName == '\0'))
	shortName = '-';

    for (; opt->longName || opt->shortName || opt->arg; opt++) {

	if ((opt->argInfo & POPT_ARG_MASK) == POPT_ARG_INCLUDE_TABLE) {
	    const struct poptOption * opt2;
	    /* Recurse on included sub-tables. */
	    if (opt->arg == NULL) continue;	/* XXX program error */
	    opt2 = findOption(opt->arg, longName, shortName, callback,
			      callbackData, singleDash);
	    if (opt2) {
		/* Sub-table data will be inheirited if no data yet. */
		/*@-nullderef@*/	/* LCL: *callback != NULL */
		if (callback && *callback &&
			callbackData && *callbackData == NULL)
		    /*@-observertrans -dependenttrans @*/
		    *callbackData = opt->descrip;
		    /*@=observertrans =dependenttrans @*/
		/*@=nullderef@*/
		return opt2;
	    }
	} else if ((opt->argInfo & POPT_ARG_MASK) == POPT_ARG_CALLBACK) {
	    cb = opt;
	} else if (longName && opt->longName &&
		   (!singleDash || (opt->argInfo & POPT_ARGFLAG_ONEDASH)) &&
		/*@-nullpass@*/		/* LCL: opt->longName != NULL */
		   !strcmp(longName, opt->longName))
		/*@=nullpass@*/
	{
	    break;
	} else if (shortName && shortName == opt->shortName) {
	    break;
	}
    }

    if (!opt->longName && !opt->shortName)
	return NULL;
    /*@-modobserver -mods @*/
    if (callback) *callback = NULL;
    if (callbackData) *callbackData = NULL;
    if (cb) {
	if (callback)
	/*@-castfcnptr@*/
	    *callback = (poptCallbackType)cb->arg;
	/*@=castfcnptr@*/
	if (!(cb->argInfo & POPT_CBFLAG_INC_DATA)) {
	    if (callbackData)
		/*@-observertrans@*/	/* FIX: typedef double indirection. */
		*callbackData = cb->descrip;
		/*@=observertrans@*/
	}
    }
    /*@=modobserver =mods @*/

    return opt;
}

static const char * findNextArg(poptContext con, unsigned argx, int delete)
	/*@modifies con @*/
{
    struct optionStackEntry * os = con->os;
    const char * arg;

    do {
	int i;
	arg = NULL;
	while (os->next == os->argc && os > con->optionStack) os--;
	if (os->next == os->argc && os == con->optionStack) break;
	if (os->argv != NULL)
	for (i = os->next; i < os->argc; i++) {
	    if (os->argb && PBM_ISSET(i, os->argb)) continue;
	    if (*os->argv[i] == '-') continue;
	    if (--argx > 0) continue;
	    arg = os->argv[i];
	    if (delete) {
		if (os->argb == NULL) os->argb = PBM_ALLOC(os->argc);
		if (os->argb != NULL)	/* XXX can't happen */
		PBM_SET(i, os->argb);
	    }
	    break;
	}
	if (os > con->optionStack) os--;
    } while (arg == NULL);
    /*@-compdef@*/	/* FIX: con->os->argv undefined */
    return arg;
    /*@=compdef@*/
}

static /*@only@*/ /*@null@*/ const char *
expandNextArg(poptContext con, const char * s)
	/*@modifies con @*/
{
    const char *a;
    size_t alen;
    char *t, *te;
    size_t tn = strlen(s) + 1;
    char c;

    te = t = malloc(tn);;
    if (t == NULL) return NULL;		/* XXX can't happen */
    while ((c = *s++) != '\0') {
	switch (c) {
#if 0	/* XXX can't do this */
	case '\\':	/* escape */
	    c = *s++;
	    break;
#endif
	case '!':
	    if (!(s[0] == '#' && s[1] == ':' && s[2] == '+'))
		break;
	    if ((a = findNextArg(con, 1, 1)) == NULL)
		break;
	    s += 3;

	    alen = strlen(a);
	    tn += alen;
	    *te = '\0';
	    t = realloc(t, tn);
	    te = t + strlen(t);
	    strncpy(te, a, alen); te += alen;
	    continue;
	    /*@notreached@*/ break;
	default:
	    break;
	}
	*te++ = c;
    }
    *te = '\0';
    t = realloc(t, strlen(t) + 1);	/* XXX memory leak, hard to plug */
    return t;
}

static void poptStripArg(poptContext con, int which)
{
    if (con->arg_strip == NULL)
	con->arg_strip = PBM_ALLOC(con->optionStack[0].argc);
    if (con->arg_strip != NULL)		/* XXX can't happen */
    PBM_SET(which, con->arg_strip);
}

static int poptSaveLong(const struct poptOption * opt, long aLong)
{
    if (opt->arg == NULL)
	return POPT_ERROR_NULLARG;

    if (opt->argInfo & POPT_ARGFLAG_NOT)
	aLong = ~aLong;
    switch (opt->argInfo & POPT_ARGFLAG_LOGICALOPS) {
    case 0:
	*((long *) opt->arg) = aLong;
	break;
    case POPT_ARGFLAG_OR:
	*((long *) opt->arg) |= aLong;
	break;
    case POPT_ARGFLAG_AND:
	*((long *) opt->arg) &= aLong;
	break;
    case POPT_ARGFLAG_XOR:
	*((long *) opt->arg) ^= aLong;
	break;
    default:
	return POPT_ERROR_BADOPERATION;
	/*@notreached@*/ break;
    }
    return 0;
}

static int poptSaveInt(const struct poptOption * opt, long aLong)
{
    if (opt->arg == NULL)
	return POPT_ERROR_NULLARG;

    if (opt->argInfo & POPT_ARGFLAG_NOT)
	aLong = ~aLong;
    switch (opt->argInfo & POPT_ARGFLAG_LOGICALOPS) {
    case 0:
	*((int *) opt->arg) = aLong;
	break;
    case POPT_ARGFLAG_OR:
	*((int *) opt->arg) |= aLong;
	break;
    case POPT_ARGFLAG_AND:
	*((int *) opt->arg) &= aLong;
	break;
    case POPT_ARGFLAG_XOR:
	*((int *) opt->arg) ^= aLong;
	break;
    default:
	return POPT_ERROR_BADOPERATION;
	/*@notreached@*/ break;
    }
    return 0;
}

#ifdef MYDEBUG
static void prtcon(const char *msg, poptContext con)
{
    if (msg) fprintf(stderr, "%s", msg);
    fprintf(stderr, "\tcon %p os %p nextCharArg \"%s\" nextArg \"%s\" argv[%d] \"%s\"\n",
	con, con->os,
	(con->os->nextCharArg ? con->os->nextCharArg : ""),
	(con->os->nextArg ? con->os->nextArg : ""),
	con->os->next,
	(con->os->argv && con->os->argv[con->os->next]
		? con->os->argv[con->os->next] : ""));
}
#endif

/* returns 'val' element, -1 on last item, POPT_ERROR_* on error */
int poptGetNextOpt(poptContext con)
{
    const struct poptOption * opt = NULL;
    int done = 0;

    if (con == NULL)
	return -1;
    while (!done) {
	const char * origOptString = NULL;
	poptCallbackType cb = NULL;
	const void * cbData = NULL;
	const char * longArg = NULL;
	int canstrip = 0;
	int shorty = 0;

	while (!con->os->nextCharArg && con->os->next == con->os->argc
		&& con->os > con->optionStack) {
	    cleanOSE(con->os--);
	}
	if (!con->os->nextCharArg && con->os->next == con->os->argc) {
	    invokeCallbacksPOST(con, con->options);
	    if (con->doExec) return execCommand(con);
	    return -1;
	}

	/* Process next long option */
	if (!con->os->nextCharArg) {
	    char * localOptString, * optString;
	    int thisopt;

	    if (con->os->argb && PBM_ISSET(con->os->next, con->os->argb)) {
		con->os->next++;
		continue;
	    }
	    thisopt = con->os->next;
	    if (con->os->argv != NULL)	/* XXX can't happen */
	    origOptString = con->os->argv[con->os->next++];

	    if (origOptString == NULL)	/* XXX can't happen */
		return POPT_ERROR_BADOPT;

	    if (con->restLeftover || *origOptString != '-') {
		if (con->leftovers != NULL)	/* XXX can't happen */
		con->leftovers[con->numLeftovers++] = origOptString;
		if (con->flags & POPT_CONTEXT_POSIXMEHARDER)
		    con->restLeftover = 1;
		continue;
	    }

	    /* Make a copy we can hack at */
	    localOptString = optString =
		strcpy(alloca(strlen(origOptString) + 1), origOptString);

	    if (optString[0] == '\0')
		return POPT_ERROR_BADOPT;

	    if (optString[1] == '-' && !optString[2]) {
		con->restLeftover = 1;
		continue;
	    } else {
		char *oe;
		int singleDash;

		optString++;
		if (*optString == '-')
		    singleDash = 0, optString++;
		else
		    singleDash = 1;

		/* XXX aliases with arg substitution need "--alias=arg" */
		if (handleAlias(con, optString, '\0', NULL))
		    continue;
		if (handleExec(con, optString, '\0'))
		    continue;

		/* Check for "--long=arg" option. */
		for (oe = optString; *oe && *oe != '='; oe++)
		    ;
		if (*oe == '=') {
		    *oe++ = '\0';
		    /* XXX longArg is mapped back to persistent storage. */
		    longArg = origOptString + (oe - localOptString);
		}

		opt = findOption(con->options, optString, '\0', &cb, &cbData,
				 singleDash);
		if (!opt && !singleDash)
		    return POPT_ERROR_BADOPT;
	    }

	    if (!opt) {
		con->os->nextCharArg = origOptString + 1;
	    } else {
		if (con->os == con->optionStack &&
		   opt->argInfo & POPT_ARGFLAG_STRIP) {
		    canstrip = 1;
		    poptStripArg(con, thisopt);
		}
		shorty = 0;
	    }
	}

	/* Process next short option */
	if (con->os->nextCharArg) {
	    /*@-branchstate@*/		/* FIX: W2DO? */
	    origOptString = con->os->nextCharArg;

	    con->os->nextCharArg = NULL;

	    if (handleAlias(con, NULL, *origOptString,
			    origOptString + 1)) {
		continue;
	    }
	    if (handleExec(con, NULL, *origOptString)) {
		/* Restore rest of short options for further processing */
		origOptString++;
		if (*origOptString != '\0')
		    con->os->nextCharArg = origOptString;
		continue;
	    }

	    opt = findOption(con->options, NULL, *origOptString, &cb,
			     &cbData, 0);
	    if (!opt)
		return POPT_ERROR_BADOPT;
	    shorty = 1;

	    origOptString++;
	    if (*origOptString != '\0')
		con->os->nextCharArg = origOptString;
	    /*@=branchstate@*/
	}

	if (opt == NULL) return POPT_ERROR_BADOPT;	/* XXX can't happen */
	if (opt->arg && (opt->argInfo & POPT_ARG_MASK) == POPT_ARG_NONE) {
	    if (poptSaveInt(opt, 1L))
		return POPT_ERROR_BADOPERATION;
	} else if ((opt->argInfo & POPT_ARG_MASK) == POPT_ARG_VAL) {
	    if (opt->arg) {
		if (poptSaveInt(opt, (long)opt->val))
		    return POPT_ERROR_BADOPERATION;
	    }
	} else if ((opt->argInfo & POPT_ARG_MASK) != POPT_ARG_NONE) {
	    con->os->nextArg = _free(con->os->nextArg);
	    /*@-usedef@*/	/* FIX: W2DO? */
	    if (longArg) {
	    /*@=usedef@*/
		/*@-evalorder@*/	/* FIX: W2DO? */
		con->os->nextArg = expandNextArg(con, longArg);
		/*@=evalorder@*/
	    } else if (con->os->nextCharArg) {
		/*@-evalorder@*/	/* FIX: W2DO? */
		con->os->nextArg = expandNextArg(con, con->os->nextCharArg);
		/*@=evalorder@*/
		con->os->nextCharArg = NULL;
	    } else {
		while (con->os->next == con->os->argc &&
		       con->os > con->optionStack) {
		    cleanOSE(con->os--);
		}
		if (con->os->next == con->os->argc) {
		    if (opt->argInfo & POPT_ARGFLAG_OPTIONAL)
			con->os->nextArg = NULL;
		    else
			return POPT_ERROR_NOARG;
		} else {

		    /* make sure this isn't part of a short arg or the
			result of an alias expansion */
		    if (con->os == con->optionStack &&
			opt->argInfo & POPT_ARGFLAG_STRIP &&
			canstrip) {
			poptStripArg(con, con->os->next);
		    }
		
		    if (con->os->argv != NULL)	/* XXX can't happen */
		    /*@-evalorder@*/	/* FIX: W2DO? */
		    con->os->nextArg =
			expandNextArg(con, con->os->argv[con->os->next++]);
		    /*@=evalorder@*/
		}
	    }

	    if (opt->arg) {
		switch (opt->argInfo & POPT_ARG_MASK) {
		case POPT_ARG_STRING:
		    /* XXX memory leak, hard to plug */
		    *((const char **) opt->arg) = (con->os->nextArg)
			? xstrdup(con->os->nextArg) : NULL;
		    break;

		case POPT_ARG_INT:
		case POPT_ARG_LONG:
		{   long aLong = 0;
		    char *end;

		    if (con->os->nextArg) {
			aLong = strtol(con->os->nextArg, &end, 0);
			if (!(end && *end == '\0'))
			    return POPT_ERROR_BADNUMBER;
		    }

		    if ((opt->argInfo & POPT_ARG_MASK) == POPT_ARG_LONG) {
			if (aLong == LONG_MIN || aLong == LONG_MAX)
			    return POPT_ERROR_OVERFLOW;
			if (poptSaveLong(opt, aLong))
			    return POPT_ERROR_BADOPERATION;
		    } else {
			if (aLong > INT_MAX || aLong < INT_MIN)
			    return POPT_ERROR_OVERFLOW;
			if (poptSaveInt(opt, aLong))
			    return POPT_ERROR_BADOPERATION;
		    }
		}   break;

		case POPT_ARG_FLOAT:
		case POPT_ARG_DOUBLE:
		{   double aDouble = 0.0;
		    char *end;

		    if (con->os->nextArg) {
			aDouble = strtod(con->os->nextArg, &end);
			if (*end != '\0')
			    return POPT_ERROR_BADNUMBER;
		    }

		    if ((aDouble - HUGE_VAL) < DBL_EPSILON ||
			(aDouble + HUGE_VAL) < DBL_EPSILON)
			return POPT_ERROR_OVERFLOW;
		    if ((aDouble - 0.0) < DBL_EPSILON && errno == ERANGE)
			return POPT_ERROR_OVERFLOW;
		    if ((opt->argInfo & POPT_ARG_MASK) == POPT_ARG_DOUBLE) {
			*((double *) opt->arg) = aDouble;
		    } else {
			if ((aDouble - FLT_MAX) > DBL_EPSILON)
			    return POPT_ERROR_OVERFLOW;
			if ((aDouble + FLT_MIN) > DBL_EPSILON)
			    return POPT_ERROR_OVERFLOW;
			*((float *) opt->arg) = aDouble;
		    }
		}   break;
		default:
		    fprintf(stdout, POPT_("option type (%d) not implemented in popt\n"),
		      opt->argInfo & POPT_ARG_MASK);
		    exit(EXIT_FAILURE);
		}
	    }
	}

	if (cb)
	    invokeCallbacksOPTION(con, con->options, opt, cbData, shorty);
	else if (opt->val && ((opt->argInfo & POPT_ARG_MASK) != POPT_ARG_VAL))
	    done = 1;

	if ((con->finalArgvCount + 2) >= (con->finalArgvAlloced)) {
	    con->finalArgvAlloced += 10;
	    con->finalArgv = realloc(con->finalArgv,
			    sizeof(*con->finalArgv) * con->finalArgvAlloced);
	}

	if (con->finalArgv != NULL)
	{   char *s = malloc((opt->longName ? strlen(opt->longName) : 0) + 3);
	    if (s != NULL) {	/* XXX can't happen */
		if (opt->longName)
		    sprintf(s, "%s%s",
			((opt->argInfo & POPT_ARGFLAG_ONEDASH) ? "-" : "--"),
			opt->longName);
		else
		    sprintf(s, "-%c", opt->shortName);
		con->finalArgv[con->finalArgvCount++] = s;
	    } else
		con->finalArgv[con->finalArgvCount++] = NULL;
	}

	if (opt->arg && (opt->argInfo & POPT_ARG_MASK) == POPT_ARG_NONE)
	    /*@-ifempty@*/ ; /*@=ifempty@*/
	else if ((opt->argInfo & POPT_ARG_MASK) == POPT_ARG_VAL)
	    /*@-ifempty@*/ ; /*@=ifempty@*/
	else if ((opt->argInfo & POPT_ARG_MASK) != POPT_ARG_NONE) {
	    if (con->finalArgv != NULL && con->os->nextArg)
	        con->finalArgv[con->finalArgvCount++] =
			/*@-nullpass@*/	/* LCL: con->os->nextArg != NULL */
			xstrdup(con->os->nextArg);
			/*@=nullpass@*/
	}
    }

    return (opt ? opt->val : -1);	/* XXX can't happen */
}

const char * poptGetOptArg(poptContext con)
{
    const char * ret = NULL;
    if (con) {
	ret = con->os->nextArg;
	con->os->nextArg = NULL;
    }
    return ret;
}

const char * poptGetArg(poptContext con)
{
    const char * ret = NULL;
    if (con && con->leftovers != NULL && con->nextLeftover < con->numLeftovers)
	ret = con->leftovers[con->nextLeftover++];
    return ret;
}

const char * poptPeekArg(poptContext con)
{
    const char * ret = NULL;
    if (con && con->leftovers != NULL && con->nextLeftover < con->numLeftovers)
	ret = con->leftovers[con->nextLeftover];
    return ret;
}

const char ** poptGetArgs(poptContext con)
{
    if (con == NULL ||
	con->leftovers == NULL || con->numLeftovers == con->nextLeftover)
	return NULL;

    /* some apps like [like RPM ;-) ] need this NULL terminated */
    con->leftovers[con->numLeftovers] = NULL;

    /*@-nullret -nullstate @*/	/* FIX: typedef double indirection. */
    return (con->leftovers + con->nextLeftover);
    /*@=nullret =nullstate @*/
}

poptContext poptFreeContext(poptContext con)
{
    int i;

    if (con == NULL) return con;
    poptResetContext(con);
    con->os->argb = _free(con->os->argb);

    if (con->aliases != NULL)
    for (i = 0; i < con->numAliases; i++) {
	con->aliases[i].longName = _free(con->aliases[i].longName);
	con->aliases[i].argv = _free(con->aliases[i].argv);
    }

    if (con->execs != NULL)
    for (i = 0; i < con->numExecs; i++) {
	con->execs[i].longName = _free(con->execs[i].longName);
	con->execs[i].script = _free(con->execs[i].script);
    }
    con->execs = _free(con->execs);

    con->leftovers = _free(con->leftovers);
    con->finalArgv = _free(con->finalArgv);
    con->appName = _free(con->appName);
    con->aliases = _free(con->aliases);
    con->otherHelp = _free(con->otherHelp);
    con->execPath = _free(con->execPath);
    con->arg_strip = PBM_FREE(con->arg_strip);
    
    con = _free(con);
    return con;
}

int poptAddAlias(poptContext con, struct poptAlias newAlias,
		/*@unused@*/ int flags)
{
    int aliasNum = con->numAliases++;
    struct poptAlias * alias;

    /* SunOS won't realloc(NULL, ...) */
    if (!con->aliases)
	con->aliases = malloc(sizeof(newAlias) * con->numAliases);
    else
	con->aliases = realloc(con->aliases,
			       sizeof(newAlias) * con->numAliases);
    alias = con->aliases + aliasNum;

    alias->longName = (newAlias.longName)
	/*@-nullpass@*/		/* FIX: malloc can return NULL. */
	? strcpy(malloc(strlen(newAlias.longName) + 1), newAlias.longName)
	/*@=nullpass@*/
	: NULL;
    alias->shortName = newAlias.shortName;
    alias->argc = newAlias.argc;
    alias->argv = newAlias.argv;

    return 0;
}

const char * poptBadOption(poptContext con, int flags)
{
    struct optionStackEntry * os = NULL;

    if (con != NULL)
	os = (flags & POPT_BADOPTION_NOALIAS) ? con->optionStack : con->os;

    /*@-nullderef@*/	/* LCL: os->argv != NULL */
    return (os && os->argv ? os->argv[os->next - 1] : NULL);
    /*@=nullderef@*/
}

const char *const poptStrerror(const int error)
{
    switch (error) {
      case POPT_ERROR_NOARG:
	return POPT_("missing argument");
      case POPT_ERROR_BADOPT:
	return POPT_("unknown option");
      case POPT_ERROR_BADOPERATION:
	return POPT_("mutually exclusive logical operations requested");
      case POPT_ERROR_NULLARG:
	return POPT_("opt->arg should not be NULL");
      case POPT_ERROR_OPTSTOODEEP:
	return POPT_("aliases nested too deeply");
      case POPT_ERROR_BADQUOTE:
	return POPT_("error in parameter quoting");
      case POPT_ERROR_BADNUMBER:
	return POPT_("invalid numeric value");
      case POPT_ERROR_OVERFLOW:
	return POPT_("number too large or too small");
      case POPT_ERROR_MALLOC:
	return POPT_("memory allocation failed");
      case POPT_ERROR_ERRNO:
	return strerror(errno);
      default:
	return POPT_("unknown error");
    }
}

int poptStuffArgs(poptContext con, const char ** argv)
{
    int argc;
    int rc;

    if ((con->os - con->optionStack) == POPT_OPTION_DEPTH)
	return POPT_ERROR_OPTSTOODEEP;

    for (argc = 0; argv[argc]; argc++)
	;

    con->os++;
    con->os->next = 0;
    con->os->nextArg = NULL;
    con->os->nextCharArg = NULL;
    con->os->currAlias = NULL;
    rc = poptDupArgv(argc, argv, &con->os->argc, &con->os->argv);
    con->os->argb = NULL;
    con->os->stuffed = 1;

    return rc;
}

const char * poptGetInvocationName(poptContext con)
{
    return (con->os->argv ? con->os->argv[0] : "");
}

int poptStrippedArgv(poptContext con, int argc, char ** argv)
{
    int numargs = argc;
    int j = 1;
    int i;
    
    if (con->arg_strip)
    for (i = 1; i < argc; i++) {
	if (PBM_ISSET(i, con->arg_strip))
	    numargs--;
    }
    
    for (i = 1; i < argc; i++) {
	if (con->arg_strip && PBM_ISSET(i, con->arg_strip))
	    continue;
	argv[j] = (j < numargs) ? argv[i] : NULL;
	j++;
    }
    
    return numargs;
}
