#ifndef UTILS_H
#define UTILS_H

#include <windows.h>

// Formata um número de bytes numa unidade legível (B, KB, MB, GB)
void FormatarBytes(double bytes, char *buffer, size_t size);

// Converte um FILETIME para um único valor de 64 bits (intervalos de 100ns)
ULONGLONG FileTimeToU64(FILETIME ft);

#endif // UTILS_H
