#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#include "roa_utils.h"
#include "rpki-object/cms/cms.h"
#include "util/cryptlib_compat.h"

// Warning - MAX_LINE hardcoded as a constant in confInterpret;
// if this changes, that must as well
#define MAX_LINE 512

enum configKeys {
    KEYVERSION = 0,
    SID,
    SIGNATURE,
    AS_ID,
    IPFAM,
    IPADDR,
    IPADDRMIN,
    IPADDRMAX,
    CERTNAME,
    KEYFILE,
    CONFIG_KEY_MAX
};

const char *configKeyStrings[] = {
    "keyversion",
    "SID",
    "signature",
    "as_id",
    "ipfam",
    "ipaddr",
    "ipaddrmin",
    "ipaddrmax",
    "certname",
    "keyfile"
};

enum forcingInstruction {
    NONE = 0,
    IPV4FAM,
    IPV6FAM,
    IPV4MIN,
    IPV6MIN,
    IPV4CONT,
    IPV6CONT
} g_lastInstruction;

static int g_iIPv4Flag;
static int g_iIPv6Flag;

/*
 ** Translation Tables as described in RFC1113 (Translation table method
 **  courtesy b64.c from Bob Trower @ base64.sourceforge.net)
 */
// Encode table
static const char cb64[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
// Decode table
static const char cd64[] =
    "|$$$}rstuvwxyz{$$$$$$$>?@ABCDEFGHIJKLMNOPQRSTUVW$$$$$$XYZ[\\]^_`abcdefghijklmnopq";


// START/END ROA armor
static const char roaStart[] = "-----BEGIN ROA-----\r\n";
static const char roaEnd[] = "-----END ROA-----\r\n";

static const char *ianaAfiStrings[] = {
    "none",
    "IPv4",
    "IPv6"
};

// ///////////////////////////////////////////////////////////
//
// Testing of locally defined enumerations
//
// ///////////////////////////////////////////////////////////

static inline int isInstructionForcing(
    enum forcingInstruction fi)
{
    if ((NONE == fi) || (IPV4CONT == fi) || (IPV6CONT == fi))
        return cFALSE;
    else
        return cTRUE;
}

// ///////////////////////////////////////////////////////////
//
// Encode/decode to Base64 functions
//
// ///////////////////////////////////////////////////////////

/*
 ** encodeblock
 **
 ** encode 3 8-bit binary bytes as 4 '6-bit' characters
 */
static void encodeblock(
    unsigned char in[3],
    unsigned char out[4],
    int len)
{
    out[0] = cb64[in[0] >> 2];
    out[1] = cb64[((in[0] & 0x03) << 4) | ((in[1] & 0xf0) >> 4)];
    out[2] =
        (unsigned char)(len >
                        1 ? cb64[((in[1] & 0x0f) << 2) | ((in[2] & 0xc0) >> 6)]
                        : '=');
    out[3] = (unsigned char)(len > 2 ? cb64[in[2] & 0x3f] : '=');
}

/*
 ** encode
 **
 ** base64 encode a stream adding padding and line breaks as per spec.
 ** ALLOCATES MEMORY that must be freed elsewhere
 */
static err_code
encode_b64(
    unsigned char *bufIn,
    int inSize,
    unsigned char **bufOut,
    int *outSize,
    int lineSize)
{
    unsigned char inTemp[3],
        outTemp[4];
    int i = 0;
    int len = 0;
    int blocksout = 0;
    int inIndex = 0;
    int outIndex = 0;
    int iTempSize = 0;
    int iArmor = 0;
    unsigned char *bufTemp = NULL;

    // Parameter sanity check
    if ((NULL == bufIn) ||
        (0 >= inSize) || (NULL == outSize) || (0 >= lineSize))
        return ERR_SCM_INVALARG;

    iTempSize = 1024;
    bufTemp = (unsigned char *)calloc(1, iTempSize);
    if (NULL == bufTemp)
        return ERR_SCM_NOMEM;

    // Push armoring onto top of file
    iArmor = strlen(roaStart);
    memcpy(bufTemp, roaStart, iArmor);
    outIndex += iArmor;

    // Encode file
    while (inIndex < inSize)
    {
        len = 0;
        for (i = 0; i < 3; i++)
        {
            if (inIndex < inSize)
            {
                inTemp[i] = (unsigned char)bufIn[inIndex];
                inIndex++;
                len++;
            }
            else
            {
                inTemp[i] = 0;
            }
        }
        if (len)
        {
            encodeblock(inTemp, outTemp, len);
            for (i = 0; i < 4; i++)
            {
                if (outIndex >= iTempSize)
                {
                    bufTemp =
                        (unsigned char *)realloc(bufTemp, iTempSize + 1024);
                    if (bufTemp == NULL)
                        return ERR_SCM_NOMEM;
                    iTempSize += 1024;
                }
                bufTemp[outIndex] = outTemp[i];
                outIndex++;
            }
            blocksout++;
        }
        if (blocksout >= (lineSize / 4) || inIndex >= inSize)
        {
            if (blocksout)
            {
                // We have to add 2 chars, so check one below our limit
                if (outIndex - 1 >= iTempSize)
                {
                    bufTemp =
                        (unsigned char *)realloc(bufTemp, iTempSize + 1024);
                    if (bufTemp == NULL)
                        return ERR_SCM_NOMEM;
                    iTempSize += 1024;
                }
                bufTemp[outIndex] = '\r';
                outIndex++;
                bufTemp[outIndex] = '\n';
                outIndex++;
            }
            blocksout = 0;
        }
    }

    // Push armoring onto bottom of file
    iArmor = strlen(roaEnd);
    if (outIndex + iArmor + 1 >= iTempSize)
    {
        bufTemp = (unsigned char *)realloc(bufTemp, iTempSize + 1024);
        if (bufTemp == NULL)
            return ERR_SCM_NOMEM;
        iTempSize += 1024;
    }
    memcpy(&bufTemp[outIndex], roaEnd, iArmor);
    outIndex += iArmor;
    bufTemp[outIndex] = 0x00;

    // Set return values
    *bufOut = bufTemp;
    *outSize = outIndex;
    return 0;
}

/*
 * A generalized pattern matching routine for finding armor. It will find any
 * string of the form X i1 i2 Y Z, where X is one or more dashes, i1 and i2
 * and the input tokens, Y is one or more dashes, and Z is any combination of
 * CR and LF.
 *
 * Returns an offset to indicate the location of the first character after the
 * armor on success, and a negative error code on failure.
 */

static int findarmor(
    char *buf,
    int buflen,
    char *i1,
    char *i2)
{
    char *endd;
    char *run;
    char *run2;

    if (buf == NULL || buflen <= 0)
        return (-1);
    endd = buf + buflen;
    run = buf;
    // one or more - characters
    while (run < endd && *run == '-')
        run++;
    if (run == buf || run >= endd)
        return (-1);
    // zero or more space characters
    while (run < endd && isspace((int)(unsigned char)(*run)))
        run++;
    if (run >= endd)
        return (-1);
    // the token i1
    if (i1 != NULL && i1[0] != 0)
    {
        if (strncmp(run, i1, strlen(i1)) != 0)
            return (-1);
    }
    run += strlen(i1);
    // zero or more whitespace characters
    while (run < endd && isspace((int)(unsigned char)(*run)))
        run++;
    if (run >= endd)
        return (-1);
    // the token i2
    if (i2 != NULL && i2[0] != 0)
    {
        if (strncmp(run, i2, strlen(i2)) != 0)
            return (-1);
    }
    run += strlen(i2);
    // zero or more whitespace characters
    while (run < endd && isspace((int)(unsigned char)(*run)))
        run++;
    if (run >= endd)
        return (-1);
    // one or more dashes
    run2 = run;
    while (run < endd && *run == '-')
        run++;
    if (run == run2 || run >= endd)
        return (-1);
    // one or more occurences of either CR or LF
    run2 = run;
    while (run < endd && (*run == '\r' || *run == '\n'))
        run++;
    if (run == run2 || run > endd)
        return (-1);
    return ((int)(run - buf));
}

/*
 ** decodeblock
 **
 ** decode 4 '6-bit' characters into 3 8-bit binary bytes
 */
static void decodeblock(
    unsigned char in[4],
    unsigned char out[3])
{
    out[0] = (unsigned char)(in[0] << 2 | in[1] >> 4);
    out[1] = (unsigned char)(in[1] << 4 | in[2] >> 2);
    out[2] = (unsigned char)(((in[2] << 6) & 0xc0) | in[3]);
}

/*
 ** decode
 **
 ** decode a base64 encoded stream discarding padding, line breaks and noise
 ** ALLOCATES MEMORY that must be freed elsewhere
 */
err_code
decode_b64(
    unsigned char *bufIn,
    int inSize,
    unsigned char **bufOut,
    int *outSize,
    char *armor)
{
    unsigned char inTemp[4],
        outTemp[3],
        v;
    int i = 0;
    int len = 0;
    int inIndex = 0;
    int outIndex = 0;
    int iTempSize = 0;
    int iArmor = 0;
    unsigned char *bufTemp = NULL;

    // Parameter sanity check
    if ((NULL == bufIn) || (0 >= inSize) || (NULL == outSize))
        return ERR_SCM_INVALARG;

    iTempSize = 1024;
    bufTemp = (unsigned char *)calloc(1, iTempSize);
    if (NULL == bufTemp)
        return ERR_SCM_NOMEM;

    // First, search for armoring at front of file
    while (inIndex < inSize)
    {
        if ('-' == bufIn[inIndex])
        {
            iArmor =
                findarmor((char *)&bufIn[inIndex], inSize - inIndex, "BEGIN",
                          armor);
            if (iArmor >= 0)
            {
                inIndex += iArmor;
                break;
            }
        }
        inIndex++;
    }

    // Decode the translation
    while (inIndex < inSize)
    {
        for (len = 0, i = 0; (i < 4) && (inIndex < inSize); i++)
        {
            v = 0;
            while ((inIndex < inSize) && (v == 0))
            {
                v = (unsigned char)bufIn[inIndex];
                // Skip the end armoring (don't translate its chars)
                if ('-' == v)
                {
                    iArmor =
                        findarmor((char *)&bufIn[inIndex], inSize - inIndex,
                                  "END", armor);
                    if (iArmor >= 0)
                        inIndex += iArmor;
                }
                v = (unsigned char)((v < 43 || v > 122) ? 0 : cd64[v - 43]);
                if (v)
                {
                    v = (unsigned char)((v == '$') ? 0 : v - 61);
                }
                inIndex++;
            }
            if (inIndex < inSize)
            {
                len++;
                if (v)
                {
                    inTemp[i] = (unsigned char)(v - 1);
                }
            }
            else
            {
                inTemp[i] = 0;
            }
        }
        if (len)
        {
            decodeblock(inTemp, outTemp);
            for (i = 0; i < len - 1; i++)
            {
                if (outIndex >= iTempSize)
                {
                    bufTemp =
                        (unsigned char *)realloc(bufTemp, iTempSize + 1024);
                    if (bufTemp == NULL)
                        return ERR_SCM_NOMEM;
                    iTempSize += 1024;
                }
                bufTemp[outIndex] = outTemp[i];
                outIndex++;
            }
        }
    }

    *bufOut = bufTemp;
    *outSize = outIndex;
    return 0;
}

// ///////////////////////////////////////////////////////////
//
// ASCII character string to character value translation
//
// ///////////////////////////////////////////////////////////

static err_code
ctocval(
    unsigned char cIn,
    unsigned char *val,
    int radix)
{
    char c;

    if (NULL == val)
        return ERR_SCM_INVALARG;

    c = toupper(cIn);
    if (('0' <= c) && ('9' >= c))
        *val = (c - '0');
    else if (('A' <= c) && ('F' >= c) && (16 == radix))
        *val = (c - 'A') + 10;
    else
        return ERR_SCM_INVALARG;

    return 0;
}

// Crappy substitute function for pleasant function that returned a short
// (now string to 2 char array) BUT it assures no endianness crap
static err_code
ip_strto2c(
    unsigned char *strToTranslate,
    unsigned char *c2Returned,
    int radix)
{
    int i = 0;
    int iLen = 0;
    err_code sta;
    unsigned short int sAns = 0;
    unsigned char cTemp = 0;
    unsigned char c2Ans[2];

    // Check for null pointers
    if ((NULL == strToTranslate) || (NULL == c2Returned))
        return ERR_SCM_INVALARG;

    // Right now, I'm only interested in supporting decimal and hex
    if ((16 != radix) && (10 != radix))
        return ERR_SCM_INVALARG;

    iLen = strlen((char *)strToTranslate);
    if ((0 >= iLen) || (iLen > 5))
        return ERR_SCM_INVALARG;

    for (i = 0; i < iLen; i++)
    {
        sAns = sAns * radix;
        sta = ctocval(strToTranslate[i], &cTemp, radix);
        if (sta < 0)
            return sta;
        sAns += cTemp;
    }

    memset(c2Ans, 0, 2);
    c2Ans[1] = (unsigned char)(sAns % 0x100);
    sAns = sAns / 0x100;
    c2Ans[0] = (unsigned char)sAns;

    memcpy(c2Returned, c2Ans, 2);
    return 0;
}

static err_code
ip_strtoc(
    unsigned char *strToTranslate,
    unsigned char *cReturned,
    int radix)
{
    int i = 0;
    int iLen = 0;
    err_code sta;
    unsigned char cAns = 0;
    unsigned char cTemp = 0;

    // Check for null pointers
    if ((NULL == strToTranslate) || (NULL == cReturned))
        return ERR_SCM_INVALARG;

    // Right now, I'm only interested in supporting decimal and hex
    if ((16 != radix) && (10 != radix))
        return ERR_SCM_INVALARG;

    iLen = strlen((char *)strToTranslate);
    if ((0 >= iLen) || (iLen > 3))
        return ERR_SCM_INVALARG;

    for (i = 0; i < iLen; i++)
    {
        cAns = cAns * radix;
        sta = ctocval(strToTranslate[i], &cTemp, radix);
        if (sta < 0)
            return sta;
        cAns += cTemp;
    }

    *cReturned = cAns;
    return 0;
}

// ///////////////////////////////////////////////////////////
//
// Functions to calculate trailing bits in octet strings that
// represent bit strings
//
// ///////////////////////////////////////////////////////////

static int calculatePrefixVals(
    int iPrefix,
    unsigned char *cBadTrailingBits,
    int *iGoodLeadingBytes)
{
    int iFullBytes = 0;
    unsigned char cGoodTrailingBits = 0;

    // Check for null pointers
    if ((NULL == cBadTrailingBits) || (NULL == iGoodLeadingBytes))
        return ERR_SCM_INVALARG;

    iFullBytes = iPrefix / 8;
    cGoodTrailingBits = (char)iPrefix % 8;
    if (0 != cGoodTrailingBits)
    {
        *cBadTrailingBits = 8 - cGoodTrailingBits;
        iFullBytes++;
    }
    else
        *cBadTrailingBits = 0;

    *iGoodLeadingBytes = iFullBytes;
    return 0;
}

static int calculateAndClearPrefix(
    int iPrefix,
    int iSize,
    unsigned char *iparray,
    unsigned char *cBadBits,
    int *iGoodBytes)
{
    int i = 0;
    int iIndex = 0;
    int iRemainder = 0;
    int iShift = 0;

    // Check for null pointers
    if ((NULL == cBadBits) || (NULL == iGoodBytes) || (NULL == iparray))
        return ERR_SCM_INVALARG;

    for (i = iSize; iPrefix < i; i--)
    {
        iIndex = (i - 1) / 8;
        iRemainder = i % 8;
        if (0 == iRemainder)
            iShift = iRemainder;
        else
            iShift = 8 - iRemainder;

        // DER encoding demands that we zero all unused trailing bits
        iparray[iIndex] &= ~(0x01 << iShift);
    }
    return calculatePrefixVals(iPrefix, cBadBits, iGoodBytes);
}

#ifdef IP_RANGES_ALLOWED

static int calculateAndClearMM(
    int iIsMin,
    int iSize,
    unsigned char *iparray,
    unsigned char *cBadTrailingBits,
    int *iGoodLeadingBytes)
{
    int i = 0;
    int iIndex = 0;
    int iRemainder = 0;
    int iShift = 0;
    char cBitwiseCheck = 0;

    // Check for null pointers
    if ((NULL == cBadTrailingBits) ||
        (NULL == iGoodLeadingBytes) || (NULL == iparray))
        return ERR_SCM_INVALARG;

    for (i = iSize; 0 < i; i--)
    {
        iIndex = (i - 1) / 8;
        iRemainder = i % 8;
        if (0 == iRemainder)
            iShift = iRemainder;
        else
            iShift = 8 - iRemainder;
        cBitwiseCheck = iparray[iIndex] & (0x01 << iShift);
        if (cTRUE == iIsMin)
        {
            if (0 != cBitwiseCheck)
                break;
        }
        else if (cFALSE == iIsMin)
        {
            if (0 == cBitwiseCheck)
                break;
            else
                // DER encoding demands that we zero all unused trailing bits
                iparray[iIndex] &= ~(0x01 << iShift);
        }
    }
    return calculatePrefixVals(i, cBadTrailingBits, iGoodLeadingBytes);
}

#endif

// ///////////////////////////////////////////////////////////
//
// IP Address octet strings from ASCII strings
//
// ///////////////////////////////////////////////////////////

// Translation of dotted decimal IPv4 addresses
//
// JFG - Note: Currently handled by this function are addresses of the format
// 168.156/24, which may not be canonical, but which interprets out to
// 168.156.0.0/24 for now.
static err_code
translateIPv4Prefix(
    unsigned char *ipstring,
    unsigned char **ipbytearray,
    int *iprefixlen)
{
    int i = 0;
    int iStringLen = 0;
    int iByteIndex = 0;
    int iTempIndex = 0;
    err_code sta;
    unsigned char cAddrPart = 0;
    unsigned char cPrefix = 0;
    unsigned char cLastChar = 0;
    unsigned char cEmpty = 0;
    unsigned char stringtemp[4];
    unsigned char arraytemp[5];

    // Check for null pointers
    if ((NULL == ipstring) || (NULL == *ipbytearray) || (NULL == iprefixlen))
        return ERR_SCM_INVALARG;

    // Max IPv4 Address Prefix
    iStringLen = strlen((char *)ipstring);
    if (19 < iStringLen)
        return ERR_SCM_INVALIPL;

    // Translate
    memset(stringtemp, 0, 4);
    memset(arraytemp, 0, 5);
    for (i = 0; (i < iStringLen) && (4 > iTempIndex); i++)
    {
        // The last couple of loops (after the prefix)
        if ('/' == cLastChar)
        {
            stringtemp[iTempIndex] = ipstring[i];
            iTempIndex++;
            continue;
        }

        // Every loop before hitting the prefix (if it comes)
        if (('.' == ipstring[i]) || ('/' == ipstring[i]))
        {
            // Validate the previous char (must have been numeric)
            sta = ctocval(cLastChar, &cEmpty, 10);
            if (sta < 0)
                return sta;

            // Currently relying on strtoc to tell us if any
            // of the characters in the string is untranslatable
            sta = ip_strtoc(stringtemp, &cAddrPart, 10);
            if (sta < 0)
                return sta;
            else
            {
                arraytemp[iByteIndex] = cAddrPart;
                iByteIndex++;
                iTempIndex = 0;
                memset(stringtemp, 0, 4);
            }
        }
        else
        {
            stringtemp[iTempIndex] = ipstring[i];
            iTempIndex++;
        }
        cLastChar = ipstring[i];
    }
    // If we got a badly formatted string at any point
    if (iTempIndex >= 4)
        return ERR_SCM_INVALIPL;

    // Translate the prefix
    // Currently relying on strtoc to tell us if any
    // of the characters in the string is untranslatable
    if ('/' == cLastChar)
    {
        sta = ip_strtoc(stringtemp, &cPrefix, 10);
        if ((0 >= cPrefix) || (32 < cPrefix) || (sta < 0))
            return ERR_SCM_INVALIPB;
    }
    // Or, if there was no prefix, finish the last
    // chunk tranlsation
    else
    {
        // Validate the previous char (must have been numeric)
        sta = ctocval(cLastChar, &cEmpty, 10);
        if (sta < 0)
            return sta;

        // Currently relying on strtoc to tell us if any
        // of the characters in the string is untranslatable
        sta = ip_strtoc(stringtemp, &cAddrPart, 10);
        if (sta < 0)
            return sta;
        else
        {
            arraytemp[iByteIndex] = cAddrPart;
            iByteIndex++;
            iTempIndex = 0;
            memset(stringtemp, 0, 4);
        }
        cPrefix = 32;
    }

    // Sanity check on number of copied bytes
    if (iByteIndex > 4)
        return ERR_SCM_INVALIPB;

    // Copy return values
    memcpy(*ipbytearray, arraytemp, 4);
    *iprefixlen = (int)cPrefix;

    return 0;
}

// Translation of hexadecimal IPv6 addresses
//
static err_code
translateIPv6Prefix(
    unsigned char *ipstring,
    unsigned char **ipbytearray,
    int *iprefixlen)
{
    int i = 0;
    int iStringLen = 0;
    int iByteIndex = 0;
    int iTempIndex = 0;
    int iColonCount = 0;
    int iSkippedBytes = 0;
    err_code sta;
    int iLoopStart = 0;
    int iPrefixMark = 0;

    unsigned char cPrefix = 0;
    unsigned char cLastChar = 0;
    unsigned char cEmpty = 0;
    unsigned char stringtemp[5];
    unsigned char arraytemp[17];
    unsigned char cAddrPartArray[2];

    // Check for null pointers
    if ((NULL == ipstring) || (NULL == *ipbytearray) || (NULL == iprefixlen))
        return ERR_SCM_INVALARG;

    // Max IPv6 Address Prefix
    iStringLen = strlen((char *)ipstring);
    if (43 < iStringLen)
        return ERR_SCM_INVALIPL;

    // First, find out how much skipping was done in the address
    // and find the location of the prefix;
    iPrefixMark = 0;
    for (i = 0; i < iStringLen; i++)
    {
        if ('/' == ipstring[i])
            iPrefixMark = i;
        if (':' == ipstring[i])
            iColonCount++;
        cLastChar = ipstring[i];
    }
    if (0 == iPrefixMark)
        iPrefixMark = iStringLen;

    // If we start or finish with a colon (presumably paired)
    // we skipped an extra pair of bytes
    if ((':' == ipstring[0]) && (':' == ipstring[1]))
    {
        iColonCount--;
        iLoopStart = 1;
        cLastChar = ':';
    }
    if ((':' == ipstring[iPrefixMark - 1])
        && (':' == ipstring[iPrefixMark - 2]))
        iColonCount--;

    // There can only be a maximum of 7 colons in any IPv6 address
    if (7 < iColonCount)
        return ERR_SCM_INVALIPB;

    // Then, translate
    memset(stringtemp, 0, 5);
    memset(arraytemp, 0, 17);
    for (i = iLoopStart; (i < iStringLen) && (5 > iTempIndex); i++)
    {
        // Tight prefix loop:
        // The last couple of loops (after the prefix)
        if ('/' == cLastChar)
        {
            stringtemp[iTempIndex] = ipstring[i];
            iTempIndex++;
            continue;
        }

        // Every loop before hitting the prefix (if it comes)
        if ((':' == ipstring[i]) || ('/' == ipstring[i]))
        {
            if (':' != cLastChar)
            {
                // Validate the previous char (must have been hex)
                sta = ctocval(cLastChar, &cEmpty, 16);
                if (sta < 0)
                    return sta;

                // Currently relying on strto2c to tell us if any
                // of the characters in the string is untranslatable
                memset(cAddrPartArray, 0, 2);
                sta = ip_strto2c(stringtemp, cAddrPartArray, 16);
                if (sta < 0)
                    return sta;
                else
                {
                    arraytemp[iByteIndex] = cAddrPartArray[0];
                    arraytemp[iByteIndex + 1] = cAddrPartArray[1];
                    iByteIndex += sizeof(short int);
                    iTempIndex = 0;
                    memset(stringtemp, 0, 5);
                }
            }
            else if (':' == ipstring[i])
            {
                iSkippedBytes = (8 - iColonCount) * 2;
                memset(&(arraytemp[iByteIndex]), 0, iSkippedBytes);
                iByteIndex += iSkippedBytes;
            }
            else
            {
                // We've hit the '/' with a preceding ':'
                // Time to exit into the tight prefix loop
                cLastChar = ipstring[i];
                continue;
            }
        }
        else
        {
            stringtemp[iTempIndex] = ipstring[i];
            iTempIndex++;
        }
        cLastChar = ipstring[i];
    }

    // If we got a badly formatted string at any point
    if (iTempIndex >= 5)
        return ERR_SCM_INVALIPL;

    // Translate the prefix
    // Currently relying on strtos to tell us if any
    // of the characters in the string is untranslatable
    if ('/' == cLastChar)
    {
        sta = ip_strtoc(stringtemp, &cPrefix, 10);
        if ((0 >= cPrefix) || (128 < cPrefix) || (sta < 0))
            return ERR_SCM_INVALIPB;
    }
    // Or, if there was no prefix, finish the last
    // chunk tranlsation
    else if (':' != cLastChar)
    {
        // Validate the previous char (must have been hex)
        sta = ctocval(cLastChar, &cEmpty, 16);
        if (sta < 0)
            return sta;

        // Currently relying on strtos to tell us if any
        // of the characters in the string is untranslatable
        sta = ip_strto2c(stringtemp, cAddrPartArray, 16);
        if (sta < 0)
            return sta;
        else
        {
            arraytemp[iByteIndex] = cAddrPartArray[0];
            arraytemp[iByteIndex + 1] = cAddrPartArray[1];
            iByteIndex += sizeof(short int);
            iTempIndex = 0;
            memset(stringtemp, 0, 5);
        }
        cPrefix = 128;
    }
    else
        cPrefix = 128;

    // Sanity check on number of copied bytes
    if (iByteIndex != 16)
        return ERR_SCM_INVALIPL;

    // Copy return values
    memcpy(*ipbytearray, arraytemp, 16);
    *iprefixlen = (int)cPrefix;

    return 0;
}

// ///////////////////////////////////////////////////////////
//
// Per-configuration item translation functions
//
// ///////////////////////////////////////////////////////////

static err_code
setVersion(
    struct CMS *roa,
    unsigned char *versionstring)
{
    int iRes = 0;
    int iLen = 0;
    int iVersion = 0;

    // Because we only accept single-digit version numbers
    iLen = strlen((char *)versionstring);
    if (1 != iLen)
        return ERR_SCM_INVALARG;

    // And all roas (for now) must be version 3
    iVersion = atoi((char *)versionstring);
    if (3 != iVersion)
        return ERR_SCM_INVALVER;

    iRes = write_casn_num(&(roa->content.signedData.version.v3), iVersion);
    if (0 > iRes)
        return ERR_SCM_INVALASN;

    iRes =
        write_casn_num(&
                       (roa->content.signedData.signerInfos.signerInfo.version.
                        v3), iVersion);
    if (0 > iRes)
        return ERR_SCM_INVALASN;

    g_lastInstruction = NONE;
    return 0;
}

/*
 FIXME: This does not handle the full range of 32-bit AS numbers.
 Fortunately, it is called only from a single unit test and is never
 used in production.  It may be better to just remove the entire
 calling code path (starts at the top from roaFromConfig).  We now
 have other ways of generating ROAs from text files other than
 roaFromConfig().  And/or we need to fix write_casn_num() as well as
 its internal limit-checking code.
*/
static err_code
setAS_ID(
    struct CMS *roa,
    unsigned char *asidstring)
{
    int iLen = 0;
    int iAS_ID = 0;

    // FIXME: We deny AS 0 and AS numbers > 999999999, but allow negatives!
    iLen = strlen((char *)asidstring);
    if (9 < iLen)
        return ERR_SCM_INVALASID;
    iAS_ID = atoi((char *)asidstring);
    if (iAS_ID == 0)
        return ERR_SCM_INVALASID;
    write_casn_num(&
                   (roa->content.signedData.encapContentInfo.eContent.roa.
                    asID), iAS_ID);
    g_lastInstruction = NONE;
    return 0;
}

static err_code
setIPFamily(
    struct CMS *roa,
    unsigned char *ipfamstring)
{
    int iLen = 0;
    int iBlocks = 0;
    unsigned char familytemp[3];
    struct ROAIPAddressFamily *roaFamily = NULL;

    // Length must be 4 (i.e. IPvX)
    iLen = strlen((char *)ipfamstring);
    if (4 != iLen)
        return ERR_SCM_INVALIPL;

    memset(familytemp, 0, 3);

    // Set AFI string (first two bytes of familytemp should be
    // 0x00 0x0Z, where Z is determined by the family)
    // after checking to make sure that each block occurs only
    // once and in the proper order
    if (0 == strcmp(ianaAfiStrings[IPV4], (char *)ipfamstring))
    {
        if ((cTRUE == g_iIPv4Flag) || (cTRUE == g_iIPv6Flag))
            return ERR_SCM_INVALIPB;
        familytemp[1] = 0x01;
        g_lastInstruction = IPV4FAM;
        g_iIPv4Flag = cTRUE;
    }
    else if (0 == strcmp(ianaAfiStrings[IPV6], (char *)ipfamstring))
    {
        if (cTRUE == g_iIPv6Flag)
            return ERR_SCM_INVALIPB;
        familytemp[1] = 0x02;
        g_lastInstruction = IPV6FAM;
        g_iIPv6Flag = cTRUE;
    }
    else
        return ERR_SCM_INVALIPB;

    // Write a new IP block directly to the casn
    iBlocks =
        num_items(&
                  (roa->content.signedData.encapContentInfo.eContent.roa.
                   ipAddrBlocks.self));
    if (0 <= iBlocks)
    {
        // If that worked, fill it with the family info
        roaFamily =
            (struct ROAIPAddressFamily *)
            inject_casn(&
                        (roa->content.signedData.encapContentInfo.eContent.roa.
                         ipAddrBlocks.self), iBlocks);
        // roaFamily = (struct ROAIPAddressFamily*)
        // member_casn(&(roa->content.signedData.encapContentInfo.eContent.roa.ipAddrBlocks.self),
        // iBlocks);
        write_casn(&(roaFamily->addressFamily), familytemp, 2);
        return 0;
    }
    return ERR_SCM_INVALIPB;
}

static err_code
setIPAddr(
    struct CMS *roa,
    unsigned char *ipaddrstring)
{
    unsigned char ipv4array[5];
    unsigned char ipv6array[17];
    unsigned char *arrayptr = NULL;
    int iPrefixSize = 0;
    int iGoodBytes = 0;
    unsigned char cBadBits = 0;
    int iBlocks = 0;
    int iAddrs = 0;
    err_code sta = 0;
    int maxLen = 0;
    uchar *addrString;

    struct ROAIPAddressFamily *roaFamily = NULL;

    struct ROAIPAddress *roaAddr = NULL;

    memset(ipv4array, 0, 5);
    memset(ipv6array, 0, 17);
    // Before anything, deal with the maxLen, if present
    addrString = (uchar *) calloc(1, strlen((char *)ipaddrstring) + 1);
    strcpy((char *)addrString, (char *)ipaddrstring);
    char *mxp = strchr((char *)addrString, '^');
    if (mxp)
    {
        sscanf(&mxp[1], "%d", &maxLen);
        *mxp = 0;               // chop it off
    }

    // First, translate the address into something meaningful
    if ((IPV4FAM == g_lastInstruction) || (IPV4CONT == g_lastInstruction))
    {
        arrayptr = &ipv4array[1];
        sta = translateIPv4Prefix(addrString, (unsigned char **)&arrayptr,
                                  &iPrefixSize);
    }
    else if ((IPV6FAM == g_lastInstruction) || (IPV6CONT == g_lastInstruction))
    {
        arrayptr = &ipv6array[1];
        sta = translateIPv6Prefix(addrString, (unsigned char **)&arrayptr,
                                  &iPrefixSize);
    }
    else
        sta = ERR_SCM_INVALIPB;
    free(addrString);
    // If translation failed, we're done
    if (sta < 0)
        return sta;

    // Otherwise, write the data to the roa
    if ((IPV4FAM == g_lastInstruction) || (IPV4CONT == g_lastInstruction))
    {
        // Pull valid leading bytes/invalid trailing bits out of the prefix
        // (necessary to constuct ASN BITSTRING), then populate the bit notice
        // and add one to the length of the array to be copied
        calculateAndClearPrefix(iPrefixSize, 32, arrayptr, &cBadBits,
                                &iGoodBytes);
        ipv4array[0] = cBadBits;
        iGoodBytes++;
        // Then, write the bitstring (as an octet string) to the latest
        // generated roaIPFam
        // after generating a new block for it
        iBlocks =
            num_items(&
                      (roa->content.signedData.encapContentInfo.eContent.roa.
                       ipAddrBlocks.self));
        if (0 < iBlocks)
        {
            roaFamily =
                (struct ROAIPAddressFamily *)
                member_casn(&
                            (roa->content.signedData.encapContentInfo.eContent.
                             roa.ipAddrBlocks.self), iBlocks - 1);
            iAddrs = num_items(&(roaFamily->addresses.self));
            if (0 <= iAddrs)
            {
                roaAddr =
                    (struct ROAIPAddress *)inject_casn(&roaFamily->addresses.
                                                       self, iAddrs);
                write_casn(&roaAddr->address, ipv4array, iGoodBytes);
                if (maxLen)
                    write_casn_num(&roaAddr->maxLength, maxLen);
            }
            else
                sta = ERR_SCM_INVALIPB;
        }
        else
            sta = ERR_SCM_INVALIPB;
        g_lastInstruction = IPV4CONT;
    }
    else if ((IPV6FAM == g_lastInstruction) || (IPV6CONT == g_lastInstruction))
    {
        // Pull valid leading bytes/invalid trailing bits out of the prefix
        // (necessary to constuct ASN BITSTRING), then populate the bit notice
        // and add one to the length of the array to be copied
        calculateAndClearPrefix(iPrefixSize, 128, arrayptr, &cBadBits,
                                &iGoodBytes);
        ipv6array[0] = cBadBits;
        iGoodBytes++;
        // Then, write the bitstring (as an octet string) to the casn
        iBlocks =
            num_items(&
                      (roa->content.signedData.encapContentInfo.eContent.roa.
                       ipAddrBlocks.self));
        if (0 < iBlocks)
        {
            roaFamily =
                (struct ROAIPAddressFamily *)
                member_casn(&
                            (roa->content.signedData.encapContentInfo.eContent.
                             roa.ipAddrBlocks.self), iBlocks - 1);
            iAddrs = num_items(&(roaFamily->addresses.self));
            if (0 <= iAddrs)
            {
                roaAddr =
                    (struct ROAIPAddress *)inject_casn(&roaFamily->addresses.
                                                       self, iAddrs);
                write_casn(&roaAddr->address, ipv6array, iGoodBytes);
                if (maxLen)
                    write_casn_num(&roaAddr->maxLength, maxLen);
            }
            else
                sta = ERR_SCM_INVALIPB;
        }
        else
            sta = ERR_SCM_INVALIPB;
        g_lastInstruction = IPV6CONT;
    }

    return sta;
}

// These two functions are only required if we allow ranges, with their
// min/max qualifiers
#ifdef IP_RANGES_ALLOWED

static err_code
setIPAddrMin(
    struct CMS *roa,
    unsigned char *ipaddrminstring)
{
    unsigned char ipv4array[5];
    unsigned char ipv6array[17];
    unsigned char *arrayptr = NULL;
    int iPrefixSize = 0;
    int iGoodBytes = 0;
    unsigned char cBadBits = 0;
    err_code sta = 0;
    int iBlocks = 0;
    int iAddrs = 0;
    struct IPAddressOrRangeA *roaAddr = NULL;
    struct ROAIPAddressFamily *roaFamily = NULL;

    memset(ipv4array, 0, 5);
    memset(ipv6array, 0, 17);

    // First, translate the address into something meaningful
    if ((IPV4FAM == g_lastInstruction) || (IPV4CONT == g_lastInstruction))
    {
        arrayptr = &ipv4array[1];
        sta =
            translateIPv4Prefix(ipaddrminstring, (unsigned char **)&arrayptr,
                                &iPrefixSize);
    }
    else if ((IPV6FAM == g_lastInstruction) || (IPV6CONT == g_lastInstruction))
    {
        arrayptr = &ipv6array[1];
        sta =
            translateIPv6Prefix(ipaddrminstring, (unsigned char **)&arrayptr,
                                &iPrefixSize);
    }
    else
        return ERR_SCM_INVALIPB;

    // If translation failed, or if we got anything other than
    // a single address, we're done
    if ((sta < 0) || (32 != iPrefixSize))
        return ERR_SCM_INVALIPB;

    // Otherwise, write the data to the roa
    if ((IPV4FAM == g_lastInstruction) || (IPV4CONT == g_lastInstruction))
    {
        // Construct ASN BITSTRING under the assumption that we have a
        // minimum address which needs paring down
        calculateAndClearMM(cTRUE, 32, arrayptr, &cBadBits, &iGoodBytes);
        ipv4array[0] = cBadBits;
        iGoodBytes++;

        // Then, write the bitstring (as an octet string) to the casn
        iBlocks =
            num_items(&
                      (roa->content.signedData.encapContentInfo.eContent.roa.
                       ipAddrBlocks.self));
        if (0 < iBlocks)
        {
            roaFamily =
                (struct ROAIPAddressFamily *)
                member_casn(&
                            (roa->content.signedData.encapContentInfo.eContent.
                             roa.ipAddrBlocks.self), iBlocks - 1);
            iAddrs = num_items(&(roaFamily->addressesOrRanges.self));
            if (0 <= iAddrs)
            {
                roaAddr =
                    (struct IPAddressOrRangeA *)
                    inject_casn(&(roaFamily->addressesOrRanges.self), iAddrs);
                write_casn(&(roaAddr->addressRange.min), ipv4array,
                           iGoodBytes);
            }
            else
                sta = ERR_SCM_INVALIPB;
        }
        else
            sta = ERR_SCM_INVALIPB;
        g_lastInstruction = IPV4MIN;
    }
    else if ((IPV6FAM == g_lastInstruction) || (IPV6CONT == g_lastInstruction))
    {
        // Construct ASN BITSTRING under the assumption that we have a
        // minimum ipv6 address which needs paring down
        calculateAndClearMM(cTRUE, 128, arrayptr, &cBadBits, &iGoodBytes);
        ipv6array[0] = cBadBits;
        iGoodBytes++;

        // Then, write the bitstring (as an octet string) to the casn
        iBlocks =
            num_items(&
                      (roa->content.signedData.encapContentInfo.eContent.roa.
                       ipAddrBlocks.self));
        if (0 < iBlocks)
        {
            roaFamily =
                (struct ROAIPAddressFamily *)
                member_casn(&
                            (roa->content.signedData.encapContentInfo.eContent.
                             roa.ipAddrBlocks.self), iBlocks - 1);
            iAddrs = num_items(&(roaFamily->addressesOrRanges.self));
            if (0 <= iAddrs)
            {
                roaAddr =
                    (struct IPAddressOrRangeA *)
                    inject_casn(&(roaFamily->addressesOrRanges.self), iAddrs);
                // roaAddr = (struct IPAddressOrRangeA*)
                // member_casn(&(roaFamily->addressesOrRanges.self), iAddrs);
                write_casn(&(roaAddr->addressRange.min), ipv6array,
                           iGoodBytes);
            }
            else
                sta = ERR_SCM_INVALIPB;
        }
        else
            sta = ERR_SCM_INVALIPB;
        g_lastInstruction = IPV6MIN;
    }

    return sta;
}

static err_code
setIPAddrMax(
    struct CMS *roa,
    unsigned char *ipaddrmaxstring)
{
    unsigned char ipv4array[5];
    unsigned char ipv6array[17];
    unsigned char *arrayptr = NULL;
    int iPrefixSize = 0;
    int iGoodBytes = 0;
    unsigned char cBadBits = 0;
    err_code sta = 0;
    int iBlocks = 0;
    int iAddrs = 0;
    struct IPAddressOrRangeA *roaAddr = NULL;
    struct ROAIPAddressFamily *roaFamily = NULL;

    memset(ipv4array, 0, 5);
    memset(ipv6array, 0, 17);

    // First, translate the address into something meaningful
    if (IPV4MIN == g_lastInstruction)
    {
        arrayptr = &ipv4array[1];
        sta =
            translateIPv4Prefix(ipaddrmaxstring, (unsigned char **)&arrayptr,
                                &iPrefixSize);
    }
    else if (IPV6MIN == g_lastInstruction)
    {
        arrayptr = &ipv6array[1];
        sta =
            translateIPv6Prefix(ipaddrmaxstring, (unsigned char **)&arrayptr,
                                &iPrefixSize);
    }
    else
        return ERR_SCM_INVALIPB;

    // If translation failed, or if we got anything other than
    // a single address, we're done
    if ((sta < 0) || (32 != iPrefixSize))
        return ERR_SCM_INVALIPB;

    // Otherwise, write the data to the roa
    if (IPV4MIN == g_lastInstruction)
    {
        // Construct ASN BITSTRING under the assumption that we have a
        // maximum ipv4 address which needs paring down
        calculateAndClearMM(cFALSE, 32, arrayptr, &cBadBits, &iGoodBytes);
        ipv4array[0] = cBadBits;
        iGoodBytes++;

        // Then, write the bitstring (as an octet string) to the casn
        iBlocks =
            num_items(&
                      (roa->content.signedData.encapContentInfo.eContent.roa.
                       ipAddrBlocks.self));
        if (0 < iBlocks)
        {
            roaFamily =
                (struct ROAIPAddressFamily *)
                member_casn(&
                            (roa->content.signedData.encapContentInfo.eContent.
                             roa.ipAddrBlocks.self), iBlocks - 1);
            iAddrs = num_items(&(roaFamily->addressesOrRanges.self));
            if (0 < iAddrs)
            {
                roaAddr =
                    (struct IPAddressOrRangeA *)
                    member_casn(&(roaFamily->addressesOrRanges.self),
                                iAddrs - 1);
                write_casn(&(roaAddr->addressRange.max), ipv4array,
                           iGoodBytes);
            }
            else
                sta = ERR_SCM_INVALIPB;
        }
        else
            sta = ERR_SCM_INVALIPB;
        g_lastInstruction = IPV4CONT;
    }
    else if (IPV6MIN == g_lastInstruction)
    {
        // Construct ASN BITSTRING under the assumption that we have a
        // maximum ipv6 address which needs paring down
        calculateAndClearMM(cFALSE, 128, arrayptr, &cBadBits, &iGoodBytes);
        ipv6array[0] = cBadBits;
        iGoodBytes++;

        // Then, write the bitstring (as an octet string) to the casn
        iBlocks =
            num_items(&
                      (roa->content.signedData.encapContentInfo.eContent.roa.
                       ipAddrBlocks.self));
        if (0 < iBlocks)
        {
            roaFamily =
                (struct ROAIPAddressFamily *)
                member_casn(&
                            (roa->content.signedData.encapContentInfo.eContent.
                             roa.ipAddrBlocks.self), iBlocks - 1);
            iAddrs = num_items(&(roaFamily->addressesOrRanges.self));
            if (0 < iAddrs)
            {
                roaAddr =
                    (struct IPAddressOrRangeA *)
                    member_casn(&(roaFamily->addressesOrRanges.self),
                                iAddrs - 1);
                write_casn(&(roaAddr->addressRange.max), ipv6array,
                           iGoodBytes);
            }
            else
                sta = ERR_SCM_INVALIPB;
        }
        else
            sta = ERR_SCM_INVALIPB;
        g_lastInstruction = IPV6CONT;
    }

    return sta;
}

#endif                          // IP_RANGES_ALLOWED

static err_code
setCertName(
    struct CMS *roa,
    unsigned char *certfilenamestring)
{
    int iRet = 0;
    int iCerts = 0;

    // Check to make sure there's only one cert and it's the one
    // we're about to read in.
    iCerts = num_items(&(roa->content.signedData.certificates.self));
    if (0 != iCerts)
        return ERR_SCM_NOTVALID;

    // Get the cert read in
    if (NULL != inject_casn(&(roa->content.signedData.certificates.self), 0))
        iRet =
            get_casn_file(&
                          (roa->content.signedData.certificates.certificate.
                           self), (char *)certfilenamestring, 0);
    g_lastInstruction = NONE;

    if (iRet > 0)
        return 0;
    else
        return ERR_SCM_INVALASN;
}

// ///////////////////////////////////////////////////////////
//
// General config file (.cnf) interpretation function
//
// ///////////////////////////////////////////////////////////

static err_code
confInterpret(
    char *filename,
    struct CMS *roa)
{
    char line[MAX_LINE + 1] = "";
    char key[MAX_LINE + 1] = "";
    char keyfileName[MAX_LINE + 1] = "";
    unsigned char value[MAX_LINE + 1] = "";

    int iRet = 0;
    err_code iRet2 = 0;
    err_code iROAState = 0;
    int iLineCount = 0;
    FILE *fp = NULL;
    enum configKeys ck = 0;

    // Acting as bools; testing for required config params
    int iConfiguredKey[CONFIG_KEY_MAX];

    for (ck = KEYVERSION; ck < CONFIG_KEY_MAX; ck++)
        iConfiguredKey[ck] = cFALSE;

    fp = fopen(filename, "r");
    if (NULL == fp)
    {
        // Error
        return ERR_SCM_COFILE;
    }

    // Initialize globals
    g_lastInstruction = NONE;
    g_iIPv4Flag = cFALSE;
    g_iIPv6Flag = cFALSE;

    // Read, line by line, into the struct
    while ((EOF != iRet) && (0 == iROAState))
    {
        iLineCount++;
        memset(line, 0, MAX_LINE + 1);
        // See warning at top of file about hardcoded "512"
        iRet = fscanf(fp, "%512[^\n]%*1c", line);
        if (0 == iRet)
        {
            getc(fp);
            continue;
        }
        if (1 == iRet)
        {
            memset(key, 0, MAX_LINE + 1);
            memset(value, 0, MAX_LINE + 1);
            if ('#' == line[0])
                continue;
            if ('[' == line[0])
                continue;
            if (2 != sscanf(line, "%s%*[ \t]%*[=]%*[ \t]%s", key, value))
            {
                iROAState = ERR_SCM_INVALARG;
            }
            else
            {
                for (ck = KEYVERSION; ck < CONFIG_KEY_MAX; ck++)
                {
                    if (0 == strcmp(configKeyStrings[ck], key))
                        break;
                }

                switch (ck)
                {
                case KEYVERSION:
                    if ((isInstructionForcing(g_lastInstruction) == cTRUE) ||
                        (cTRUE == iConfiguredKey[ck]))
                    {
                        iRet2 = ERR_SCM_INVALARG;
                        break;
                    }
                    iRet2 = setVersion(roa, value);
                    iConfiguredKey[ck] = cTRUE;
                    break;
                case SID:
                    if ((isInstructionForcing(g_lastInstruction) == cTRUE) ||
                        (cTRUE == iConfiguredKey[ck]))
                    {
                        iRet2 = ERR_SCM_INVALARG;
                        break;
                    }
                    iConfiguredKey[ck] = cTRUE;
                    break;
                case SIGNATURE:
                    // JFG - In the real world, we're going to calculate this
                    // instead of getting it from a file

                    break;
                case AS_ID:
                    if ((isInstructionForcing(g_lastInstruction) == cTRUE) ||
                        (cTRUE == iConfiguredKey[ck]))
                    {
                        iRet2 = ERR_SCM_INVALARG;
                        break;
                    }
                    iRet2 = setAS_ID(roa, value);
                    iConfiguredKey[ck] = cTRUE;
                    break;
                case IPFAM:
                    if (isInstructionForcing(g_lastInstruction) == cTRUE)
                    {
                        iRet2 = ERR_SCM_INVALARG;
                        break;
                    }
                    iRet2 = setIPFamily(roa, value);
                    iConfiguredKey[ck] = cTRUE;
                    break;
                case IPADDR:
                    // Check for valid previous instruction
                    // resides in subfunction
                    iRet2 = setIPAddr(roa, value);
                    iConfiguredKey[ck] = cTRUE;
                    break;
#ifdef IP_RANGES_ALLOWED
                case IPADDRMIN:
                    // Check for valid previous instruction
                    // resides in subfunction
                    iRet2 = setIPAddrMin(roa, value);
                    iConfiguredKey[ck] = cTRUE;
                    break;
                case IPADDRMAX:
                    // Check for valid previous instruction
                    // resides in subfunction
                    iRet2 = setIPAddrMax(roa, value);
                    iConfiguredKey[ck] = cTRUE;
                    break;
#endif                          // IP_RANGES_ALLOWED
                case CERTNAME:
                    if ((isInstructionForcing(g_lastInstruction) == cTRUE) ||
                        (cTRUE == iConfiguredKey[ck]))
                    {
                        iRet2 = ERR_SCM_INVALARG;
                        break;
                    }
                    iRet2 = setCertName(roa, value);
                    iConfiguredKey[ck] = cTRUE;
                    break;
                case KEYFILE:
                    if ((isInstructionForcing(g_lastInstruction)) ||
                        (cTRUE == iConfiguredKey[ck]))
                    {
                        iRet2 = ERR_SCM_INVALARG;
                        break;
                    }
                    strncpy(keyfileName, (char *)value,
                            sizeof(keyfileName) - 1);
                    iConfiguredKey[ck] = cTRUE;
                    break;
                case CONFIG_KEY_MAX:
                default:
                    iROAState = ERR_SCM_INVALARG;
                    break;
                }

                if (iRet2 < 0)
                {
                    iROAState = iRet2;
                }
            }
        }
    }

    // If we didn't finish an IP address block (uh-oh!)
    if (isInstructionForcing(g_lastInstruction) == cTRUE)
    {
        iROAState = ERR_SCM_INVALIPB;
    }

    // If we didn't have one of the required parameters
    for (ck = KEYVERSION; ck < CONFIG_KEY_MAX; ck++)
    {
        if (cFALSE == iConfiguredKey[ck])
        {
            if ((AS_ID == ck) || (KEYFILE == ck) || (IPFAM == ck))
            {
                iROAState = ERR_SCM_INVALARG;
            }
        }
    }

    iRet = fclose(fp);
    if (iROAState < 0)
        return iROAState;
    const char *ap = signCMS(roa, keyfileName, 0);
    if (ap)
        iROAState = ERR_SCM_INVALSIG;
    return iROAState;
}

// ///////////////////////////////////////////////////////////
//
// Exported functions from roa_utils.h
//
// ///////////////////////////////////////////////////////////
err_code
roaFromFile(
    char *fname,
    int fmt,
    int doval,
    struct CMS *rp)
{
    err_code iReturn;
    int fd;
    off_t iSize;
    ssize_t amt_read;
    int buf_tmp_size;
    unsigned char *buf;
    unsigned char *buf_tmp;
    struct stat sb;

    if (NULL == fname)
        return ERR_SCM_INVALARG;        // we need an input file

    // read in the file
    if ((fd = open(fname, (O_RDONLY))) < 0)
        return ERR_SCM_COFILE;
    else if (fstat(fd, &sb) != 0)
    {
        (void)close(fd);
        return ERR_SCM_COFILE;
    }
    else
    {
        iSize = sb.st_size;
        if ((buf = calloc(1, iSize)) == NULL)
        {
            (void)close(fd);
            return ERR_SCM_NOMEM;
        }
        amt_read = read(fd, buf, iSize);
        (void)close(fd);
        if (amt_read != iSize)
        {
            free(buf);
            return ERR_SCM_BADFILE;
        }
    }
    // handle format-specific processing
    CMS(rp, 0);                 // initialize the ROA
    switch (fmt)
    {
    case FMT_PEM:
        // Decode buffer from b64, skipping armor
        if ((iReturn =
             decode_b64(buf, iSize, &buf_tmp, &buf_tmp_size, "ROA")) != 0)
            break;
        free(buf);              // drop old buffer, use new one
        buf = buf_tmp;
        iSize = buf_tmp_size;
        // IMPORTANT: NO break, control falls through
    case FMT_DER:
        iReturn = 0;
        // did we use all of buf, no more and no less?
        int ret;
        if ((ret = decode_casn_lth(&rp->self, buf, iSize)) < 0)
        {
            fprintf(stderr, "roaFromFile: scan failed at offset %d\n", -ret);
            delete_casn(&rp->self);
            iReturn = ERR_SCM_INVALASN;
        }
        break;

    case FMT_CONF:
        iReturn = confInterpret(fname, rp);
        break;

    default:
        iReturn = ERR_SCM_INVALARG;
        break;
    }
    free(buf);                  // all done with this now

    // if we're ok and caller wants validation, it's time
    if ((0 == iReturn) && (cFALSE != doval))
        iReturn = roaValidate(rp);

    // if we got this far and everything is OK, send it back to caller
    return iReturn;
}

err_code
roaToFile(
    struct CMS *r,
    char *fname,
    int fmt)
{
    int fd = 0;
    err_code iReturn = 0;
    int written;
    int iSizeDER;
    int iSizePEM = 0;
    unsigned char *buf_der;
    unsigned char *buf_pem = NULL;

    // Parameter validity checks
    if (NULL == fname)
        return ERR_SCM_INVALARG;

    // Encode CASN
    (void)unlink(fname);        // security fix, needed to allow both EXCL and
                                // CREAT
    if ((fd = open(fname, (O_EXCL | O_WRONLY | O_CREAT | O_TRUNC), 0755)) < 0)
        return ERR_SCM_COFILE;
    if ((iSizeDER = size_casn(&(r->self))) < 0)
    {
        close(fd);
        return ERR_SCM_INVALASN;
    }
    buf_der = (unsigned char *)calloc(1, iSizeDER);
    if (buf_der == NULL)
    {
        close(fd);
        return ERR_SCM_NOMEM;
    }
    (void)encode_casn(&(r->self), buf_der);     // returns num bytes written

    iReturn = 0;
    switch (fmt)
    {
    case FMT_PEM:
        // JFG - Ask what length PEM file lines expect to be
        // (right now, hardcoded to 50)
        iReturn = encode_b64(buf_der, iSizeDER, &buf_pem, &iSizePEM, 50);
        if (iReturn != 0)
            break;
        written = write(fd, buf_pem, iSizePEM); // Write to file
        free(buf_pem);
        if (iSizePEM != written)
            iReturn = ERR_SCM_INVALASN;
        break;

    case FMT_DER:
        written = write(fd, buf_der, iSizeDER); // Write to file
        if (iSizeDER != written)
            iReturn = ERR_SCM_INVALASN;
        break;

    case FMT_CONF:
        // NOT OUR CONCERN RIGHT NOW
        // NO NEED TO DO THIS.
        break;

    default:
        iReturn = ERR_SCM_INVALARG;
        break;
    }

    (void)close(fd);
    free(buf_der);
    return iReturn;
}
