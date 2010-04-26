DESCRIPTION

tardiff and tarpatch are tools to compute the differences between binary files,
and to reconstruct files from a base file and a list of differences. They work
with a block size of 512 bytes, which makes them suitable for computing
differences of tar files, which is also their intended use.


DEPENDENCIES

  - zlib 1.2.3 (or compatible)
  - OpenSSL 0.9.8 (or compatible)


EXAMPLE

Suppose I back-up my files in a tar archive every week. After three weeks, I
have three files:

    files-1.tar
    files-2.tar
    files-3.tar

Unfortunately, these archives take up a lot of storage space, since all files
are stored every week, even if they haven't changed. With tardiff, I can store
only the differences instead and keep the last archive as a reference point:

    tardiff files-2.tar files-1.tar diff-2-to-1 && rm files-1.tar
    tardiff files-3.tar files-2.tar diff-3-to-2 && rm files-2.tar

Obtaining again three files:

    diff-2-to-1
    diff-3-to-2
    files-3.tar

If the original tar archives have contents in common, the difference files will
be smaller than the original tar files; typically much smaller. I can recreate
the original files using tarpatch as follows:

    tarpatch files-3.tar diff-3-to-2 files-2.tar && rm diff-3-to-2
    tarpatch files-2.tar diff-2-to-1 files-1.tar && rm diff-2-to-1

tardiff and tarpatch can read files compressed with gzip, so typically tardiff
is applied to compressed tar archives.


USAGE

tardiff <file1> <file2> <diff>
    Creates a file with the differences between file 1 and file 2.

    Uses temporary disk space in the order of 20 bytes per input block (or
    around 4% of file 1's size).

    Either <file1> or <file2> can be specified as "-", in which case data is
    read from standard input. If <diff> is specified as "-", output is written
    to standard output.

tarpatch <file1> <diff> <file2>
    Recreates file 2 with the set of differences given.

    <diff> may be specified as "-" in which case data is read from standard
    input. <file2> may be specified as "-" to write to standard output.

    Note that <file1> must be a file that allows seeking. Therefore, this file
    cannot be read from standard input. Futhermore, it is recommended that this
    file is not compressed: seeking in compressed files is possible, but much
    slower than in uncompressed files.

tardiffmerge [-f] <diff1> .. <diff2> <diff-output>
    Reads two or more diff files and combines their contents into a single set
    of differences, usually decreasing the (combined) file size considerably.

    The input files must allow seeking. tardiffmerge tries to reorder the diff
    file arguments so they can be meaningfully combined, unless the -f option
    is specified, which forces tardiffmerge to adhere to the order used on the
    command line. In this case tardiffmerge will still detect incorrect ordering
    of files. This option is mainly useful to speed up the operation.

tardiffinfo <file1> .. <fileN>
    Reads all the files passed on the command line, and for each diff file,
    prints the checksum of the input and output file. For each data file (i.e.
    all files that are not diff files) its checksum is printed.

    For files that cannot be read, and for diff files that cannot be applied
    directly or indirectly to any of the data files, an error is printed to the
    standard output stream, and the tool will exit with a non-zero status code.


Alternatively, these tools can be called by passing an option to tardiff:

    tardiff -p  or  tardiff --patch     is equivalent to tarpatch
    tardiff -m  or  tardiff --merge     is equivalent to tardiffmerge
    tardiff -i  or  tardiff --info      is equivalent to tardiffinfo

An optional argument of "--" can be passed to tardiff to separate options from
filenames, e.g.:

    tardiff -i -- -filename-starting-with-a-hypen-


CONSISTENCY

tardiff stores an MD5 checksum of the output file in the diff file, which is
verified by tarpatch when recreating the output file. To verify that your base
and difference files are intact, simply run:

    tarpatch file1.tar diff /dev/null

If no errors are reported, the output file could be reconstructed correctly.

As a precaution, tardiff and tarpatch will refuse to overwrite existing files,
but you can override this behaviour using I/O redirection:

    tardiff file1.tar file2.tar - > tardiff
    tarpatch file1.tar diff - > file2.tar


COMPRESSION

Input files may be compressed with gzip and are decompressed transparently.
Output files are always uncompressed, but can be compressed on the fly, e.g.:

    # Create a gzipped diff file
    tardiff file1.tar.zg file2.tar.gz - | gzip > tardiff.gz

    # Reconstruct gzipped tar file
    tarpatch file1.tar.gz tardiff.gz - | gzip > file2.tar.gz
    # WARNING: using a gzipped input file is slow!

Note that in this case, the recreated compressed file may not be bitwise
identical to the original compressed file. Also, beware of unintentionally
overwriting existing files using I/O redirection.


BUGS/LIMITATIONS

tardiff uses MD5 checksums to identify common blocks in file 1 and file 2, so if
the files contain any MD5 collisions (different blocks that hash to the same MD5
output) then the generated patch file will be incorrect. This is unlikely to
occur by accident, but can be done on purpose since hash collisions for MD5 are
known.

Due to limitations in the diff file format, the size of input files cannot
exceed 2 terabyte.
