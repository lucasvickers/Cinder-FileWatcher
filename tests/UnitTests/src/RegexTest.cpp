#include "cinder/app/Platform.h"
#include "cinder/app/App.h"
#include "cinder/Utilities.h"
#include "catch.hpp"

#include <chrono>

#include "utils.h"
#include "FileWatcher.h"

using namespace ci;
using namespace std;
using namespace ci::app;


TEST_CASE( "RegexTest" )
{
	SECTION( "Directory is watched with regex expression" )
	{
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
				actions[file.filename()].process( type );
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
		
		CI_ASSERT( actions[target.filename()].modified >= 1 );
		CI_ASSERT( actions[dummy.filename()].modified == 0 );
		CI_ASSERT( actions[remove.filename()].removed == 1 );
		CI_ASSERT( actions[awesomejpg.filename()].modified >= 1 );
		CI_ASSERT( actions[awesomepng.filename()].modified == 0 );
	}
	
	SECTION( "Directory is watched with regex expression on subfolder and subfolder triggers watch." )
	{
		CI_ASSERT( createTestingDir( getTestingPath() ) );
		CI_ASSERT( createTestingDir( getTestingPath() / "subdir" ) );
		
		std::string regex = ".*\\/subdir\\/.*\\.jpg$";
		
		
		blah blah do this next
	}
	
}
