//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#pragma once

#include <boost/enable_shared_from_this.hpp>	// TODO migrate to standard C++
#include <boost/filesystem.hpp>					// TODO migrate to standard C++
#include <CoreServices/CoreServices.h>

#include <boost/unordered_set.hpp>				// TODO migrate to standard C++

#include <deque>
#include <thread>
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
		// multimap is filesystem::path and pointer to the PathEntry
		pathsMmap_.insert( std::make_pair( path, &(iter.first->second) ) );
		
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
		// multimap is filesystem::path and pointer to the FileEntry
		filesMmap_.insert( std::make_pair( file, &(iter.first->second) ) );
		
		stop_fsevents();
		start_fsevents();
		
		return id;
	}
	
	void remove( uint64_t id )
	{
		std::lock_guard<std::mutex> lock( paths_mutex_ );
		
		// remove from containers
		if( id % 2 == 0 ) {
			// remove file
			// remove from files and filesMmap
		} else {
			// remove path
			// remove from paths and pathsMmap
		}
		
		stop_fsevents();
		start_fsevents();
		
		// NEED unique_id -> path, regex_match
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
	
	void verify_event( const file_monitor_event &ev )
	{
		// NEED: path -> multiple path entries, file -> multiple file entries
		pushback_event( ev );
	}
	
	void pushback_event( const file_monitor_event &ev )
	{
		std::lock_guard<std::mutex> lock( events_mutex_ );
		if( run_ ) {
			events_.push_back( ev );
			events_cond_.notify_all();
		}
	}
	
private:
	
	// move to header to fix this
	
	class PathEntry
	{
	public:
		
		PathEntry( const boost::filesystem::path &path,
			   const std::string &regex_match )
		: path( path ), regex_match( regex_match )
		{}
		
		boost::filesystem::path path;
		std::string regex_match;
		// callback data
	};
	
	class FileEntry
	{
	public:
		
		FileEntry( const boost::filesystem::path &path )
		: path( path )
		{ }
		
		boost::filesystem::path path;
		// callback data
	};
	
	// references entries
	static size_t path_hash( const boost::filesystem::path &p )
	{
		return boost::filesystem::hash_value( p );
	}
	std::unordered_multimap<boost::filesystem::path, PathEntry*, decltype( &path_hash )> pathsMmap_;
	std::unordered_multimap<boost::filesystem::path, FileEntry*, decltype( &path_hash )> filesMmap_;
	
	CFArrayRef make_array( const decltype( pathsMmap_ ) &paths, const decltype( filesMmap_ ) &files )
	{
		/*
		 CFMutableArrayRef arr = CFArrayCreateMutable( kCFAllocatorDefault, in.size(), &kCFTypeArrayCallBacks );
		 for (auto str : in) {
		 CFStringRef cfstr = CFStringCreateWithCString( kCFAllocatorDefault, str.c_str(), kCFStringEncodingUTF8 );
		 CFArrayAppendValue( arr, cfstr );
		 CFRelease(cfstr);
		 }
		 return arr;
		 */
	}
	
	void start_fsevents()
	{
		if ( paths_.size() == 0 ) {
			fsevents_ = nullptr;
			return;
		}
		
		// NEED: list of unique files / paths
		
		FSEventStreamContext context = {0, this, NULL, NULL, NULL};
		fsevents_ = FSEventStreamCreate( kCFAllocatorDefault,
									 &filemonitor::file_monitor_impl::fsevents_callback,
									 &context,
									 make_array( pathsMmap_, filesMmap_ ),
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
			// yield and let the callback do the work?
			std::this_thread::yield();
		}
		
		FSEventStreamScheduleWithRunLoop( fsevents_, runloop_, kCFRunLoopDefaultMode );
		FSEventStreamStart( fsevents_ );
		runloop_cond_.notify_all();
		FSEventStreamFlushAsync( fsevents_ );
		
		// todo delete fsevents_ ?  Create pattern
	}
	
	void stop_fsevents()
	{
		if (fsevents_)
		{
			FSEventStreamStop( fsevents_ );
			// todo do we need to unschedule this?
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
			// TODO: keep track of these, because we don't necessarily want to return folders as events
			// kFSEventStreamEventFlagItemIsDir
			// kFSEventStreamEventFlagItemIsFile
			
			boost::filesystem::path path( paths[i] );
			if( eventFlags[i] & kFSEventStreamEventFlagNone ) {
				// todo log this
			}
			if( eventFlags[i] & kFSEventStreamEventFlagMustScanSubDirs ) {
				// Events coalesced into a single event.  Docs recommend a directory scan to figure out what
				// changed.  I should log errors and see if this ever actually happens.
			}
			if( eventFlags[i] & kFSEventStreamEventFlagItemCreated ) {
				impl->verify_event( file_monitor_event( path, file_monitor_event::added ) );
			}
			if( eventFlags[i] & kFSEventStreamEventFlagItemRemoved ) {
				impl->verify_event( file_monitor_event( path, file_monitor_event::removed ) );
			}
			if( eventFlags[i] & kFSEventStreamEventFlagItemModified ) {
				impl->verify_event( file_monitor_event( path, file_monitor_event::modified ) );
			}
			if( eventFlags[i] & kFSEventStreamEventFlagItemRenamed )
			{
				if( ! boost::filesystem::exists( path ) )
				{
					impl->verify_event( file_monitor_event( path, file_monitor_event::renamed_old ) );
				}
				else
				{
					impl->verify_event( file_monitor_event( path, file_monitor_event::renamed_new ) );
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
	
	std::mutex 								paths_mutex_;
	
	// TODO explore the use of hashmaps
	
	// ids
	uint64_t 								nextFileID_{0};
	uint64_t								nextPathID_{1};
	
	// owns entries
	std::unordered_map<uint64_t, PathEntry> paths_;
	std::unordered_map<uint64_t, FileEntry> files_;

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
