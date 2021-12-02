#ifndef MYUTILS_H
#define MYUTILS_H

#ifdef DEBUG
#define LOG_DEBUG(msg)                                                         \
  do {                                                                         \
    errs() << "\u001b[33m[DEBUG] \u001b[0m" << msg << "\n";                    \
  } while (0)
#else
#define LOG_DEBUG(msg)                                                         \
  do {                                                                         \
  } while (0)
#endif

#endif // MYUTILS_H