VERSION CONTROL: MERCURIAL
--------------------------

Mercurial is a distributed revision control system written in Python.
This module implements Mercurial's API for the Version Control API.

STATUS
------

The following items have been completed:

- Core Mercurial PHP wrapper functions
- Install/uninstall hooks
- Database schema
- Module information hook

STRUCTURE
---------

hg/            Contains code for interfacing with Mercurial via command line
  templates/   These are our custom templates to minimize necessary log parsing
tests/         SimpleTest unit tests for hg/

DATABASE
--------

This module uses extra tables to cache data from Mercurial. They are:

versioncontrol_hg_repositories
    Standard issue, see versioncontrol_repositories and
    versioncontrol_cvs_repositories for more information.

versioncontrol_hg_commits
    Keeps track of Mercurial changesets. While we use versioncontrol's
    `vc_op_id` primary key for internal housekeeping, the true key is
    `node`, which represents the nodeid for the changeset.

versioncontrol_hg_item_revisions
    Keeps track of file-wise changes in Mercurial. This is actually a
    foreign key mapping to versioncontrol_hg_commits, as a file in
    Mercurial is usually uniquely identified by the changeset ID and
    its path (the file also has a nodeid, but this is internal.)

versioncontrol_hg_tags
    This is a foreign key mapping of tags to changesets, the equivalent
    for this database in Mercurial is the '.hgtags' file.

versioncontrol_hg_parents
    This is a foreign key mapping of parents to changesets. A changeset
    will usually have one parent, but two parents are possible if there
    was a merge.

Also, we use some of the default tables in unusual ways:

versioncontrol_branch_operations
versioncontrol_tag_operations
    It is somewhat difficult to detect when a branch or tag has taken
    place by merely inspecting the logs: additions to .hgtags using
    `hg tag` do not show up in the log at all, and branches are merely
    indicated by the sudden occurence of a new branch. We are still
    considering how to manage these, if we will manage them at all.

versioncontrol_commits
    We are not sure whether or not `revision` should be used, and whether
    or not the contents of `node` in versioncontrol_hg_commits should be
    stored here; the field is not indexed.
