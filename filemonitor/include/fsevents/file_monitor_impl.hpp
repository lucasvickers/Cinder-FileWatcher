#pragma once

#include <boost/enable_shared_from_this.hpp>	// TODO migrate to standard C++ / cinder if possible
#include <boost/filesystem.hpp>					// TODO migrate to standard C++ / cinder if possible

#include <CoreServices/CoreServices.h>
#include <deque>
#include <thread>
#include <functional>
#include <time.h>

namespace filemonitor {
	
class file_monitor_impl :
public boost::enable_shared_from_this<file_monitor_impl>
{
	
public:
	file_monitor_impl()
	: run_(true),
	work_thread_( &file_monitor_impl::work_thread, this )
	{}
	
	~file_monitor_impl()
	{
		// The work thread is stopped and joined.
		stop_work_thread();
		work_thread_.join();
		stop_fsevents();
	}
	
	uint64_t add_path( const boost::filesystem::path &path, const std::string &regex_match )
	{
		std::lock_guard<std::mutex> lock( paths_mutex_ );
		
		uint64_t id = nextPathID_;
		nextPathID_ += 2;
		
		auto iter = paths_.emplace( id, PathEntry( path, regex_match ) );
		assert( iter.second );
		// iter.first is the multimap iter. iter is filesystem::path, pointer to the PathEntry
		pathsMmap_.insert( std::make_pair( path, &(iter.first->second) ) );
		
		assert( pathsMmap_.size() == paths_.size() );
		
		stop_fsevents();
		start_fsevents();
		
		return id;
	}
	
	uint64_t add_file( const boost::filesystem::path &file )
	{
		std::lock_guard<std::mutex> lock( paths_mutex_ );
		
		uint64_t id = nextFileID_;
		nextFileID_ += 2;
		
		auto iter = files_.emplace( id, FileEntry( file ) );
		assert( iter.second );
		// iter.first is the multimap iter. iter is filesystem::path, pointer to the FileEntry
		filesMmap_.insert( std::make_pair( file, &(iter.first->second) ) );
		
		assert( filesMmap_.size() == files_.size() );
		
		stop_fsevents();
		start_fsevents();
		
		return id;
	}
	
	void remove( uint64_t id )
	{
		std::lock_guard<std::mutex> lock( paths_mutex_ );
		
		// remove from containers
		if( id % 2 == 0 ) {
			// even is file
			removeEntry<decltype( files_ ), decltype( filesMmap_ )>( id, files_, filesMmap_ );
			assert( filesMmap_.size() == files_.size() );
		} else {
			// odd is path
			removeEntry<decltype( paths_ ), decltype( pathsMmap_ )>( id, paths_, pathsMmap_ );
			assert( pathsMmap_.size() == paths_.size() );
		}
		
		stop_fsevents();
		start_fsevents();
	}
	
	void destroy()
	{
		std::lock_guard<std::mutex> lock( events_mutex_ );
		run_ = false;
		events_cond_.notify_all();
	}
	
	file_monitor_event popfront_event( boost::system::error_code &ec )
	{
		std::unique_lock<std::mutex> lock( events_mutex_ );
		while( run_ && events_.empty() ) {
			events_cond_.wait( lock );
		}
		file_monitor_event ev;
		if( ! events_.empty() ) {
			ec = boost::system::error_code();
			ev = events_.front();
			events_.pop_front();
		} else {
			ec = boost::asio::error::operation_aborted;
		}
		return ev;
	}
	
	void verify_event( const boost::filesystem::path &path, file_monitor_event::event_type type )
	{
		// NEED: path -> multiple path entries, file -> multiple file entries
		// TODO move onto worker thread
		// TODO implement this check
		
		// TODO pass ID (not 0)
		pushback_event( file_monitor_event( path, type, 0 ) );
	}
	
	void pushback_event( const file_monitor_event &ev )
	{
		std::lock_guard<std::mutex> lock( events_mutex_ );
		// TODO move run into verify_event
		if( run_ ) {
			events_.push_back( ev );
			events_cond_.notify_all();
		}
	}
	
private:
	
	void start_fsevents()
	{
		if ( paths_.size() == 0 && files_.size() == 0 ) {
			fsevents_ = nullptr;
			return;
		}
		
		// Need to pass FSEvents an array of unique paths, both files and folders.
		// We assume that there are no duplicates between pathsMmap and filesMmap
		//  since one is only files and one only folders
		
		CFMutableArrayRef allPaths = CFArrayCreateMutable( kCFAllocatorDefault, pathsMmap_.size() + filesMmap_.size(), &kCFTypeArrayCallBacks );

		// TODO in C++14 move to templaced lambda function
		for( auto path : pathsMmap_ ) {
			 CFStringRef cfstr = CFStringCreateWithCString( kCFAllocatorDefault, path.first.string().c_str(), kCFStringEncodingUTF8 );
			 CFArrayAppendValue( allPaths, cfstr );
			 CFRelease(cfstr);
		}
		for( auto file : filesMmap_ ) {
			CFStringRef cfstr = CFStringCreateWithCString( kCFAllocatorDefault, file.first.string().c_str(), kCFStringEncodingUTF8 );
			CFArrayAppendValue( allPaths, cfstr );
			CFRelease(cfstr);
		}
		
		FSEventStreamContext context = {0, this, NULL, NULL, NULL};
		fsevents_ = FSEventStreamCreate( kCFAllocatorDefault,
									 &filemonitor::file_monitor_impl::fsevents_callback,
									 &context,
									 allPaths,
									 //todo determine when we need to support historical events (if ever, I hope never)
									 kFSEventStreamEventIdSinceNow, 		// only new modifications
									 (CFTimeInterval) 1.0, 					// 1 second latency interval
									 kFSEventStreamCreateFlagFileEvents );
		FSEventStreamRetain( fsevents_ );
		
		if( ! fsevents_ )
		{
			// TODO move this out of boost namespace
			boost::system::system_error e( boost::system::error_code( errno, boost::system::get_system_category() ),
										  "filemonitor::file_monitor_impl::init_fsevents: fsevents failed" );
			boost::throw_exception(e);
		}
		
		while( ! runloop_ ) {
			// TODO yield and let the callback do the work?
			std::this_thread::yield();
		}
		
		FSEventStreamScheduleWithRunLoop( fsevents_, runloop_, kCFRunLoopDefaultMode );
		FSEventStreamStart( fsevents_ );
		runloop_cond_.notify_all();
		FSEventStreamFlushAsync( fsevents_ );
		
		// TODO delete fsevents_ ?  Create pattern check
	}
	
	void stop_fsevents()
	{
		if (fsevents_)
		{
			FSEventStreamStop( fsevents_ );
			// TODO do we need to unschedule this?
			// FSEventStreamUnscheduleFromRunLoop(fsevents_, runloop_, kCFRunLoopDefaultMode);
			FSEventStreamInvalidate( fsevents_ );
			FSEventStreamRelease( fsevents_ );
		}
	}
	
	static void fsevents_callback( ConstFSEventStreamRef streamRef,
							   void *clientCallBackInfo,
							   size_t numEvents,
							   void *eventPaths,
							   const FSEventStreamEventFlags eventFlags[],
							   const FSEventStreamEventId eventIds[] )
	{
		size_t i;
		char **paths = (char**)eventPaths;
		file_monitor_impl* impl = (file_monitor_impl*)clientCallBackInfo;
		
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
				impl->verify_event( path, file_monitor_event::added );
			}
			if( eventFlags[i] & kFSEventStreamEventFlagItemRemoved ) {
				impl->verify_event( path, file_monitor_event::removed );
			}
			if( eventFlags[i] & kFSEventStreamEventFlagItemModified ) {
				impl->verify_event( path, file_monitor_event::modified );
			}
			if( eventFlags[i] & kFSEventStreamEventFlagItemRenamed )
			{
				if( ! boost::filesystem::exists( path ) )
				{
					impl->verify_event( path, file_monitor_event::renamed_old );
				}
				else
				{
					impl->verify_event( path, file_monitor_event::renamed_new );
				}
			}
		}
	}
	
	void work_thread()
	{
		runloop_ = CFRunLoopGetCurrent();
		
		while( running() )
		{
			std::unique_lock<std::mutex> lock( runloop_mutex_ );
			runloop_cond_.wait( lock );
			CFRunLoopRun();
		}
	}
	
	bool running()
	{
		std::lock_guard<std::mutex> lock( work_thread_mutex_ );
		return run_;
	}
	
	void stop_work_thread()
	{
		// Access to run_ is sychronized with running().
		std::lock_guard<std::mutex> lock( work_thread_mutex_ );
		run_ = false;
		CFRunLoopStop( runloop_ ); // exits the thread
		runloop_cond_.notify_all();
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
	}
	
	class PathEntry
	{
	public:
		
		PathEntry( const boost::filesystem::path &path,
			   const std::string &regex_match )
		: path( path ), regex_match( regex_match )
		{}
		
		boost::filesystem::path path;
		std::string 			regex_match;
	};
	
	class FileEntry
	{
	public:
		
		FileEntry( const boost::filesystem::path &path )
		: path( path )
		{ }
		
		boost::filesystem::path path;
	};
	
	std::mutex 								paths_mutex_;
	
	// ids
	uint64_t 								nextFileID_{2};
	uint64_t								nextPathID_{1};
	
	// TODO explore the use of hashmaps
	
	// owns entries
	std::unordered_map<uint64_t, PathEntry> paths_;
	std::unordered_map<uint64_t, FileEntry> files_;

	// references entries
	struct path_hash {
		size_t operator()( const boost::filesystem::path &p ) const {

			return std::hash<std::string>()( p.string() );
			
			// TODO resolve fs::hash_value once namespace has been converted
			//return boost::filesystem::hash_value( p );
		}
	};
	std::unordered_multimap<boost::filesystem::path, PathEntry*, path_hash> pathsMmap_;
	std::unordered_multimap<boost::filesystem::path, FileEntry*, path_hash> filesMmap_;
	
	bool 									run_{false};
	CFRunLoopRef 							runloop_;
	std::mutex 								runloop_mutex_;
	std::condition_variable 				runloop_cond_;
	
	std::mutex 								work_thread_mutex_;
	std::thread 							work_thread_;
	
	FSEventStreamRef 						fsevents_;
	std::mutex 								events_mutex_;
	std::condition_variable 				events_cond_;
	std::deque<file_monitor_event> 			events_;
};
	
} // filemonitor namespace
