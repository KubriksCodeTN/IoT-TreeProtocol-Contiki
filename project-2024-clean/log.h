#ifndef __LOG_H__
#define __LOG_H__

#ifdef __DEBUG

#define LOG(...) printf(__VA_ARGS__)

#else

#define LOG(...)

#endif

#endif /* __LOG_H__ */
