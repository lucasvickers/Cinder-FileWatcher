
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
	mFileMonitor = unique_ptr<filemonitor::file_monitor>( new filemonitor::file_monitor( mIoService ) );
	
	mAsioWork = auto_ptr<boost::asio::io_service::work>( new boost::asio::io_service::work( mIoService ) );
	
	// prime the first handler
	mFileMonitor->async_monitor( std::bind( &FileWatcher::fileEventHandler, this, std::placeholders::_1, std::placeholders::_2 ) );
	
	// sync to udpate
	app::App::get()->getSignalUpdate().connect( bind( &FileWatcher::update, this ) );
}
	
FileWatcher *FileWatcher::instance()
{
	static FileWatcher sInstance;
	return &sInstance;
}

WatchedFile FileWatcher::watchFile( const fs::path &file,
									const WatchCallback &callback )
{
	uint64_t wid = instance()->mFileMonitor->add_file( file );
	WatchedFile obj = WatchedFile( wid , file, callback );
	// register the callback
	auto it = instance()->mRegisteredCallbacks.insert( std::pair<uint64_t, WatchCallback>( wid, obj.mCallback ) );
	
	// double check item didn't exist, should be impossible
	CI_ASSERT( it.second );
	return obj;
}
	
WatchedPath FileWatcher::watchPath( const fs::path &path,
									const std::string &regex,
									const WatchCallback &callback )
{
	uint64_t wid = instance()->mFileMonitor->add_path( path, regex );
	WatchedPath obj = WatchedPath( wid , path, regex, callback );
	auto it = instance()->mRegisteredCallbacks.insert( std::pair<uint64_t, WatchCallback>( wid, obj.mCallback ) );
	
	// double check item didn't exist, should be impossible
	CI_ASSERT( it.second );
	return obj;
}

void FileWatcher::removeWatch( uint64_t wid )
{
	// TODO remove
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
	// TODO do asio stuff
}

void FileWatcher::fileEventHandler( const boost::system::error_code &ec,
								    const filemonitor::file_monitor_event &ev )
{
	// TODO do stuff
	
	// add the next handler
	mFileMonitor->async_monitor( std::bind( &FileWatcher::fileEventHandler, this, std::placeholders::_1, std::placeholders::_2 ) );
}

// ----------------------------------------------------------------------------------------------------
// MARK: - WatchedFile
// ----------------------------------------------------------------------------------------------------
WatchedFile::WatchedFile( WatchedFile &&other )
{
	mWatchId = other.mWatchId;
	mPath = other.mPath;
	mCallback = other.mCallback;
	
	other.mWatchId = 0;
	other.mPath = "";
	other.mCallback = nullptr;
}

WatchedFile& WatchedFile::operator=( WatchedFile &&rhs )
{
	mWatchId = rhs.mWatchId;
	mPath = rhs.mPath;
	mCallback = rhs.mCallback;
	
	rhs.mWatchId = 0;
	rhs.mPath = "";
	rhs.mCallback = nullptr;
	
	return *this;
}

// ----------------------------------------------------------------------------------------------------
// MARK: - WatchedPath
// ----------------------------------------------------------------------------------------------------
WatchedPath::WatchedPath( WatchedPath &&other )
{
	mWatchId = other.mWatchId;
	mPath = other.mPath;
	mCallback = other.mCallback;
	mRegexMatch = other.mRegexMatch;
	
	other.mWatchId = 0;
	other.mPath = "";
	other.mCallback = nullptr;
	other.mRegexMatch = "";
}

WatchedPath& WatchedPath::operator=( WatchedPath &&rhs )
{
	mWatchId = rhs.mWatchId;
	mPath = rhs.mPath;
	mCallback = rhs.mCallback;
	mRegexMatch = rhs.mRegexMatch;
	
	rhs.mWatchId = 0;
	rhs.mPath = "";
	rhs.mCallback = nullptr;
	rhs.mRegexMatch = "";
	
	return *this;
}
	
} // namespace filewatcher