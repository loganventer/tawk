#ifndef APP_CORE_RESTORE_REPORT_H
#define APP_CORE_RESTORE_REPORT_H

#include "core/backup_manifest.h"

/* What a restore put back, and where the data it replaced went. */
typedef struct RestoreReport {
    BackupManifest manifest;     /* what the backup held */
    char           aside_suffix[64];   /* replaced data was renamed with this suffix */
    int            moved_aside;  /* how many existing items were moved aside */
    int            unchecked;    /* the backup was made before backups could detect a change to them */
} RestoreReport;

#endif
