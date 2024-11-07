#pragma once

/* Prints information about an error in execution of a system function
   and finishes the program. */
extern void syserr(const char* fmt, ...);

/* Prints information about an error and finishes the program. */
extern void fatal(const char* fmt, ...);
