#pragma once

#include "cinder/Cinder.h"
#include "cinder/Exception.h"
#include "cinder/Filesystem.h"
#include "cinder/Noncopyable.h"

// TODO possibly forward declare and move to cpp.  Would avoid exposing any .hpp stuff
#include "file_monitor.hpp"

namespace filewatcher {
	
class WatchedObject;
class WatchedFile;
class WatchedPath;
	
// expose internal event types
typedef filemonitor::file_monitor_event::event_type EventType;
	
typedef std::function<void ( const ci::fs::path&, EventType type )> WatchCallback;

//! Global object for managing live-asset monitoring.  Handles the asio service
//! and passes along updates as needed
class FileWatcher {

  //! allow WatchedObjects access to removeWatch and updateCallback
  friend class WatchedObject;
	
  public:
	static FileWatcher *instance();

	//! Creates a watch of a single file
	static WatchedFile watchFile( const ci::fs::path &file,
								  const WatchCallback &callback );
	
	//! Creates a watch of a directory and subdirectories given a regex match
	static WatchedPath watchPath( const ci::fs::path &path,
								  const std::string &regex,
								  const WatchCallback &callback );

	~FileWatcher() { mAsioWork.reset(); }
	
  private:
	
	FileWatcher();
	
	//! WatchedObject deconstructors will call this
	void removeWatch( uint64_t wid );
	
	void updateCallback( uint64_t wid, const WatchCallback &callback );

	//! Updates occur on asio, but we'll synchronize callbacks to ci::update loop.
	//! (this could change if we want it to)
	void update();
	
	// TODO migrate away from boost error_codes?
	void fileEventHandler( const boost::system::error_code &ec,
						   const filemonitor::file_monitor_event &ev );
	
	std::map<uint64_t, WatchCallback> 				mRegisteredCallbacks;

	boost::asio::io_service 						mIoService;
	std::unique_ptr<filemonitor::file_monitor> 		mFileMonitor;
	std::unique_ptr<boost::asio::io_service::work>	mAsioWork;
};


class WatchedObject : private ci::Noncopyable {
						  
  public:
	uint64_t getId() const { return mWatchId; }
	ci::fs::path getPath() const { return mPath; }
	void updateCallback( const WatchCallback &callback );
						  
  protected:

	WatchedObject()
	: mWatchId( 0 ) { }
	
	WatchedObject( uint64_t wid,
				  const ci::fs::path &path,
				  const WatchCallback &callback )
	: mWatchId( wid ), mPath( path ), mCallback( callback ) { }
	
	//! causes it to be deleted from the file_manager service
	virtual ~WatchedObject();

	//! when utilized the ID will always be > 0
	uint64_t 		mWatchId;
	ci::fs::path 	mPath;
	WatchCallback	mCallback;

};

//! Used to watch a single file
class WatchedFile : public WatchedObject {

	//! Allow FileWatcher access to constructors
	friend class FileWatcher;
	
  public:
	WatchedFile( WatchedFile &&other );
	WatchedFile& operator=( WatchedFile &&rhs );
	
	WatchedFile() { }
	
  protected:
	WatchedFile( uint64_t wid,
				 const ci::fs::path &path,
				 const WatchCallback &callback )
	: WatchedObject( wid, path, callback ) { }

};

//! Used to watch a directory structure via regex
class WatchedPath : public WatchedObject {
	
	//! Allow FileWatcher access to constructors
	friend class FileWatcher;
	
  public:
	WatchedPath( WatchedPath &&other );
	WatchedPath& operator=( WatchedPath &&rhs );
	
	WatchedPath() { }
	
	std::string const getRegex() { return mRegexMatch; }

  protected:
	WatchedPath( uint64_t wid,
				 const ci::fs::path &path,
				 const std::string &regexMatch,
				 const WatchCallback &callback )
	: WatchedObject( wid, path, callback ), mRegexMatch( regexMatch ) { }
						
	std::string 	mRegexMatch;
};

} // namespace filewatcher