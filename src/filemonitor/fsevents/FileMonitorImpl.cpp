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

#include "FileMonitorImpl.h"
#include <boost/asio/error.hpp>

#include <CoreServices/CoreServices.h>

namespace filemonitor {

FileMonitorImpl::~FileMonitorImpl()
{
	// The work thread is stopped and joined.
	stopWorkThread();
	mWorkThread.join();
	stopFsevents();
}

uint64_t FileMonitorImpl::addPath( const boost::filesystem::path &path, const std::string &regexMatch )
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

uint64_t FileMonitorImpl::addFile( const boost::filesystem::path &file )
{
	std::lock_guard<std::mutex> lock( mPathsMutex );
	
	uint64_t id = mNextFileId;
	mNextFileId += 2;
	
	auto iter = mFiles.emplace( id, FileEntry( file, id ) );
	assert( iter.second );
	// iter.first is the multimap iter. iter is filesystem::path, pointer to the FileEntry
	mFilesMmap.insert( std::make_pair( file, &(iter.first->second) ) );
	
	assert( mFilesMmap.size() == mFiles.size() );
	
	// increment the file target (can be multiple watches on a directory)
	// fsevents wants the path not the file, so pass the parent_path
	incrementTarget( file.parent_path() );
	
	stopFsevents();
	startFsevents();
	
	return id;
}

void FileMonitorImpl::remove( uint64_t id )
{
	std::lock_guard<std::mutex> lock( mPathsMutex );
	
	boost::filesystem::path path;
	
	// remove from containers
	if( id % 2 == 0 ) {
		// even is file
		path = removeEntry<decltype( mFiles ), decltype( mFilesMmap )>( id, mFiles, mFilesMmap );
		decrementTarget( path.parent_path() );
	} else {
		// odd is path
		path = removeEntry<decltype( mPaths )>( id, mPaths );
		decrementTarget( path );
	}
	
	stopFsevents();
	startFsevents();
}

void FileMonitorImpl::destroy()
{
	std::lock_guard<std::mutex> lock( mEventsMutex );
	mRun = false;
	mEventsCond.notify_all();
}

FileMonitorEvent FileMonitorImpl::popFrontEvent( boost::system::error_code &ec )
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

void FileMonitorImpl::verifyEvent( const boost::filesystem::path &path, FileMonitorEvent::EventType type )
{
	if( ! mRun ) {
		return;
	}
	
	// TODO confirm this winds up in proper worker thread
	
	//! check for exact file matches, streamlined map search to keep complexity minimal
	auto range = mFilesMmap.equal_range( path );
	for( auto it = range.first; it != range.second; ++it ) {
		pushBackEvent( FileMonitorEvent( path, type, it->second->entryID ) );
	}
	
	//! check every regex possibility, which is computationally more expensive
	for( const auto &it : mPaths ) {
		if( std::regex_match( path.string(), it.second.regexMatch ) ) {
			pushBackEvent( FileMonitorEvent( path, type, it.second.entryID ) );
		}
	}
}

void FileMonitorImpl::pushBackEvent( const FileMonitorEvent &ev )
{
	std::lock_guard<std::mutex> lock( mEventsMutex );
	mEvents.push_back( ev );
	mEventsCond.notify_all();
}

void FileMonitorImpl::startFsevents()
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
	CFRelease( allPaths );
	
	if( ! mFsevents )
	{
		// TODO move this out of boost namespace
		boost::system::system_error e( boost::system::error_code( errno, boost::system::get_system_category() ),
									  "filemonitor::FileMonitorImpl::init_fsevents: fsevents failed" );
		boost::throw_exception(e);
	}
	
	while( !mRunloop ) {
		std::this_thread::yield();
	}
	
	FSEventStreamScheduleWithRunLoop( mFsevents, mRunloop, kCFRunLoopDefaultMode );
	FSEventStreamStart( mFsevents );
	mRunloopCond.notify_all();
	FSEventStreamFlushAsync( mFsevents );
}

void FileMonitorImpl::stopFsevents()
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

void FileMonitorImpl::fseventsCallback( ConstFSEventStreamRef streamRef,
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

void FileMonitorImpl::workThread()
{
	mRunloop = CFRunLoopGetCurrent();
	
	while( running() )
	{
		std::unique_lock<std::mutex> lock( mRunloopMutex );
		mRunloopCond.wait( lock );
		CFRunLoopRun();
	}
}

bool FileMonitorImpl::running()
{
	std::lock_guard<std::mutex> lock( mWorkThreadMutex );
	return mRun;
}

void FileMonitorImpl::stopWorkThread()
{
	std::lock_guard<std::mutex> lock( mWorkThreadMutex );
	mRun = false;
	CFRunLoopStop( mRunloop ); // exits the thread
	mRunloopCond.notify_all();
}

void FileMonitorImpl::incrementTarget( const boost::filesystem::path &path )
{
	auto it = mAllTargetsMap.find( path );
	if( it != mAllTargetsMap.end() ) {
		it->second += 1;
	} else {
		auto it = mAllTargetsMap.insert( std::make_pair( path, 1 ) );
		assert( it.second );
	}
}

void FileMonitorImpl::decrementTarget( const boost::filesystem::path &path )
{
	auto it = mAllTargetsMap.find( path );
	assert( it != mAllTargetsMap.end() );
	
	if( it->second == 1 ) {
		mAllTargetsMap.erase( it );
	} else {
		it->second -= 1;
	}
}
	
} // filemonitor namespace
