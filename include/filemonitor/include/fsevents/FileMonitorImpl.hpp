#pragma once

#include <boost/enable_shared_from_this.hpp>	// TODO migrate to standard C++ / cinder if possible
#include <boost/filesystem.hpp>					// TODO migrate to standard C++ / cinder if possible

#include <CoreServices/CoreServices.h>
#include <thread>
#include <regex>

namespace filemonitor {
	
class FileMonitorImpl :
public std::enable_shared_from_this<FileMonitorImpl>
{
	
public:
	FileMonitorImpl()
	: mRun(true), mWorkThread( &FileMonitorImpl::workThread, this ), mFsevents( nullptr )
	{}
	
	~FileMonitorImpl()
	{
		// The work thread is stopped and joined.
		stopWorkThread();
		mWorkThread.join();
		stopFsevents();
	}
	
	uint64_t addPath( const boost::filesystem::path &path, const std::string &regexMatch )
	{
		std::lock_guard<std::mutex> lock( mPathsMutex );
		
		uint64_t id = mNextPathId;
		mNextPathId += 2;
		
		auto iter = mPaths.emplace( id, PathEntry( path, regexMatch, id ) );
		assert( iter.second );
		
		incrementTarget( path );
		
		stopFsevents();
		startFsevents();
		
		return id;
	}
	
	uint64_t addFile( const boost::filesystem::path &file )
	{
		std::lock_guard<std::mutex> lock( mPathsMutex );
		
		uint64_t id = mNextFileId;
		mNextFileId += 2;
		
		auto iter = mFiles.emplace( id, FileEntry( file, id ) );
		assert( iter.second );
		// iter.first is the multimap iter. iter is filesystem::path, pointer to the FileEntry
		mFilesMmap.insert( std::make_pair( file, &(iter.first->second) ) );
		
		assert( mFilesMmap.size() == mFiles.size() );
		
		incrementTarget( file );
	
		stopFsevents();
		startFsevents();
		
		return id;
	}
	
	void remove( uint64_t id )
	{
		std::lock_guard<std::mutex> lock( mPathsMutex );
		
		boost::filesystem::path path;

		// remove from containers
		if( id % 2 == 0 ) {
			// even is file
			removeEntry<decltype( mFiles ), decltype( mFilesMmap )>( id, mFiles, mFilesMmap );
		} else {
			// odd is path
			removeEntry<decltype( mPaths )>( id, mPaths );
		}
		
		decrementTarget( path );
		
		stopFsevents();
		startFsevents();
	}
	
	void destroy()
	{
		std::lock_guard<std::mutex> lock( mEventsMutex );
		mRun = false;
		mEventsCond.notify_all();
	}
	
	FileMonitorEvent popFrontEvent( boost::system::error_code &ec )
	{
		std::unique_lock<std::mutex> lock( mEventsMutex );
		while( mRun && mEvents.empty() ) {
			mEventsCond.wait( lock );
		}
		FileMonitorEvent ev;
		if( ! mEvents.empty() ) {
			ec = boost::system::error_code();
			ev = mEvents.front();
			mEvents.pop_front();
		} else {
			ec = boost::asio::error::operation_aborted;
		}
		return ev;
	}
	
	void verifyEvent( const boost::filesystem::path &path, FileMonitorEvent::EventType type )
	{
		if( ! mRun ) {
			return;
		}
		
		// TODO move onto worker thread ?
		
		//! check for exact file matches, streamlined map search to keep complexity minimal
		auto range = mFilesMmap.equal_range( path );
		for( auto it = range.first; it != range.second; ++it ) {
			pushBackEvent( FileMonitorEvent( path, type, it->second->entryId ) );
		}
		
		//! check every regex possibility, which is computationally more expensive
		for( const auto &it : mPaths ) {
			if( std::regex_match( path.string(), it.second.regexMatch ) ) {
				pushBackEvent( FileMonitorEvent( path, type, it.second.entryId ) );
			}
		}
	}
	
	void pushBackEvent( const FileMonitorEvent &ev )
	{
		std::lock_guard<std::mutex> lock( mEventsMutex );
		mEvents.push_back( ev );
		mEventsCond.notify_all();
	}
	
private:
	
	void startFsevents()
	{
		if ( mPaths.size() == 0 && mFiles.size() == 0 ) {
			mFsevents = nullptr;
			return;
		}
		
		// Need to pass FSEvents an array of unique paths, both files and folders.
		// We assume that there are no duplicates between pathsMmap and filesMmap
		//  since one is only files and one only folders
		
		CFMutableArrayRef allPaths = CFArrayCreateMutable( kCFAllocatorDefault, mAllTargetsMap.size(), &kCFTypeArrayCallBacks );

		for( const auto &path : mAllTargetsMap ) {
			
			CFStringRef cfstr = CFStringCreateWithCString( kCFAllocatorDefault, path.first.string().c_str(), kCFStringEncodingUTF8 );
			CFArrayAppendValue( allPaths, cfstr );
			CFRelease(cfstr);
		}
		
		FSEventStreamContext context = {0, this, NULL, NULL, NULL};
		mFsevents = FSEventStreamCreate( kCFAllocatorDefault,
									 &filemonitor::FileMonitorImpl::fseventsCallback,
									 &context,
									 allPaths,
									 //todo determine when we need to support historical events (if ever, I hope never)
									 kFSEventStreamEventIdSinceNow, 		// only new modifications
									 (CFTimeInterval) 1.0, 					// 1 second latency interval
									 kFSEventStreamCreateFlagFileEvents );
		FSEventStreamRetain( mFsevents );
		
		if( ! mFsevents )
		{
			// TODO move this out of boost namespace
			boost::system::system_error e( boost::system::error_code( errno, boost::system::get_system_category() ),
										  "filemonitor::FileMonitorImpl::init_fsevents: fsevents failed" );
			boost::throw_exception(e);
		}
		
		FSEventStreamScheduleWithRunLoop( mFsevents, mRunloop, kCFRunLoopDefaultMode );
		FSEventStreamStart( mFsevents );
		mRunloopCond.notify_all();
		FSEventStreamFlushAsync( mFsevents );
		
		// TODO delete mFsevents ?  Create pattern check
	}
	
	void stopFsevents()
	{
		if (mFsevents)
		{
			FSEventStreamStop( mFsevents );
			// TODO do we need to unschedule this?
			// FSEventStreamUnscheduleFromRunLoop(mFsevents, mRunloop, kCFRunLoopDefaultMode);
			FSEventStreamInvalidate( mFsevents );
			FSEventStreamRelease( mFsevents );
		}
		mFsevents = nullptr;
	}
	
	static void fseventsCallback( ConstFSEventStreamRef streamRef,
							   void *clientCallBackInfo,
							   size_t numEvents,
							   void *eventPaths,
							   const FSEventStreamEventFlags eventFlags[],
							   const FSEventStreamEventId eventIds[] )
	{
		size_t i;
		char **paths = (char**)eventPaths;
		FileMonitorImpl* impl = (FileMonitorImpl*)clientCallBackInfo;
		
		for( i = 0; i < numEvents; ++i )
		{
			// TODO keep track of these, because we don't necessarily want to return folders as events
			// kFSEventStreamEventFlagItemIsDir
			// kFSEventStreamEventFlagItemIsFile
			
			boost::filesystem::path path( paths[i] );
			if( eventFlags[i] & kFSEventStreamEventFlagNone ) {
				// TODO log this
			}
			if( eventFlags[i] & kFSEventStreamEventFlagMustScanSubDirs ) {
				// Events coalesced into a single event.  Docs recommend a directory scan to figure out what
				// changed.  I should log errors and see if this ever actually happens.
			}
			if( eventFlags[i] & kFSEventStreamEventFlagItemCreated ) {
				impl->verifyEvent( path, FileMonitorEvent::ADDED );
			}
			if( eventFlags[i] & kFSEventStreamEventFlagItemRemoved ) {
				impl->verifyEvent( path, FileMonitorEvent::REMOVED );
			}
			if( eventFlags[i] & kFSEventStreamEventFlagItemModified ) {
				impl->verifyEvent( path, FileMonitorEvent::MODIFIED );
			}
			if( eventFlags[i] & kFSEventStreamEventFlagItemRenamed )
			{
				if( ! boost::filesystem::exists( path ) )
				{
					impl->verifyEvent( path, FileMonitorEvent::RENAMED_OLD );
				}
				else
				{
					impl->verifyEvent( path, FileMonitorEvent::RENAMED_NEW );
				}
			}
		}
	}
	
	void workThread()
	{
		mRunloop = CFRunLoopGetCurrent();
		
		while( running() )
		{
			std::unique_lock<std::mutex> lock( mRunloopMutex );
			mRunloopCond.wait( lock );
			CFRunLoopRun();
		}
	}
	
	bool running()
	{
		// TODO fix this lock, makes no sense
		std::lock_guard<std::mutex> lock( mWorkThreadMutex );
		return mRun;
	}
	
	void stopWorkThread()
	{
		// Access to mRun is sychronized with running().
		std::lock_guard<std::mutex> lock( mWorkThreadMutex );
		mRun = false;
		CFRunLoopStop( mRunloop ); // exits the thread
		mRunloopCond.notify_all();
	}
	
	template<typename mapType>
	void removeEntry( uint64_t id, mapType &idMap )
	{
		auto iter = idMap.find( id );
		assert( iter != idMap.end() );
		
		idMap.erase( iter );
	}
	
	template<typename mapType, typename mmapType>
	void removeEntry( uint64_t id, mapType &idMap, mmapType &pathMmap )
	{
		auto iter = idMap.find( id );
		assert( iter != idMap.end() );
		
		auto range = pathMmap.equal_range( iter->second.path );
		assert( range.first != pathMmap.end() );
		
		auto rangeIter = range.first;
		while( rangeIter != range.second ) {
			// check pointer values
			if( rangeIter->second == &( iter->second ) ) {
				break;
			}
			++rangeIter;
		}
		assert( rangeIter != range.second );
		
		// do deletes
		idMap.erase( iter );
		pathMmap.erase( rangeIter );
		
		assert( mFilesMmap.size() == mFiles.size() );
	}
	
	void incrementTarget( const boost::filesystem::path &path )
	{
		auto it = mAllTargetsMap.find( path );
		if( it != mAllTargetsMap.end() ) {
			it->second += 1;
		} else {
			auto it = mAllTargetsMap.insert( std::make_pair( path, 1 ) );
			assert( it.second );
		}
	}

	void decrementTarget( const boost::filesystem::path &path )
	{
		auto it = mAllTargetsMap.find( path );
		assert( it != mAllTargetsMap.end() );
		
		if( it->second == 1 ) {
			mAllTargetsMap.erase( it );
		} else {
			it->second -= 1;
		}
	}
	
	class PathEntry
	{
	public:
		
		PathEntry( const boost::filesystem::path &path,
				   const std::string &regexMatch,
				   uint64_t entryId )
		: path( path ), regexMatch( regexMatch ), entryId( entryId )
		{}
		
		uint64_t				entryId;
		boost::filesystem::path path;
		std::regex 				regexMatch;
	};
	
	class FileEntry
	{
	public:
		
		FileEntry( const boost::filesystem::path &path,
				   uint64_t entryId )
		: path( path ), entryId( entryId )
		{ }
		
		uint64_t				entryId;
		boost::filesystem::path path;
	};
	
	std::mutex 								mPathsMutex;
	
	// ids, always > 0
	uint64_t 								mNextFileId{2};
	uint64_t								mNextPathId{1};
	
	// TODO explore the use of hashmaps
	
	// owns entries
	std::unordered_map<uint64_t, PathEntry> mPaths;
	std::unordered_map<uint64_t, FileEntry> mFiles;

	// references entries
	struct pathHash {
		size_t operator()( const boost::filesystem::path &p ) const {

			return std::hash<std::string>()( p.string() );
			
			// TODO resolve fs::hash_value once namespace has been converted
			//return boost::filesystem::hash_value( p );
		}
	};
	
	// TODO explore maps vs sets performance
	
	//! used for quick lookup of file specific activity
	std::unordered_multimap<boost::filesystem::path, FileEntry*, pathHash> mFilesMmap;
	
	//! used to keep track of all watched targets, both file and paths
	std::unordered_map<boost::filesystem::path, uint32_t, pathHash> 		mAllTargetsMap;
	
	bool 									mRun{false};
	CFRunLoopRef 							mRunloop;
	std::mutex 								mRunloopMutex;
	std::condition_variable 				mRunloopCond;
	
	std::mutex 								mWorkThreadMutex;
	std::thread 							mWorkThread;
	
	FSEventStreamRef 						mFsevents;
	std::mutex 								mEventsMutex;
	std::condition_variable 				mEventsCond;
	std::deque<FileMonitorEvent> 			mEvents;
};
	
} // filemonitor namespace
