#include <assert.h>
#include <libgen.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

extern int tardiff(int argc, char *argv[], char *flags);
extern int tarpatch(int argc, char *argv[], char *flags);
extern int tardiffinfo(int argc, char *argv[], char *flags);
extern int tardiffmerge(int argc, char *argv[], char *flags);

static enum Tool { none, diff, patch, info, merge } tool = none;

static void (*usage_func)(void);
static int (*tool_func)(int, char**, char*);
static int min_args, max_args;
static const char *tool_flags;
static char flags[256];

static void usage_tardiff()
{
    printf("Usage:\n"
           "\ttardiff <file1> <file2> <diff>\n"
           "\ttardiff (-p|--patch) <file1> <diff> <file2>\n"
           "\ttardiff (-m|--merge) [-f] <diff1> <diff2> [..] <diff>\n"
           "\ttardiff (-i|--info)  <file> [..]\n");
}

static void usage_tarpatch()
{
    printf("Usage:\n"
           "\ttarpatch <file1> <diff> <file2>\n");
}

static void usage_tardiffmerge()
{
    printf("Usage:\n"
           "\ttardiffmerge [-f] <diff1> <diff2> [..] <diff>\n");
}

static void usage_tardiffinfo()
{
    printf("Usage:\n"
           "\ttardiffinfo <file> [..]\n");
}

static bool select_tool(enum Tool new_tool)
{
    /* only allowed to change the selected tool once */
    if (tool != none && tool != diff) return 0;

    switch (new_tool)
    {
    case diff:
        tool        = diff;
        tool_func   = &tardiff;
        if (usage_func == NULL) usage_func  = &usage_tardiff;
        min_args    =  3;
        max_args    =  3;
        tool_flags  = "";
        break;

    case patch:
        tool        = patch;
        tool_func   = &tarpatch;
        if (usage_func == NULL) usage_func  = &usage_tarpatch;
        min_args    =  3;
        max_args    =  3;
        tool_flags  = "";
        break;

    case merge:
        tool        = merge;
        tool_func   = &tardiffmerge;
        if (usage_func == NULL) usage_func  = &usage_tardiffmerge;
        min_args    =  3;
        max_args    = -1;
        tool_flags  = "f";
        break;

    case info:
        tool        = info;
        tool_func   = &tardiffinfo;
        if (usage_func == NULL) usage_func  = &usage_tardiffinfo;
        min_args    =  1;
        max_args    = -1;
        tool_flags  = "";
        break;

    default:
        return false;
    }

    return true;
}

static int add_flag(char f)
{
    char *p;
    if (tool_flags == NULL || strchr(tool_flags, f) == NULL) return false;
    for (p = flags; *p != '\0'; ++p) if (*p == f) return true;
    p[0] = f;
    p[1] = '\0';
    return true;
}

static char **parse_options(int argc, char *argv[])
{
    int i;
    for (i = 1; i < argc; ++i)
    {
        if (argv[i][0] != '-') break;

        /* Recognize -- as indicating end-of-options */
        if (strcmp(argv[i], "--") == 0)
        {
            ++i;
            break;
        }

        if (!((strcmp(argv[i], "-p") == 0 || strcmp(argv[i], "--patch") == 0)
              ? select_tool(patch) :
              (strcmp(argv[i], "-i") == 0 || strcmp(argv[i], "--info") == 0)
              ? select_tool(info) :
              (strcmp(argv[i], "-m") == 0 || strcmp(argv[i], "--merge") == 0)
              ? select_tool(merge) :
              (argv[i][0] == '-' && argv[i][1] != '\0' && argv[i][2] == '\0')
              ? add_flag(argv[i][1]) : false))
        {
            printf("Unrecognized option: %s\n", argv[i]);
            exit(EXIT_FAILURE);
        }
    }
    return &argv[i];
}

int main(int argc, char *argv[])
{
    const char *name = basename(argv[0]);
    char **args_begin, **args_end;
    int num_args;

    if (strcmp(name, "tarpatch") == 0)
        select_tool(patch);
    else
    if (strcmp(name, "tardiffmerge") == 0)
        select_tool(merge);
    else
    if (strcmp(name, "tardiffinfo") == 0)
        select_tool(info);
    else
        select_tool(diff);

    /* Process arguments */
    args_begin = parse_options(argc, argv);
    args_end   = &argv[argc];
    num_args   = args_end - args_begin;
    if (num_args < min_args || (max_args != -1 && num_args > max_args))
    {
        (*usage_func)();
        exit(EXIT_FAILURE);
    }

    return (*tool_func)(num_args, args_begin, flags);
}
