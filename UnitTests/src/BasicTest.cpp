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


TEST_CASE( "BasicFileTest" )
{
	SECTION( "Single file is watched, modified, and change detected within 2 seconds." )
	{
		fs::remove_all( getTestingPath() );
		CI_ASSERT( createTestingDir( getTestingPath() ) );
		
		fs::path target = getTestingPath() / "basictest.txt";
		fs::path dummy = getTestingPath() / "dummy.txt";
		fs::path dummy2 = getTestingPath() / "dummy2.txt";
		writeToFile( target, "start" );
		writeToFile( dummy, "dummy" );
		
		filewatcher::WatchedTarget watchedFile;

		ActionMap actions;
		watchedFile = filewatcher::FileWatcher::watchFile( target,
		  [ &actions ]( const ci::fs::path& file, filewatcher::EventType type ) {
			  actions[file].process( type );
		  } );
		
		CI_ASSERT( watchedFile.isFile() );

		// modify the temp file
		writeToFile( target, "finish" );
		writeToFile( dummy, "fake" );
		writeToFile( dummy2, "fake" );
		
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
		CI_ASSERT( actions[dummy2].modified == 0 && actions[dummy2].added == 0 );
	}
}
