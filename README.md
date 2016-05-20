query
=====

Usage: `query [OPTION] [!] COMMAND [ARGUMENT...]`

This tool reads a list of files from stdin, pipes the contents of each file
into the specified command and prints the name of the file if the command
succeeds. The name of the file is exposed to the command via the environment
variable `QUERY_FILENAME`.

## Examples ##

Query was designed to be used in conjunction with tools like `find(1)` and
AT&T's `tw`.

Find all dynamically linked executables:

- `find -type f | query ldd /dev/fd/0`
- `find -type f | query sh -c 'ldd "$QUERY_FILENAME"'`

Find all files ending in ".json" that are malformed:

- `find -type f -iname '*.json' | query -s ! python -m json.tool`

Find all POSIX shell scripts that contain Bash-isms:

- `find -type f -iname '*.sh' | query -s checkbashisms`

## Options ##

**NOTE:** Option parsing stops at the first non-option argument.

- -!: Only print filenames when the COMMAND fails.
- -0: File names are delimited by null bytes.
- -h: Show this text and exit.
- -n: File names are line-delimited. This the default behavior.
- -s: Redirect stderr of the subprocess to /dev/null.
- -w: File names are delimited by ASCII whitespace.

## Exit Statuses ##

- 1: Fatal error encountered.
- 2: Non-fatal error encountered.
