VERSION CONTROL: MERCURIAL
--------------------------

Mercurial is a distributed revision control system written in Python.
This module implements Mercurial's API for the Version Control API.

STRUCTURE
---------

hg/            Contains code for interfacing with Mercurial via command line
  templates/   These are custom templates as per http://hgbook.red-bean.com/hgbookch11.html#x15-25100011
tests/         SimpleTest unit tests for hg/

STATUS
------



KNOWN BUGS
----------

Mercurial's file output is not lossless; files with spaces in them
cannot be distinguished from two separate files. Since most repositories
don't contain such files, we're going to ignore this for now, but
this needs an up-stream fix in templater.py.
