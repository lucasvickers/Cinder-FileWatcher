
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

WatchedFileRef FileWatcher::watchFile( const fs::path &file,
									   const WatchCallback &callback )
{
	uint64_t id = instance()->mFileMonitor->add_file( file );
	WatchedFileRef obj = std::make_shared<WatchedFile>( WatchedFile( id , file, callback ) );
	auto it = instance()->mRegisteredWatches.insert( std::pair<uint64_t, WatchedObjectRef>( id, obj ) );
	
	// double check item didn't exist, should be impossible
	CI_ASSERT( it.second );
	return obj;
}
	
WatchedPathRef FileWatcher::watchPath( const fs::path &path,
									   const std::string &regex,
									   const WatchCallback &callback )
{
	uint64_t id = instance()->mFileMonitor->add_path( path, regex );
	WatchedPathRef obj = std::make_shared<WatchedPath>( WatchedPath( id , path, regex, callback ) );
	auto it = instance()->mRegisteredWatches.insert( std::pair<uint64_t, WatchedObjectRef>( id, obj ) );
	
	// double check item didn't exist, should be impossible
	CI_ASSERT( it.second );
	return obj;
}

void FileWatcher::removeWatch( WatchedObject *obj )
{
	// TODO remove
	int r = instance()->mRegisteredWatches.erase( obj->getId() );
	
	CI_ASSERT( r == 1 );
	mFileMonitor->remove( obj->getId() );
}

void FileWatcher::update()
{

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
WatchedFile::WatchedFile( uint64_t wid,
						  const ci::fs::path &path,
						  const WatchCallback &callback )
{
	//TODO anything beyond copy?
}
	
void WatchedFile::callback()
{
	//TODO can this be in the base class?
}
	
// ----------------------------------------------------------------------------------------------------
// MARK: - WatchedPath
// ----------------------------------------------------------------------------------------------------
WatchedPath::WatchedPath( uint64_t wid,
						  const ci::fs::path &path,
						  const std::string &regexMatch,
						  const WatchCallback &callback )
{
	//TODO anything beyond copy?
}
	
void WatchedPath::callback()
{
	//TODO can this be in the base class?
}
	
} // namespace filewatcher