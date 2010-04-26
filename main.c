#include <assert.h>
#include <libgen.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

extern int tardiff(int argc, char *argv[]);
extern int tarpatch(int argc, char *argv[]);
extern int tardiffinfo(int argc, char *argv[]);
int tardiffmerge(int argc, char *argv[]);

static enum Tool { none, diff, patch, info, merge } tool = none;

static void (*usage_func)(void);
static int (*tool_func)(int, char**);
static int min_args, max_args;

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
           "\ttardiffmerge <diff1> <diff2> [..] <diff>\n");
}

static void usage_tardiffinfo()
{
    printf("Usage:\n"
           "\ttardiffinfo <file> [..]\n");
}

static int select_tool(enum Tool new_tool)
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
        break;

    case patch:
        tool        = patch;
        tool_func   = &tarpatch;
        if (usage_func == NULL) usage_func  = &usage_tarpatch;
        min_args    =  3;
        max_args    =  3;
        break;

    case merge:
        tool        = merge;
        tool_func   = &tardiffmerge;
        if (usage_func == NULL) usage_func  = &usage_tardiffmerge;
        min_args    =  3;
        max_args    = -1;
        break;

    case info:
        tool        = info;
        tool_func   = &tardiffinfo;
        if (usage_func == NULL) usage_func  = &usage_tardiffinfo;
        min_args    =  1;
        max_args    = -1;
        break;

    default:
        return 0;
    }

    return 1;
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
              ? select_tool(merge) : 0))
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

    return (*tool_func)(num_args, args_begin);
}
