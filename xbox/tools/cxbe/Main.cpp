// This file is part of Cxbe
// SPDX-License-Identifier: GPL-2.0-or-later

// SPDX-FileCopyrightText: 2002-2003 Aaron Robinson <caustik@caustik.com>
// SPDX-FileCopyrightText: 2019 Jannik Vogel
// SPDX-FileCopyrightText: 2021 Stefan Schmidt

#include "Common.h"
#include "Exe.h"
#include "Xbe.h"

#include <stdlib.h>
#include <string.h>

// a whole file's bytes; false if it can't be opened or read or is empty
static bool ReadWholeFile(const char *path, std::vector<uint08> &bytes)
{
    FILE *file = fopen(path, "rb");
    if(file == NULL)
        return false;
    fseek(file, 0, SEEK_END);
    long size = ftell(file);
    fseek(file, 0, SEEK_SET);
    bool ok = false;
    if(size > 0)
    {
        bytes.resize((size_t)size);
        ok = fread(&bytes[0], 1, (size_t)size, file) == (size_t)size;
    }
    fclose(file);
    return ok;
}

// a 32-bit number in hex ("43480001" or "0x43480001"): hex digits only, no
// spaces or sign, at most 8 of them; false if it isn't one
static bool ParseHex32(const char *text, uint32 *value)
{
    uint32 parsed = 0;
    int digits = 0;

    if(text[0] == '0' && (text[1] == 'x' || text[1] == 'X'))
        text += 2;
    for(; *text != '\0'; text++, digits++)
    {
        char c = *text;
        uint32 nibble;

        if(c >= '0' && c <= '9')
            nibble = (uint32)(c - '0');
        else if(c >= 'a' && c <= 'f')
            nibble = (uint32)(c - 'a' + 10);
        else if(c >= 'A' && c <= 'F')
            nibble = (uint32)(c - 'A' + 10);
        else
            return false;
        if(digits == 8)
            return false;
        parsed = (parsed << 4) | nibble;
    }
    if(digits == 0)
        return false;
    *value = parsed;
    return true;
}

// program entry point
int main(int argc, char *argv[])
{
    char szErrorMessage[ERROR_LEN + 1] = { 0 };
    char szExeFilename[OPTION_LEN + 1] = { 0 };
    char szXbeFilename[OPTION_LEN + 1] = { 0 };
    char szDumpFilename[OPTION_LEN + 1] = { 0 };
    char szXbeTitle[OPTION_LEN + 1] = "Untitled";
    char szMode[OPTION_LEN + 1] = "retail";
    char szLogo[OPTION_LEN + 1] = "";
    char szTitleImage[OPTION_LEN + 1] = "";
    char szDebugPath[OPTION_LEN + 1] = "";
    char szLimit64MB[OPTION_LEN + 1] = "yes";
    char szTitleId[OPTION_LEN + 1] = "";
    char szAltTitleIds[OPTION_LEN + 1] = "";
    char szRatings[OPTION_LEN + 1] = "";
    char szSaveImage[OPTION_LEN + 1] = "";
    Xbe::CertificateOptions certificate;
    bool bRetail;
    bool bLimit64MB;

    const char *program = argv[0];
    const char *program_desc = "CXBE EXE to XBE (win32 to Xbox) Relinker (Version: " VERSION ")";
    Option options[] = {
        { szExeFilename, NULL, "exefile" },         { szXbeFilename, "OUT", "filename" },
        { szDumpFilename, "DUMPINFO", "filename" }, { szXbeTitle, "TITLE", "title" },
        { szMode, "MODE", "{debug|retail}" },       { szLogo, "LOGO", "filename" },
        { szTitleImage, "TITLEIMAGE", "filename" }, { szDebugPath, "DEBUGPATH", "path" },
        { szLimit64MB, "LIMIT64MB", "{yes|no}" },   { szTitleId, "TITLEID", "hex" },
        { szAltTitleIds, "ALTTITLEID", "hex[,hex...]" }, { szRatings, "RATINGS", "hex" },
        { szSaveImage, "SAVEIMAGE", "filename" },
        { NULL }
    };

    if(ParseOptions(argv, argc, options, szErrorMessage))
    {
        goto cleanup;
    }

    if(CompareString(szMode, "RETAIL"))
        bRetail = true;
    else if(CompareString(szMode, "DEBUG"))
        bRetail = false;
    else
    {
        strncpy(szErrorMessage, "invalid MODE", ERROR_LEN);
        goto cleanup;
    }

    if(CompareString(szLimit64MB, "YES"))
        bLimit64MB = true;
    else if(CompareString(szLimit64MB, "NO"))
        bLimit64MB = false;
    else
    {
        strncpy(szErrorMessage, "invalid LIMIT64MB", ERROR_LEN);
        goto cleanup;
    }

    if(szTitleId[0] != '\0' && (!ParseHex32(szTitleId, &certificate.dwTitleId) || certificate.dwTitleId == 0))
    {
        strncpy(szErrorMessage, "invalid TITLEID (a hex number other than 0, e.g. 43480001)", ERROR_LEN);
        goto cleanup;
    }

    if(szAltTitleIds[0] != '\0')
    {
        char *id = strtok(szAltTitleIds, ",");
        while(id != NULL)
        {
            uint32 value;
            if(!ParseHex32(id, &value) || value == 0 || certificate.alternateTitleIds.size() >= 0x10)
            {
                strncpy(szErrorMessage, "invalid ALTTITLEID (up to 16 hex numbers, comma separated)", ERROR_LEN);
                goto cleanup;
            }
            certificate.alternateTitleIds.push_back(value);
            id = strtok(NULL, ",");
        }
    }

    if(szRatings[0] != '\0' && !ParseHex32(szRatings, &certificate.dwGameRatings))
    {
        strncpy(szErrorMessage, "invalid RATINGS (a hex number, e.g. 2 for M)", ERROR_LEN);
        goto cleanup;
    }

    if(strlen(szXbeTitle) > 40)
    {
        printf("WARNING: Title too long, trimming\n");
        szXbeTitle[40] = '\0';
    }

    // verify we received the required parameters
    if(szExeFilename[0] == '\0')
    {
        ShowUsage(program, program_desc, options);
        return 1;
    }

    // if we don't have an Xbe filename, generate one from szExeFilename
    if(szXbeFilename[0] == '\0')
    {
        if(GenerateFilename(szXbeFilename, ".xbe", szExeFilename, ".exe"))
        {
            strncpy(szErrorMessage, "Unable to generate Exe Path", ERROR_LEN);
            goto cleanup;
        }
    }

    // open and convert Exe file
    {
        Exe *ExeFile = new Exe(szExeFilename);

        if(ExeFile->GetError() != 0)
        {
            strncpy(szErrorMessage, ExeFile->GetError(), ERROR_LEN);
            goto cleanup;
        }

        std::vector<uint08> logo;
        std::vector<uint08> *LogoPtr = nullptr;
        if(szLogo[0] != '\0')
        {
            logo = pgmToLogoBitmap(szLogo);
            logo = Xbe::ImageToLogoBitmap(logo);
            LogoPtr = &logo;
        }

        // Optional dashboard title image: the raw bytes of a $$XTIMAGE XPR (the
        // same format as a TitleImage.xbx) get embedded as a $$XTIMAGE section so
        // the XBE carries its icon like a retail title.
        std::vector<uint08> titleImage;
        std::vector<uint08> *TitleImagePtr = nullptr;
        if(szTitleImage[0] != '\0')
        {
            if(!ReadWholeFile(szTitleImage, titleImage))
            {
                strncpy(szErrorMessage, "Unable to read TITLEIMAGE file", ERROR_LEN);
                goto cleanup;
            }
            TitleImagePtr = &titleImage;
        }

        // Optional dashboard save image: the raw bytes of a SaveImage.xbx XPR,
        // embedded as a $$XSIMAGE section (XAPI writes it beside a save)
        std::vector<uint08> saveImage;
        if(szSaveImage[0] != '\0')
        {
            if(!ReadWholeFile(szSaveImage, saveImage))
            {
                strncpy(szErrorMessage, "Unable to read SAVEIMAGE file", ERROR_LEN);
                goto cleanup;
            }
            certificate.saveImage = &saveImage;
        }

        Xbe *XbeFile =
            new Xbe(ExeFile, szXbeTitle, bRetail, LogoPtr, szDebugPath, TitleImagePtr, bLimit64MB, &certificate);

        if(XbeFile->GetError() != 0)
        {
            strncpy(szErrorMessage, XbeFile->GetError(), ERROR_LEN);
            goto cleanup;
        }

        if(szDumpFilename[0] != 0)
        {
            FILE *outfile = fopen(szDumpFilename, "wt");
            XbeFile->DumpInformation(outfile);
            fclose(outfile);

            if(XbeFile->GetError() != 0)
            {
                if(XbeFile->IsFatal())
                {
                    strncpy(szErrorMessage, XbeFile->GetError(), ERROR_LEN);
                    goto cleanup;
                }
                else
                {
                    printf("DUMPINFO -> Warning: %s\n", XbeFile->GetError());
                    XbeFile->ClearError();
                }
            }
        }

        XbeFile->Export(szXbeFilename);

        if(XbeFile->GetError() != 0)
        {
            strncpy(szErrorMessage, XbeFile->GetError(), ERROR_LEN);
            goto cleanup;
        }
    }

cleanup:

    if(szErrorMessage[0] != 0)
    {
        ShowUsage(program, program_desc, options);

        printf("\n");
        printf(" *  Error : %s\n", szErrorMessage);

        return 1;
    }

    return 0;
}
