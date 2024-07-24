#ifndef S4TERM_ERROR_H
#define S4TERM_ERROR_H

void e_print_usage (void);
void e_system (const char *const);

void e_check_ptr (const void *const, const char *const);
void e_at_lexing (const char *const, const char*, const unsigned short, const unsigned short, ...);

#endif