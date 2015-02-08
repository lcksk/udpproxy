#ifndef __GLOBAL_LOGGER_H__
#define __GLOBAL_LOGGER_H__

#ifndef __LOGGER_H__
#define __LOGGER_H__
struct logger {
	struct logger_impl *handler;
};
/* flag definitions for logger_init() */
#define LOGGER_ROTATE_BY_SIZE 0x1
#define LOGGER_ROTATE_PER_HOUR 0x2
#define LOGGER_ROTATE_PER_DAY 0x4
#define LOGGER_ROTATE_FLAG_MASK 0x7
#define LOGGER_ROTATE_DEFAULT LOGGER_ROTATE_PER_DAY

int logger_init(struct logger *, const char *prefix,
				unsigned int flags, unsigned int max_megabytes);
void logger_destroy(struct logger *);
#ifdef NDEBUG
#define logger_debug(lp, fmt, ...)
#else
#define logger_debug(lp, fmt, ...) __logger_debug(lp, __FILE__, __LINE__, fmt, ##__VA_ARGS__)
#endif
#define logger_user(lp, fmt, ...) __logger_user(lp, __FILE__, __LINE__, fmt, ##__VA_ARGS__) /* user specific logs */
#define logger_info(lp, fmt, ...) __logger_info(lp, __FILE__, __LINE__, fmt, ##__VA_ARGS__)
#define logger_warning(lp, fmt, ...) __logger_warning(lp, __FILE__, __LINE__, fmt, ##__VA_ARGS__)
#define logger_error(lp, fmt, ...) __logger_error(lp, __FILE__, __LINE__, fmt, ##__VA_ARGS__)
#define logger_fatal(lp, fmt, ...) __logger_fatal(lp, __FILE__, __LINE__, fmt, ##__VA_ARGS__)
/* ------------------------------------------------------------------------- */
#ifndef NDEBUG
void __logger_debug(struct logger *, const char *filename, int line,
					const char *fmt, ...);
#endif
void __logger_user(struct logger *, const char *filename, int line,
				   const char *fmt, ...);
void __logger_info(struct logger *, const char *filename, int line,
				   const char *fmt, ...);
void __logger_warning(struct logger *, const char *filename, int line,
					  const char *fmt, ...);
void __logger_error(struct logger *, const char *filename, int line,
					const char *fmt, ...);
void __logger_fatal(struct logger *, const char *filename, int line,
					const char *fmt, ...);
#endif

void log_destroy(void);
#ifdef NDEBUG
#define log_debug(fmt, ...)
#else
#define log_debug(fmt, ...) __log_debug(__FILE__, __LINE__, fmt, ##__VA_ARGS__)
#endif
#define log_user(fmt, ...) __log_user(__FILE__, __LINE__, fmt, ##__VA_ARGS__)
#define log_info(fmt, ...) __log_info(__FILE__, __LINE__, fmt, ##__VA_ARGS__)
#define log_warning(fmt, ...) __log_warning(__FILE__, __LINE__, fmt, ##__VA_ARGS__)
#define log_error(fmt, ...) __log_error(__FILE__, __LINE__, fmt, ##__VA_ARGS__)
#define log_fatal(fmt, ...) __log_fatal(__FILE__, __LINE__, fmt, ##__VA_ARGS__)
/* ------------------------------------------------------------------------- */
#ifndef NDEBUG
void __log_debug(const char *filename, int line, const char *fmt, ...);
#endif
void __log_user(const char *filename, int line, const char *fmt, ...);
void __log_info(const char *filename, int line, const char *fmt, ...);
void __log_warning(const char *filename, int line, const char *fmt, ...);
void __log_error(const char *filename, int line, const char *fmt, ...);
void __log_fatal(const char *filename, int line, const char *fmt, ...);
#endif
