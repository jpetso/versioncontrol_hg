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
  $raw_commits = _versioncontrol_hg_log_parse_commits($raw_logs, $repository);

  // Update commits in database:
  foreach ($raw_commits as $raw_commit) {
    list ($commit, $commit_actions) = $raw_commit;
    versioncontrol_insert_commit($commit, $commit_actions);
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
 * versioncontrol_insert_commit().
 *
 * @warning
 *    Some really black-magicky path munging is going on in here to
 *    accomodate commitlog. I'm not sure if this is proper or we've
 *    got an upstream bug on our hands.
 *
 * @param $raw_logs
 *    Array from hg_log() to be parsed.
 * @param $repository
 *    Repository array as per versioncontrol_get_repository().
 * @return
 *    Array list, with items in form of array(0 => $commit, 1 => $commit_actions)
 *    which are ready for insertion using versioncontrol_insert_commit().
 */
function _versioncontrol_hg_log_parse_commits($raw_logs, $repository) {
  $ret = array();
  
  foreach ($raw_logs as $log) {
    // Each log entry corresponds to a commit.
    
    // We use a "raw commit" structure to contain both parameters to
    // versioncontrol_insert_commit()
    $raw_commit = array(array(), array());
    $commit =& $raw_commit[0];
    $commit_actions =& $raw_commit[1];
    
    // Build $commit:
    $commit['repository'] = $repository;
    $commit['date'] = $log['_date'];
    $commit['message'] = $log['desc'];
    $commit['revision'] = $log['node'];
    // This is reflected accordingly in the {versioncontrol_hg_accounts} table:
    $commit['username'] = $log['_email'];
    $commit['hg_specific'] = array(
      'branch_id' => versioncontrol_ensure_branch($log['branches'], $repository['repo_id']),
      'rev' => $log['rev'],
      // Using key [1] to retrieve only SHA-1 hash
      'manifest' => $log['_manifest'][1],
    );
    
    // Build $commit_actions:
    
    // These lookups must be tested with isset(), NOT !empty()
    $lookup_adds   = array_flip($log['file_adds']);
    $lookup_dels   = array_flip($log['file_dels']);
    $lookup_copies = array_flip($log['file_copies']);
    foreach ($log['files'] as $file) {
      $commit_action = array();
      
      // CVS's implementation involves building a large array with
      // everything you could possibly need, and then excising things
      // based on options. While that approach may take less code,
      // it is less robust to change.
      
      if (isset($lookup_adds[$file])) {
        $commit_action['action'] = VERSIONCONTROL_ACTION_ADDED;
      }
      elseif (isset($lookup_dels[$file])) {
        $commit_action['action'] = VERSIONCONTROL_ACTION_DELETED;
      }
      elseif (isset($lookup_copies[$file])) {
        // This doesn't ever seem to happen in practice.
        $commit_action['action'] = VERSIONCONTROL_ACTION_COPIED;
      }
      else {
        // File was modified
        $commit_action['action'] = VERSIONCONTROL_ACTION_MODIFIED;
      }
      
      // Normalize Mercurial's file declaration "CVS" style to accomodate
      // commitlog.
      $file = "/$file";
      
      // Logs do not have information for 'modified', probably because
      // file_copies is never filled with anything.
      $commit_action['modified'] = FALSE;
      
      if ($commit_action['action'] != VERSIONCONTROL_ACTION_DELETED) {
        $commit_action['current item'] = array(
          // Directories are not directly versioned by Mercurial; they
          // get magically pruned CVS-style.
          'type' => VERSIONCONTROL_ITEM_FILE,
          'path' => $file,
          // This ends up not actually being saved; is it not necessary?
          'revision' => $commit['revision'],
          // Note that rev/manifest/branch are not included here; they're
          // only a join away with the commits table
        );
      } else {
        $commit_action['hg_specific'] = array(
          // Add a breadcrumb revision so that versioncontrol_hg_commit()
          // can process the deletion accordingly.
          'revision' => $commit['revision'],
        );
      }
      
      if ($commit_action['action'] != VERSIONCONTROL_ACTION_ADDED) {
        $commit_action['source items'] = array();
        foreach ($log['_parents'] as $parent) {
          list($rev, $nodeid) = $parent;
          if ($rev != '-1') {
            $item = array(
              'type' => VERSIONCONTROL_ITEM_FILE,
              // TODO: We need to do a nodeid lookup in order to
              // find the proper filename; that functionality has not
              // been implemented yet, and needs to also take into account
              // nodeids that have not been inserted in the database yet.
              // This file also needs to munged.
              'path' => "/$file",
              'revision' => $nodeid,
            );
            $commit_action['source items'][] = $item;
          }
        }
      }
      
      $commit_actions[$file] = $commit_action;
      
    }
    
    $ret[] = $raw_commit;
  }
  
  return $ret;
}

/**
 * Determines the latest revision of a series of log entries.
 */
function _versioncontrol_hg_log_parse_latest_rev($raw_logs) {
  $max_rev = 0;
  foreach ($raw_logs as $log) {
    if ($log['rev'] > $max_rev) {
      $max_rev = $log['rev'];
    }
  }
  return $max_rev;
}