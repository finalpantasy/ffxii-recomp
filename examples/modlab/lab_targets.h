/* The mod lab's stand-in game functions (src/mod_lab.cpp). Every lab mod
 * implements the same three hooks, in its own language:
 *
 *   LAB_DAMAGE  filter:  original returns rcx * 3 + rdx; the hook calls the
 *               original, then sets rax = rax * 2 + 1.
 *   LAB_LOOKUP  replace: rax = u16 table[rcx % 4096] * 3 + 7, then return.
 *   LAB_SCORE   replace: rax = sum over k = 0..31 of
 *               u16 table[(rcx + 7k) % 4096] * (k + 1), then return.
 *
 * The table is 4096 u16 values at guest address LAB_TABLE. */
#ifndef FFXII_LAB_TARGETS_H
#define FFXII_LAB_TARGETS_H
#define LAB_DAMAGE 0x1000
#define LAB_LOOKUP 0x2000
#define LAB_SCORE 0x3000
#define LAB_TABLE 0x10000
#endif
