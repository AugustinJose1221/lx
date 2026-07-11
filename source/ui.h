#ifndef LX_UI_H
#define LX_UI_H

#include "filter.h"
#include "logfile.h"

/* Run the interactive viewer. Takes ownership of `filter` (may be NULL);
 * filter_str is the expression it was compiled from (may be "").
 * detail_lines limits each value in the entry inspector to that many
 * wrapped lines (0 = default cap of 500 characters). Returns the
 * process exit code. */
int ui_run(LogFile *lf, const char *filter_str, FNode *filter, int follow,
           int detail_lines);

#endif
