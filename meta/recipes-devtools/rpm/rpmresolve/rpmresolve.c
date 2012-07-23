/* OpenEmbedded RPM resolver utility

  Written by: Paul Eggleton <paul.eggleton@linux.intel.com>

  Copyright 2012 Intel Corporation

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License version 2 as
  published by the Free Software Foundation.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License along
  with this program; if not, write to the Free Software Foundation, Inc.,
  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.

*/

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>

#include <rpmdb.h>
#include <rpmtypes.h>
#include <rpmtag.h>
#include <rpmts.h>
#include <rpmmacro.h>
#include <rpmcb.h>
#include <rpmlog.h>
#include <argv.h>
#include <mire.h>

int getPackageStr(rpmts ts, const char *NVRA, rpmTag tag, char **value)
{
    int rc = -1;
    rpmmi mi = rpmtsInitIterator(ts, RPMTAG_NVRA, NVRA, 0);
    Header h;
    if ((h = rpmmiNext(mi)) != NULL) {
        HE_t he = (HE_t) memset(alloca(sizeof(*he)), 0, sizeof(*he));
        he->tag = tag;
        rc = (headerGet(h, he, 0) != 1);
        if(rc==0)
            *value = strdup((char *)he->p.ptr);
    }
    (void)rpmmiFree(mi);
    return rc;
}

int loadTs(rpmts **ts, int *tsct, const char *dblistfn)
{
    int count = 0;
    int sz = 5;
    int rc = 0;
    int listfile = 1;
    struct stat st_buf;

    rc = stat(dblistfn, &st_buf);
    if(rc != 0) {
        perror("stat");
        return 1;
    }
    if(S_ISDIR(st_buf.st_mode))
        listfile = 0;

    if(listfile) {
        *ts = malloc(sz * sizeof(rpmts));
        FILE *f = fopen(dblistfn, "r" );
        if(f) {
            char line[2048];
            while(fgets(line, sizeof(line), f)) {
                int len = strlen(line) - 1;
                if(len > 0)
                    // Trim trailing whitespace
                    while(len > 0 && isspace(line[len]))
                        line[len--] = '\0';

                if(len > 0) {
                    // Expand array if needed
                    if(count == sz) {
                        sz += 5;
                        *ts = (rpmts *)realloc(*ts, sz);
                    }

                    char *dbpathm = malloc(strlen(line) + 10);
                    sprintf(dbpathm, "_dbpath %s", line);
                    rpmDefineMacro(NULL, dbpathm, RMIL_CMDLINE);
                    free(dbpathm);

                    rpmts tsi = rpmtsCreate();
                    (*ts)[count] = tsi;
                    rc = rpmtsOpenDB(tsi, O_RDONLY);
                    if( rc ) {
                        fprintf(stderr, "Failed to open database %s\n", line);
                        rc = -1;
                        break;
                    }

                    count++;
                }
            }
            fclose(f);
            *tsct = count;
        }
        else {
            perror(dblistfn);
            rc = -1;
        }
    }
    else {
        // Load from single database
        *ts = malloc(sizeof(rpmts));
        char *dbpathm = malloc(strlen(dblistfn) + 10);
        sprintf(dbpathm, "_dbpath %s", dblistfn);
        rpmDefineMacro(NULL, dbpathm, RMIL_CMDLINE);
        free(dbpathm);

        rpmts tsi = rpmtsCreate();
        (*ts)[0] = tsi;
        rc = rpmtsOpenDB(tsi, O_RDONLY);
        if( rc ) {
            fprintf(stderr, "Failed to open database %s\n", dblistfn);
            rc = -1;
        }
        *tsct = 1;
    }

    return rc;
}

int processPackages(rpmts *ts, int tscount, const char *packagelistfn, int ignoremissing)
{
    int rc = 0;
    int count = 0;
    int sz = 100;
    int i = 0;
    int missing = 0;

    FILE *f = fopen(packagelistfn, "r" );
    if(f) {
        char line[255];
        while(fgets(line, sizeof(line), f)) {
            int len = strlen(line) - 1;
            if(len > 0)
                // Trim trailing whitespace
                while(len > 0 && isspace(line[len]))
                    line[len--] = '\0';

            if(len > 0) {
                int found = 0;
                for(i=0; i<tscount; i++) {
                    ARGV_t keys = NULL;
                    rpmdb db = rpmtsGetRdb(ts[i]);
                    rc = rpmdbMireApply(db, RPMTAG_NAME,
                                RPMMIRE_STRCMP, line, &keys);
                    if (keys) {
                        int nkeys = argvCount(keys);
                        if( nkeys == 1 ) {
                            char *value = NULL;
                            rc = getPackageStr(ts[i], keys[0], RPMTAG_PACKAGEORIGIN, &value);
                            if(rc == 0)
                                printf("%s\n", value);
                            else
                                fprintf(stderr, "Failed to get package origin for %s\n", line);
                            found = 1;
                        }
                        else if( nkeys > 1 ) {
                            fprintf(stderr, "Multiple matches for %s!\n", line);
                        }
                    }
                    if(found)
                        break;
                }

                if( !found ) {
                    if( ignoremissing ) {
                        fprintf(stderr, "unable to find package %s - ignoring\n", line);
                    }
                    else {
                        fprintf(stderr, "unable to find package %s\n", line);
                        missing = 1;
                    }
                }
            }
            count++;
        }
        fclose(f);

        if( missing ) {
            fprintf(stderr, "ERROR: some packages were missing\n");
            rc = 1;
        }
    }
    else {
        perror(packagelistfn);
        rc = -1;
    }

    return rc;
}

void usage()
{
    fprintf(stderr, "OpenEmbedded rpm resolver utility\n");
    fprintf(stderr, "syntax: rpmresolve [-i] <dblistfile> <packagelistfile>\n");
}

int main(int argc, char **argv)
{
    rpmts *ts = NULL;
    int tscount = 0;
    int rc = 0;
    int i;
    int c;
    int ignoremissing = 0;

    opterr = 0;
    while ((c = getopt (argc, argv, "i")) != -1) {
        switch (c) {
            case 'i':
                ignoremissing = 1;
                break;
            case '?':
                if(isprint(optopt))
                    fprintf(stderr, "Unknown option `-%c'.\n", optopt);
                else
                    fprintf(stderr, "Unknown option character `\\x%x'.\n",
                        optopt);
                usage();
                return 1;
            default:
                abort();
        }
    }

    if( argc - optind < 1 ) {
        usage();
        return 1;
    }

    const char *dblistfn = argv[optind];

    //rpmSetVerbosity(RPMLOG_DEBUG);

    rpmReadConfigFiles( NULL, NULL );
    rpmDefineMacro(NULL, "__dbi_txn create nofsync", RMIL_CMDLINE);

    rc = loadTs(&ts, &tscount, dblistfn);
    if( rc )
        return 1;
    if( tscount == 0 ) {
        fprintf(stderr, "Please specify database list file or database location\n");
        return 1;
    }

    if( argc - optind < 2 ) {
        fprintf(stderr, "Please specify package list file\n");
        return 1;
    }
    const char *pkglistfn = argv[optind+1];
    rc = processPackages(ts, tscount, pkglistfn, ignoremissing);

    for(i=0; i<tscount; i++)
        (void) rpmtsCloseDB(ts[i]);
    free(ts);

    return rc;
}
