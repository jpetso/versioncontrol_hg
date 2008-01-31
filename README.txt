VERSION CONTROL: MERCURIAL
--------------------------

Author: Edward Z. Yang (ezyang, http://drupal.org/user/211688)

Mercurial is a distributed revision control system written in Python.
This module implements Mercurial's API for the Version Control API.

STATUS
------

The following items have been completed:

- Core Mercurial PHP wrapper functions
- Install/uninstall hooks
- Database schema
- Module information hook
- Log to database adapter
- Basic integration with commit log

TODO
----

- Tags/Branches logging
- The rest of the required functions
- Documentation on which order one should implement things for a
  versioncontrol backend (based on this experience)
- ???

KNOWN ISSUES
------------

- Using email as account name results in commit log displaying the
  email to the world. It is unknown if, when we give versioncontrol
  the ability to lookup uids based on emails, these emails will be
  suppressed from public view.

- Parent information has not been recorded yet in the
  versioncontrol_hg_parents table. This shows up when we're retrieving
  commit actions and we don't know what the previous revision was.
  
  RELATED: Commit log probably does not have the capacity
  to display two previous versions; versioncontrol_hg_commit() and
  _versioncontrol_hg_log_parse_commits() are using hg_specific for
  some out of band communication of the parent revision--this should not
  be necessary?

- Commit log seems to barf over our path names because they don't have
  leading slashes. Should we bend to its will, or patch it so that its
  directory munging algorithm is better.

- 'file_copies' as per the Mercurial output doesn't ever seem to be
  triggered; the implementation for this case is accordingly patchy,
  especially use of the 'modified' flag.

- $commit_action['source items'] doesn't seem to be saved--which function is
  responsible for it?
  
- There are number of 'vcs_specific' items that are relied on by
  commitlog; I, on the other hand, have been methodically removing extra
  pieces that it seemed like I didn't need. Commitlog should not be
  relying on this data, one would think--otherwise, it needs to be better
  documented.
  
  RELATED: What bits of data are being set but never being used?

- On the topic of source items, the parent nodeids we receive are for
  the merged changesets, so there's no way to reliably reverse engineer
  the source files?

- There is a lot of functionality that feels like it would be better
  placed in versioncontrol itself; namely [versioncontrol_vcs]_get_directory_item(),
  [versioncontrol_vcs]_get_commit_branches, and large portions of
  [versioncontrol_vcs]_commit (which should have a good deal of the
  commit action tomfoolery auto-updated by versioncontrol.)

- There's a lot of docblock duplication, which worries me. We ought to be
  able to say hook_[hookname] and defer the documenting to versioncontrol
  itself.

- Using SHA-1 hashes for revision is really ugly; maybe we should use the
  non-portable revision numbers? (Ideally, compact nodeids would be
  used, but we need a way to calculate them on the fly due to the
  risk of collisions.)

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
    `node`, which is stashed away in versioncontrol_commits as `revision`.

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
    
    NOT IMPLEMENTED
    It is somewhat difficult to detect when a branch or tag has taken
    place by merely inspecting the logs: additions to .hgtags using
    `hg tag` do not show up in the log at all, and branches are merely
    indicated by the sudden occurence of a new branch. 
