DESCRIPTION

tardiff and tarpatch are tools to compute the differences between binary files,
and to reconstruct files from a base file and a list of differences. They work
with a block size of 512 bytes, which makes them suitable for computing
differences of tar files, which is also their intended use.


DEPENDENCIES

  - zlib 1.2.3 (or compatible)
  - OpenSSL 0.9.8h (or compatible)


EXAMPLE

Suppose I back-up my files in a tar archive every week. After three weeks, I
have three files:

    files-1.tar
    files-2.tar
    files-3.tar

Unfortunately, these files take up a lot of storage space, since all files are
stored every week, even if they haven't changed. With tardiff, I can store only
the differences instead and keep the last archive as a reference point:

    tardiff files-2.tar files-1.tar diff-2-to-1 && rm files-1.tar
    tardiff files-3.tar files-2.tar diff-3-to-2 && rm files-2.tar

Obtaining again three files:

    diff-2-to-1
    diff-3-to-2
    files-3.tar

If the original tar archives have contents in common, the difference files will
be smaller than the original tar files; typically much smaller. We can recreate
the original files using tarpatch as follows:

    tarpatch files-3.tar diff-3-to-2 files-2.tar && rm diff-3-to-2
    tarpatch files-2.tar diff-2-to-1 files-1.tar && rm diff-2-to-1

tardiff and tarpatch can read files compressed with gzip, so typically tardiff
is applied to compressed tar archives. However, using compressed archives with
tarpatch is not recommended (see below for details).


USAGE

tardiff <file1> <file2> <diff>
    Creates a file with the differences between file 1 and file 2.

    Diffing files is fast, since each file is read only once, but uses a lot of
    memory (around 6.25% of file 1, plus a few MB of constant memory).

    Either <file1> or <file2> can be specified as "-", in which case data is
    read from standard input. If <diff> is specified as "-", output is written
    to standard output.

tarpatch <file1> <diff> <file2>
    Recreates file 2 with the set of differences given.

    <diff> may be specified as "-" in which case data is read from standard
    input. <file2> may be specified as "-" to write to standard output.

    Note that <file1> must be a file that allows seeking. Therefore, you this
    file cannot be read from standard input. Furthermore, it is recommended that
    this file is uncompressed before running tarpatch


CONSISTENCY

tardiff stores an MD5 checksum of file 2 in the diff file, which is checked by
tarpatch. To verify that your base and difference files are intact, simply run:

    tarpatch file1.tar diff /dev/null

If no errors are reported, file 2 could be reconstructed correctly.

As a precaution, tardiff and tarpatch will refuse to overwrite existing files,
but you can override this behaviour using I/O redirection:

    tardiff file1.tar file2.tar - > tardiff
    tarpatch file1.tar diff - > file2.tar


COMPRESSION

Input files may be compressed with gzip. Output files are always uncompressed,
but can be compressed on the fly, e.g. as follows:

    # Create a gzipped diff file
    tardiff file1.tar.zg file2.tar.gz - | gzip > tardiff.gz

    # Reconstruct gzipped tar file
    tarpatch file1.tar.gz tardiff.gz - | gzip > file2.tar.gz
    # WARNING: using a gzipped input file is slow!

Note that in this case, the recreated compressed file may not be bitwise
identical to the original compressed file. Also, beware of unintentionally
overwriting existing files using I/O redirection.


BUGS

Note that the MD5 digest is used to identify common blocks of file 1 and file 2,
so if the files contain any MD5 collisions (different blocks that hash to the
same MD5 output) then the generated patch file will be incorrect.
This is highly unlikely to happen accidentally, but it can be constructed on
purpose since hash collisions for MD5 are known.
