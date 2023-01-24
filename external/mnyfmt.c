// (C) Copyright Adolfo Di Mare 2011
// Use, modification and distribution are subject to the
// Boost Software License, Version 1.0.
// (See accompanying file LICENSE_1_0.txt or copy at
// http://www.boost.org/LICENSE_1_0.txt)

// Revision history:
// Oct 2011      Adolfo Di Mare ==> Initial (non boost) version
// Jun+Ago 2012  Adolfo Di Mare ==> CE correction

// mnyfmt.c (C) 2011 adolfo@di-mare.com

#include "mnyfmt.h"

#ifdef __cplusplus
// To compile the C source with a C++ compiler, override the file's extension.
// The GNU gcc compiler does this with option -x: gcc -x c++
#endif

/** \file   mnyfmt.c
    \brief  Implementation for \c mnyfmt().

    \author Adolfo Di Mare <adolfo@di-mare.com>
    \date   2011
*/

// Examples and unit test cases are in in file mnyfmtts.c

/** Formats and stores in \c fmtstr the money amount.

    Before invocation, the formatting pattern (picture clause) is stored in
    result string \c fmtstr. To avoid using \c (double) values that have many
    round off problems, the parameter for this function is an integer scaled
    to 10^CE digits. For example, when using CE==2 digits, the monetary value
    "$2,455.87" is represented by the integer '245587', and if CE==4 digits
    are used, the integer value would be '24558700'.

    - The (integer) value to format is \c moneyval.
    - Overwrites \c fmtstr with the formatted value.
    - On error, leaves \c fmtstr untouched and returns \c (char*)(0).
    - If the \c fmtstr does not have enough format characters \c '9' for
      the integer part to format, of if the \c '-' cannot fit on top of
      a \c '9' character, \c fmtstr remains untouched and the value
      returned is \c (char*)(0).

    - The valid range for CE, the 'current exponent', is [0..6]
      [ a CE of 7 or bigger leaves \c fmtstr untouched and the value
      returned is \c (char*)(0) ].
    - The first occurrence of the character \c dec is the decimal fraction
      separator (usually \c'.' or \c',').

    - When the decimal fraction separator character \c dec does not appear
      in \c fmtstr it is assumed to be \c '\0' (end of string character).
    - After the \c dec separator all the leading consecutive \c '9' format
      characters are substituted with the corresponding digit from the
      decimal part in \c moneyval, using digit zero \c '0' as fill character.
    - All digits that immediately follow the decimal fraction separator are
      changed either to zero or to the corresponding digit taken from the
      decimal part in \c moneyval.
    - Both the integer part and the fractional part are filled with the digits
      that correspond to its position. This means that a format string like
      \c "9999.9999" wild yield \c "0123.8700" as result when
      \c moneyval==1238700 and \c CE==4.
    - Characters trailing after the \c dec separator that are not the \c '9'
      format digit are left untouched.
    - All format characters \c '9' appearing before the decimal separator \c
      dec will be replaced by digit zero \c '0' if the corresponding digit in
      \c moneyval is not significant.
    - When \c moneyval is negative, the \c '-' sign will be place over the
      \c '9' immediately before the more significant digit.
    - Non format characters in \c fmtstr are left untouched.
    - The negative sign always is \c '-' and it is always placed on top of
      the corresponding format character.

    - Returns \c (char*)(0) when the formatted value does not fit within
      \c strlen(fmtstr) characters.
    - Returns a pointer to the first significant digit in the formatted string,
      within \c fmtstr or to the \c '-' sign if the formatted value is negative.
    - Before storing the format string in \c fmtstr, the programmer must ensure
      that \c fmtstr is big enough to hold the format string.

    \remark
    - This routine basically substitutes each \c '9' character in \c fmtstr for
      its corresponding decimal digit, or \c '0' when it is not a significant
      digit. All other characters within \c fmtstr remain untouched.
    - As it happens with many C functions, before invocation the programmer
      must be sure that \c fmtstr is big enough to copy on it the complete
      format string, as otherwise memory beyond \c fmtstr would be overwritten.
    - There is no \c (wchart_t) version for this function, as it is meant to
      place digits in a formatting string. After placement, the result string
      can be converted to other forms.

    \dontinclude mnyfmtts.c
    \skipline    test.example
    \until       }}

    \see mnyfmtts.c
*/
#define CE 2
char* mnyfmt(char *fmtstr, char dec, mnyfmt_long moneyval/*, const unsigned CE*/) {
    #ifndef __cplusplus
        const int false = 0; const int true = !false;
    #endif
    static mnyfmt_long TENPOW[6+1] = { 1,10,100,1000,10000,100000,1000000 };
    if ( CE>(-1+sizeof(TENPOW)/sizeof(*TENPOW)) ) { return 0; }

    char *pDec, *pSign, *p;
    unsigned nDigits, nFrac;
    unsigned i, nNines;
    int  isPositive; // (0<=moneyval)
    char digit[ 8 * (  sizeof(mnyfmt_long) > CE
                       ? sizeof(mnyfmt_long)
                       : CE
                    ) / 3 ];
    // ensure that digit[] can hold more than 2 digits
    typedef int check_digit_size[ ( (2<=sizeof(CE)) ? 1 : -1 ) ];

    if (!( fmtstr) ) { return 0; } // null pointer mistake
//  if (!(*fmtstr) ) { return 0; } // null string mistake

    // determine dec position and store it in 'pDec'
    pDec = fmtstr; // points to the decimal separator (or eos)
    nNines = 0; // # of 'mnyfmt_format_chars' in fmtstr before decimal separator
    while ( *pDec!=0 ) {
        if ( *pDec==mnyfmt_format_char ) { ++nNines; } // count
        if ( *pDec==dec ) { break; }    // mark
        ++pDec;
    }
    if ( pDec==fmtstr || nNines == 0 ) { return 0; } // NULL error

    // separate the integer and the fractional parts
    if ( 0 <= moneyval ) { isPositive = true;}
    else {
        isPositive = false;
        moneyval = -moneyval;
        if ( moneyval<0 ) { return 0; } // -LONG_LONG_MAX-1 case
    }
    nFrac    = (unsigned int)( moneyval % TENPOW[CE] ); // fractional part
    moneyval = ( moneyval / TENPOW[CE] ); // integer part

    // store moneyval's digits in array digit[]
    if ( moneyval==0 ) {
        nDigits = 1; // number of digits in integer part
        digit[0] = 0;
    }
    else {
        nDigits = 0;
        do { // get all integer part digits
            digit[nDigits] = moneyval % 10;
            moneyval /= 10;
            ++nDigits;
        } while ( moneyval!=0 );
    }

    // check that fmtstr has enough mnyfmt_format_chars
    if ( isPositive ) {
        if ( nNines < nDigits ) { return 0; }
    }
    else { // use one mnyfmt_format_char for '-' sign
        if ( nNines < nDigits+1 ) { return 0; }
    }

    // store digit[] into fmtstr[]
    p = pDec;
    pSign = 0; // pSign && pSignificative
    for ( i=0; i<nNines; ++i ) {
        while ( *p!=mnyfmt_format_char ) { --p; } // backtrack to previous one
        if ( i<nDigits ) {
            *p = digit[i]+'0';
            pSign = p; // points to the most significative digit
        }
        else if ( ! isPositive ) { // isNegative ==> store '-'
            *p = '-';
            pSign = p;
            isPositive = true;
        }
        else {
            *p = '0'; // replace leading mnyfmt_format_chars with '0'
        }
        --p;
    }

    // deal with the fractional part
    if ( *pDec==0 ) {
        // eos ==> ignore nFrac
    }
    else {
        p = pDec+1;
        for ( i=0; i<CE; ++i ) {
            digit[i] = nFrac % 10;
            nFrac /= 10;
        }

        for ( i=CE; (*p)==mnyfmt_format_char ; ++p ) {
            if ( i>0 ) {
                --i;
                *p = digit[i]+'0';
            }
            else {
                *p = '0';
            }
        }
    }


    // deal with the fractional part
    if (true) { }
    else if ( *pDec==0 ) {
        // eos ==> ignore nFrac
    }
    else {
        p = pDec+1;
        if ( (*p) != mnyfmt_format_char ) {
            // no format for fraction ==> ignore nFrac
        }

        // find last  in 'fmtstr'
        // store all digits from nFrac into digit[]
        for ( i=0; i<CE; ++i ) {
            digit[i] = nFrac % 10;
            nFrac    = nFrac / 10;
        }
        // store digit[] into fmtstr[]
        i = CE;
        for(;;) {
            --i; ++p;
            if ( *p!=mnyfmt_format_char ) { break; }
            else {
                *p = digit[i] +'0';
            }
            if ( i==0 ) { break; }
        }
        while ( *p==mnyfmt_format_char ) { *p='0'; ++p; }
    }

    if (false) {
        // make sure that nFrac fits within the format string
        p = ( (*pDec==0) ? pDec : pDec+1 );
        i = nFrac;
        while (*p==mnyfmt_format_char) {
            i /= 10;
            ++p;
        }
        if ( i!=0 ) { return 0; }; // not enough digits in format string
    }

    return pSign;
}

// EOF: mnyfmt.c

