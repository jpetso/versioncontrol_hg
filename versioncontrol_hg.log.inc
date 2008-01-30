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
 * @warning
 *    This code should not directly interface with the Mercurial
 *    executable; the hg_* functions defined in hg/hg.inc should be used
 *    instead. If you need functionality not yet defined, submit a patch
 *    for hg/hg.inc implementing it.
 *
 * @warning
 *    This function does not account for memory exhaustion.
 *    An alternative implementation could read each log entry one by
 *    one from the file (as CVS's implementation does), however, the
 *    resulting data-structure would still cause large amounts of memory
 *    usage (this is a primary deficiency of CVS) and would require
 *    multiple database queries. After determining which heuristics
 *    need to be able to access previous and later log entries, we will
 *    refactor accordingly; an alternate implementation would be to
 *    limit rev_range so that only batches of the logs are parsed at
 *    a time. Nevertheless, real world usage data is necessary before
 *    we can make a decision in this respect.
 *
 * @param $repository
 *    Repository variable as returned by versioncontrol_get_repository()
 */
function _versioncontrol_hg_log_update_repository(&$repository) {
  
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
  
  _versioncontrol_hg_log_preprocess($raw_logs);
  
  // We could refactor each of these into a "handle" function, e.g.
  // _versioncontrol_hg_log_handle_commits, which performs parsing and
  // database work.
  
  // Generate commits:
  $commits = _versioncontrol_hg_log_parse_commits($raw_logs);
  return;

  // Update commits in database:
  foreach ($commits as $raw_commit) {
    versioncontrol_insert_commit($raw_commit[0], $raw_commit[1]);
  }
  
  // TODO: Branches/Tags generation and updates
  
  // Update latest revision
  $latest_rev = _versioncontrol_hg_log_parse_latest_rev($raw_logs);
  $repository['hg_specific']['latest_rev'] = $latest_rev;
  db_query('UPDATE {versioncontrol_hg_repositories}
            SET latest_rev = %d WHERE repo_id = %d',
            $repository['hg_specific']['latest_rev'], $repository['repo_id']);
  
}

/**
 * Perform common pre-processing on logs from hg_logs(), although no
 * major changes to the array's structure.
 *
 * @warning
 *    This function MUST NOT be lossy or fundamentally change the log
 *    structure; any results of this pre-processing should be stored in
 *    unique keys prefixed with an underscore, so as to prevent collisions
 *    with the keys specified in hg/templates/changeset.tmpl.
 *
 * @param
 *    Raw logs variable to pre-process. The following additional indexes
 *    will be added:
 *
 *      '_date' => String UTC Unix timestamp of changeset
 *      '_offset' => Integer time zone offset of commiter
 *      '_manifest' => Array parent nodeid
 *      '_parents' => array(0 => First parent nodeid, 1 => Second parent nodeid)
 *      '_author' => String author name w/o email
 *      '_email' => String author's email
 *
 *    Nodeids take the form of array(string revision number, string SHA-1 hash)
 */
function _versioncontrol_hg_log_preprocess(&$raw_logs) {
  
  for ($i = 0, $c = count($raw_logs); $i < $c; $i++) {
    $log =& $raw_logs[$i];
    
    // Parse Mercurial's log format into Unix timestamp and offset.
    // We do NOT convert the timestamp into an integer to prevent overflow
    list($log['_date'], $offset) = explode('.', $log['date']);
    $log['_offset'] = (int) $offset;
    
    // Split manifest nodeid (note that we are not converting the revision
    // number to an integer either.)
    $log['_manifest'] = explode(':', $log['manifest']);
    
    // Split parents, and then split their nodeids
    $parents = explode(' ', $log['parents']);
    foreach ($parents as $k => $v) {
      $log['_parents'][$k] = explode(':', $v);
    }
    
    // Parse author into author name and email
    // This is somewhat fragile, but should work as long as a user does
    // not have '>' in their name! :-)
    $bits = explode('<', $log['author']);
    $log['_author'] = rtrim($bits[0]);
    if (!empty($bits[1])) {
      $log['_email'] = rtrim($bits[1], '>');
    }
    else {
      $log['_email'] = null;
    }
  }
  
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
  
  
}
