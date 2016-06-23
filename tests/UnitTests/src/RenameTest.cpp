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


TEST_CASE( "RenameTest" )
{
	SECTION( "Single file is renamed and change detected within 2 seconds." )
	{
		fs::remove_all( getTestingPath() );
		CI_ASSERT( createTestingDir( getTestingPath() ) );
		
		fs::path original = getTestingPath() / "original.txt";
		fs::path renamed = getTestingPath() / "renamed.txt";
		writeToFile( original, "start" );
		
		filewatcher::WatchedTarget watchedFile;
		
		ActionMap actions;
		watchedFile = filewatcher::FileWatcher::watchFile( original,
			[ &actions ]( const ci::fs::path& file, filewatcher::EventType type ) {
				std::cout << "Action on file " << file << std::endl;
			  actions[file].process( type );
			} );

		CI_ASSERT( watchedFile.isFile() );
		
		// rename file
		fs::rename( original, renamed );
		
		// wait 2 seconds
		std::chrono::time_point<std::chrono::system_clock> waitTime =
		std::chrono::system_clock::now() + std::chrono::seconds( 2 );
		
		// poll the service to force a check before app updates
		while( std::chrono::system_clock::now() < waitTime ) {
			filewatcher::FileWatcher::instance()->poll();
			ci::sleep( 1000 / 30 );
		}
		
		CI_ASSERT( actions[original].renamedOld == 1 );
		
		// Currently we can't watch for the new file name.  we wouldn't know which callback
		// to assign it to.  Perhaps there is some info that lets us know this, but tbd.
		//CI_ASSERT( actions[renamed].renamedNew == 1 );
		//CI_ASSERT( actions[renamed].added == 1 );
	}
}
