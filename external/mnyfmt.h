#pragma once
// mnyfmt.h (C) 2013 adolfo@di-mare.com
// mnyfmt.c: A simple function to format money or currency amounts

/** \file   mnyfmt.h
    \brief  Header file for \c mnyfmt().
    This C function formats money amounts using a picture clause.
    \see  http://google.com/search?as_qdr=all&num=100&as_q=picture+clause

    \author Adolfo Di Mare <adolfo@di-mare.com>
    \date   2013
*/

#ifndef MNYFMT_ADOLFO
#define MNYFMT_ADOLFO

typedef long long mnyfmt_long;

/// (2^128 < 10^40) && (2*40 < 96) ==> char[96] is big enough for 128 bits
#define mnyfmt_size 96

/// Formatting character for \c mnyfmt().
#define mnyfmt_format_char (char)('9')

#ifdef __cplusplus
extern "C" {
#endif

    // CE->Currency Exponent ==> MCE->Minor Currency Unit

    // The complete Doxygen documentation is in file 'mnyfmt.c'
    // Examples and unit test cases are in in file 'mnyfmtts.c'

    /*  Formats 'moneyval' leaving the result in picture clause 'fmtstr'.
        The money amount stored in 'moneyval' is an integer scaled to 'CE' digits
        and the decimal separator is 'dec'.

        For example, when using CE==2 digits, the monetary value $-455.87 should
        be stored in variable 'moneyval' as the integer '-45587', and if CE==4
        digits are used, 'moneyval' should be '-4558700'.

        The format string has many '9's that get replaced by the corresponding
        digit. For example, when "999,999,999.99" is stored in 'fmtstr', the
        computed result for '-455.87' would be "0,00-,455.87" and the pointer
        value returned would point to '-' within 'fmtstr' (for this example,
        CE==2 and dec=='.').
    */
    char* mnyfmt(char* fmtstr, char dec, mnyfmt_long moneyval/*, unsigned CE*/);

    /*  // test.neg.comma
        // (2^128 < 10^40) && (2*40<96) ==> char[96] holds a 128
        char *sgn, fmtstr[96];          //  bit integer

        strcpy(             fmtstr ,    "9,999,999.99" );
        if (( sgn = mnyfmt( fmtstr , '.'     ,-45587,2 ) )) {
            assertTrue( eqstr( sgn,         "-,455.87" ) );
            assertTrue( eqstr( fmtstr,  "0,00-,455.87" ) );
            // handle the "-," problem
            if ( (*sgn=='-') && (','==*(sgn+1)) ) {
                ++sgn; *sgn = '-'; // put '-' sign in a nice place
            }
            assertTrue( eqstr( sgn,         "-455.87" ) );
            assertTrue( eqstr( fmtstr, "0,00--455.87" ) );
        }
    */

#ifdef __cplusplus
}
#endif

#endif

