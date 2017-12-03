// crr.cpp --- CopyRenameReplace by katahiromz
// Copyright (C) 2017 Katayama Hirofumi MZ. License: CC0
#include "MString.hpp"
#include "MFileAPI.h"
#include <string>

enum EXITCODE
{
    EXITCODE_SUCCESS = 0,
    EXITCODE_CANTREAD,
    EXITCODE_CANTWRITE,
    EXITCODE_CANTCREATEDIR,
    EXITCODE_NOSOURCE,
    EXITCODE_INVALIDDEST,
    EXITCODE_NOTDIR,
    EXITCODE_NOTFILE,
    EXITCODE_TOOLONGPATH,
    EXITCODE_INVALIDNAME,
    EXITCODE_INVALIDCHAR
};

/**************************************************************************/

void show_help(void)
{
    printf("crr --- CopyRenameReplace version 0.3 by katahiromz\n");
    printf("Usage: crr \"source\" \"destination\" \"string1\" \"string2\"\n");
    printf("       crr \"source\" \"destination\" \"string1\" \"string2\" \"string3\" \"string4\" ...\n");
    printf("\n");
    printf("Copies files/directories, with renaming and replacing \"string1\" with \"string2\", \"string3\" with \"string4\", ...\n");
}

void stderr_wsprintf(const TCHAR *szFormat, ...)
{
    TCHAR szOutput[1024];
    va_list va;
    va_start(va, szFormat);
    wvsprintf(szOutput, szFormat, va);
    fputs(MTextToAnsi(szOutput).c_str(), stderr);
    va_end(va);
}

int CheckChars(const MChar *str)
{
    for (const MChar *pch = str; *pch; ++pch)
    {
        const MChar *found = _tcschr(TEXT("\\/:,;*?\"<>|+&~"), *str);
        if (found)
        {
            stderr_wsprintf(
                TEXT("ERROR: '%s' contains invalid character '%c'.\n"),
                str, *found);
            return EXITCODE_INVALIDCHAR;
        }
    }
    return EXITCODE_SUCCESS;
}

int CheckName(const MChar *name)
{
    int ret = EXITCODE_SUCCESS;
    do
    {
        size_t len = _tcslen(name);
        if (len == 0 || name[len - 1] == TEXT('$'))
        {
            ret = EXITCODE_INVALIDNAME;
            break;
        }
        static const MChar *invalids[] =
        {
            TEXT("."), TEXT(".."), 
            TEXT("CON"), TEXT("PRN"), TEXT("AUX"), TEXT("NUL"), TEXT("CLOCK$"),
            TEXT("COM0"), TEXT("COM1"), TEXT("COM2"), TEXT("COM3"), TEXT("COM4"),
            TEXT("COM5"), TEXT("COM6"), TEXT("COM7"), TEXT("COM8"), TEXT("COM9"),
            TEXT("LPT0"), TEXT("LPT1"), TEXT("LPT2"), TEXT("LPT3"), TEXT("LPT4"),
            TEXT("LPT5"), TEXT("LPT6"), TEXT("LPT7"), TEXT("LPT8"), TEXT("LPT9")
        };
        for (size_t i = 0; i < sizeof(invalids) / sizeof(invalids[0]); ++i)
        {
            if (lstrcmpi(name, invalids[i]) == 0)
            {
                ret = EXITCODE_INVALIDNAME;
                break;
            }
        }
    } while (0);

    switch (ret)
    {
    case EXITCODE_INVALIDNAME:
        stderr_wsprintf(TEXT("ERROR: '%s' is invalid name.\n"), name);
        break;
    case EXITCODE_SUCCESS:
        ret = CheckChars(name);
        break;
    default:
        assert(0);
        break;
    }
    return ret;
}

/**************************************************************************/

struct MapEntry
{
    MString first;
    MString second;
};
struct MapType
{
    std::vector<MapEntry> m_entries;

    bool empty() const
    {
        return size() == 0;
    }
    size_t size() const
    {
        return m_entries.size();
    }
    MString& operator[](const MString& str)
    {
        for (size_t i = 0; i < size(); ++i)
        {
            if (m_entries[i].first == str)
                return m_entries[i].second;
        }
        MapEntry entry;
        entry.first = str;
        m_entries.push_back(entry);
        return m_entries[size() - 1].second;
    }

    typedef const MapEntry *const_iterator;
    const_iterator begin() const
    {
        return &m_entries[0];
    }
    const_iterator end() const
    {
        return &m_entries[0] + size();
    }
};

void ReplaceStringByMap(MString& str, const MapType& the_map)
{
    MapType::const_iterator it, end = the_map.end();
    for (it = the_map.begin(); it != end; ++it)
    {
        //stderr_wsprintf("%s: %s\n", it->first.c_str(), it->second.c_str());
        mstr_replace_all(str, it->first, it->second);
    }
}

int ReplaceFile(const MString& src_file, const MString& dest_file, const MapType& the_map)
{
    size_t size;
    BYTE *old_bin = mfile_GetContents(src_file.c_str(), &size);
    if (old_bin == NULL)
    {
        stderr_wsprintf(TEXT("ERROR: Unable to read file: '%s'\n"), src_file.c_str());
        return EXITCODE_CANTREAD;
    }

    MTextType text_type;
    text_type.nNewLine = MNEWLINE_NOCHANGE;
    MStringW wide = mstr_from_bin(old_bin, size, &text_type);

#ifdef UNICODE
    ReplaceStringByMap(wide, the_map);
#else
    MString text(MWideToText(wide).c_str());
    ReplaceStringByMap(text, the_map);
    wide = MTextToWide(text);
#endif

    text_type.nNewLine = MNEWLINE_NOCHANGE;
    std::string new_bin = mbin_from_str(wide, text_type);

    int ret = EXITCODE_SUCCESS;
    if (!mfile_PutContents(dest_file.c_str(), &new_bin[0], new_bin.size()))
    {
        stderr_wsprintf(TEXT("ERROR: Unable to write file: '%s'\n"), dest_file.c_str());
        ret = EXITCODE_CANTWRITE;
    }

    free(old_bin);

    return ret;
}

int CopyRenameReplaceFile(const MString& path0, const MString& path1, const MapType& the_map)
{
    MChar szPath1[MAX_PATH];
    lstrcpy(szPath1, path1.c_str());

    MChar *pch = mpath_FindTitle(szPath1);
    int ret = CheckName(pch);
    if (ret)
        return ret;

    MString str = pch;
    ReplaceStringByMap(str, the_map);
    lstrcpy(pch, str.c_str());

    return ReplaceFile(path0, szPath1, the_map);
}

int CopyRenameReplaceDir(const MString& path0, const MString& path1, const MapType& the_map)
{
    stderr_wsprintf("Getting path list...\n");
    std::vector<MString> paths;
    mdir_GetFullPathList(path0.c_str(), paths);

    int ret = EXITCODE_SUCCESS;
    std::vector<MString>::iterator it, end = paths.end();

    stderr_wsprintf("Checking pathnames...\n");
    for (it = paths.begin(); it != end; ++it)
    {
        MString& old_path = *it;

        MString new_path = old_path;

        if (new_path.find(path0) != 0)
            continue;

        new_path.replace(0, path0.size(), "");
        //stderr_wsprintf("--- %s\n", new_path.c_str());
        ReplaceStringByMap(new_path, the_map);
        //stderr_wsprintf("+++ %s\n", new_path.c_str());

        MChar *title = mpath_FindTitle(&new_path[0]);
        ret = CheckName(title);
        if (ret)
            return ret;
    }

    stderr_wsprintf("Processing...\n");
    for (it = paths.begin(); it != end; ++it)
    {
        MString& old_path = *it;

        MString new_path = old_path;

        if (new_path.find(path0) != 0)
            continue;

        new_path.replace(0, path0.size(), "");
        //stderr_wsprintf("--- %s\n", new_path.c_str());
        ReplaceStringByMap(new_path, the_map);
        //stderr_wsprintf("+++ %s\n", new_path.c_str());
        new_path = path1 + new_path;

        if (mdir_Exists(old_path.c_str()))
        {
            if (!mdir_Create(new_path.c_str()) &&
                !mdir_Exists(new_path.c_str()))
            {
                stderr_wsprintf("NG: \"%s\" --> \"%s\"\n", old_path.c_str(), new_path.c_str());
                ret = EXITCODE_CANTCREATEDIR;
                break;
            }
        }
        else
        {
            int ret2 = ReplaceFile(old_path, new_path, the_map);
            if (ret != EXITCODE_SUCCESS)
            {
                stderr_wsprintf("NG: \"%s\" --> \"%s\"\n", old_path.c_str(), new_path.c_str());
                ret = ret2;
                break;
            }
        }
        stderr_wsprintf("OK: \"%s\" --> \"%s\"\n", old_path.c_str(), new_path.c_str());
    }

    if (ret == ERROR_SUCCESS)
        stderr_wsprintf("Done.\n");

    return ret;
}

int CopyRenameReplaceMain(const MString& path0, const MString& path1, const MapType& the_map)
{
    if (mdir_Exists(path0.c_str()))
    {
        if (!mpath_Exists(path1.c_str()))
        {
            if (!mdir_Create(path1.c_str()))
            {
                stderr_wsprintf(TEXT("ERROR: Unable to create destination directory.\n"));
                return EXITCODE_CANTCREATEDIR;
            }
        }
        else if (mfile_Exists(path1.c_str()))
        {
            stderr_wsprintf(TEXT("ERROR: destination is not directory.\n"));
            return EXITCODE_NOTDIR;
        }
        return CopyRenameReplaceDir(path0, path1, the_map);
    }
    else
    {
        if (!mpath_Exists(path0.c_str()))
        {
            stderr_wsprintf(TEXT("ERROR: source doesn't exist.\n"));
            return EXITCODE_NOSOURCE;
        }
        else if (mdir_Exists(path1.c_str()))
        {
            stderr_wsprintf(TEXT("ERROR: destination is not a normal file.\n"));
            return EXITCODE_NOTFILE;
        }
        return CopyRenameReplaceFile(path0, path1, the_map);
    }
}

int CopyRenameReplace(const MString& item0, const MString& item1, const MapType& the_map)
{
    if (item0 == item1)
    {
        stderr_wsprintf(TEXT("ERROR: source and destination are same.\n"));
        return EXITCODE_INVALIDDEST;
    }

    MChar cur_dir[MAX_PATH];
    mdir_Get(cur_dir, MAX_PATH);

    if (lstrlen(cur_dir) + 1 >= MAX_PATH ||
        item0.size() >= MAX_PATH || item1.size() >= MAX_PATH)
    {
        stderr_wsprintf(TEXT("ERROR: path is too long.\n"));
        return EXITCODE_TOOLONGPATH;
    }

    MChar path0[MAX_PATH], path1[MAX_PATH];
    mpath_GetFullPath(path0, item0.c_str());
    mpath_GetFullPath(path1, item1.c_str());
    mpath_AddSep(path0);
    mpath_AddSep(path1);

    MString strPath0 = path0, strPath1 = path1;

    if (strPath1.find(strPath0) == 0)
    {
        stderr_wsprintf(TEXT("ERROR: destination contains source directory.\n"));
        return EXITCODE_INVALIDDEST;
    }

    int ret = CopyRenameReplaceMain(strPath0, strPath1, the_map);

    mdir_Set(cur_dir);
    return ret;
}

/**************************************************************************/

int main(int argc, char **argv)
{
#ifdef UNICODE
    WCHAR **targv = CommandLineToArgvW(GetCommandLineW(), &argc);
#else
    char **targv = argv;
#endif

    if (argc < 5 ||
        lstrcmpi(argv[1], TEXT("--help")) == 0 ||
        lstrcmpi(argv[1], TEXT("--version")) == 0)
    {
        show_help();
        return 0;
    }

    stderr_wsprintf("source     : \"%s\"\n", targv[1]);
    stderr_wsprintf("destination: \"%s\"\n", targv[2]);

    MapType the_map;
    int k = 0;
    for (int i = 3; i + 1 < argc; ++k, i += 2)
    {
        MChar *from = targv[i + 0];
        MChar *to = targv[i + 1];

        int ret = CheckChars(to);
        if (ret)
            return ret;

        the_map[from] = to;

        stderr_wsprintf("String #%d: \"%s\" --> \"%s\"\n", k, from, to);
    }

    int ret = CopyRenameReplace(targv[1], targv[2], the_map);

#ifdef UNICODE
    LocalFree(targv);
#endif

    return ret;
}

/**************************************************************************/
