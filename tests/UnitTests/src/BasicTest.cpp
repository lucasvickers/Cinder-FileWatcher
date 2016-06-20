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
		CI_ASSERT( createTestingDir( getTestingPath() ) );
		
		fs::path target = getTestingPath() / "basictest.txt";
		fs::path dummy = getTestingPath() / "dummy.txt";
		writeToFile( target, "start" );
		
		filewatcher::WatchedTarget watchedFile;

		ActionMap actions;
		watchedFile = filewatcher::FileWatcher::watchFile( target,
		  [ &actions ]( const ci::fs::path& file, filewatcher::EventType type ) {
			  actions[file.filename()].process( type );
		  } );
		
		CI_ASSERT( watchedFile.isFile() );

		// modify the temp file
		writeToFile( target, "finish" );
		writeToFile( dummy, "fake" );
		
		// wait 2 seconds
		std::chrono::time_point<std::chrono::system_clock> waitTime =
			std::chrono::system_clock::now() + std::chrono::seconds( 2 );
		
		// poll the service to force a check before app updates
		while( std::chrono::system_clock::now() < waitTime ) {
			filewatcher::FileWatcher::instance()->poll();
			ci::sleep( 1000 / 30 );
		}

		CI_ASSERT( actions[target.filename()].modified >= 1 );
	}
	
}
