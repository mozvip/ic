#ifndef IC_I18N_H
#define IC_I18N_H
#include <libintl.h>
#define IC_(message) dgettext("ic", message)
#endif