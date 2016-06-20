/*
 Copyright (c) 2016, Lucas Vickers - All rights reserved.
 
 Redistribution and use in source and binary forms, with or without modification, are permitted provided that
 the following conditions are met:
 
 * Redistributions of source code must retain the above copyright notice, this list of conditions and
 the following disclaimer.
 * Redistributions in binary form must reproduce the above copyright notice, this list of conditions and
 the following disclaimer in the documentation and/or other materials provided with the distribution.
 
 THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED
 WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
 PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR
 ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED
 TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 POSSIBILITY OF SUCH DAMAGE.
 */

#pragma once

#include "cinder/Cinder.h"
#include "cinder/Exception.h"
#include "cinder/Filesystem.h"
#include "cinder/Noncopyable.h"

#include "FileMonitor.h"

namespace filewatcher {
	
// expose internal event types
typedef filemonitor::FileMonitorEvent::EventType EventType;
	
typedef std::function<void ( const ci::fs::path&, EventType type )> WatchCallback;
	
class WatchedTarget;

//! Object for managing live-asset monitoring.  Handles the asio service
//! and passes along updates as needed
class FileWatcher {

  //! allow WatchedObjects access to removeWatch and updateCallback
  friend class WatchedTarget;
	
  public:
	static FileWatcher *instance();

	//! Creates a watch of a single file
	static WatchedTarget watchFile( const ci::fs::path &file,
								    const WatchCallback &callback );
	
	//! Creates a watch of a directory and subdirectories given a regex match
	static WatchedTarget watchPath( const ci::fs::path &path,
								    const std::string &regex,
								    const WatchCallback &callback );

	~FileWatcher() { mAsioWork.reset(); }
	
	//! Registers update routine with current cinder app
	void registerUpdate();
	
	//! Polls the asio service to check for updates
	void poll();
	
	
  private:
	
	FileWatcher();
	
	//! WatchedObject deconstructors will call this
	void removeWatch( uint64_t wid );
	
	void updateCallback( uint64_t wid, const WatchCallback &callback );

	//! Updates routine can be synced with cinder if desired
	void update();
	
	// TODO migrate away from boost error_codes?
	void fileEventHandler( const boost::system::error_code &ec,
						   const filemonitor::FileMonitorEvent &ev );
	
	std::map<uint64_t, WatchCallback> 				mRegisteredCallbacks;

	boost::asio::io_service 						mIoService;
	std::unique_ptr<filemonitor::FileMonitor> 		mFileMonitor;
	std::unique_ptr<boost::asio::io_service::work>	mAsioWork;
};


template <typename KeyT, typename ContainerT>
class WatchedMap : public std::map<KeyT, ContainerT> {
	// TODO add routines to make accessing a bit cleaner
};
	
// User defined map
template <typename KeyT>
using WatchedTargetMap = WatchedMap<KeyT, WatchedTarget>;
	

//! Used to watch a single file
class WatchedTarget : private ci::Noncopyable {

	//! Allow FileWatcher access to constructors
	friend class FileWatcher;
	
  public:
	//! Creates a dead object (non watching target)
	WatchedTarget()
	: mWatchId( 0 ) { }
	
	//! Valid objects will be de-registered from the FileMonitor service
	~WatchedTarget();
	
	WatchedTarget( WatchedTarget &&other );
	WatchedTarget& operator=( WatchedTarget &&rhs );
	
	// TODO allow default constructor?
	//WatchedTarget() { }
	
	uint64_t getId() const { return mWatchId; }
	
	//! Check if we're watching a path
	bool isPath() const { return ! mRegexMatch.empty(); }
	//! Check if we're watching a specific file
	bool isFile() const { return mRegexMatch.empty(); }

	//! Get the path that is being watched (file or general path)
	ci::fs::path getPath() const { return mPath; }
	//! Get the regex that is being applied to the watch
	std::string getRegex() const { return mRegexMatch; }
	
	//! Updates the callback that will be triggered when a change is registered
	void updateCallback( const WatchCallback &callback );

		
  protected:
	//! Constructor for watching a file
	WatchedTarget( uint64_t wid,
				   const ci::fs::path &path,
				   const WatchCallback &callback )
	: mWatchId( wid ),
	  mPath( path ),
	  mCallback( callback )
	{ }
	
	//! Constructor for watching a path
	WatchedTarget( uint64_t wid,
				   const ci::fs::path &path,
				   const std::string &regexMatch,
				   const WatchCallback &callback )
	: mWatchId( wid ),
	  mPath( path ),
	  mCallback( callback ),
	  mRegexMatch( regexMatch )
	{ }
	
	//! when utilized the ID will always be > 0
	uint64_t 		mWatchId;
	ci::fs::path 	mPath;
	WatchCallback	mCallback;
	std::string 	mRegexMatch;
		
};

} // namespace filewatcher
