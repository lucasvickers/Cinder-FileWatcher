#include "cinder/app/Platform.h"
#include "cinder/app/App.h"
#include "cinder/Utilities.h"
#include "catch.hpp"

#include <chrono>
#include <sstream>

#include "utils.h"
#include "FileWatcher.h"

using namespace ci;
using namespace std;
using namespace ci::app;


TEST_CASE( "RegexTest" )
{
	SECTION( "Directory is watched with regex expression" )
	{
		fs::remove_all( getTestingPath() );
		CI_ASSERT( createTestingDir( getTestingPath() ) );
		
		std::string regex = ".*\\.jpg$";
		fs::path path = getTestingPath();
		
		fs::path target = path / "exists.jpg";
		fs::path dummy = path / "dummy.png";
		fs::path remove = path / "remove.jpg";
		fs::path awesomejpg = path / "awesome.jpg";
		fs::path awesomepng = path / "awesome.png";
		
		writeToFile( target, "start" );
		writeToFile( dummy, "start" );
		writeToFile( remove, "delete" );
		
		ActionMap actions;
		filewatcher::WatchedTarget watchedPath;
		
		watchedPath = filewatcher::FileWatcher::watchPath( path, regex,
			[ &actions ]( const ci::fs::path& file, filewatcher::EventType type ) {
				actions[file].process( type );
			} );
		
		CI_ASSERT( watchedPath.isPath() );
		
		// modify the files
		writeToFile( target, "finish" );
		writeToFile( dummy, "fake" );
		writeToFile( awesomejpg, "cool" );
		writeToFile( awesomepng, "cool2" );
		fs::remove( remove );
		
		// wait 2 seconds
		std::chrono::time_point<std::chrono::system_clock> waitTime =
		std::chrono::system_clock::now() + std::chrono::seconds( 2 );
		
		// poll the service to force a check before app updates
		while( std::chrono::system_clock::now() < waitTime ) {
			filewatcher::FileWatcher::instance()->poll();
			ci::sleep( 1000 / 30 );
		}
		
		CI_ASSERT( actions[target].modified >= 1 );
		CI_ASSERT( actions[dummy].modified == 0 );
		CI_ASSERT( actions[remove].removed == 1 );
		CI_ASSERT( actions[awesomejpg].modified >= 1 );
		CI_ASSERT( actions[awesomepng].modified == 0 );
	}
	
	SECTION( "Directory is watched with regex expression on subfolder and subfolder triggers watch." )
	{
		fs::path root = getTestingPath();
		fs::remove_all( root );
		CI_ASSERT( createTestingDir( root ) );
		
		fs::path subdir = root / "subdir";
		CI_ASSERT( createTestingDir( subdir) );
		
		std::string regex = ".*\\/subdir\\/.*\\.jpg$";
		
		std::vector<fs::path> hits;
		std::vector<fs::path> misses;
		std::vector<fs::path> deleteHits;
		std::vector<fs::path> deleteMisses;
		std::vector<fs::path> createHits;
		std::vector<fs::path> createMisses;
		
		int testSize = 5;
		
		// hits
		for(int i=0; i<testSize; ++i) {
			std::stringstream ss;
			ss << subdir.string() << "/hit" << i << ".jpg";
			hits.push_back( ss.str() );
			
			writeToFile( hits.back(), "hit" );
		}
		
		// misses
		for(int i=0; i<testSize; ++i) {
			std::stringstream ss;
			ss << subdir.string() << "/miss" << i << ".png";
			misses.push_back( ss.str() );
			
			writeToFile( misses.back(), "miss" );
		}
		
		// delete hits
		for(int i=0; i<testSize; ++i) {
			std::stringstream ss;
			ss << subdir.string() << "/deletehit" << i << ".jpg";
			deleteHits.push_back( ss.str() );
			
			writeToFile( deleteHits.back(), "deleteHit" );
		}

		// delete miss
		for(int i=0; i<testSize; ++i) {
			std::stringstream ss;
			ss << subdir.string() << "/deletemiss" << i << ".png";
			deleteMisses.push_back( ss.str() );
			
			writeToFile( deleteMisses.back(), "deleteMiss" );
		}
		
		ActionMap actions;
		filewatcher::WatchedTarget watchedPath;
		
		watchedPath = filewatcher::FileWatcher::watchPath( root, regex,
			[ &actions ]( const ci::fs::path& file, filewatcher::EventType type ) {
			  actions[file].process( type );
			} );
		
		// hits
		for( auto file : hits ) {
			writeToFile( file, "hit" );
		}
		
		// misses
		for( auto file : misses ) {
			writeToFile( file, "miss" );
		}
		
		// delete hits
		for( auto file : deleteHits ) {
			fs::remove( file );
		}
		
		// delete misses
		for( auto file : deleteMisses ) {
			fs::remove( file );
		}
		
		// create hits
		for(int i=0; i<testSize; ++i) {
			std::stringstream ss;
			ss << subdir.string() << "/createhit" << i << ".jpg";
			createHits.push_back( ss.str() );
			
			writeToFile( createHits.back(), "createHit" );
		}
		
		// create misses
		for(int i=0; i<testSize; ++i) {
			std::stringstream ss;
			ss << subdir.string() << "/createmiss" << i << ".png";
			createMisses.push_back( ss.str() );
			
			writeToFile( createMisses.back(), "createmiss" );
		}
		
		// wait 5 seconds
		std::chrono::time_point<std::chrono::system_clock> waitTime =
		std::chrono::system_clock::now() + std::chrono::seconds( 5 );
		
		// poll the service to force a check before app updates
		while( std::chrono::system_clock::now() < waitTime ) {
			filewatcher::FileWatcher::instance()->poll();
			ci::sleep( 1000 / 30 );
		}
		
		// verify
		
		// hits
		for( auto file : hits ) {
			CI_ASSERT( actions[file].modified >= 1 );
		}
		
		// misses
		for( auto file : misses ) {
			CI_ASSERT( actions[file].modified == 0 );
		}
		
		// delete hits
		for( auto file : deleteHits ) {
			CI_ASSERT( actions[file].removed == 1 );
		}
		
		// delete misses
		for( auto file : deleteMisses ) {
			CI_ASSERT( actions[file].removed == 0 );
		}
		
		// create hits
		for( auto file : createHits ) {
			auto hit = actions[file];
			CI_ASSERT( actions[file].added == 1 );
		}
		
		// create misses
		for( auto file : createMisses ) {
			CI_ASSERT( actions[file].added == 0 );
		}
		
	}
}
