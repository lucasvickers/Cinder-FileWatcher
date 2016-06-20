# Cinder-FileMonitor
ASIO file monitor for cinder

# Description

This tool allows for cross platform (eventually os x, windows, and linux) monitoring of both files and directories.  The interface allows for the monitoring of a single file, as well as the monitoring of a folder via a regex search string.

All callbacks occur in lambda functions.  Eventually this could get tied into the core of Cinder allowing for a concept of 'live resources'

# Details

Currently we only make a callback on ADDED, REMOVED, MODIFIED, and RENAMED events.  When an application modifies a file, multiple things can happen such as a timestamp changes before a file is modified.  I tried to only select the most important events to avoid spurious callbacks.

# Examples

See _samples/FileMonitor_