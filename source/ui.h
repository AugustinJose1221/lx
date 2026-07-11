#ifndef LX_UI_H
#define LX_UI_H

#include "filter.h"
#include "logfile.h"

/* Run the interactive viewer. Takes ownership of `filter` (may be NULL);
 * filter_str is the expression it was compiled from (may be ""). Returns
 * the process exit code. */
int ui_run(LogFile *lf, const char *filter_str, FNode *filter, int follow);

#endif
