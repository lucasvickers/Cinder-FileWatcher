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

#include "FileWatcher.h"

#include "cinder/app/App.h"

using namespace ci;
using namespace std;

namespace filewatcher {
	

// ----------------------------------------------------------------------------------------------------
// MARK: - FileWatcher
// ----------------------------------------------------------------------------------------------------
FileWatcher::FileWatcher()
{
	// TODO move this into cinder's asio
	
	// setup our filemonitor asio service and link it to the internal cinder io_service
	mFileMonitor = unique_ptr<filemonitor::FileMonitor>( new filemonitor::FileMonitor( mIoService ) );
	
	mAsioWork = auto_ptr<boost::asio::io_service::work>( new boost::asio::io_service::work( mIoService ) );
	
	// prime the first handler
	mFileMonitor->asyncMonitor( std::bind( &FileWatcher::fileEventHandler, this, std::placeholders::_1, std::placeholders::_2 ) );
}
	
FileWatcher *FileWatcher::instance()
{
	static FileWatcher sInstance;
	return &sInstance;
}
	
void FileWatcher::registerUpdate()
{
	app::App::get()->getSignalUpdate().connect( bind( &FileWatcher::update, this ) );
}

void FileWatcher::poll()
{
	mIoService.poll();
}


WatchedTarget FileWatcher::watchFile( const fs::path &file,
									  const WatchCallback &callback )
{
	uint64_t wid = instance()->mFileMonitor->addFile( file );
	WatchedTarget obj = WatchedTarget( wid , file, callback );
	// register the callback
	auto it = instance()->mRegisteredCallbacks.insert( std::pair<uint64_t, WatchCallback>( wid, obj.mCallback ) );
	
	// double check item didn't exist, should be impossible
	CI_ASSERT( it.second );
	return obj;
}
	
WatchedTarget FileWatcher::watchPath( const fs::path &path,
									  const std::string &regex,
									  const WatchCallback &callback )
{
	uint64_t wid = instance()->mFileMonitor->addPath( path, regex );
	WatchedTarget obj = WatchedTarget( wid , path, regex, callback );
	auto it = instance()->mRegisteredCallbacks.insert( std::pair<uint64_t, WatchCallback>( wid, obj.mCallback ) );
	
	// double check item didn't exist, should be impossible
	CI_ASSERT( it.second );
	return obj;
}

void FileWatcher::removeWatch( uint64_t wid )
{

	int r = instance()->mRegisteredCallbacks.erase( wid );
	
	CI_ASSERT( r == 1 );
	mFileMonitor->remove( wid );
}
	
void FileWatcher::updateCallback( uint64_t wid, const WatchCallback &callback )
{
	int r = instance()->mRegisteredCallbacks.erase( wid );
	CI_ASSERT( r == 1 );
	instance()->mRegisteredCallbacks.insert( std::pair<uint64_t, WatchCallback>( wid, callback ) );
}
	

void FileWatcher::update()
{
	mIoService.poll();
}

void FileWatcher::fileEventHandler( const boost::system::error_code &ec,
								    const filemonitor::FileMonitorEvent &ev )
{
	// TODO do stuff
	cout << "EC: " << ec << endl;
	cout << "EV: " << ev << endl << endl;
	
	//! re-process if no error
	if( ! ec && ev.type != filemonitor::FileMonitorEvent::NONE )
	{
		auto it = mRegisteredCallbacks.find( ev.id );
		
		//! it's possible that we can remove a watch before the callback triggered,
		//! so don't treat this as an error.
		if( it != mRegisteredCallbacks.end() ) {
			it->second( ev.path, ev.type );
		}
	} else {
		//! TODO some error handling
	}
	
	
	//! add the next handler
	mFileMonitor->asyncMonitor( std::bind( &FileWatcher::fileEventHandler, this, std::placeholders::_1, std::placeholders::_2 ) );
}
	
// ----------------------------------------------------------------------------------------------------
// MARK: - WatchedTarget
// ----------------------------------------------------------------------------------------------------
WatchedTarget::~WatchedTarget()
{
	cout << "Destructor called";
	//! mWatchID of 0 means we're a dead object who transfered ownership
	if( mWatchId > 0 ) {
		FileWatcher::instance()->removeWatch( mWatchId );
		cout <<", and removed a watch";
	}
	cout << "." << endl;
}

WatchedTarget::WatchedTarget( WatchedTarget &&other )
{
	mWatchId = other.mWatchId;
	mPath = other.mPath;
	mCallback = other.mCallback;
	mRegexMatch = other.mRegexMatch;
	
	//! Setting id to 0 tells us it's a dead object
	other.mWatchId = 0;
	other.mPath = "";
	other.mCallback = nullptr;
	other.mRegexMatch = "";
}

WatchedTarget& WatchedTarget::operator=( WatchedTarget &&rhs )
{
	mWatchId = rhs.mWatchId;
	mPath = rhs.mPath;
	mCallback = rhs.mCallback;
	mRegexMatch = rhs.mRegexMatch;
	
	//! Setting id to 0 tells us it's a dead object
	rhs.mWatchId = 0;
	rhs.mPath = "";
	rhs.mCallback = nullptr;
	rhs.mRegexMatch = "";
	
	return *this;
}
	
} // namespace filewatcher
