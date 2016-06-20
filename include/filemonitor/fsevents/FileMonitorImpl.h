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

#include <boost/enable_shared_from_this.hpp>	// TODO migrate to standard C++ / cinder if possible
#include <boost/filesystem.hpp>					// TODO migrate to standard C++ / cinder if possible

#include <CoreServices/CoreServices.h>
#include <thread>
#include <regex>
#include <unordered_map>

#include "FileMonitorEvent.h"

namespace filemonitor {
	
class FileMonitorImpl :
public std::enable_shared_from_this<FileMonitorImpl>
{
  public:
	FileMonitorImpl()
	: mRun(true), mWorkThread( &FileMonitorImpl::workThread, this ), mFsevents( nullptr )
	{}
	
	~FileMonitorImpl();
	
	uint64_t addPath( const boost::filesystem::path &path, const std::string &regexMatch );
	
	uint64_t addFile( const boost::filesystem::path &file );
	
	void remove( uint64_t id );
	
	void destroy();
	
	FileMonitorEvent popFrontEvent( boost::system::error_code &ec );
	
	void verifyEvent( const boost::filesystem::path &path, FileMonitorEvent::EventType type );
	
	void pushBackEvent( const FileMonitorEvent &ev );
	
  private:
	
	void startFsevents();
	
	void stopFsevents();
	
	static void fseventsCallback( ConstFSEventStreamRef streamRef,
								  void *clientCallBackInfo,
								  size_t numEvents,
								  void *eventPaths,
								  const FSEventStreamEventFlags eventFlags[],
								  const FSEventStreamEventId eventIds[] );
	
	void workThread();
	
	bool running();
	
	void stopWorkThread();
	
	//! Templated to make it easier to swap out map types
	//! takes the ID and returns the path that was associated / removed
	template<typename mapType>
	boost::filesystem::path removeEntry( uint64_t id, mapType &idMap )
	{
		boost::filesystem::path path;
		
		auto iter = idMap.find( id );
		assert( iter != idMap.end() );

		path = iter->second.path;
		// Removes the entry based on the ID
		idMap.erase( iter );
		
		return path;
	}
	
	//! Templated to make it easier to swap out map types
	//! takes the ID and returns the path that was associated / removed
	template<typename mapType, typename mmapType>
	boost::filesystem::path removeEntry( uint64_t id, mapType &idMap, mmapType &pathMmap )
	{
		boost::filesystem::path path;
		
		auto iter = idMap.find( id );
		assert( iter != idMap.end() );
		
		auto range = pathMmap.equal_range( iter->second.path );
		assert( range.first != pathMmap.end() );
		
		auto rangeIter = range.first;
		while( rangeIter != range.second ) {
			// check id values for match
			if( rangeIter->second->entryID == iter->second.entryID ) {
				break;
			}
			++rangeIter;
		}
		assert( rangeIter != range.second );
		
		path = iter->second.path;
		// Removes the entry based on the ID
		idMap.erase( iter );
		// Removes the path that was based on path and ID
		pathMmap.erase( rangeIter );
		
		assert( mFilesMmap.size() == mFiles.size() );
		
		return path;
	}
	
	void incrementTarget( const boost::filesystem::path &path );

	void decrementTarget( const boost::filesystem::path &path );
	
	class PathEntry
	{
	public:
		
		PathEntry( const boost::filesystem::path &path,
				   const std::string &regexMatch,
				   uint64_t entryID )
		: path( path ), regexMatch( regexMatch ), entryID( entryID )
		{}
		
		uint64_t				entryID;
		boost::filesystem::path path;
		std::regex 				regexMatch;
	};
	
	class FileEntry
	{
	public:
		
		FileEntry( const boost::filesystem::path &path,
				   uint64_t entryID )
		: path( path ), entryID( entryID )
		{ }
		
		uint64_t				entryID;
		boost::filesystem::path path;
	};
	
	std::mutex 								mPathsMutex;
	
	// ids, always > 0
	uint64_t 								mNextFileId{2};
	uint64_t								mNextPathId{1};
	
	// TODO explore the use of hashmaps
	
	//! Owns entries data
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
	
	//! Used for quick lookup of file specific activity via a path
	//! Multimap to support multiple watches on a single file
	//! References entries data
	std::unordered_multimap<boost::filesystem::path, FileEntry*, pathHash>	mFilesMmap;
	
	//! Used to keep track of all watched targets, both file and paths
	//! This is used for creating the watch list as it contains both files and paths
	std::unordered_map<boost::filesystem::path, uint32_t, pathHash>			mAllTargetsMap;
	
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
