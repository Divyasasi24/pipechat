/* crypto.c - Caesar cipher (message encryption extension) */
#include "pipechat.h"

static char shift_letter(char c, int shift)
{
    if (c >= 'a' && c <= 'z') {
        return (char)('a' + (c - 'a' + shift) % 26);
    }
    if (c >= 'A' && c <= 'Z') {
        return (char)('A' + (c - 'A' + shift) % 26);
    }
    return c;                             /* digits, spaces, symbols */
}

void caesar_encrypt(char *text, int shift)
{
    shift = ((shift % 26) + 26) % 26;     /* normalise to 0..25 */
    if (shift == 0) {
        return;
    }
    for (; *text != '\0'; text++) {
        *text = shift_letter(*text, shift);
    }
}

void caesar_decrypt(char *text, int shift)
{
    caesar_encrypt(text, -shift);
}