//file: fix_UTF8.cpp
/*-----------------------------------------------------------------------------------------------------

fix_UTF8: repair UTF-8 codes in Eudora mailboxes

This is a command-line (non-GUI) program that changes the non-ASCII UTF-8 characters
stored inside a Eudora mailbox file into some related ASCII representation that
will be rendered correctly.

It reads the character translations from a file named "translations.txt".
Each line contains a hex byte string representing a UTF-8 character to search for,
and a quoted string representing the ASCII replacement characters. The rules are:
  - The string searched for may be 1 to 4 bytes long.
  - The replacement may not be longer than the string searched for.
  - If the replacement is shorter than the search string, the remaining bytes
    are zeroed in the mailbox, which is ignored when Eudora renders the text.
  - The replacement string may be delimited either by " or ', and the delimiter
    may not appear within the string.
  - The strings may be separated by one or more spaces.
  - The rest of the line after the replacement string is treated as a comment.

  Example translations.txt lines:
      E28093 "-"    En dash
      E28094 "--"   Em dash
      E2809C '"'    left double quote
      E280A6 "..."  horizontal ellipsis
      C2A0  " "   non-breaking space
      C2A9  "c"   copyright sign

This program should not be used when Eudora is running, and there is
a check at the beginning that attempts to enforce that.

When you restart Eudora after modifying a .mbx mailbox file, Eudora will
rebuild the .toc table-of-contents file if it has an older date. In order
to prevent that, we change the timestamp of the table-of-contents file
to the current time, even though we don't modify it.

The program is invoked with a single argument which is the base filename
of both the mailbox and table-of-contents files:

      fix_UTF8 In

The translations.txt file is expected to be in the current directory.

I don't guarantee this will work well for you, so keep a backup
of the mailbox file in case you don't like what it did!


----- Change log -------

12 Sep 2021, L. Shustek, V0.1   first version

------------------------------------------------------------------------------------------------------*/
/* Copyright(c) 2021, Len Shustek
The MIT License(MIT)
Permission is hereby granted, free of charge, to any person obtaining a copy of this software
and associated documentation files(the "Software"), to deal in the Software without
restriction, including without limitation the rights to use, copy, modify, merge, publish,
distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the
Software is furnished to do so, subject to the following conditions :

The above copyright notice and this permission notice shall be included in all copies or
substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING
BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
NONINFRINGEMENT.IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE. */

#define VERSION "0.1"
#define DEBUG false

#include "stdafx.h"
#include <stdio.h>
#include <stdio.h>
#include <stdlib.h>
#include <Share.h>
#include <Windows.h>

#define max(x, y) (((x) > (y)) ? (x) : (y))
#define min(x, y) (((x) < (y)) ? (x) : (y))
typedef unsigned char byte;

#define BLKSIZE 4096    // how much we read or write at a time, for efficiency
#define MAX_TRANSLATIONS 250
#define MAX_STR 4       // max size of search and replacement strings
#define MAXNAME 80

byte buffer[2 * BLKSIZE];
int bufferlen=0;        // how much is in the buffer
int bufferpos=0;        // where we're looking in the buffer, 0..BLKSIZE-1
fpos_t filepos1st;      // the file position of the first half of the buffer's data
fpos_t filepos2nd;      // the file position of the second half of the buffer's data
fpos_t fileposnext;     // the next file position to read
bool dirty1st=false;    // do we need to write the first half of the buffer?
bool dirty2nd=false;    // do we need to write the second half of the buffer?
int total_changes = 0;
FILE *fid;

struct translation_t { // table of translations
   byte srch[MAX_STR];  // chars to search for
   int srchlen;         // how many chars
   byte repl[MAX_STR];  // chars to replace it with
   int repllen;         // how many chars
   int used;            // how often this translation was used
} translation[MAX_TRANSLATIONS] = { 0 };
int num_translations = 0;

void assert(bool test, const char *msg) {
   if (!test) {
      printf("ERROR: %s\n", msg);
      exit(8); } }

void show_stats(void) {
   printf("%d changes were made\n", total_changes);
   for (int ndx = 0; ndx < num_translations; ++ndx) {
      int byt;
      printf("  ");
      for (byt = 0; byt < translation[ndx].srchlen; ++byt)
         printf("%02X", translation[ndx].srch[byt]);
      for (; byt < MAX_STR; ++byt) printf("  ");
      printf(" changed to \"");
      for (byt = 0; byt < translation[ndx].repllen; ++byt)
         printf("%c", translation[ndx].repl[byt]);
      printf("\"");
      for (; byt < MAX_STR; ++byt) printf(" ");
      printf(" %d time%c\n", translation[ndx].used, translation[ndx].used == 1 ? ' ' : 's'); } }

void chk_buffer(bool forceend) { // keep the pointer in the first half of the buffer
   if (forceend || bufferpos >= BLKSIZE) {
      if (dirty1st) {
         if (DEBUG) printf("writing first %d bytes of buffer to file position %lld\n", min(BLKSIZE, bufferlen), filepos1st);
         fsetpos(fid, &filepos1st);// write out first half if it was changed
         fwrite(buffer, 1, min(BLKSIZE, bufferlen), fid); }
      memcpy(buffer, buffer + BLKSIZE, BLKSIZE);  // slide second half to first half
      filepos1st = filepos2nd;
      filepos2nd = fileposnext;
      fsetpos(fid, &fileposnext);
      int nbytes = fread(buffer + BLKSIZE, 1, BLKSIZE, fid); // read (up to) another half
      fgetpos(fid, &fileposnext);
      if (DEBUG) printf("buffer starts at file pos %lld, read %d bytes at file pos %lld, next file pos %lld\n", filepos1st, nbytes, filepos2nd, fileposnext);
      dirty1st = dirty2nd;
      dirty2nd = false;
      bufferlen = bufferlen - BLKSIZE + nbytes;
      bufferpos -= BLKSIZE; } }

void read_translations(void) { // parse the translations file
#define TRANSLATION_FILE "translations.txt"
#define LINESIZE 80
   char line[LINESIZE];
   assert(fid = fopen(TRANSLATION_FILE, "r"), "Can't open" TRANSLATION_FILE);
   while (fgets(line, LINESIZE, fid)) {
      printf("%s", line);
      int chrndx = 0;
      while (line[chrndx] == ' ') ++chrndx;
      if (line[chrndx] == 0 || line[chrndx] == '\n') continue;
      struct translation_t *tp = &translation[num_translations];
      for (int nibndx = 0; ; ++nibndx) {  // parse hex string
         char ch = line[chrndx++];
         if (ch == ' ') break;
         if (ch >= '0' && ch <= '9') ch -= '0';
         else if (ch >= 'A' && ch <= 'F') ch -= 'A' - 10;
         else if (ch >= 'a' && ch <= 'f') ch -= 'a' - 10;
         else assert(false, "bad hex");
         assert(nibndx < MAX_STR * 2, "hex string too long");
         tp->srch[nibndx >> 1] = (tp->srch[nibndx >> 1] << 4) | ch;
         if (nibndx & 1) ++tp->srchlen; }
      assert((chrndx & 1) == 1, "odd number of hex chars");
      assert(++num_translations < MAX_TRANSLATIONS, "too many translations");
      while (line[chrndx] == ' ') ++chrndx;
      char delim = line[chrndx++];
      assert(delim == '"' || delim == '\'', "missing string delimiter");
      for (int rndx = 0; line[chrndx] != delim; ++rndx) { // parse replacement string
         assert(rndx < MAX_STR, "replacement string too long");
         tp->repl[rndx] = line[chrndx++];
         ++tp->repllen; }
      assert(tp->repllen <= tp->srchlen, "replacement longer than search string"); }
   fclose(fid);
   printf("processed and stored %d translations\n", num_translations); }

void show_help(void) {
   printf("\nChange UTF-8 characters to close ASCII equivalents (padded with zeroes) in Eudora mailboxes.\n");
   printf("It reads translation.txt, which has lines like:\n");
   printf("   E28098 \"'\"   left single quote\n");
   printf("   C2A1 '\"'  double quote\n");
   printf("   E280A6 \"...\"   ellipsis\n");
   printf("invoke as: fix_UTF8 filename\n");
   printf("The mailbox filename.mbx is changed in place, so keep a backup!\n");
   printf("It also updates the timestamp of filename.toc so Eudora doesn't rebuild it.\n");
   exit(8); }

bool eudora_running(void) {
#define LINESIZE 80
   char buf[LINESIZE];
#define EUDORA_NAME "Eudora.exe"
   snprintf(buf, LINESIZE, "tasklist /FI \"IMAGENAME eq " EUDORA_NAME"\"");
   if (DEBUG)printf("%s\n", buf);
   FILE *fp = _popen(buf, "r");
   assert(fp, "can't start tasklist\n");
   bool running = false;
   while (fgets(buf, LINESIZE, fp)) {
      if (DEBUG) printf("tasklist: %s", buf);
      // see if any line starts with the Eudora executable file name
      if (strncmp(buf, EUDORA_NAME, sizeof(EUDORA_NAME) - 1) == 0)
         running = true; }
   assert(_pclose(fp) == 0, "can't find 'tasklist' command; check path\n");
   return running; }

void update_filetime(const char *filepath) {
#if 0  // this technique doesn't work if a path was specified and we're not running in the same directory
   char stringbuf[MAXNAME];
   snprintf(stringbuf, MAXNAME, "copy /b %s+,,", filepath);
   if (DEBUG) printf("%s\n", stringbuf);
   int rc = system(stringbuf); // update timestamp of the corresponding TOC file
   printf("%s timestamp %s\n", argv[1], rc == 0 ? "updated" : "could not be updated");
#endif
   // this technique seems to work in more cases
   SYSTEMTIME thesystemtime;
   GetSystemTime(&thesystemtime); // get time now
   FILETIME thefiletime;
   SystemTimeToFileTime(&thesystemtime, &thefiletime); // convert to file time format
   printf("updating timestamp of %s\n", filepath);
   HANDLE filehandle = // get a handle that allow atttributes to be written
      CreateFile(filepath, FILE_WRITE_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
   assert(filehandle != INVALID_HANDLE_VALUE, "can't open handle to update timestamp");
   assert(SetFileTime(filehandle, (LPFILETIME)NULL, (LPFILETIME)NULL, &thefiletime), "can't update timestamp");
   CloseHandle(filehandle); }

int main(int argc, char **argv) {
   printf("\"fix_UTF8\"  version %s  (c) L. Shustek, 2021\n", VERSION);
   if (argc <= 1) show_help();
   assert(!eudora_running(), "Eudora is running; stop it first");
   read_translations();

   char stringbuf[MAXNAME];
   snprintf(stringbuf, MAXNAME, "%s.mbx", argv[1]);
   printf("opening mailbox file %s\n", stringbuf);
   assert(fid = fopen(stringbuf, "rb+"), "can't open file");
   fgetpos(fid, &filepos1st);
   bufferlen = fread(buffer, 1, BLKSIZE, fid); // prime both halves of the buffer
   fgetpos(fid, &filepos2nd);
   bufferlen += fread(buffer + BLKSIZE, 1, BLKSIZE, fid);
   fgetpos(fid, &fileposnext);
   if (DEBUG) printf("buffer primed with %d bytes, filepos1st %lld, filepos2nd %lld\n", bufferlen, filepos1st, filepos2nd);

   do {
      chk_buffer(false);
      for (int xlate = 0; xlate < num_translations; ++xlate) { // try each translation at this position
         if (memcmp(buffer + bufferpos, translation[xlate].srch, translation[xlate].srchlen) == 0) {
            memcpy(buffer + bufferpos, translation[xlate].repl, translation[xlate].repllen); // do substitution
            if (bufferpos < BLKSIZE) dirty1st = true; else dirty2nd = true;
            bufferpos += translation[xlate].repllen - 1; // point to the last byte replaced
            int zerocount = translation[xlate].srchlen - translation[xlate].repllen;
            while (zerocount--)  buffer[++bufferpos] = 0; // pad with zeros
            if (bufferpos >= BLKSIZE) dirty2nd = true;
            ++translation[xlate].used;
            ++total_changes;
            break; } }
      ++bufferpos; }
   while (bufferpos < bufferlen);
   while (bufferlen > 0) chk_buffer(true);
   fclose(fid);
   show_stats();
   // update the TOC file's timestamp so Eudora won't rebuild it
   snprintf(stringbuf, MAXNAME, "%s.toc", argv[1]);
   update_filetime(stringbuf);
   return 0; }

