****FLASH: This program has been superseded by Eudora_fix_mbx.
**** See https://github.com/LenShustek/Eudora_fix_mbx

fix_UTF8: repair UTF-8 codes in Eudora mailboxes

This is a command-line (non-GUI) Windows program that changes the non-ASCII
UTF-8 characters stored inside a Eudora mailbox file into some related ASCII
representation that will be rendered correctly.

It reads the character translations from lines in a file named "translations.txt".
Each line contains a hex byte string representing a UTF-8 character to search for,
and a quoted string representing the ASCII replacement characters. The rules are:
  - The string searched for may be 1 to 4 bytes long.
  - The replacement may not be longer than the string searched for.
  - If the replacement is shorter than the search string, the remaining bytes
    are zeroed in the mailbox, and they are ignored when Eudora renders the text.
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
a check that attempts to enforce that.

When you restart Eudora after modifying a .mbx mailbox file, Eudora will
rebuild the .toc table-of-contents file if it has an older date. In order
to prevent that, we change the timestamp of the table-of-contents file
to the current time, even though we don't modify that file..

The program is invoked with a single argument which is the base filename
of both the mailbox and table-of-contents files:

      fix_UTF8 In

The translations.txt file is expected to be in the current directory.

I don't guarantee this will work well for you, so keep a backup
of the mailbox file in case you don't like what it did!

Len Shustek, 12 Sept 2021
