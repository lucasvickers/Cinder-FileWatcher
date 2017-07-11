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


TEST_CASE( "FileDeletionTest" )
{
	SECTION( "Single file is deleted and delete detected within 2 seconds." )
	{
		fs::remove_all( getTestingPath() );
		CI_ASSERT( createTestingDir( getTestingPath() ) );
		
		fs::path target = getTestingPath() / "todelete.txt";
		writeToFile( target, "start" );
		
		filewatcher::WatchedTarget watchedFile;
		
		ActionMap actions;
		watchedFile = filewatcher::FileWatcher::watchFile( target,
			[ &actions ]( const ci::fs::path& file, filewatcher::EventType type ) {
			  actions[file].process( type );
			} );
		
		// delete it
		fs::remove( target );
		
		// wait 2 seconds
		std::chrono::time_point<std::chrono::system_clock> waitTime =
		std::chrono::system_clock::now() + std::chrono::seconds( 2 );
		
		// poll the service to force a check before app updates
		while( std::chrono::system_clock::now() < waitTime ) {
			filewatcher::FileWatcher::instance()->poll();
			ci::sleep( 1000 / 30 );
		}
		
		CI_ASSERT( actions[target].removed == 1 );
	}
}
