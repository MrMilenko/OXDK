// This file is part of Cxbe
// SPDX-License-Identifier: GPL-2.0-or-later

// SPDX-FileCopyrightText: 2002-2003 Aaron Robinson <caustik@caustik.com>
// SPDX-FileCopyrightText: 2019 Jannik Vogel
// SPDX-FileCopyrightText: 2021 Stefan Schmidt

#include "Common.h"
#include "Exe.h"
#include "Xbe.h"

#include <string.h>

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
    bool bRetail;
    bool bLimit64MB;

    const char *program = argv[0];
    const char *program_desc = "CXBE EXE to XBE (win32 to Xbox) Relinker (Version: " VERSION ")";
    Option options[] = {
        { szExeFilename, NULL, "exefile" },         { szXbeFilename, "OUT", "filename" },
        { szDumpFilename, "DUMPINFO", "filename" }, { szXbeTitle, "TITLE", "title" },
        { szMode, "MODE", "{debug|retail}" },       { szLogo, "LOGO", "filename" },
        { szTitleImage, "TITLEIMAGE", "filename" }, { szDebugPath, "DEBUGPATH", "path" },
        { szLimit64MB, "LIMIT64MB", "{yes|no}" },
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
            FILE *ti = fopen(szTitleImage, "rb");
            if(ti == NULL)
            {
                strncpy(szErrorMessage, "Unable to open TITLEIMAGE file", ERROR_LEN);
                goto cleanup;
            }
            fseek(ti, 0, SEEK_END);
            long tiSize = ftell(ti);
            fseek(ti, 0, SEEK_SET);
            if(tiSize > 0)
            {
                titleImage.resize((size_t)tiSize);
                if(fread(&titleImage[0], 1, (size_t)tiSize, ti) == (size_t)tiSize)
                    TitleImagePtr = &titleImage;
            }
            fclose(ti);
            if(TitleImagePtr == nullptr)
            {
                strncpy(szErrorMessage, "Unable to read TITLEIMAGE file", ERROR_LEN);
                goto cleanup;
            }
        }

        Xbe *XbeFile =
            new Xbe(ExeFile, szXbeTitle, bRetail, LogoPtr, szDebugPath, TitleImagePtr, bLimit64MB);

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
