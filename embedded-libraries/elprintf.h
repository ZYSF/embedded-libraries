/* The beginnings of a basic "printf" (snprintf etc.) implementation. 
 * UNFINISHED. Only basic wrapper functions right now.
 */

#ifndef ELPRINTF_H
#define ELPRINTF_H

#include <stdarg.h>
#include <stdint.h>

/*
void printThis( const char* format, ... ) {
    va_list args;
    va_start( args, format );
    vprintf( stderr, format, args );
    va_end( args );
}
*/

#define ELPRINTF_STATIC     static
#define ELPRINTF_INLINE     static inline

ELPRINTF_INLINE uintptr_t elprintf_c(char* buffer, uintptr_t bufferSize, uintptr_t* sizeVariable, char character) {
    if (*sizeVariable >= bufferSize) {
        return 0;
    } else {
        buffer[*sizeVariable] = character;
        sizeVariable[0]++;
        return 1;
    }
}

ELPRINTF_INLINE uintptr_t elprintf_s(char* buffer, uintptr_t bufferSize, uintptr_t* sizeVariable, const char* str) {
    if (str == NULL) {
        str = "NULL";
    }
    uintptr_t i;
    for (i = 0; str[i] != 0; i++) {
        if (elprintf_c(buffer, bufferSize, sizeVariable, str[i]) != 1) {
            // Finish early if failed.
            return i;
        }
    }
    return i;
}

ELPRINTF_INLINE uintptr_t elprintf_v(char* buffer, uintptr_t bufferSize, const char* formatString, va_list varargs) {
    uintptr_t size = 0;
    uintptr_t i = 0;
    while (formatString[i] != 0) {
    }
}

ELPRINTF_STATIC uintptr_t elprintf(char* buffer, uintptr_t bufferSize, const char* formatString, ...) {
    va_list varargs;
    va_start(varargs, formatString);
    uintptr_t result = elprintf_v(buffer, bufferSize, formatString, varargs);
    va_end(varargs);
    return result;
}

/* From ifndef at top of file: */
#endif

