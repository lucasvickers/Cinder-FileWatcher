#pragma once

#include "cinder/Cinder.h"
#include "cinder/Exception.h"
#include "cinder/Filesystem.h"
#include "cinder/Noncopyable.h"

// TODO possibly forward declare and move to cpp.  Would avoid exposing any .hpp stuff
#include "file_monitor.hpp"

namespace filewatcher {

//typedef std::shared_ptr<class WatchedObject> WatchedObjectRef;
//typedef std::shared_ptr<class WatchedFile> WatchedFileRef;
//typedef std::shared_ptr<class WatchedPath> WatchedPathRef;
	
typedef std::function<void ( const std::vector<ci::fs::path>& )> WatchCallback;

//! Global object for managing live-asset monitoring.  Handles the asio service
//! and passes along updates as needed
class FileWatcher {

  //! allow WatchedObjects to remove themselves upon deletion
  friend class WatchedObject;
	
  public:
	static FileWatcher *instance();

	//! Creates a watch of a single file
	static WatchedFileRef watchFile( const ci::fs::path &file,
									 const WatchCallback &callback );
	
	//! Creates a watch of a directory and subdirectories given a regex match
	static WatchedPathRef watchPath( const ci::fs::path &path,
									 const std::string &regex,
									 const WatchCallback &callback );
	
	// TODO enable this once I understand the architecture
	//void watch( const WatchedObjectRef &watch );

  private:
	
	FileWatcher();
	
	// TODO how will this work again?  this will be called from the constructor
	//void registerWatch( WatchedObjectRef obj );
	// deconstructors will call this
	void removeWatch( WatchedObject *obj );

	//! Updates occur on asio, but we'll synchronize callbacks to ci::update loop.
	//! (this could change if we want it to)
	void update();
	
	// TODO migrate away from boost error_codes?
	void fileEventHandler( const boost::system::error_code &ec,
						   const filemonitor::file_monitor_event &ev );
	
	std::map<uint64_t, WatchedObjectRef> 			mRegisteredWatches;

	boost::asio::io_service 						mIoService;
	std::unique_ptr<filemonitor::file_monitor> 		mFileMonitor;
	std::unique_ptr<boost::asio::io_service::work>	mAsioWork;
};


class WatchedObject : public std::enable_shared_from_this<WatchedObject> {
		
  //! allow FileWatcher to have management
  friend FileWatcher;
		
  public:
	uint64_t getId() const { return mWatchId; }
	ci::fs::path getPath() const { return mPath; }

  protected:
	// TODO need to study the noncopyable stuff before I understand how to architect this
	// TODO probably also need to make the container structure
	//virtual void registerWatch() = 0;
	//virtual void removeWatch() = 0;
	virtual void callback() = 0;
	
	// TODO calls registerWatch
	//virtual WatchedObject( uint64_t uid );
	WatchedObject()
		: mWatchId( 0 ) {}
	
	//! causes it to be deleted from the file_manager service
	virtual ~WatchedObject() {
		FileWatcher::instance()->removeWatch( this );
	}

	//! when utilized the ID will always be > 0
	uint64_t 		mWatchId;
	ci::fs::path 	mPath;
};

//! Used to watch a single file
class WatchedFile : public WatchedObject,
					private ci::Noncopyable {
						
  //! allow FileWatcher management
  friend class FileWatcher;
						
  protected:
	WatchedFile( uint64_t wid,
				 const ci::fs::path &path,
				 const WatchCallback &callback );
						
	void callback() override;
						
	WatchCallback	mCallback;
};

//! Used to watch a directory structure via regex
class WatchedPath : public WatchedObject,
					private ci::Noncopyable {

  //! allow FileWatcher to have management
  friend FileWatcher;
  
  public:
	std::string const getRegex() { return mRegexMatch; }

  protected:
	WatchedPath( uint64_t wid,
				 const ci::fs::path &path,
				 const std::string &regexMatch,
				 const WatchCallback &callback );

	void callback() override;
						
	WatchCallback	mCallback;
						
	std::string 	mRegexMatch;
};

} // namespace filewatcher