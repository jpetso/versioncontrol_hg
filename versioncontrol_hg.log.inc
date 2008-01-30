<?php
// $Id$

/**
 * @file
 * Mercurial backend for Version Control API - Provides Mercurial commit
 * information and account management as a pluggable backend.
 *
 * This file defines internal functions for processing logs.
 *
 * Copyright 2008 by Edward Z. Yang (ezyang, http://drupal.org/user/211688)
 */

/**
 * Update repository's database tables by interfacing with Mercurial.
 *
 * @note
 *    This is a generic function for making updates. We may refactor this
 *    into multiple functions dealing with specific pieces of functionality
 *    if it gets too large.
 *
 * @warning
 *    This code should not directly interface with the Mercurial
 *    executable; the hg_* functions defined in hg/hg.inc should be used
 *    instead. If you need functionality not yet defined, submit a patch
 *    for hg/hg.inc implementing it.
 *
 * @param $repository
 *    Repository variable as returned by versioncontrol_get_repository()
 */
function _versioncontrol_hg_log_update_repository(&$repository) {
  
  // We're missing functions in the implementation left and right.
  // Don't run please!
  return;
  
  // Because we only support local repositories, `hg pull` is NOT called
  // before we perform these changes. It is the responsibility of all
  // branch maintainers to push their changes to this repository so that
  // their changes are registered.
  
  $latest_rev = $repository['hg_specific']['latest_rev'];
  if ($latest_rev == 0) {
    $rev_range = null;
  } else {
    $rev_range = ($latest_rev + 1) .':';
  }
  $raw_logs = hg_log($repository['root'], $rev_range);
  // watchdog('special', hg_last_cmd());
  
  if (empty($raw_logs)) {
    // No new logs, abort.
    return;
  }
  
  // Perform common processing on $raw_logs, such as converting the
  // date format, figuring out usernames and exploding nodeids with
  // revisions attached to them.
  _versioncontrol_hg_log_preprocess($raw_logs);
  
  // We could refactor each of these into a "handle" function, e.g.
  // _versioncontrol_hg_log_handle_commits, which performs parsing and
  // database work:
  
  // Update commits
  $commits    = _versioncontrol_hg_log_parse_commits($raw_logs);
  foreach ($commits as $raw_commit) {
    versioncontrol_insert_commit($raw_commit[0], $raw_commit[1]);
  }
  
  // Update latest revision
  $latest_rev = _versioncontrol_hg_log_parse_latest_rev($raw_logs);
  $repository['hg_specific']['latest_rev'] = $latest_rev;
  db_query('UPDATE {versioncontrol_hg_repositories}
            SET latest_rev = %d WHERE repo_id = %d',
            $repository['hg_specific']['latest_rev'], $repository['repo_id']);
  
}



/**
 * Parses the array output of hg_log() into form ready for
 * versioncontrol_insert_commit()
 *
 * @param $raw_logs
 *    Array from hg_log() to be parsed.
 * @return
 *    Array list, with items in form of array(0 => $commit, 1 => $commit_actions)
 *    which are ready for insertion using versioncontrol_insert_commit().
 */
function _versioncontrol_hg_log_parse_commits($raw_logs) {
  $ret = array();
  // unimplemented
}
