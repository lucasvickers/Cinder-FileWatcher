# Cinder-FileMonitor
ASIO file monitor for cinder

DEV NOTES:
Initial work was done to support polling and kqueue's.  kqueues support direct file changes.  After conversations, it was realized we needed support for both files and folders.

KQueues: These work by opening up direct handle ids that are associated with files.  We then poll to see if any of them change.  This is very streamlined regarding file monitoring, but this can't monitor folders or folder sub-content.

FSevents: Takes a list of paths, either folders or files.  If a file, callback will occur on changes to that specific file.  If a folder, callbacks will occur on changes to all sub-folders and files.  The challenge is that you do not have knowledge of which watched folder triggered a callback.

Windows: 
